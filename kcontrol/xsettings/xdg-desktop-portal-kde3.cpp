/*
 * xdg-desktop-portal-kde3: a backend of xdg-desktop-portal for the KDE 3
 * session that implements only the Settings portal
 * (org.freedesktop.impl.portal.Settings).
 *
 * libadwaita and GTK take the system's fonts, colour scheme and accent
 * colour from the Settings portal (a GTK 4 on Wayland all of its
 * settings, libadwaita in Flatpak also the document and monospace
 * fonts).  xdg-desktop-portal-gtk answers from GSettings, which holds
 * GNOME's choices, not KDE's.  This backend answers with KDE's (see
 * portalsettings.h); kde3-portals.conf lists it before
 * xdg-desktop-portal-gtk, and xdg-desktop-portal takes each setting
 * from the first backend that has it, so the rest still comes from GTK.
 *
 * D-Bus starts it on demand (org.freedesktop.impl.portal.desktop.kde3).
 * It watches kdeglobals, kcminputrc and kcmfonts in $KDEHOME and emits
 * SettingChanged for each setting that changed when KDE writes them.
 *
 * Copyright (C) 2026 Serghei Amelian
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 */

// GIO first: Qt defines "signals", which GLib uses as an identifier
#include <gio/gio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <qfile.h>

#include <kconfig.h>
#include <kglobal.h>
#include <kinstance.h>
#include <kstandarddirs.h>

#include "portalsettings.h"

static const char busName[] = "org.freedesktop.impl.portal.desktop.kde3";
static const char objectPath[] = "/org/freedesktop/portal/desktop";
static const char interfaceName[] = "org.freedesktop.impl.portal.Settings";

static const char introspection[] =
    "<node>"
    "  <interface name='org.freedesktop.impl.portal.Settings'>"
    "    <method name='ReadAll'>"
    "      <arg type='as' name='namespaces' direction='in'/>"
    "      <arg type='a{sa{sv}}' name='value' direction='out'/>"
    "    </method>"
    "    <method name='Read'>"
    "      <arg type='s' name='namespace' direction='in'/>"
    "      <arg type='s' name='key' direction='in'/>"
    "      <arg type='v' name='value' direction='out'/>"
    "    </method>"
    "    <signal name='SettingChanged'>"
    "      <arg type='s' name='namespace'/>"
    "      <arg type='s' name='key'/>"
    "      <arg type='v' name='value'/>"
    "    </signal>"
    "    <property name='version' type='u' access='read'/>"
    "  </interface>"
    "</node>";

// KConfig writes a file and then sends its KIPC message; a module that
// saves several files (the fonts module: kdeglobals and kcmfonts) is
// read once
static const guint settleMs = 200;

static const char *const watchedFiles[] = { "kdeglobals", "kcminputrc", "kcmfonts", 0 };

struct Portal
{
    GMainLoop *loop;
    GDBusConnection *connection;
    GVariant *settings;
    guint timer;
    int exitCode;
};

static GVariant *readSettings()
{
    // new objects: KConfig reads the files when it is created
    KConfig globals( "kdeglobals", true, false );
    KConfig input( "kcminputrc", true, false );
    KConfig fonts( "kcmfonts", true, false );
    return KPortalSettings::collect( globals, input, fonts,
				     getenv( "XCURSOR_THEME" ), getenv( "XCURSOR_SIZE" ) );
}

static void emitChanged( const char *ns, const char *key, GVariant *value, void *data )
{
    Portal *portal = static_cast<Portal *>( data );
    if ( !portal->connection )
	return;
    GError *error = 0;
    if ( !g_dbus_connection_emit_signal( portal->connection, 0, objectPath, interfaceName,
					 "SettingChanged",
					 g_variant_new( "(ssv)", ns, key, value ), &error ) ) {
	g_warning( "SettingChanged %s %s: %s", ns, key, error->message );
	g_error_free( error );
    }
}

static gboolean reread( gpointer data )
{
    Portal *portal = static_cast<Portal *>( data );
    portal->timer = 0;
    GVariant *now = readSettings();
    KPortalSettings::compare( portal->settings, now, emitChanged, portal );
    g_variant_unref( portal->settings );
    portal->settings = now;
    return G_SOURCE_REMOVE;
}

static bool isWatched( GFile *file )
{
    if ( !file )
	return false;
    char *name = g_file_get_basename( file );
    bool watched = false;
    for ( const char *const *f = watchedFiles; *f && !watched; ++f )
	watched = name && strcmp( name, *f ) == 0;
    g_free( name );
    return watched;
}

static void configDirChanged( GFileMonitor *, GFile *file, GFile *otherFile,
			      GFileMonitorEvent event, gpointer data )
{
    Portal *portal = static_cast<Portal *>( data );
    // KSaveFile writes a new file and renames it over the old one
    if ( event != G_FILE_MONITOR_EVENT_CHANGES_DONE_HINT && event != G_FILE_MONITOR_EVENT_CREATED
	 && event != G_FILE_MONITOR_EVENT_DELETED && event != G_FILE_MONITOR_EVENT_MOVED_IN
	 && event != G_FILE_MONITOR_EVENT_MOVED_OUT && event != G_FILE_MONITOR_EVENT_RENAMED )
	return;
    if ( !isWatched( file ) && !isWatched( otherFile ) )
	return;
    if ( portal->timer )
	g_source_remove( portal->timer );
    portal->timer = g_timeout_add( settleMs, reread, portal );
}

