/*
 * The data side of the XSETTINGS manager (kded_xsettings): the table of
 * settings and its serialization into the _XSETTINGS_SETTINGS property
 * (freedesktop.org XSETTINGS specification 0.5), and the mapping of
 * KDE's settings to the values GTK and the other XSETTINGS clients
 * expect.  Kept free of X calls, for the tests.
 *
 * Copyright (C) 2026 Serghei Amelian
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 */

#ifndef XSETTINGSDATA_H
#define XSETTINGSDATA_H

#include <qcstring.h>
#include <qmap.h>
#include <qstring.h>

class QFont;

namespace KXSettings
{

struct Value
{
    enum Type { Int = 0, String = 1 };   // the types of the protocol (no Color)
    Type type;
    int i;
    QCString s;                          // UTF-8
    unsigned long serial;                // the serial of its last change

    bool operator==( const Value &o ) const
    { return type == o.type && ( type == Int ? i == o.i : s == o.s ); }
};

// The settings with their last-change serials.  update() takes the
// complete set of the moment: a setting that changed gets the serial of
// this update, one that is no more there is dropped, and the table's
// serial only advances when something changed.
class Table
{
public:
    Table() : m_serial( 0 ) {}

    // the set to apply: name -> value (serials are ignored)
    typedef QMap<QCString, Value> Map;
    static Value intValue( int i );
    static Value stringValue( const QString &s );

    // true if anything changed
    bool update( const Map &settings );
    unsigned long serial() const { return m_serial; }
    const Map &settings() const { return m_settings; }

    // the contents of the _XSETTINGS_SETTINGS property, in the byte
    // order of this machine (which it declares)
    QByteArray serialize() const;

private:
    Map m_settings;
    unsigned long m_serial;
};

// A font as a Pango font description string, which is what Gtk/FontName
// holds: "Family [Weight] [Italic] size", with a comma after a family
// that ends in a word Pango would take for a style ("Ubuntu Condensed,
// 11").  The foundry that Qt 3 appends as " [Foundry]" is dropped; a
// font with a pixel size gets "px".
QString pangoFontName( const QFont &font );

// libXcursor's default cursor size when neither XCURSOR_SIZE nor the
// Xcursor.size resource set one: from Xft.dpi if that is set (dpi > 0),
// otherwise the smaller screen dimension / 48.
int xcursorDefaultSize( int dpi, int screenWidth, int screenHeight );

}

#endif
