/*
 * The touchpad settings of the Mouse control module (touchpadrc) and
 * their application to the touchpads of the X server: the XInput 2
 * device properties of the libinput X driver (xf86-input-libinput).
 * Shared by the control module, which applies them when they are saved,
 * and kded_touchpad, which applies them at login and to every touchpad
 * that appears later.
 *
 * A setting that touchpadrc does not name keeps the default of each
 * device (its "libinput ... Default" property), except tap-to-click,
 * which is on unless touchpadrc turns it off (libinput's default is
 * off).  A setting a device lacks (no property) is left alone.
 *
 * Copyright (C) 2026 Serghei Amelian
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 */

#ifndef TOUCHPADSETTINGS_H
#define TOUCHPADSETTINGS_H

#include <qstring.h>
#include <qvaluelist.h>

class KConfig;
typedef struct _XDisplay Display;

namespace KTouchpad
{

// the index of each method in the libinput properties' arrays
enum ScrollMethod { ScrollNone = -1, ScrollTwoFinger = 0, ScrollEdge = 1 };
enum ClickMethod { ClickNone = -1, ClickButtonAreas = 0, ClickFinger = 1 };
enum AccelProfile { AccelAdaptive = 0, AccelFlat = 1 };
// the buttons of a tap with 1, 2 and 3 fingers
enum TapButtonMap { TapLeftRightMiddle = 0, TapLeftMiddleRight = 1 };

// the settings a device has, and the settings touchpadrc names (one bit
// each)
enum Feature {
    HasTap = 1 << 0,
    HasTapDrag = 1 << 1,
    HasTapDragLock = 1 << 2,
    HasTapButtonMap = 1 << 3,
    HasScrollMethod = 1 << 4,
    HasNaturalScroll = 1 << 5,
    HasDisableWhileTyping = 1 << 6,
    HasAccelSpeed = 1 << 7,
    HasAccelProfile = 1 << 8,
    HasClickMethod = 1 << 9,
    HasMiddleEmulation = 1 << 10,
    HasDisableOnExternalMouse = 1 << 11
};

struct Settings
{
    Settings();

    bool tap;
    bool tapDrag;
    bool tapDragLock;
    int tapButtonMap;		// TapButtonMap
    int scrollMethod;		// ScrollMethod
    bool naturalScroll;
    bool disableWhileTyping;
    double accelSpeed;		// -1 (slowest) .. 1 (fastest)
    int accelProfile;		// AccelProfile
    int clickMethod;		// ClickMethod
    bool middleEmulation;
    bool disableOnExternalMouse;

    bool operator==( const Settings &o ) const;
    bool operator!=( const Settings &o ) const { return !( *this == o ); }
};

struct Device
{
    Device() : id( -1 ), libinput( false ), features( 0 ), scrollMethods( 0 ),
	       clickMethods( 0 ), accelProfiles( 0 ) {}

    int id;			// XInput 2 device id
    QString name;
    bool libinput;		// driven by xf86-input-libinput
    unsigned features;		// Feature bits
    unsigned scrollMethods;	// 1 << ScrollMethod, of the available ones
    unsigned clickMethods;	// 1 << ClickMethod
    unsigned accelProfiles;	// 1 << AccelProfile
};

typedef QValueList<Device> DeviceList;

// the touchpads of the display (libinput or not), in XInput 2 id order;
// empty without XInput 2
DeviceList touchpads( Display *dpy );

// the device's own defaults, but with tap-to-click on
Settings defaults( Display *dpy, const Device &dev );

// the settings of touchpadrc over the given defaults; *named gets the
// Feature bits of those touchpadrc names (with a valid value)
Settings load( KConfig *config, const Settings &defaults, unsigned *named = 0 );

// the Feature bits of the settings that differ
unsigned differences( const Settings &a, const Settings &b );

// writes to touchpadrc the named settings (Feature bits), whatever any
// device's default is, and removes the others: each device then keeps its
// own default for those
void save( KConfig *config, const Settings &settings, unsigned named );

// sets the device's properties to the settings (only those it has)
void apply( Display *dpy, const Device &dev, const Settings &settings );

// loads touchpadrc and applies it to every libinput touchpad of the
// display, each over its own defaults; the number of touchpads
int applyAll( Display *dpy );

}

#endif
