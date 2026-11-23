/*
 * kded_touchpad: applies the touchpad settings (touchpadrc, see
 * touchpadsettings.h) when the session starts and to every touchpad that
 * the X server adds or enables later (XInput 2 hierarchy events).
 *
 * Copyright (C) 2026 Serghei Amelian
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 */

#ifndef TOUCHPADDAEMON_H
#define TOUCHPADDAEMON_H

#include <qtimer.h>

#include <kdedmodule.h>

class QSocketNotifier;
typedef struct _XDisplay Display;

class TouchpadDaemon : public KDEDModule
{
    Q_OBJECT
    K_DCOP

public:
    TouchpadDaemon( const QCString &obj );
    ~TouchpadDaemon();

k_dcop:
    // reads touchpadrc again and applies it to every touchpad; the
    // number of libinput touchpads
    int reconfigure();

private slots:
    void readEvents();
    void apply();

private:
    // an own connection: the hierarchy events never reach the Qt event
    // loop of kded
    Display *m_dpy;
    int m_opcode;
    QSocketNotifier *m_notifier;
    QTimer m_timer;
};

#endif
