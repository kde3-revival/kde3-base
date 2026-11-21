/*
 * kded_xsettings: the XSETTINGS manager of the KDE session.  It tells
 * the toolkits that read XSETTINGS (GTK 2/3/4 and, through their GTK
 * platform theme, Qt 5/6; LibreOffice, Firefox and Chromium/Electron
 * through GTK) KDE's font, DPI, anti-aliasing, cursor and mouse
 * settings, so that they look like the KDE applications.
 *
 * Copyright (C) 2026 Serghei Amelian
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 */

#ifndef XSETTINGSD_H
#define XSETTINGSD_H

#include <qtimer.h>

#include <kdedmodule.h>

#include "xsettingsdata.h"

class QSocketNotifier;
typedef struct _XDisplay Display;

class XSettingsDaemon : public KDEDModule
{
    Q_OBJECT
    K_DCOP

public:
    XSettingsDaemon( const QCString &obj );
    ~XSettingsDaemon();

k_dcop:
    // reads the settings again and publishes what changed
    void reconfigure();
    // true while this module is the XSETTINGS manager of the display
    bool isManager();

private slots:
    void readEvents();
    void update();
    void fontChanged();
    void settingsChanged( int category );

private:
    bool acquire();
    void release();
    KXSettings::Table::Map collect();
    void publish();

    Display *m_dpy;
    unsigned long m_window;
    unsigned long m_selection;
    unsigned long m_settingsAtom;
    unsigned long m_resourceAtom;
    QSocketNotifier *m_notifier;
    QTimer m_timer;
    KXSettings::Table m_table;
};

#endif
