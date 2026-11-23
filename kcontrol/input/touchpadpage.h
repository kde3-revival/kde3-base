/*
 * The Touchpad tab of the Mouse control module: the settings of the
 * libinput X driver's touchpads (see touchpad/touchpadsettings.h),
 * applied when saved.  Without a touchpad it says so.
 *
 * Copyright (C) 2026 Serghei Amelian
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 */

#ifndef TOUCHPADPAGE_H
#define TOUCHPADPAGE_H

#include <qvaluelist.h>
#include <qwidget.h>

#include "touchpadsettings.h"

class QCheckBox;
class QComboBox;
class QRadioButton;
class KDoubleNumInput;

class TouchpadPage : public QWidget
{
    Q_OBJECT

public:
    TouchpadPage( QWidget *parent = 0, const char *name = 0 );

    // touchpadrc, or the defaults
    void load( bool useDefaults );
    // writes touchpadrc and applies it to the touchpads
    void save();

signals:
    void changed();

private slots:
    void updateEnabled();

private:
    KTouchpad::Settings fromWidgets() const;
    void toWidgets( const KTouchpad::Settings &s );
    bool has( unsigned feature ) const { return m_features & feature; }

    KTouchpad::DeviceList m_devices;
    // of the first libinput touchpad: the defaults that touchpadrc
    // leaves out
    KTouchpad::Settings m_defaults;
    KTouchpad::Settings m_loaded;
    // the settings touchpadrc names (KTouchpad::Feature bits): what the
    // user chose, not each device's default
    unsigned m_named;
    // what any of the libinput touchpads has
    unsigned m_features;
    unsigned m_scrollMethods;
    bool m_libinput;

    QCheckBox *m_tap;
    QCheckBox *m_tapDrag;
    QCheckBox *m_tapDragLock;
    QComboBox *m_tapButtonMap;
    QRadioButton *m_scrollTwoFinger;
    QRadioButton *m_scrollEdge;
    QRadioButton *m_scrollNone;
    QCheckBox *m_naturalScroll;
    KDoubleNumInput *m_accelSpeed;
    QComboBox *m_accelProfile;
    QValueList<int> m_accelProfiles;	// of the combo's items
    QComboBox *m_clickMethod;
    QValueList<int> m_clickMethods;	// of the combo's items
    QCheckBox *m_middleEmulation;
    QCheckBox *m_disableWhileTyping;
    QCheckBox *m_disableOnExternalMouse;
};

#endif
