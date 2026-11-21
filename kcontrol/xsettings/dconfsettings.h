/*
 * The KDE-derived GSettings keys of the KDE 3 session's own dconf
 * database (user-db:kde3, ~/.config/dconf/kde3; see startkde): what
 * kded_xsettings writes there, and the writing itself.  Only that
 * database is ever written: the user's own one (user-db:user), which
 * the GNOME sessions use, is never named here.  Kept free of Qt, for
 * the tests.
 *
 * Copyright (C) 2026 Serghei Amelian
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 */

#ifndef DCONFSETTINGS_H
#define DCONFSETTINGS_H

typedef struct _GVariant GVariant;

namespace KDconfSettings
{

// the name of the session's database and the D-Bus object of
// dconf-service that writes it
extern const char dbName[];
extern const char writerPath[];

// true if the dconf profile file at path (the session's $DCONF_PROFILE)
// has the session's database as its writable (first) database
bool profileUsesSessionDb( const char *path );

// the dconf changes (a{smv}, "/org/gnome/desktop/interface/<key>" ->
// value) of the org.gnome.desktop.interface keys of all (from
// KPortalSettings::collect()) that are not the same in before (null: all
// of them), a new reference; null when nothing changed
GVariant *changes( GVariant *all, GVariant *before );

// sends changes to dconf-service for the session's database (and only
// that one); true if dconf-service took them
bool write( GVariant *changes );

// writes what changed in all since *last (null: everything); when
// dconf-service took it, *last becomes all (a new reference, the old one
// is released); else *last stays, so the next call tries again
void sync( GVariant *all, GVariant **last );

// g_variant_unref() of a non-null value, for the Qt side
void release( GVariant *value );

}

#endif
