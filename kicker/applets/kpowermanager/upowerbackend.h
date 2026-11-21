/*
 * Copyright (C) 2012-2014 Serghei Amelian <serghei.amelian@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifndef _UPOWER_BACKEND_H_
#define _UPOWER_BACKEND_H_


#include <qobject.h>

#include "powersources.h"


/*
 * Follows the power sources UPower (org.freedesktop.UPower on the system
 * bus) knows, with libupower-glib.  upowerbackend.cpp is the only file
 * that includes GLib headers (before any Qt header: Qt 3 defines
 * "signals"); the library's signals are dispatched by the GLib main loop
 * Qt runs on.  Nothing blocks: the client and the devices are fetched
 * asynchronously, and while UPower cannot be reached the backend tries
 * again every half minute.
 *
 * For testing against a fake UPower, point DBUS_SYSTEM_BUS_ADDRESS to
 * another bus.
 */
class UPowerBackend : public QObject {

    Q_OBJECT

public:
    UPowerBackend();
    ~UPowerBackend();

    const PowerSources &powerSources() const;

signals:
    void powerSourcesChanged(const PowerSources &);

private slots:
    void initialize();

private:
    void changed();

    struct Private;
    friend struct Private;
    Private *d;
};


#endif
