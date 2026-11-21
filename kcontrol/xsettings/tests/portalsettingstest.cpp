/*
 * Tests of the settings of xdg-desktop-portal-kde3 (portalsettings):
 * what KDE's configuration gives, the namespace patterns of ReadAll(),
 * Read() of a missing key and the changed keys.
 *
 *   portalsettingstest   run the tests, exit status = failures
 */

// GLib first: Qt defines "signals", which GLib uses as an identifier
#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <qcolor.h>
#include <qfile.h>
#include <qstringlist.h>

#include <kconfig.h>
#include <kinstance.h>

#include "portalsettings.h"

using namespace KPortalSettings;

static int failures = 0;

#define CHECK( cond ) do { if ( !( cond ) ) { \
    printf( "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond ); ++failures; } } while ( 0 )
#define CHECK_EQ( a, b ) do { if ( !( ( a ) == ( b ) ) ) { \
    printf( "FAIL %s:%d: %s == %s\n", __FILE__, __LINE__, #a, #b ); ++failures; } } while ( 0 )

static QCString tmpDir;

static QString writeFile( const char *name, const char *contents )
{
    const QString path = QFile::decodeName( tmpDir ) + "/" + name;
    QFile f( path );
    if ( f.open( IO_WriteOnly | IO_Truncate ) ) {
	f.writeBlock( contents, strlen( contents ) );
	f.close();
    }
    return path;
}

// the settings from these files, with these environment variables
static GVariant *settings( const char *globals, const char *input, const char *fonts,
			   const char *theme = 0, const char *size = 0 )
{
    KConfig g( writeFile( "kdeglobals", globals ), true, false );
    KConfig i( writeFile( "kcminputrc", input ), true, false );
    KConfig f( writeFile( "kcmfonts", fonts ), true, false );
    return collect( g, i, f, theme, size );
}

static QCString string( GVariant *all, const char *ns, const char *key )
{
    GVariant *v = lookup( all, ns, key );
    if ( !v )
	return "<none>";
    QCString s = v && g_variant_is_of_type( v, G_VARIANT_TYPE_STRING )
	? QCString( g_variant_get_string( v, 0 ) ) : QCString( "<not a string>" );
    g_variant_unref( v );
    return s;
}

static QCString printed( GVariant *all, const char *ns, const char *key )
{
    GVariant *v = lookup( all, ns, key );
    if ( !v )
	return "<none>";
    char *p = g_variant_print( v, TRUE );
    QCString s( p );
    g_free( p );
    g_variant_unref( v );
    return s;
}

static void testDefaults()
{
    GVariant *s = settings( "", "", "" );
    const char *gnome = "org.gnome.desktop.interface";
    const char *fdo = "org.freedesktop.appearance";
    CHECK_EQ( string( s, gnome, "font-name" ), QCString( "Sans Serif 10" ) );
    CHECK_EQ( string( s, gnome, "document-font-name" ), QCString( "Sans Serif 10" ) );
    CHECK_EQ( string( s, gnome, "monospace-font-name" ), QCString( "Monospace 10" ) );
    CHECK_EQ( printed( s, gnome, "text-scaling-factor" ), QCString( "1.0" ) );
    CHECK_EQ( string( s, gnome, "color-scheme" ), QCString( "default" ) );
    // not KDE's: left to the next backend
    CHECK_EQ( printed( s, gnome, "cursor-theme" ), QCString( "<none>" ) );
    CHECK_EQ( printed( s, gnome, "cursor-size" ), QCString( "<none>" ) );
    CHECK_EQ( printed( s, gnome, "gtk-theme" ), QCString( "<none>" ) );
    CHECK_EQ( printed( s, gnome, "icon-theme" ), QCString( "<none>" ) );
    CHECK_EQ( printed( s, gnome, "clock-format" ), QCString( "<none>" ) );
    CHECK_EQ( printed( s, "org.gnome.desktop.a11y.interface", "high-contrast" ), QCString( "<none>" ) );
    CHECK_EQ( printed( s, fdo, "color-scheme" ), QCString( "uint32 0" ) );
    CHECK_EQ( printed( s, fdo, "contrast" ), QCString( "<none>" ) );
    CHECK_EQ( printed( s, fdo, "reduced-motion" ), QCString( "<none>" ) );
    // KDE's default selection colour, 103,141,178
    GVariant *accent = lookup( s, fdo, "accent-color" );
    CHECK( accent && g_variant_is_of_type( accent, G_VARIANT_TYPE( "(ddd)" ) ) );
    if ( accent ) {
	double r, g, b;
	g_variant_get( accent, "(ddd)", &r, &g, &b );
	CHECK( r == 103 / 255.0 && g == 141 / 255.0 && b == 178 / 255.0 );
	g_variant_unref( accent );
    }
    g_variant_unref( s );
}

static const char darkGlobals[] =
    "[General]\n"
    "background=48,48,48\n"
    "selectBackground=255,0,0\n"
    "font=DejaVu Sans,11,-1,5,50,0,0,0,0,0\n"
    "fixed=Hack,9,-1,5,50,0,0,0,0,0\n";

static void testKdeSettings()
{
    GVariant *s = settings( darkGlobals, "[Mouse]\ncursorTheme=breeze_cursors\ncursorSize=32\n",
			    "[General]\nforceFontDPI=120\n", "Adwaita", "24" );
    const char *gnome = "org.gnome.desktop.interface";
    const char *fdo = "org.freedesktop.appearance";
    CHECK_EQ( string( s, gnome, "font-name" ), QCString( "DejaVu Sans 11" ) );
    CHECK_EQ( string( s, gnome, "document-font-name" ), QCString( "DejaVu Sans 11" ) );
    CHECK_EQ( string( s, gnome, "monospace-font-name" ), QCString( "Hack 9" ) );
    CHECK_EQ( printed( s, gnome, "text-scaling-factor" ), QCString( "1.25" ) );
    CHECK_EQ( string( s, gnome, "cursor-theme" ), QCString( "breeze_cursors" ) );
    CHECK_EQ( printed( s, gnome, "cursor-size" ), QCString( "32" ) );
    CHECK_EQ( string( s, gnome, "color-scheme" ), QCString( "prefer-dark" ) );
    CHECK_EQ( printed( s, fdo, "color-scheme" ), QCString( "uint32 1" ) );
    CHECK_EQ( printed( s, fdo, "accent-color" ), QCString( "(1.0, 0.0, 0.0)" ) );
    g_variant_unref( s );

    // the cursor from the environment when the mouse module has none
    s = settings( "", "", "", "Adwaita", "24" );
    CHECK_EQ( string( s, gnome, "cursor-theme" ), QCString( "Adwaita" ) );
    CHECK_EQ( printed( s, gnome, "cursor-size" ), QCString( "24" ) );
    g_variant_unref( s );

    // the boundary of dark: luma below 128
    CHECK( prefersDark( QColor( 127, 127, 127 ) ) );
    CHECK( !prefersDark( QColor( 128, 128, 128 ) ) );
    CHECK( !prefersDark( QColor( 239, 239, 239 ) ) );
    CHECK( prefersDark( QColor( 0, 0, 255 ) ) );
}

static QStringList namespaces( GVariant *all )
{
    QStringList l;
    GVariantIter iter;
    const char *ns;
    GVariant *values;
    g_variant_iter_init( &iter, all );
    while ( g_variant_iter_next( &iter, "{&s@a{sv}}", &ns, &values ) ) {
	l.append( ns );
	g_variant_unref( values );
    }
    l.sort();
    return l;
}

static QString filtered( GVariant *all, const char *const *patterns )
{
    GVariant *f = filter( all, patterns );
    const QString s = namespaces( f ).join( " " );
    g_variant_unref( f );
    return s;
}

static void testFilter()
{
    GVariant *s = settings( "", "", "" );
    const QString both( "org.freedesktop.appearance org.gnome.desktop.interface" );
    const char *none[] = { 0 };
    CHECK_EQ( filtered( s, none ), both );
    CHECK_EQ( filtered( s, 0 ), both );
    const char *empty[] = { "", 0 };
    CHECK_EQ( filtered( s, empty ), both );
    const char *fdo[] = { "org.freedesktop.appearance", 0 };
    CHECK_EQ( filtered( s, fdo ), QString( "org.freedesktop.appearance" ) );
    const char *gnome[] = { "org.gnome.*", 0 };
    CHECK_EQ( filtered( s, gnome ), QString( "org.gnome.desktop.interface" ) );
    const char *two[] = { "org.freedesktop.*", "org.gnome.desktop.interface", 0 };
    CHECK_EQ( filtered( s, two ), both );
    const char *other[] = { "org.kde.kdeglobals.General", "org.gnome.desktop.a11y.*", 0 };
    CHECK( filtered( s, other ).isEmpty() );
    // a pattern is a prefix only with '*'
    const char *prefix[] = { "org.gnome", 0 };
    CHECK( filtered( s, prefix ).isEmpty() );

    GVariant *v = lookup( s, "org.gnome.desktop.interface", "gtk-theme" );
    CHECK( !v );
    v = lookup( s, "org.gnome.desktop.wm.preferences", "button-layout" );
    CHECK( !v );
    g_variant_unref( s );
}

static void collectChange( const char *ns, const char *key, GVariant *, void *data )
{
    static_cast<QStringList *>( data )->append( QString( ns ) + " " + key );
}

static QString changes( GVariant *before, GVariant *now )
{
    QStringList l;
    compare( before, now, collectChange, &l );
    l.sort();
    return l.join( "; " );
}

static void testChanges()
{
    GVariant *light = settings( "", "", "" );
    GVariant *light2 = settings( "[General]\nbackground=239,239,239\n", "", "" );
    CHECK( changes( light, light2 ).isEmpty() );

    // the colours change: both colour schemes and the accent
    GVariant *dark = settings( "[General]\nbackground=48,48,48\nselectBackground=255,0,0\n", "", "" );
    CHECK_EQ( changes( light, dark ), QString( "org.freedesktop.appearance accent-color; "
					       "org.freedesktop.appearance color-scheme; "
					       "org.gnome.desktop.interface color-scheme" ) );
    // the general font changes: also the document font
    GVariant *font = settings( "[General]\nfont=DejaVu Sans,11,-1,5,50,0,0,0,0,0\n", "", "" );
    CHECK_EQ( changes( light, font ), QString( "org.gnome.desktop.interface document-font-name; "
					       "org.gnome.desktop.interface font-name" ) );
    // a cursor theme appears
    GVariant *cursor = settings( "", "[Mouse]\ncursorTheme=whiteglass\n", "" );
    CHECK_EQ( changes( light, cursor ), QString( "org.gnome.desktop.interface cursor-theme" ) );
    // without settings before (the first reading): everything
    CHECK_EQ( changes( 0, light ).contains( ';' ), 6 );

    g_variant_unref( light );
    g_variant_unref( light2 );
    g_variant_unref( dark );
    g_variant_unref( font );
    g_variant_unref( cursor );
}

int main()
{
    char dirTemplate[] = "/tmp/portalsettingstest.XXXXXX";
    if ( !mkdtemp( dirTemplate ) ) {
	perror( "mkdtemp" );
	return 1;
    }
    tmpDir = dirTemplate;
    // KDE's configuration of the user is not read: KConfig gets the paths
    setenv( "KDEHOME", tmpDir + "/kdehome", 1 );
    KInstance instance( "portalsettingstest" );

    testDefaults();
    testKdeSettings();
    testFilter();
    testChanges();

    const char *const files[] = { "kdeglobals", "kcminputrc", "kcmfonts", 0 };
    for ( const char *const *f = files; *f; ++f )
	unlink( tmpDir + "/" + *f );
    // KInstance makes $KDEHOME/share/config
    rmdir( tmpDir + "/kdehome/share/config" );
    rmdir( tmpDir + "/kdehome/share" );
    rmdir( tmpDir + "/kdehome" );
    rmdir( tmpDir );

    if ( failures )
	printf( "%d failure(s)\n", failures );
    else
	printf( "all passed\n" );
    return failures;
}
