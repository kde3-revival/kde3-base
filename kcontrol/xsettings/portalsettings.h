/*
 * The settings that xdg-desktop-portal-kde3 gives to the Settings portal
 * (org.freedesktop.impl.portal.Settings): KDE's fonts, cursor, colour
 * scheme and accent colour, as GNOME's org.gnome.desktop.interface keys
 * and the freedesktop.org org.freedesktop.appearance keys.  Kept free of
 * D-Bus, for the tests.
 *
 * Copyright (C) 2026 Serghei Amelian
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 */

#ifndef PORTALSETTINGS_H
#define PORTALSETTINGS_H

class KConfig;
class QColor;

typedef struct _GVariant GVariant;

namespace KPortalSettings
{

// KDE's settings as an a{sa{sv}} (namespace -> key -> value), a new
// reference (not floating).  globals, input and fonts are kdeglobals,
// kcminputrc and kcmfonts; xcursorTheme and xcursorSize are
// $XCURSOR_THEME and $XCURSOR_SIZE (or null), libXcursor's choice when
// the mouse module has none.
//
// Only what KDE decides is there, so that xdg-desktop-portal takes
// everything else (the GTK and icon themes, the clock format, ...) from
// the next backend, xdg-desktop-portal-gtk.  The cursor theme and size
// are left out when neither the mouse module nor the environment set
// them.
GVariant *collect( KConfig &globals, KConfig &input, KConfig &fonts,
		   const char *xcursorTheme, const char *xcursorSize );

// true if a colour scheme with this window background is a dark one
bool prefersDark( const QColor &background );

// the namespaces of all (as from collect()) that match one of the
// patterns (ReadAll(): a namespace, or a prefix followed by '*'; no
// patterns or an empty one match all), a new reference
GVariant *filter( GVariant *all, const char *const *patterns );

// the value of namespace.key in all, a new reference or null
GVariant *lookup( GVariant *all, const char *ns, const char *key );

// calls changed() for each key of now that is not in before or has
// another value there
void compare( GVariant *before, GVariant *now,
	      void ( *changed )( const char *ns, const char *key, GVariant *value, void *data ),
	      void *data );

}

#endif
