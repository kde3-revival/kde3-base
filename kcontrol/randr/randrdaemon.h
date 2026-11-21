/*
 * kded_randr: keeps the arrangement of the monitors (displayrc) when
 * monitors are connected or disconnected, and remembers it.
 *
 * Copyright (C) 2026 Serghei Amelian
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 */

#ifndef RANDRDAEMON_H
#define RANDRDAEMON_H

#include <qmap.h>
#include <qtimer.h>

#include <kdedmodule.h>

#include "randrpolicy.h"

class QSocketNotifier;
typedef struct _XDisplay Display;

class RandRDaemon : public KDEDModule
{
    Q_OBJECT
    K_DCOP

public:
    RandRDaemon( const QCString &obj );
    ~RandRDaemon();

k_dcop:
    // applies the saved profile of the connected monitors (or one
    // derived from what is known about them) now; false if that failed
    // or the monitors are in power save
    bool reconfigure();
    // the identity of the connected monitors (the profile name)
    QString connectedSet();

private slots:
    void readEvents();
    void decide();

private:
    bool query( KRandR::ScreenState &state );
    bool apply( const KRandR::Layout &layout, const QString &set );
    bool monitorsAsleep();
    bool enabled();

    Display *m_dpy;
    int m_eventBase;
    unsigned long m_edidAtom;
    QSocketNotifier *m_notifier;
    QTimer m_timer;
    KRandR::HotplugPolicy m_policy;
    // the connection of each output, as the events report it
    QMap<unsigned long, bool> m_connected;
    bool m_connectionChanged;
    long m_lastEvent;
};

#endif
