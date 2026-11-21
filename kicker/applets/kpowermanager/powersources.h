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

#ifndef _POWERSOURCES_H_
#define _POWERSOURCES_H_


#include <qglobal.h>


// What UPower says of the power sources: the mains (line power) and the
// battery, if there are any
struct PowerSources
{
    PowerSources()
    {
        daemon.connected = false;
        powerLine.present = false;
        powerLine.online = true;
        battery.present = false;
        battery.state = Battery::Unknown;
        battery.percentage = 0;
        battery.capacity = 0;
        battery.timeToEmpty = 0;
        battery.timeToFull = 0;
        battery.energyRate = 0;
    }

    struct Daemon
    {
        bool connected;
    } daemon;

    struct PowerLine
    {
        bool present; // UPower has a line power device
        bool online;  // without one: not running on battery
    } powerLine;

    struct Battery
    {
        // as UPower's UpDeviceState
        enum State
        {
            Unknown,
            Charging,
            Discharging,
            Empty,
            FullCharged,
            PendingCharge,
            PendingDischarge
        };

        bool present; // a battery that powers the machine
        State state;
        double percentage;
        double capacity;
        Q_INT64 timeToEmpty;
        Q_INT64 timeToFull;
        double energyRate;
    } battery;
};


#endif
