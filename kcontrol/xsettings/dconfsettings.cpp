/*
 * The KDE-derived keys of the session's dconf database, see
 * dconfsettings.h.
 *
 * Copyright (C) 2026 Serghei Amelian
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 */

#include <gio/gio.h>
#include <stdio.h>
#include <string.h>

#include "dconfsettings.h"

namespace KDconfSettings
{

const char dbName[] = "kde3";
const char writerPath[] = "/ca/desrt/dconf/Writer/kde3";

static const char interfaceNs[] = "org.gnome.desktop.interface";
static const char interfaceDir[] = "/org/gnome/desktop/interface/";

bool profileUsesSessionDb( const char *path )
{
    if ( !path || path[0] != '/' )
	return false;
    FILE *f = fopen( path, "r" );
    if ( !f )
	return false;
    // the first line that is not empty or a comment, as dconf reads it
    char line[256];
    bool found = false;
    while ( fgets( line, sizeof line, f ) ) {
	char *s = line;
	while ( g_ascii_isspace( *s ) )
	    ++s;
	char *end = s + strcspn( s, "#\n" );
	while ( end > s && g_ascii_isspace( end[-1] ) )
	    --end;
	if ( end == s )
	    continue;
	*end = 0;
	found = strcmp( s, "user-db:kde3" ) == 0;
	break;
    }
    fclose( f );
    return found;
}

GVariant *changes( GVariant *all, GVariant *before )
{
    GVariant *now = g_variant_lookup_value( all, interfaceNs, G_VARIANT_TYPE_VARDICT );
    if ( !now )
	return 0;
    GVariant *old = before ? g_variant_lookup_value( before, interfaceNs, G_VARIANT_TYPE_VARDICT ) : 0;

    GVariantBuilder out;
    g_variant_builder_init( &out, G_VARIANT_TYPE( "a{smv}" ) );
    bool any = false;
    GVariantIter iter;
    const char *key;
    GVariant *value;
    g_variant_iter_init( &iter, now );
    while ( g_variant_iter_next( &iter, "{&sv}", &key, &value ) ) {
	GVariant *was = old ? g_variant_lookup_value( old, key, 0 ) : 0;
	if ( !was || !g_variant_equal( was, value ) ) {
	    gchar *path = g_strconcat( interfaceDir, key, NULL );
	    g_variant_builder_add( &out, "{smv}", path, value );
	    g_free( path );
	    any = true;
	}
	if ( was )
	    g_variant_unref( was );
	g_variant_unref( value );
    }
    // a key that KDE no more sets (the cursor theme) keeps its value
    if ( old )
	g_variant_unref( old );
    g_variant_unref( now );
    GVariant *result = g_variant_ref_sink( g_variant_builder_end( &out ) );
    if ( !any ) {
	g_variant_unref( result );
	return 0;
    }
    return result;
}

bool write( GVariant *changes )
{
    GError *error = 0;
    GDBusConnection *bus = g_bus_get_sync( G_BUS_TYPE_SESSION, 0, &error );
    if ( !bus ) {
	g_warning( "kded_xsettings: no session bus: %s", error->message );
	g_error_free( error );
	return false;
    }
    // what DConfClient sends: Change(ay) with the serialised a{smv}, to
    // the writer object of the database (dconf-engine.c)
    GVariant *blob = g_variant_new_fixed_array( G_VARIANT_TYPE_BYTE, g_variant_get_data( changes ),
						g_variant_get_size( changes ), 1 );
    GVariant *reply = g_dbus_connection_call_sync( bus, "ca.desrt.dconf", writerPath,
						   "ca.desrt.dconf.Writer", "Change",
						   g_variant_new( "(@ay)", blob ), G_VARIANT_TYPE( "(s)" ),
						   G_DBUS_CALL_FLAGS_NONE, 10000, 0, &error );
    g_object_unref( bus );
    if ( !reply ) {
	g_warning( "kded_xsettings: writing the dconf database %s failed: %s", dbName, error->message );
	g_error_free( error );
	return false;
    }
    g_variant_unref( reply );
    return true;
}

void sync( GVariant *all, GVariant **last )
{
    GVariant *c = changes( all, *last );
    if ( c ) {
	const bool ok = write( c );
	g_variant_unref( c );
	if ( !ok )
	    return;
    }
    if ( *last )
	g_variant_unref( *last );
    *last = g_variant_ref( all );
}

void release( GVariant *value )
{
    if ( value )
	g_variant_unref( value );
}

}
