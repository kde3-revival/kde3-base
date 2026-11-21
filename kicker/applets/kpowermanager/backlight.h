/*
 * Copyright (C) 2012 Serghei Amelian <serghei.amelian@gmail.com>
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

#ifndef _BACKLIGHT_H_
#define _BACKLIGHT_H_


/*
 * The brightness of the screen's backlight: read from sysfs
 * (/sys/class/backlight), changed through systemd-logind's
 * org.freedesktop.login1.Session.SetBrightness, which needs no root.
 * backlight.cpp is the only file with GLib headers.
 *
 * Of several backlight devices, the firmware one is preferred, then the
 * platform one, then the raw one (as systemd and the other desktops do).
 * Without any, maxBrightness() is 0.
 *
 * For testing, KPOWERMANAGER_BACKLIGHT_SYSFS replaces /sys/class/backlight
 * and DBUS_SYSTEM_BUS_ADDRESS points to another bus with a fake logind.
 */
class Backlight {
public:
    Backlight();
    ~Backlight();

    int minBrightness() const;
    int maxBrightness() const;

    int brightness() const;
    void setBrightness(int level);

private:
    Backlight(const Backlight &);
    Backlight &operator=(const Backlight &);

    struct Private;
    Private *d;
};


#endif
