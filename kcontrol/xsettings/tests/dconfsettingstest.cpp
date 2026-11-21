/*
 * Tests of the session's dconf keys (dconfsettings.h): the profile
 * check and the changes.  With --write, also writes KDE's settings (of
 * $KDEHOME) to the session's database, for the tests of startkde's
 * database with a private session bus (tests/dconftest.sh).
 */

#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <kconfig.h>
#include <kinstance.h>

#include "../dconfsettings.h"
#include "../portalsettings.h"

static int failures = 0;

static void check( bool ok, const char *what )
{
    if ( !ok ) {
	fprintf( stderr, "FAIL: %s\n", what );
	++failures;
    }
}

static bool profileWith( const char *contents )
{
    char path[] = "/tmp/dconfsettingstest.XXXXXX";
    int fd = mkstemp( path );
    if ( fd < 0 )
	return false;
    FILE *f = fdopen( fd, "w" );
    fputs( contents, f );
    fclose( f );
    bool r = KDconfSettings::profileUsesSessionDb( path );
    unlink( path );
    return r;
}

static GVariant *settings( const char *font, int size )
{
    GVariantBuilder i;
    g_variant_builder_init( &i, G_VARIANT_TYPE_VARDICT );
    g_variant_builder_add( &i, "{sv}", "font-name", g_variant_new_string( font ) );
    g_variant_builder_add( &i, "{sv}", "cursor-size", g_variant_new_int32( size ) );
    GVariantBuilder a;
    g_variant_builder_init( &a, G_VARIANT_TYPE_VARDICT );
    g_variant_builder_add( &a, "{sv}", "color-scheme", g_variant_new_uint32( 1 ) );
    GVariantBuilder all;
    g_variant_builder_init( &all, G_VARIANT_TYPE( "a{sa{sv}}" ) );
    g_variant_builder_add( &all, "{s@a{sv}}", "org.gnome.desktop.interface", g_variant_builder_end( &i ) );
    g_variant_builder_add( &all, "{s@a{sv}}", "org.freedesktop.appearance", g_variant_builder_end( &a ) );
    return g_variant_ref_sink( g_variant_builder_end( &all ) );
}

int main( int argc, char **argv )
{
    if ( argc > 1 && strcmp( argv[1], "--write" ) == 0 ) {
	KInstance instance( "dconfsettingstest" );
	KConfig globals( "kdeglobals", true, false );
	KConfig input( "kcminputrc", true, false );
	KConfig fonts( "kcmfonts", true, false );
	GVariant *all = KPortalSettings::collect( globals, input, fonts, 0, 0 );
	GVariant *last = 0;
	KDconfSettings::sync( all, &last );
	const bool ok = last != 0;
	KDconfSettings::release( last );
	KDconfSettings::release( all );
	return ok ? 0 : 1;
    }

    check( !KDconfSettings::profileUsesSessionDb( 0 ), "null path" );
    check( !KDconfSettings::profileUsesSessionDb( "kde3" ), "relative path" );
    check( !KDconfSettings::profileUsesSessionDb( "/nonexistent/profile" ), "missing file" );
    check( profileWith( "user-db:kde3\n" ), "plain profile" );
    check( profileWith( "# comment\n\n  user-db:kde3  # db\nsystem-db:local\n" ), "comments and blanks" );
    check( !profileWith( "user-db:user\n" ), "user's database" );
    check( !profileWith( "system-db:local\nuser-db:kde3\n" ), "not the first database" );
    check( !profileWith( "user-db:kde3x\n" ), "other name" );

    GVariant *a = settings( "Sans 10", 24 );
    GVariant *b = settings( "Sans 12", 24 );
    GVariant *c = KDconfSettings::changes( a, 0 );
    check( c && g_variant_n_children( c ) == 2, "all keys the first time" );
    if ( c ) {
	GVariant *v = g_variant_lookup_value( c, "/org/gnome/desktop/interface/font-name", 0 );
	check( v && g_variant_is_of_type( v, G_VARIANT_TYPE( "mv" ) ), "font-name as a dconf path" );
	if ( v )
	    g_variant_unref( v );
	check( !g_variant_lookup_value( c, "/org/freedesktop/appearance/color-scheme", 0 ),
	       "no appearance keys" );
	g_variant_unref( c );
    }
    c = KDconfSettings::changes( a, a );
    check( c == 0, "no changes" );
    c = KDconfSettings::changes( b, a );
    check( c && g_variant_n_children( c ) == 1, "only the changed key" );
    if ( c )
	g_variant_unref( c );
    g_variant_unref( a );
    g_variant_unref( b );

    if ( failures )
	return 1;
    printf( "dconfsettingstest: all passed\n" );
    return 0;
}
