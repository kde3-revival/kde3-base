/*
 * Control module for the monitors: arrangement, resolution, refresh rate,
 * rotation and the primary monitor (RandR 1.2).
 *
 * Copyright (C) 2026 Serghei Amelian
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 */

#ifndef DISPLAYMODULE_H
#define DISPLAYMODULE_H

#include <kcmodule.h>

#include "randrcore.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;
class MonitorCanvas;

class DisplayModule : public KCModule
{
    Q_OBJECT

public:
    DisplayModule( QWidget *parent, const char *name, const QStringList & );

    virtual void load();
    virtual void save();
    virtual void defaults();
    virtual QString quickHelp() const;

private slots:
    void outputSelected( int index );
    void monitorMoved();
    void enabledToggled( bool on );
    void primaryToggled( bool on );
    void resolutionChanged( int index );
    void rateChanged( int index );
    void rotationChanged( int index );
    void hotplugToggled( bool on );
    void identify();
    void desktopResized();
    void markUnsaved();

private:
    void updateControls();
    void fillRates();
    void setModuleChanged( bool layoutChanged );
    // the current monitor's size changed: keep its neighbours next to it
    void sizeChanged( int bx, int by, int bw, int bh );
    bool confirm();
    // save() did not save: keep Apply available
    void stillChanged();
    const KRandR::Output *currentOutput() const;

    bool m_available;
    bool m_applying;
    bool m_updating;
    bool m_layoutChanged;
    KRandR::ScreenState m_state;
    KRandR::Layout m_layout;
    int m_current;
    QValueList<QSize> m_sizes;      // of the resolution combo
    QValueList<double> m_rates;     // of the rate combo
    QValueList<int> m_rotations;    // of the rotation combo

    MonitorCanvas *m_canvas;
    QComboBox *m_output;
    QCheckBox *m_enabled;
    QCheckBox *m_primary;
    QComboBox *m_resolution;
    QComboBox *m_rate;
    QComboBox *m_rotation;
    QPushButton *m_identify;
    QCheckBox *m_hotplug;
    QLabel *m_note;
};

#endif
