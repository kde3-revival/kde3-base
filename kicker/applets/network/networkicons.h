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

#ifndef NETWORKICONS_H
#define NETWORKICONS_H

#include <qimage.h>

/**
 * The icons of the Wi-Fi signal and the airplane mode, which the icon
 * themes of KDE 3 do not have, drawn at the size asked for (smooth
 * edges, transparent background).
 */
namespace NetworkIcons
{
    enum Badge
    {
        NoBadge,
        ErrorBadge,     // a red circle with a white cross
        OffBadge        // a grey circle with a white bar
    };

    /** The Wi-Fi fan with @p bars of its 4 parts lit (0 to 4); greyed
        out when @p enabled is false. */
    QImage wifi(int size, int bars, bool enabled = true, Badge badge = NoBadge);

    QImage airplane(int size);

    /** Adds @p badge to the lower right corner of @p image. */
    QImage addBadge(const QImage &image, Badge badge);

    /** The number of bars for a signal of @p strength percent. */
    int bars(int strength);
}

#endif
