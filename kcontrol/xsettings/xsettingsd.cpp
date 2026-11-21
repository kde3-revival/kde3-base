/*
 * kded_xsettings: the XSETTINGS manager of the KDE session
 * (freedesktop.org XSETTINGS specification 0.5).
 *
 * What it publishes:
 *
 *   Xft/DPI, Xft/Antialias, Xft/Hinting, Xft/HintStyle, Xft/RGBA
 *       from the Xft.* X resources, which krdb exports from KDE's font
 *       settings (Control Center, Fonts): the X resources stay the one
 *       source, so that the clients of both agree.  Without Xft.dpi,
 *       the DPI of the X screen, which is what Qt 3 then uses.
 *   Gtk/FontName
 *       KDE's general font, as a Pango font description.
 *   Gtk/CursorThemeName, Gtk/CursorThemeSize
 *       the cursor theme and size of the mouse module, else the ones
 *       libXcursor uses (XCURSOR_THEME/SIZE, Xcursor.* resources, its
 *       default size).
 *   Net/DoubleClickTime, Net/CursorBlink, Net/CursorBlinkTime
 *       KDE's double click interval and text cursor blink rate.
 *
 * The GTK theme, icon theme and key theme (Net/ThemeName,
 * Net/IconThemeName, Gtk/KeyThemeName) are not published: KDE does not
 * choose them, and GTK 2, 3 and 4 then each keep their own (gtkrc,
 * settings.ini), which a published value would override for all three.
 *
 * It does nothing if another XSETTINGS manager owns the display (a
 * GNOME settings daemon, xsettingsd, ...), and stops if one takes the
 * selection over.  The settings are published again when the X
 * resources change (krdb runs xrdb whenever the fonts, colours or style
 * are applied), when KDE's fonts or mouse settings change (KIPC) and on
 * "dcop kded xsettings reconfigure".
 *
 * It uses its own X connection, as kded_randr does, so that the events
 * it selects on the root window do not go through kded's Qt connection.
 *
 * Copyright (C) 2026 Serghei Amelian
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 */

#include "xsettingsd.h"

#include <qfile.h>
#include <qfont.h>
#include <qsocketnotifier.h>

#include <kapplication.h>
#include <kconfig.h>
#include <kdebug.h>

#include <ctype.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xresource.h>

using namespace KXSettings;

// several changes come together (xrdb and a KIPC message after applying
// the fonts): publish once
static const int settleMs = 200;

extern "C" {
    KDE_EXPORT KDEDModule *create_xsettings( const QCString &obj )
    {
	return new XSettingsDaemon( obj );
    }
}

XSettingsDaemon::XSettingsDaemon( const QCString &obj )
    : KDEDModule( obj ), m_dpy( 0 ), m_window( 0 ), m_selection( 0 ),
      m_settingsAtom( 0 ), m_resourceAtom( 0 ), m_notifier( 0 )
{
    connect( &m_timer, SIGNAL( timeout() ), SLOT( update() ) );
    connect( kapp, SIGNAL( kdisplayFontChanged() ), SLOT( fontChanged() ) );
    connect( kapp, SIGNAL( settingsChanged( int ) ), SLOT( settingsChanged( int ) ) );
    acquire();
}

XSettingsDaemon::~XSettingsDaemon()
{
    release();
}

bool XSettingsDaemon::acquire()
{
    m_dpy = XOpenDisplay( DisplayString( qt_xdisplay() ) );
    if ( !m_dpy )
	return false;
    XrmInitialize();

    const int screen = DefaultScreen( m_dpy );
    const Window root = RootWindow( m_dpy, screen );
    m_selection = XInternAtom( m_dpy, QCString().sprintf( "_XSETTINGS_S%d", screen ), False );
    m_settingsAtom = XInternAtom( m_dpy, "_XSETTINGS_SETTINGS", False );
    m_resourceAtom = XInternAtom( m_dpy, "RESOURCE_MANAGER", False );

    if ( XGetSelectionOwner( m_dpy, m_selection ) != None ) {
	kdDebug() << "kded_xsettings: another XSETTINGS manager runs" << endl;
	XCloseDisplay( m_dpy );
	m_dpy = 0;
	return false;
    }

    // the changes of the X resources
    XSelectInput( m_dpy, root, PropertyChangeMask );

    m_window = XCreateSimpleWindow( m_dpy, root, -1, -1, 1, 1, 0, 0, 0 );
    XSelectInput( m_dpy, m_window, PropertyChangeMask );

    // the settings are on the window before it becomes the manager; the
    // property change gives the timestamp for taking the selection
    m_table.update( collect() );
    publish();
    XEvent ev;
    XWindowEvent( m_dpy, m_window, PropertyChangeMask, &ev );
    const Time time = ev.xproperty.time;

    // another manager may have come meanwhile
    if ( XGetSelectionOwner( m_dpy, m_selection ) != None ) {
	kdDebug() << "kded_xsettings: another XSETTINGS manager runs" << endl;
	release();
	return false;
    }
    XSetSelectionOwner( m_dpy, m_selection, m_window, time );
    if ( XGetSelectionOwner( m_dpy, m_selection ) != m_window ) {
	kdDebug() << "kded_xsettings: could not become the XSETTINGS manager" << endl;
	release();
	return false;
    }

    // tell the clients that wait for a manager
    XClientMessageEvent cm;
    memset( &cm, 0, sizeof( cm ) );
    cm.type = ClientMessage;
    cm.window = root;
    cm.message_type = XInternAtom( m_dpy, "MANAGER", False );
    cm.format = 32;
    cm.data.l[0] = time;
    cm.data.l[1] = m_selection;
    cm.data.l[2] = m_window;
    XSendEvent( m_dpy, root, False, StructureNotifyMask, reinterpret_cast<XEvent *>( &cm ) );
    XFlush( m_dpy );

    m_notifier = new QSocketNotifier( ConnectionNumber( m_dpy ), QSocketNotifier::Read, this );
    connect( m_notifier, SIGNAL( activated( int ) ), SLOT( readEvents() ) );
    if ( XQLength( m_dpy ) > 0 )
	QTimer::singleShot( 0, this, SLOT( readEvents() ) );
    return true;
}

