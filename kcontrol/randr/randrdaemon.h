/*
 * kded_randr: applies the saved display configuration (displayrc) when
 * monitors are connected or disconnected.
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

#include <qtimer.h>

#include <kdedmodule.h>

class QSocketNotifier;
namespace KRandR { struct ScreenState; }
typedef struct _XDisplay Display;

class RandRDaemon : public KDEDModule
{
    Q_OBJECT
    K_DCOP

public:
    RandRDaemon( const QCString &obj );
    ~RandRDaemon();

k_dcop:
    // applies the saved profile of the connected monitors (or the
    // default layout) now; false if that failed
    bool reconfigure();
    // the identity of the connected monitors (the profile name)
    QString connectedSet();

private slots:
    void readEvents();
    void outputsChanged();

private:
    bool query( KRandR::ScreenState &state );
    bool apply( const KRandR::ScreenState &state );
    bool monitorsAsleep();

    Display *m_dpy;
    int m_eventBase;
    unsigned long m_edidAtom;
    QSocketNotifier *m_notifier;
    QTimer m_settle;
    QString m_lastSet;
    bool m_wasAsleep;
};

#endif
