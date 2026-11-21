/*
    Copyright (C) 2026 Serghei Amelian

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
    02110-1301, USA.
*/

#ifndef TRAYITEM_H
#define TRAYITEM_H

#include <qpixmap.h>
#include <qstring.h>
#include <qstringlist.h>

class QWidget;

/*
 * An icon of the system tray: an embedded window (XEmbed) or a
 * StatusNotifierItem.  The tray gives each one the same square.
 */
class TrayItem
{
public:
    virtual ~TrayItem() {}

    virtual QWidget *widget() = 0;

    // The name in the "Configure System Tray" dialog.
    virtual QString displayName() const = 0;
    // What the dialog keeps in the list of hidden icons for it.
    virtual QString hideKey() const = 0;
    // Whether an entry of that list stands for this icon.
    virtual bool isListed(const QStringList &hidden) const = 0;
    // An icon among the hidden ones regardless of the list (a passive
    // StatusNotifierItem).
    virtual bool isPassive() const { return false; }
    virtual QPixmap dialogIcon() const = 0;

    // The side of the square, in pixels.
    virtual void setIconSize(int size) = 0;
    virtual void setBackground() {}
};

#endif
