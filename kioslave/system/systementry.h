/* This file is part of the KDE project

   This library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Library General Public
   License as published by the Free Software Foundation; either
   version 2 of the License, or (at your option) any later version.

   This library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Library General Public License for more details.

   You should have received a copy of the GNU Library General Public License
   along with this library; see the file COPYING.LIB.  If not, write to
   the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
   Boston, MA 02110-1301, USA.
*/

#ifndef SYSTEMENTRY_H
#define SYSTEMENTRY_H

#include <kdesktopfile.h>
#include <kglobalsettings.h>

#include <qdir.h>
#include <qstring.h>

/**
 * The local path of a system:/ entry.  An entry with
 * X-KDE-UserPath=document stands for the user's documents folder; it has
 * no path (and is not shown) when that folder is the home folder.  Its
 * path used to come from a shell command run through Path[$e]=$(...),
 * which KConfig no longer supports.
 */
inline QString systemEntryPath(const KDesktopFile &desktop)
{
	if (desktop.readEntry("X-KDE-UserPath") == "document") {
		QString doc = QDir::cleanDirPath(KGlobalSettings::documentPath());
		QString home = QDir::cleanDirPath(QDir::homeDirPath());
		return doc == home ? QString::null : doc;
	}
	return desktop.readPath();
}

#endif
