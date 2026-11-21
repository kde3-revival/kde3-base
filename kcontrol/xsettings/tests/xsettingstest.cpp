/*
 * Tests of the XSETTINGS property format and of the mapping of KDE's
 * settings (xsettingsdata).  The property is checked against a reader
 * written after the specification, as GTK's xsettings client reads it.
 *
 *   xsettingstest        run the tests, exit status = failures
 */

#include "xsettingsdata.h"

#include <qfont.h>


#include <stdio.h>
#include <string.h>

using namespace KXSettings;

static int failures = 0;

#define CHECK( cond ) do { if ( !( cond ) ) { \
    printf( "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond ); ++failures; } } while ( 0 )
#define CHECK_EQ( a, b ) do { if ( !( ( a ) == ( b ) ) ) { \
    printf( "FAIL %s:%d: %s == %s\n", __FILE__, __LINE__, #a, #b ); ++failures; } } while ( 0 )

// --- a reader after the specification ----------------------------------------

struct Parsed
{
    bool ok;
    unsigned long serial;
    QMap<QCString, Value> settings;
};

static Q_UINT32 get32( const QByteArray &a, uint pos, bool msb )
{
    const unsigned char *p = reinterpret_cast<const unsigned char *>( a.data() ) + pos;
    return msb ? ( Q_UINT32( p[0] ) << 24 | p[1] << 16 | p[2] << 8 | p[3] )
	       : ( Q_UINT32( p[3] ) << 24 | p[2] << 16 | p[1] << 8 | p[0] );
}

static unsigned get16( const QByteArray &a, uint pos, bool msb )
{
    const unsigned char *p = reinterpret_cast<const unsigned char *>( a.data() ) + pos;
    return msb ? ( p[0] << 8 | p[1] ) : ( p[1] << 8 | p[0] );
}

static Parsed parse( const QByteArray &a )
{
    Parsed r;
    r.ok = false;
    if ( a.size() < 12 || a.size() % 4 )
	return r;
    const bool msb = a[0] == 1;
    r.serial = get32( a, 4, msb );
    const Q_UINT32 n = get32( a, 8, msb );
    uint pos = 12;
    for ( Q_UINT32 i = 0; i < n; ++i ) {
	if ( pos + 4 > a.size() )
	    return r;
	Value v;
	v.type = static_cast<Value::Type>( a[pos] );
	const unsigned len = get16( a, pos + 2, msb );
	pos += 4;
	if ( pos + ( ( len + 3 ) & ~3u ) + 4 > a.size() )
	    return r;
	QCString name( a.data() + pos, len + 1 );
	pos += ( len + 3 ) & ~3u;
	v.serial = get32( a, pos, msb );
	pos += 4;
	if ( v.type == Value::Int ) {
	    v.i = static_cast<Q_INT32>( get32( a, pos, msb ) );
	    pos += 4;
	} else if ( v.type == Value::String ) {
	    const Q_UINT32 slen = get32( a, pos, msb );
	    pos += 4;
	    if ( pos + ( ( slen + 3 ) & ~3u ) > a.size() )
		return r;
	    v.s = QCString( a.data() + pos, slen + 1 );
	    pos += ( slen + 3 ) & ~3u;
	} else
	    return r;
	r.settings.insert( name, v );
    }
    r.ok = pos == a.size();
    return r;
}

// --- tests -------------------------------------------------------------------

static void testTable()
{
    Table t;
    Table::Map m;
    m["Xft/DPI"] = Table::intValue( 96 * 1024 );
    m["Gtk/FontName"] = Table::stringValue( QString::fromUtf8( "Noto Sans, 10" ) );
    m["Net/CursorBlink"] = Table::intValue( -1 );
    m["Abc"] = Table::stringValue( "" );   // an empty string, a name that needs padding
    CHECK( t.update( m ) );
    CHECK_EQ( t.serial(), 1ul );

    Parsed p = parse( t.serialize() );
    CHECK( p.ok );
    CHECK_EQ( p.serial, 1ul );
    CHECK_EQ( p.settings.count(), 4u );
    CHECK_EQ( p.settings["Xft/DPI"].type, Value::Int );
    CHECK_EQ( p.settings["Xft/DPI"].i, 98304 );
    CHECK_EQ( p.settings["Net/CursorBlink"].i, -1 );
    CHECK_EQ( p.settings["Gtk/FontName"].type, Value::String );
    CHECK_EQ( p.settings["Gtk/FontName"].s, QCString( "Noto Sans, 10" ) );
    CHECK_EQ( p.settings["Abc"].s, QCString( "" ) );
    CHECK_EQ( p.settings["Gtk/FontName"].serial, 1ul );

    // the same again: no change, the serial stays
    CHECK( !t.update( m ) );
    CHECK_EQ( t.serial(), 1ul );

    // one value changes: only it gets the new serial
    m["Xft/DPI"] = Table::intValue( 120 * 1024 );
    CHECK( t.update( m ) );
    p = parse( t.serialize() );
    CHECK( p.ok );
    CHECK_EQ( p.serial, 2ul );
    CHECK_EQ( p.settings["Xft/DPI"].serial, 2ul );
    CHECK_EQ( p.settings["Xft/DPI"].i, 120 * 1024 );
    CHECK_EQ( p.settings["Gtk/FontName"].serial, 1ul );

    // a setting that goes away is a change
    m.remove( "Abc" );
    CHECK( t.update( m ) );
    CHECK_EQ( t.serial(), 3ul );
    p = parse( t.serialize() );
    CHECK( p.ok );
    CHECK_EQ( p.settings.count(), 3u );

    // the type changes with the same name: a change
    m["Net/CursorBlink"] = Table::stringValue( "-1" );
    CHECK( t.update( m ) );

    // the empty table
    Table e;
    p = parse( e.serialize() );
    CHECK( p.ok );
    CHECK_EQ( p.settings.count(), 0u );
}

static void testFontName()
{
    QFont f( "Sans Serif", 10 );
    CHECK_EQ( pangoFontName( f ), QString( "Sans Serif 10" ) );
    f.setBold( true );
    CHECK_EQ( pangoFontName( f ), QString( "Sans Serif Bold 10" ) );
    f.setItalic( true );
    CHECK_EQ( pangoFontName( f ), QString( "Sans Serif Bold Italic 10" ) );
    QFont g( "Noto Sans [GOOG]", 9, QFont::Light );
    CHECK_EQ( pangoFontName( g ), QString( "Noto Sans Light 9" ) );
    QFont h( "Ubuntu Condensed", 11, QFont::DemiBold );
    CHECK_EQ( pangoFontName( h ), QString( "Ubuntu Condensed, Semi-Bold 11" ) );
    QFont px( "DejaVu Sans" );
    px.setPixelSize( 13 );
    CHECK_EQ( pangoFontName( px ), QString( "DejaVu Sans 13px" ) );
    QFont half( "Sans", 10 );
    half.setPointSizeFloat( 10.5 );
    CHECK_EQ( pangoFontName( half ), QString( "Sans 10.5" ) );
    QFont black( "Sans", 12, QFont::Black );
    CHECK_EQ( pangoFontName( black ), QString( "Sans Heavy 12" ) );
    QFont semi( "Foo Semi-Light", 10 );
    CHECK_EQ( pangoFontName( semi ), QString( "Foo Semi-Light, 10" ) );
    QFont num( "Font 3", 10 );
    CHECK_EQ( pangoFontName( num ), QString( "Font 3, 10" ) );
}

static void testCursorSize()
{
    CHECK_EQ( xcursorDefaultSize( 96, 5120, 1440 ), 21 );
    CHECK_EQ( xcursorDefaultSize( 0, 5120, 1440 ), 30 );
    CHECK_EQ( xcursorDefaultSize( 0, 1024, 768 ), 16 );
    CHECK_EQ( xcursorDefaultSize( 120, 1024, 768 ), 26 );
}

int main()
{
    testTable();
    testFontName();
    testCursorSize();
    if ( failures )
	printf( "%d failure(s)\n", failures );
    else
	printf( "all passed\n" );
    return failures;
}
