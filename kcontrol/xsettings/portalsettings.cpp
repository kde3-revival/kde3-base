/*
 * The settings of the Settings portal backend, see portalsettings.h.
 *
 * Copyright (C) 2026 Serghei Amelian
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 */

// GLib first: Qt defines "signals", which GLib uses as an identifier
#include <glib.h>
#include <stdlib.h>
#include <string.h>

#include <qcolor.h>
#include <qfont.h>

#include <kconfig.h>

#include "portalsettings.h"
#include "xsettingsdata.h"

namespace KPortalSettings
{

static const char interfaceNs[] = "org.gnome.desktop.interface";
static const char appearanceNs[] = "org.freedesktop.appearance";

bool prefersDark( const QColor &background )
{
    // the luma of ITU-R BT.601, as qGray() takes it
    return qGray( background.rgb() ) < 128;
}

GVariant *collect( KConfig &globals, KConfig &input, KConfig &fonts,
		   const char *xcursorTheme, const char *xcursorSize )
{
    GVariantBuilder interface;
    g_variant_builder_init( &interface, G_VARIANT_TYPE_VARDICT );

    // the fonts, as KGlobalSettings::generalFont() and fixedFont() read
    // them, in the Pango form that kded_xsettings gives to Gtk/FontName;
    // KDE has no font of its own for documents
    globals.setGroup( "General" );
    QFont general( "Sans Serif", 10 );
    general.setStyleHint( QFont::SansSerif );
    general = globals.readFontEntry( "font", &general );
    const QCString generalName = KXSettings::pangoFontName( general ).utf8();
    g_variant_builder_add( &interface, "{sv}", "font-name", g_variant_new_string( generalName ) );
    g_variant_builder_add( &interface, "{sv}", "document-font-name", g_variant_new_string( generalName ) );

    QFont fixed( "Monospace", 10 );
    fixed.setStyleHint( QFont::TypeWriter );
    fixed = globals.readFontEntry( "fixed", &fixed );
    g_variant_builder_add( &interface, "{sv}", "monospace-font-name",
			   g_variant_new_string( KXSettings::pangoFontName( fixed ).utf8() ) );

    // the DPI the fonts module forces (krdb's Xft.dpi), else the 96 that
    // krdb sets: GTK takes 96 times this factor
    fonts.setGroup( "General" );
    const int dpi = fonts.readNumEntry( "forceFontDPI", 0 );
    g_variant_builder_add( &interface, "{sv}", "text-scaling-factor",
			   g_variant_new_double( dpi > 0 ? dpi / 96.0 : 1.0 ) );

    // the cursor theme and size, as kded_xsettings takes them (without
    // the X resources, which the portal cannot follow)
    input.setGroup( "Mouse" );
    QCString cursorTheme = input.readEntry( "cursorTheme" ).utf8();
    if ( cursorTheme.isEmpty() )
	cursorTheme = xcursorTheme;
    if ( !cursorTheme.isEmpty() )
	g_variant_builder_add( &interface, "{sv}", "cursor-theme", g_variant_new_string( cursorTheme ) );
    int cursorSize = input.readNumEntry( "cursorSize", 0 );
    if ( cursorSize <= 0 && xcursorSize )
	cursorSize = atoi( xcursorSize );
    if ( cursorSize > 0 )
	g_variant_builder_add( &interface, "{sv}", "cursor-size", g_variant_new_int32( cursorSize ) );

    // the colours, with the defaults of KApplication::createApplicationPalette()
    const QColor kde34Background( 239, 239, 239 );
    const QColor kde34Blue( 103, 141, 178 );
    const QColor background = globals.readColorEntry( "background", &kde34Background );
    const QColor highlight = globals.readColorEntry( "selectBackground", &kde34Blue );
    const bool dark = prefersDark( background );
    g_variant_builder_add( &interface, "{sv}", "color-scheme",
			   g_variant_new_string( dark ? "prefer-dark" : "default" ) );

    GVariantBuilder appearance;
    g_variant_builder_init( &appearance, G_VARIANT_TYPE_VARDICT );
    // 1: prefer dark; a light scheme is no preference, as GNOME's "default"
    g_variant_builder_add( &appearance, "{sv}", "color-scheme", g_variant_new_uint32( dark ? 1 : 0 ) );
    g_variant_builder_add( &appearance, "{sv}", "accent-color",
			   g_variant_new( "(ddd)", highlight.red() / 255.0,
					  highlight.green() / 255.0, highlight.blue() / 255.0 ) );
    g_variant_builder_add( &appearance, "{sv}", "contrast", g_variant_new_uint32( 0 ) );

    GVariantBuilder all;
    g_variant_builder_init( &all, G_VARIANT_TYPE( "a{sa{sv}}" ) );
    g_variant_builder_add( &all, "{s@a{sv}}", interfaceNs, g_variant_builder_end( &interface ) );
    g_variant_builder_add( &all, "{s@a{sv}}", appearanceNs, g_variant_builder_end( &appearance ) );
    return g_variant_ref_sink( g_variant_builder_end( &all ) );
}

static bool matches( const char *ns, const char *const *patterns )
{
    if ( !patterns || !*patterns )
	return true;
    for ( ; *patterns; ++patterns ) {
	const char *p = *patterns;
	const size_t len = strlen( p );
	if ( len == 0 || strcmp( p, ns ) == 0 )
	    return true;
	if ( p[len - 1] == '*' && strncmp( p, ns, len - 1 ) == 0 )
	    return true;
    }
    return false;
}

GVariant *filter( GVariant *all, const char *const *patterns )
{
    GVariantBuilder out;
    g_variant_builder_init( &out, G_VARIANT_TYPE( "a{sa{sv}}" ) );
    GVariantIter iter;
    const char *ns;
    GVariant *values;
    g_variant_iter_init( &iter, all );
    while ( g_variant_iter_next( &iter, "{&s@a{sv}}", &ns, &values ) ) {
	if ( matches( ns, patterns ) )
	    g_variant_builder_add( &out, "{s@a{sv}}", ns, values );
	g_variant_unref( values );
    }
    return g_variant_ref_sink( g_variant_builder_end( &out ) );
}

GVariant *lookup( GVariant *all, const char *ns, const char *key )
{
    GVariant *values = g_variant_lookup_value( all, ns, G_VARIANT_TYPE_VARDICT );
    if ( !values )
	return 0;
    GVariant *value = g_variant_lookup_value( values, key, 0 );
    g_variant_unref( values );
    return value;
}

void compare( GVariant *before, GVariant *now,
	      void ( *changed )( const char *ns, const char *key, GVariant *value, void *data ),
	      void *data )
{
    GVariantIter nsIter;
    const char *ns;
    GVariant *values;
    g_variant_iter_init( &nsIter, now );
    while ( g_variant_iter_next( &nsIter, "{&s@a{sv}}", &ns, &values ) ) {
	GVariantIter keyIter;
	const char *key;
	GVariant *value;
	g_variant_iter_init( &keyIter, values );
	while ( g_variant_iter_next( &keyIter, "{&sv}", &key, &value ) ) {
	    GVariant *old = before ? lookup( before, ns, key ) : 0;
	    if ( !old || !g_variant_equal( old, value ) )
		changed( ns, key, value, data );
	    if ( old )
		g_variant_unref( old );
	    g_variant_unref( value );
	}
	g_variant_unref( values );
    }
}

}