void XSettingsDaemon::release()
{
    m_timer.stop();
    delete m_notifier;
    m_notifier = 0;
    if ( m_dpy ) {
	if ( m_window )
	    XDestroyWindow( m_dpy, m_window );
	XCloseDisplay( m_dpy );
    }
    m_dpy = 0;
    m_window = 0;
}

bool XSettingsDaemon::isManager()
{
    return m_dpy != 0;
}

void XSettingsDaemon::readEvents()
{
    if ( !m_dpy )
	return;
    bool resourcesChanged = false;
    while ( m_dpy && XPending( m_dpy ) ) {
	XEvent ev;
	XNextEvent( m_dpy, &ev );
	if ( ev.type == SelectionClear && ev.xselectionclear.window == m_window
	     && ev.xselectionclear.selection == m_selection ) {
	    kdDebug() << "kded_xsettings: another XSETTINGS manager took over" << endl;
	    release();
	    return;
	}
	if ( ev.type == PropertyNotify && ev.xproperty.atom == m_resourceAtom
	     && ev.xproperty.window != m_window )
	    resourcesChanged = true;
    }
    if ( resourcesChanged )
	m_timer.start( settleMs, true );
}

void XSettingsDaemon::fontChanged()
{
    reconfigure();
}

void XSettingsDaemon::settingsChanged( int category )
{
    if ( category == KApplication::SETTINGS_MOUSE )
	reconfigure();
}

void XSettingsDaemon::reconfigure()
{
    // a manager that was there at login may be gone; acquire() never
    // takes the selection from one that runs
    if ( !m_dpy && !acquire() )
	return;
    m_timer.start( settleMs, true );
}

void XSettingsDaemon::update()
{
    if ( !m_dpy )
	return;
    if ( m_table.update( collect() ) )
	publish();
    if ( XQLength( m_dpy ) > 0 )
	QTimer::singleShot( 0, this, SLOT( readEvents() ) );
}

void XSettingsDaemon::publish()
{
    QByteArray data = m_table.serialize();
    XChangeProperty( m_dpy, m_window, m_settingsAtom, m_settingsAtom, 8, PropModeReplace,
		     reinterpret_cast<unsigned char *>( data.data() ), data.size() );
    XFlush( m_dpy );
}

// --- what to publish ---------------------------------------------------------

namespace
{

// the X resources as they are now (Xlib's XResourceManagerString() is
// what they were when the connection was opened)
class Resources
{
public:
    Resources( Display *dpy, Window root, Atom atom ) : m_db( 0 )
    {
	Atom type;
	int format;
	unsigned long n, after;
	unsigned char *data = 0;
	if ( XGetWindowProperty( dpy, root, atom, 0, LONG_MAX / 4, False, XA_STRING,
				 &type, &format, &n, &after, &data ) == Success
	     && data && type == XA_STRING && format == 8 )
	    m_db = XrmGetStringDatabase( reinterpret_cast<char *>( data ) );
	if ( data )
	    XFree( data );
    }
    ~Resources()
    {
	if ( m_db )
	    XrmDestroyDatabase( m_db );
    }
    // "Xft", "dpi" -> the value of Xft.dpi, null if not set
    QCString get( const char *program, const char *name ) const
    {
	if ( !m_db )
	    return QCString();
	QCString fullName = QCString( program ) + "." + name;
	// the class is the name with capitals, as XGetDefault() looks it up
	// (a copy: QCString is explicitly shared)
	QCString fullClass = fullName.copy();
	fullClass[0] = toupper( fullClass[0] );
	int dot = fullClass.find( '.' );
	fullClass[dot + 1] = toupper( fullClass[dot + 1] );
	char *type = 0;
	XrmValue value;
	if ( !XrmGetResource( m_db, fullName, fullClass, &type, &value ) || !value.addr )
	    return QCString();
	return QCString( value.addr, value.size + 1 ).stripWhiteSpace();
    }

private:
    XrmDatabase m_db;
};

// libXft's reading of a boolean resource: -1 if it is not one
int xftBool( const QCString &v )
{
    if ( v.isEmpty() )
	return -1;
    const QCString l = v.lower();
    if ( l[0] >= '0' && l[0] <= '9' )
	return atoi( l ) != 0;
    if ( l == "true" || l == "yes" || l == "on" || l == "t" || l == "y" )
	return 1;
    if ( l == "false" || l == "no" || l == "off" || l == "f" || l == "n" )
	return 0;
    return -1;
}

}

