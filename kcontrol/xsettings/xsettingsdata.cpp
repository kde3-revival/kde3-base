/*
 * The data side of the XSETTINGS manager, see xsettingsdata.h.
 *
 * Copyright (C) 2026 Serghei Amelian
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 */

#include "xsettingsdata.h"

#include <qfont.h>
#include <qregexp.h>

#include <string.h>

namespace KXSettings
{

Value Table::intValue( int i )
{
    Value v;
    v.type = Value::Int;
    v.i = i;
    v.serial = 0;
    return v;
}

Value Table::stringValue( const QString &s )
{
    Value v;
    v.type = Value::String;
    v.i = 0;
    v.s = s.utf8();
    v.serial = 0;
    return v;
}

bool Table::update( const Map &settings )
{
    bool changed = settings.count() != m_settings.count();
    const unsigned long next = m_serial + 1;
    Map result;
    for ( Map::ConstIterator it = settings.begin(); it != settings.end(); ++it ) {
	Value v = it.data();
	Map::ConstIterator old = m_settings.find( it.key() );
	if ( old != m_settings.end() && old.data() == v )
	    v.serial = old.data().serial;
	else {
	    v.serial = next;
	    changed = true;
	}
	result.insert( it.key(), v );
    }
    if ( changed ) {
	m_settings = result;
	m_serial = next;
    }
    return changed;
}

static void put8( QByteArray &a, uint &pos, unsigned char c )
{
    a[pos++] = c;
}

static void put16( QByteArray &a, uint &pos, unsigned short v )
{
    memcpy( a.data() + pos, &v, 2 );
    pos += 2;
}

static void put32( QByteArray &a, uint &pos, Q_UINT32 v )
{
    memcpy( a.data() + pos, &v, 4 );
    pos += 4;
}

static void putBytes( QByteArray &a, uint &pos, const char *data, uint len )
{
    memcpy( a.data() + pos, data, len );
    pos += len;
    while ( pos % 4 )
	a[pos++] = 0;
}

static uint padded( uint len )
{
    return ( len + 3 ) & ~3u;
}

QByteArray Table::serialize() const
{
    uint size = 12;
    for ( Map::ConstIterator it = m_settings.begin(); it != m_settings.end(); ++it ) {
	size += 4 + padded( it.key().length() ) + 4;
	if ( it.data().type == Value::Int )
	    size += 4;
	else
	    size += 4 + padded( it.data().s.length() );
    }

    QByteArray a( size );
    uint pos = 0;
    // the byte order of the data: the machine's own
    const unsigned short probe = 1;
    put8( a, pos, *reinterpret_cast<const unsigned char *>( &probe ) ? 0 /* LSBFirst */ : 1 /* MSBFirst */ );
    put8( a, pos, 0 );
    put8( a, pos, 0 );
    put8( a, pos, 0 );
    put32( a, pos, m_serial );
    put32( a, pos, m_settings.count() );
    for ( Map::ConstIterator it = m_settings.begin(); it != m_settings.end(); ++it ) {
	const Value &v = it.data();
	put8( a, pos, v.type );
	put8( a, pos, 0 );
	put16( a, pos, it.key().length() );
	putBytes( a, pos, it.key().data(), it.key().length() );
	put32( a, pos, v.serial );
	if ( v.type == Value::Int )
	    put32( a, pos, static_cast<Q_UINT32>( v.i ) );
	else {
	    put32( a, pos, v.s.length() );
	    putBytes( a, pos, v.s.data(), v.s.length() );
	}
    }
    return a;
}

// the words of pango_font_description_from_string() besides the size
// (pango-fonts.c), compared without case and '-' as Pango does
static bool isPangoStyleWord( const QString &word )
{
    static const char *const words[] = {
	"normal", "roman", "oblique", "italic",
	"smallcaps", "allsmallcaps", "petitecaps", "allpetitecaps", "unicase", "titlecaps",
	"ultracondensed", "extracondensed", "condensed", "semicondensed",
	"semiexpanded", "expanded", "extraexpanded", "ultraexpanded",
	"thin", "ultralight", "extralight", "light", "semilight", "demilight", "book",
	"regular", "medium", "semibold", "demibold", "bold", "ultrabold", "extrabold",
	"heavy", "black", "ultrablack", "extrablack", "ultraheavy",
	"notrotated", "south", "upsidedown", "north", "rotatedleft", "east",
	"rotatedright", "west", 0
    };
    QString w = word.lower();
    w.remove( '-' );
    if ( w.isEmpty() )
	return false;
    for ( int i = 0; words[i]; ++i )
	if ( w == words[i] )
	    return true;
    // a size, "10", "10.5" or "13px", or "@wght=500" (variations)
    if ( w[0] == '@' || w[0].isDigit() )
	return true;
    return false;
}

QString pangoFontName( const QFont &font )
{
    QString family = font.family();
    // Qt 3 tells fonts of the same family from different foundries
    // apart as "Family [Foundry]"
    family.replace( QRegExp( "\\s*\\[[^\\]]*\\]$" ), QString::null );
    if ( family.isEmpty() )
	family = "Sans";

    // Pango reads the words at the end as styles and the size, so a
    // family ending in one of those ("Ubuntu Condensed") gets the comma
    // that ends the family list.  Only then: simpler readers (Qt 5's
    // GNOME and GTK platform themes) take everything before the last
    // space as the family.
    QString name = family;
    if ( isPangoStyleWord( family.section( ' ', -1 ) ) )
	name += ",";
    const int w = font.weight();
    if ( w <= 18 )
	name += " Ultra-Light";
    else if ( w <= 37 )
	name += " Light";
    else if ( w >= 56 && w <= 68 )
	name += " Semi-Bold";
    else if ( w >= 69 && w <= 81 )
	name += " Bold";
    else if ( w >= 82 )
	name += " Heavy";
    if ( font.italic() )
	name += " Italic";
    if ( font.pointSizeFloat() > 0 )
	name += " " + QString::number( font.pointSizeFloat() );
    else
	name += " " + QString::number( font.pixelSize() ) + "px";
    return name;
}

int xcursorDefaultSize( int dpi, int screenWidth, int screenHeight )
{
    int size = dpi > 0 ? dpi * 16 / 72 : 0;
    if ( size == 0 )
	size = QMIN( screenWidth, screenHeight ) / 48;
    return size;
}

}