static void methodCall( GDBusConnection *, const char *, const char *, const char *,
			const char *method, GVariant *parameters,
			GDBusMethodInvocation *invocation, gpointer data )
{
    Portal *portal = static_cast<Portal *>( data );
    if ( strcmp( method, "ReadAll" ) == 0 ) {
	const char **patterns = 0;
	g_variant_get( parameters, "(^a&s)", &patterns );
	GVariant *result = KPortalSettings::filter( portal->settings, patterns );
	g_free( patterns );
	g_dbus_method_invocation_return_value( invocation, g_variant_new( "(@a{sa{sv}})", result ) );
	g_variant_unref( result );
    } else if ( strcmp( method, "Read" ) == 0 ) {
	const char *ns, *key;
	g_variant_get( parameters, "(&s&s)", &ns, &key );
	GVariant *value = KPortalSettings::lookup( portal->settings, ns, key );
	if ( value ) {
	    g_dbus_method_invocation_return_value( invocation, g_variant_new( "(v)", value ) );
	    g_variant_unref( value );
	} else {
	    // xdg-desktop-portal then asks the next backend
	    g_dbus_method_invocation_return_dbus_error( invocation,
							"org.freedesktop.portal.Error.NotFound",
							"Requested setting not found" );
	}
    } else {
	g_dbus_method_invocation_return_dbus_error( invocation, "org.freedesktop.DBus.Error.UnknownMethod",
						    "Unknown method" );
    }
}

static GVariant *getProperty( GDBusConnection *, const char *, const char *, const char *,
			      const char *property, GError **error, gpointer )
{
    if ( strcmp( property, "version" ) == 0 )
	return g_variant_new_uint32( 1 );
    g_set_error( error, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_PROPERTY, "Unknown property %s", property );
    return 0;
}

static void busAcquired( GDBusConnection *connection, const char *, gpointer data )
{
    Portal *portal = static_cast<Portal *>( data );
    static const GDBusInterfaceVTable vtable = { methodCall, getProperty, 0, { 0 } };

    GError *error = 0;
    GDBusNodeInfo *node = g_dbus_node_info_new_for_xml( introspection, &error );
    if ( !node ) {
	g_critical( "%s", error->message );
	g_error_free( error );
	portal->exitCode = 1;
	g_main_loop_quit( portal->loop );
	return;
    }
    if ( !g_dbus_connection_register_object( connection, objectPath, node->interfaces[0],
					     &vtable, portal, 0, &error ) ) {
	g_critical( "cannot register %s: %s", objectPath, error->message );
	g_error_free( error );
	portal->exitCode = 1;
	g_main_loop_quit( portal->loop );
    } else {
	portal->connection = connection;
    }
    g_dbus_node_info_unref( node );
}

static void nameLost( GDBusConnection *, const char *name, gpointer data )
{
    Portal *portal = static_cast<Portal *>( data );
    g_message( "%s is owned by another program or the session bus is gone, exiting", name );
    portal->connection = 0;
    g_main_loop_quit( portal->loop );
}

int main( int argc, char **argv )
{
    bool replace = false;
    for ( int i = 1; i < argc; ++i ) {
	if ( strcmp( argv[i], "-r" ) == 0 || strcmp( argv[i], "--replace" ) == 0 )
	    replace = true;
	else {
	    fprintf( stderr, "Usage: %s [--replace]\n"
		     "The Settings portal backend of the KDE 3 session, started by D-Bus.\n", argv[0] );
	    return strcmp( argv[i], "-h" ) == 0 || strcmp( argv[i], "--help" ) == 0 ? 0 : 2;
	}
    }

    // only local files: GIO would otherwise start GVfs's daemon
    g_setenv( "GIO_USE_VFS", "local", TRUE );

    KInstance instance( "xdg-desktop-portal-kde3" );

    Portal portal;
    portal.loop = g_main_loop_new( 0, FALSE );
    portal.connection = 0;
    portal.settings = readSettings();
    portal.timer = 0;
    portal.exitCode = 0;

    // $KDEHOME/share/config, where KDE writes the files (the system-wide
    // ones change with the installation, which needs a new session)
    const QCString configDir = QFile::encodeName( KGlobal::dirs()->localkdedir() + "share/config" );
    GFile *dir = g_file_new_for_path( configDir );
    GError *error = 0;
    GFileMonitor *monitor = g_file_monitor_directory( dir, G_FILE_MONITOR_WATCH_MOVES, 0, &error );
    if ( monitor )
	g_signal_connect( monitor, "changed", G_CALLBACK( configDirChanged ), &portal );
    else {
	g_warning( "cannot watch %s, changes will not be seen: %s", configDir.data(), error->message );
	g_error_free( error );
    }
    g_object_unref( dir );

    GBusNameOwnerFlags flags = G_BUS_NAME_OWNER_FLAGS_ALLOW_REPLACEMENT;
    if ( replace )
	flags = GBusNameOwnerFlags( flags | G_BUS_NAME_OWNER_FLAGS_REPLACE );
    const guint owner = g_bus_own_name( G_BUS_TYPE_SESSION, busName, flags,
					busAcquired, 0, nameLost, &portal, 0 );

    g_main_loop_run( portal.loop );

    g_bus_unown_name( owner );
    if ( portal.timer )
	g_source_remove( portal.timer );
    if ( monitor )
	g_object_unref( monitor );
    g_variant_unref( portal.settings );
    g_main_loop_unref( portal.loop );
    return portal.exitCode;
}