Table::Map XSettingsDaemon::collect()
{
    Table::Map s;
    const int screen = DefaultScreen( m_dpy );
    Resources res( m_dpy, RootWindow( m_dpy, screen ), m_resourceAtom );

    // the DPI: Xft.dpi, else that of the X screen, as Qt 3 takes it
    double dpi = res.get( "Xft", "dpi" ).toDouble();
    if ( dpi <= 0 && DisplayHeightMM( m_dpy, screen ) > 0 )
	dpi = DisplayHeight( m_dpy, screen ) * 25.4 / DisplayHeightMM( m_dpy, screen );
    if ( dpi <= 0 )
	dpi = 96;
    s["Xft/DPI"] = Table::intValue( qRound( dpi * 1024 ) );

    int b = xftBool( res.get( "Xft", "antialias" ) );
    if ( b >= 0 )
	s["Xft/Antialias"] = Table::intValue( b );
    b = xftBool( res.get( "Xft", "hinting" ) );
    if ( b >= 0 )
	s["Xft/Hinting"] = Table::intValue( b );
    QCString v = res.get( "Xft", "hintstyle" );
    if ( v == "hintnone" || v == "hintslight" || v == "hintmedium" || v == "hintfull" )
	s["Xft/HintStyle"] = Table::stringValue( v );
    v = res.get( "Xft", "rgba" );
    if ( v == "none" || v == "rgb" || v == "bgr" || v == "vrgb" || v == "vbgr" )
	s["Xft/RGBA"] = Table::stringValue( v );

    // the font, as KGlobalSettings::generalFont() reads it (which keeps
    // what it read until a KIPC message, not on reconfigure())
    KConfig globals( "kdeglobals", true, false );
    globals.setGroup( "General" );
    QFont font( "Sans Serif", 10 );
    font.setStyleHint( QFont::SansSerif );
    font = globals.readFontEntry( "font", &font );
    s["Gtk/FontName"] = Table::stringValue( pangoFontName( font ) );

    // the cursor theme and size, in libXcursor's order after the mouse
    // module's own settings
    KConfig input( "kcminputrc", true, false );
    input.setGroup( "Mouse" );
    QString cursorTheme = input.readEntry( "cursorTheme" );
    if ( cursorTheme.isEmpty() )
	cursorTheme = QFile::decodeName( getenv( "XCURSOR_THEME" ) );
    if ( cursorTheme.isEmpty() )
	cursorTheme = QFile::decodeName( res.get( "Xcursor", "theme" ) );
    if ( !cursorTheme.isEmpty() )
	s["Gtk/CursorThemeName"] = Table::stringValue( cursorTheme );
    int cursorSize = input.readNumEntry( "cursorSize", 0 );
    if ( cursorSize <= 0 )
	cursorSize = atoi( getenv( "XCURSOR_SIZE" ) ? getenv( "XCURSOR_SIZE" ) : "0" );
    if ( cursorSize <= 0 )
	cursorSize = res.get( "Xcursor", "size" ).toInt();
    if ( cursorSize <= 0 )
	cursorSize = xcursorDefaultSize( int( res.get( "Xft", "dpi" ).toDouble() ),
					 DisplayWidth( m_dpy, screen ), DisplayHeight( m_dpy, screen ) );
    s["Gtk/CursorThemeSize"] = Table::intValue( cursorSize );

    // the mouse and text cursor timings, as KApplication applies them
    globals.setGroup( "KDE" );
    s["Net/DoubleClickTime"] = Table::intValue( globals.readNumEntry( "DoubleClickInterval", 400 ) );
    int blink = globals.readNumEntry( "CursorBlinkRate", 1000 );
    if ( blink != 0 && blink < 200 )
	blink = 200;
    if ( blink > 2000 )
	blink = 2000;
    s["Net/CursorBlink"] = Table::intValue( blink != 0 );
    if ( blink != 0 )
	s["Net/CursorBlinkTime"] = Table::intValue( blink );

    return s;
}

#include "xsettingsd.moc"
