/*
 * The touchpad settings and the libinput X driver's device properties
 * (see touchpadsettings.h).
 *
 * The properties (xf86-input-libinput, libinput-properties.h): 8-bit
 * flags of XA_INTEGER, one each for a boolean ("... Enabled") or one per
 * method for a choice (at most one set: "... Method Enabled"), and the
 * 32-bit FLOAT "libinput Accel Speed".  The driver creates a property
 * only when the device has the setting, so its presence is the test.
 * XIGetProperty and XIChangeProperty take the values packed at their
 * own size (8 bits: one byte each; 32 bits: four bytes each).
 *
 * Copyright (C) 2026 Serghei Amelian
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 */

#include "touchpadsettings.h"

#include <math.h>
#include <string.h>

#include <qmap.h>

#include <kconfig.h>

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/extensions/XInput.h>
#include <X11/extensions/XInput2.h>

#include <vector>

namespace KTouchpad
{

namespace
{

// the properties (xf86-input-libinput)
const char propTap[] = "libinput Tapping Enabled";
const char propTapDrag[] = "libinput Tapping Drag Enabled";
const char propTapDragLock[] = "libinput Tapping Drag Lock Enabled";
const char propTapButtonMap[] = "libinput Tapping Button Mapping Enabled";
const char propTapButtonMapDefault[] = "libinput Tapping Button Mapping Default";
const char propNaturalScroll[] = "libinput Natural Scrolling Enabled";
const char propDisableWhileTyping[] = "libinput Disable While Typing Enabled";
const char propScrollMethods[] = "libinput Scroll Methods Available";
const char propScrollMethod[] = "libinput Scroll Method Enabled";
const char propClickMethods[] = "libinput Click Methods Available";
const char propClickMethod[] = "libinput Click Method Enabled";
const char propAccelSpeed[] = "libinput Accel Speed";
const char propAccelProfiles[] = "libinput Accel Profiles Available";
const char propAccelProfile[] = "libinput Accel Profile Enabled";
const char propMiddleEmulation[] = "libinput Middle Emulation Enabled";
const char propSendEventsModes[] = "libinput Send Events Modes Available";
const char propSendEventsMode[] = "libinput Send Events Mode Enabled";

// the index of "disabled on external mouse" in the send events modes
const int sendEventsOnExternalMouse = 1;

const char group[] = "Touchpad";

typedef std::vector<unsigned char> Bytes;

// the errors of the requests to a device (BadDevice when it went away,
// BadValue/BadMatch when the driver refuses a value): counted, not fatal
int trappedErrors = 0;

int trapError( Display *, XErrorEvent * )
{
    ++trappedErrors;
    return 0;
}

class ErrorTrap
{
public:
    explicit ErrorTrap( Display *dpy ) : m_dpy( dpy )
    {
	XSync( m_dpy, False );
	trappedErrors = 0;
	m_old = XSetErrorHandler( trapError );
    }
    ~ErrorTrap()
    {
	XSync( m_dpy, False );
	XSetErrorHandler( m_old );
    }

private:
    Display *m_dpy;
    XErrorHandler m_old;
};

Atom atom( Display *dpy, const char *name )
{
    return XInternAtom( dpy, name, True );
}

// the number of values the driver gives a property (its "... Default"
// and "... Available" companions have as many): one per method for a
// choice, else one; the acceleration profiles are adaptive and flat, and
// custom since xf86-input-libinput 1.3
void expectedCount( const char *name, unsigned *min, unsigned *max )
{
    const QString n = QString::fromLatin1( name );
    *min = *max = 1;
    if ( n.contains( "Scroll Method" ) )
	*min = *max = 3;
    else if ( n.contains( "Click Method" ) || n.contains( "Send Events Mode" )
	      || n.contains( "Tapping Button Mapping" ) )
	*min = *max = 2;
    else if ( n.contains( "Accel Profile" ) ) {
	*min = 2;
	*max = 3;
    }
}

// the 8-bit values of a property; empty when the device lacks it or it
// is not what the driver makes (XA_INTEGER, the expected count, whole)
Bytes readBytes( Display *dpy, int dev, const char *name )
{
    Bytes values;
    const Atom prop = atom( dpy, name );
    if ( prop == None )
	return values;
    Atom type;
    int format;
    unsigned long count, after;
    unsigned char *data = 0;
    if ( XIGetProperty( dpy, dev, prop, 0, 16, False, AnyPropertyType,
			&type, &format, &count, &after, &data ) != Success )
	return values;
    unsigned min, max;
    expectedCount( name, &min, &max );
    if ( type == XA_INTEGER && format == 8 && after == 0 && count >= min && count <= max )
	values.assign( data, data + count );
    if ( data )
	XFree( data );
    return values;
}

// the single FLOAT value of a property; false when the device lacks it
bool readFloat( Display *dpy, int dev, const char *name, double *value )
{
    const Atom prop = atom( dpy, name );
    const Atom floatType = atom( dpy, "FLOAT" );
    if ( prop == None || floatType == None )
	return false;
    Atom type;
    int format;
    unsigned long count, after;
    unsigned char *data = 0;
    if ( XIGetProperty( dpy, dev, prop, 0, 1, False, floatType,
			&type, &format, &count, &after, &data ) != Success )
	return false;
    const bool ok = type == floatType && format == 32 && count == 1 && after == 0;
    if ( ok ) {
	float f;
	memcpy( &f, data, sizeof( f ) );
	*value = f;
    }
    if ( data )
	XFree( data );
    return ok;
}

void writeBytes( Display *dpy, int dev, const char *name, const Bytes &values )
{
    const Atom prop = atom( dpy, name );
    if ( prop == None || values.empty() )
	return;
    XIChangeProperty( dpy, dev, prop, XA_INTEGER, 8, PropModeReplace,
		      const_cast<unsigned char *>( &values[0] ), values.size() );
}

void writeFloat( Display *dpy, int dev, const char *name, double value )
{
    const Atom prop = atom( dpy, name );
    const Atom floatType = atom( dpy, "FLOAT" );
    if ( prop == None || floatType == None )
	return;
    const float f = value;
    unsigned char data[sizeof( f )];
    memcpy( data, &f, sizeof( f ) );
    XIChangeProperty( dpy, dev, prop, floatType, 32, PropModeReplace, data, 1 );
}

// a boolean property: its value, or fallback when the device lacks it
bool readBool( Display *dpy, int dev, const QString &name, bool fallback )
{
    const Bytes v = readBytes( dpy, dev, name.latin1() );
    return v.empty() ? fallback : v[0] != 0;
}

void writeBool( Display *dpy, int dev, const char *name, bool on )
{
    Bytes v = readBytes( dpy, dev, name );
    if ( v.empty() )
	return;
    // a non-zero value other than 1 (a mode) stays as it is
    if ( ( v[0] != 0 ) == on )
	return;
    v[0] = on ? 1 : 0;
    writeBytes( dpy, dev, name, v );
}

// a choice: the index of the set flag, -1 for none, fallback when the
// device lacks the property
int readChoice( Display *dpy, int dev, const QString &name, int fallback )
{
    const Bytes v = readBytes( dpy, dev, name.latin1() );
    if ( v.empty() )
	return fallback;
    for ( unsigned i = 0; i < v.size(); ++i )
	if ( v[i] )
	    return i;
    return -1;
}

// the available ones of a choice, as bits
unsigned readAvailable( Display *dpy, int dev, const char *name )
{
    const Bytes v = readBytes( dpy, dev, name );
    unsigned bits = 0;
    for ( unsigned i = 0; i < v.size() && i < 32; ++i )
	if ( v[i] )
	    bits |= 1u << i;
    return bits;
}

// sets the choice index (-1: none) when the device has it available
void writeChoice( Display *dpy, int dev, const char *name, int index, unsigned available )
{
    if ( index >= 0 && !( available & ( 1u << index ) ) )
	return;
    Bytes v = readBytes( dpy, dev, name );
    if ( v.empty() || index >= int( v.size() ) )
	return;
    Bytes wanted( v.size(), 0 );
    if ( index >= 0 )
	wanted[index] = 1;
    if ( wanted != v )
	writeBytes( dpy, dev, name, wanted );
}

QString defaultOf( const char *name )
{
    return QString::fromLatin1( name ) + " Default";
}

// the names of touchpadrc's values
const char *const scrollNames[] = { "None", "TwoFinger", "Edge" };	// ScrollMethod + 1
const char *const clickNames[] = { "None", "ButtonAreas", "Clickfinger" };	// ClickMethod + 1
const char *const profileNames[] = { "Adaptive", "Flat" };
const char *const tapMapNames[] = { "LeftRightMiddle", "LeftMiddleRight" };

// the reading of touchpadrc's keys: a key that is there with a known
// value is named (its bit set in *named), else the fallback
int readName( KConfig *config, const char *key, const char *const *names, int count,
	      int offset, int fallback, unsigned bit, unsigned *named )
{
    if ( !config->hasKey( key ) )
	return fallback;
    const QString value = config->readEntry( key );
    for ( int i = 0; i < count; ++i )
	if ( value.lower() == QString::fromLatin1( names[i] ).lower() ) {
	    *named |= bit;
	    return i - offset;
	}
    return fallback;
}

bool readFlag( KConfig *config, const char *key, bool fallback, unsigned bit, unsigned *named )
{
    if ( !config->hasKey( key ) )
	return fallback;
    *named |= bit;
    return config->readBoolEntry( key, fallback );
}

// a named setting is written, the others removed
void writeName( KConfig *config, const char *key, const char *const *names,
		int offset, int value, bool named )
{
    if ( named )
	config->writeEntry( key, QString::fromLatin1( names[value + offset] ) );
    else
	config->deleteEntry( key );
}

void writeFlag( KConfig *config, const char *key, bool value, bool named )
{
    if ( named )
	config->writeEntry( key, value );
    else
	config->deleteEntry( key );
}

// the accel speed as touchpadrc and the control module keep it: in
// steps of 0.01
double roundSpeed( double speed )
{
    if ( speed < -1 )
	speed = -1;
    if ( speed > 1 )
	speed = 1;
    return floor( speed * 100 + 0.5 ) / 100;
}

}

Settings::Settings()
    : tap( true ), tapDrag( true ), tapDragLock( false ),
      tapButtonMap( TapLeftRightMiddle ), scrollMethod( ScrollTwoFinger ),
      naturalScroll( false ), disableWhileTyping( true ), accelSpeed( 0 ),
      accelProfile( AccelAdaptive ), clickMethod( ClickButtonAreas ),
      middleEmulation( false ), disableOnExternalMouse( false )
{
}

bool Settings::operator==( const Settings &o ) const
{
    return tap == o.tap && tapDrag == o.tapDrag && tapDragLock == o.tapDragLock
	&& tapButtonMap == o.tapButtonMap && scrollMethod == o.scrollMethod
	&& naturalScroll == o.naturalScroll && disableWhileTyping == o.disableWhileTyping
	&& roundSpeed( accelSpeed ) == roundSpeed( o.accelSpeed )
	&& accelProfile == o.accelProfile && clickMethod == o.clickMethod
	&& middleEmulation == o.middleEmulation
	&& disableOnExternalMouse == o.disableOnExternalMouse;
}

DeviceList touchpads( Display *dpy )
{
    DeviceList list;
    int opcode, event, error;
    if ( !XQueryExtension( dpy, "XInputExtension", &opcode, &event, &error ) )
	return list;

    ErrorTrap trap( dpy );
    int major = 2, minor = 0;
    if ( XIQueryVersion( dpy, &major, &minor ) != Success || major < 2 )
	return list;

    // the XInput 1 type of each device: the drivers (libinput, synaptics)
    // give touchpads XI_TOUCHPAD
    QMap<int, bool> typedTouchpad;
    const Atom touchpadType = atom( dpy, XI_TOUCHPAD );
    int count1 = 0;
    XDeviceInfo *info1 = XListInputDevices( dpy, &count1 );
    for ( int i = 0; i < count1; ++i )
	typedTouchpad[int( info1[i].id )] = touchpadType != None && info1[i].type == touchpadType;
    if ( info1 )
	XFreeDeviceList( info1 );

    const Atom tapAtom = atom( dpy, propTap );
    const Atom sendEventsAtom = atom( dpy, propSendEventsModes );

    int count = 0;
    XIDeviceInfo *info = XIQueryDevice( dpy, XIAllDevices, &count );
    for ( int i = 0; i < count; ++i ) {
	const XIDeviceInfo &d = info[i];
	if ( d.use != XISlavePointer && d.use != XIFloatingSlave )
	    continue;

	int nprops = 0;
	Atom *props = XIListProperties( dpy, d.deviceid, &nprops );
	bool hasTap = false, libinput = false;
	for ( int p = 0; p < nprops; ++p ) {
	    if ( props[p] == None )
		continue;
	    if ( props[p] == tapAtom )
		hasTap = libinput = true;
	    else if ( props[p] == sendEventsAtom )
		libinput = true;
	}
	if ( props )
	    XFree( props );
	// libinput offers tapping on touchpads only
	if ( !hasTap && !typedTouchpad[d.deviceid] )
	    continue;

	Device dev;
	dev.id = d.deviceid;
	dev.name = QString::fromUtf8( d.name );
	dev.libinput = libinput;
	if ( libinput ) {
	    const int id = d.deviceid;
	    struct { const char *name; unsigned bit; } flags[] = {
		{ propTap, HasTap },
		{ propTapDrag, HasTapDrag },
		{ propTapDragLock, HasTapDragLock },
		{ propTapButtonMap, HasTapButtonMap },
		{ propScrollMethod, HasScrollMethod },
		{ propNaturalScroll, HasNaturalScroll },
		{ propDisableWhileTyping, HasDisableWhileTyping },
		{ propAccelProfile, HasAccelProfile },
		{ propClickMethod, HasClickMethod },
		{ propMiddleEmulation, HasMiddleEmulation }
	    };
	    for ( unsigned f = 0; f < sizeof( flags ) / sizeof( flags[0] ); ++f )
		if ( !readBytes( dpy, id, flags[f].name ).empty() )
		    dev.features |= flags[f].bit;
	    double speed;
	    if ( readFloat( dpy, id, propAccelSpeed, &speed ) )
		dev.features |= HasAccelSpeed;
	    dev.scrollMethods = readAvailable( dpy, id, propScrollMethods );
	    dev.clickMethods = readAvailable( dpy, id, propClickMethods );
	    dev.accelProfiles = readAvailable( dpy, id, propAccelProfiles );
	    if ( readAvailable( dpy, id, propSendEventsModes ) & ( 1u << sendEventsOnExternalMouse ) )
		dev.features |= HasDisableOnExternalMouse;
	    if ( !dev.scrollMethods )
		dev.features &= ~HasScrollMethod;
	    if ( !dev.clickMethods )
		dev.features &= ~HasClickMethod;
	    if ( !dev.accelProfiles )
		dev.features &= ~HasAccelProfile;
	}
	list.append( dev );
    }
    if ( info )
	XIFreeDeviceInfo( info );
    return list;
}

Settings defaults( Display *dpy, const Device &dev )
{
    Settings s;
    if ( !dev.libinput )
	return s;
    ErrorTrap trap( dpy );
    const int id = dev.id;
    // tap-to-click stays on (Settings())
    s.tapDrag = readBool( dpy, id, defaultOf( propTapDrag ), s.tapDrag );
    s.tapDragLock = readBool( dpy, id, defaultOf( propTapDragLock ), s.tapDragLock );
    s.tapButtonMap = readChoice( dpy, id, propTapButtonMapDefault, s.tapButtonMap );
    if ( s.tapButtonMap < 0 )
	s.tapButtonMap = TapLeftRightMiddle;
    s.naturalScroll = readBool( dpy, id, defaultOf( propNaturalScroll ), s.naturalScroll );
    s.disableWhileTyping = readBool( dpy, id, defaultOf( propDisableWhileTyping ), s.disableWhileTyping );
    s.scrollMethod = readChoice( dpy, id, defaultOf( propScrollMethod ), s.scrollMethod );
    s.clickMethod = readChoice( dpy, id, defaultOf( propClickMethod ), s.clickMethod );
    s.middleEmulation = readBool( dpy, id, defaultOf( propMiddleEmulation ), s.middleEmulation );
    s.accelProfile = readChoice( dpy, id, defaultOf( propAccelProfile ), s.accelProfile );
    // the custom profile (index 2) is not offered
    if ( s.accelProfile != AccelAdaptive && s.accelProfile != AccelFlat )
	s.accelProfile = AccelAdaptive;
    double speed;
    if ( readFloat( dpy, id, defaultOf( propAccelSpeed ).latin1(), &speed ) )
	s.accelSpeed = roundSpeed( speed );
    s.disableOnExternalMouse =
	readChoice( dpy, id, defaultOf( propSendEventsMode ), -1 ) == sendEventsOnExternalMouse;
    return s;
}

Settings load( KConfig *config, const Settings &defaults, unsigned *named )
{
    unsigned n = 0;
    Settings s = defaults;
    config->setGroup( group );
    s.tap = readFlag( config, "TapToClick", s.tap, HasTap, &n );
    s.tapDrag = readFlag( config, "TapAndDrag", s.tapDrag, HasTapDrag, &n );
    s.tapDragLock = readFlag( config, "TapDragLock", s.tapDragLock, HasTapDragLock, &n );
    s.tapButtonMap = readName( config, "TapButtonMap", tapMapNames, 2, 0, s.tapButtonMap,
			       HasTapButtonMap, &n );
    s.scrollMethod = readName( config, "ScrollMethod", scrollNames, 3, 1, s.scrollMethod,
			       HasScrollMethod, &n );
    s.naturalScroll = readFlag( config, "NaturalScroll", s.naturalScroll, HasNaturalScroll, &n );
    s.disableWhileTyping = readFlag( config, "DisableWhileTyping", s.disableWhileTyping,
				     HasDisableWhileTyping, &n );
    if ( config->hasKey( "PointerSpeed" ) ) {
	s.accelSpeed = roundSpeed( config->readDoubleNumEntry( "PointerSpeed", s.accelSpeed ) );
	n |= HasAccelSpeed;
    }
    s.accelProfile = readName( config, "AccelProfile", profileNames, 2, 0, s.accelProfile,
			       HasAccelProfile, &n );
    s.clickMethod = readName( config, "ClickMethod", clickNames, 3, 1, s.clickMethod,
			      HasClickMethod, &n );
    s.middleEmulation = readFlag( config, "MiddleButtonEmulation", s.middleEmulation,
				  HasMiddleEmulation, &n );
    s.disableOnExternalMouse = readFlag( config, "DisableOnExternalMouse", s.disableOnExternalMouse,
					 HasDisableOnExternalMouse, &n );
    if ( named )
	*named = n;
    return s;
}

unsigned differences( const Settings &a, const Settings &b )
{
    unsigned d = 0;
    if ( a.tap != b.tap )
	d |= HasTap;
    if ( a.tapDrag != b.tapDrag )
	d |= HasTapDrag;
    if ( a.tapDragLock != b.tapDragLock )
	d |= HasTapDragLock;
    if ( a.tapButtonMap != b.tapButtonMap )
	d |= HasTapButtonMap;
    if ( a.scrollMethod != b.scrollMethod )
	d |= HasScrollMethod;
    if ( a.naturalScroll != b.naturalScroll )
	d |= HasNaturalScroll;
    if ( a.disableWhileTyping != b.disableWhileTyping )
	d |= HasDisableWhileTyping;
    if ( roundSpeed( a.accelSpeed ) != roundSpeed( b.accelSpeed ) )
	d |= HasAccelSpeed;
    if ( a.accelProfile != b.accelProfile )
	d |= HasAccelProfile;
    if ( a.clickMethod != b.clickMethod )
	d |= HasClickMethod;
    if ( a.middleEmulation != b.middleEmulation )
	d |= HasMiddleEmulation;
    if ( a.disableOnExternalMouse != b.disableOnExternalMouse )
	d |= HasDisableOnExternalMouse;
    return d;
}

void save( KConfig *config, const Settings &s, unsigned named )
{
    config->setGroup( group );
    writeFlag( config, "TapToClick", s.tap, named & HasTap );
    writeFlag( config, "TapAndDrag", s.tapDrag, named & HasTapDrag );
    writeFlag( config, "TapDragLock", s.tapDragLock, named & HasTapDragLock );
    writeName( config, "TapButtonMap", tapMapNames, 0, s.tapButtonMap, named & HasTapButtonMap );
    writeName( config, "ScrollMethod", scrollNames, 1, s.scrollMethod, named & HasScrollMethod );
    writeFlag( config, "NaturalScroll", s.naturalScroll, named & HasNaturalScroll );
    writeFlag( config, "DisableWhileTyping", s.disableWhileTyping, named & HasDisableWhileTyping );
    if ( named & HasAccelSpeed )
	config->writeEntry( "PointerSpeed", roundSpeed( s.accelSpeed ) );
    else
	config->deleteEntry( "PointerSpeed" );
    writeName( config, "AccelProfile", profileNames, 0, s.accelProfile, named & HasAccelProfile );
    writeName( config, "ClickMethod", clickNames, 1, s.clickMethod, named & HasClickMethod );
    writeFlag( config, "MiddleButtonEmulation", s.middleEmulation, named & HasMiddleEmulation );
    writeFlag( config, "DisableOnExternalMouse", s.disableOnExternalMouse,
	       named & HasDisableOnExternalMouse );
    config->sync();
}

void apply( Display *dpy, const Device &dev, const Settings &s )
{
    if ( !dev.libinput )
	return;
    ErrorTrap trap( dpy );
    const int id = dev.id;
    writeBool( dpy, id, propTap, s.tap );
    writeBool( dpy, id, propTapDrag, s.tapDrag );
    writeBool( dpy, id, propTapDragLock, s.tapDragLock );
    writeChoice( dpy, id, propTapButtonMap, s.tapButtonMap, 3 );
    writeChoice( dpy, id, propScrollMethod, s.scrollMethod, dev.scrollMethods );
    writeBool( dpy, id, propNaturalScroll, s.naturalScroll );
    writeBool( dpy, id, propDisableWhileTyping, s.disableWhileTyping );
    double speed;
    if ( readFloat( dpy, id, propAccelSpeed, &speed )
	 && roundSpeed( speed ) != roundSpeed( s.accelSpeed ) )
	writeFloat( dpy, id, propAccelSpeed, roundSpeed( s.accelSpeed ) );
    writeChoice( dpy, id, propAccelProfile, s.accelProfile, dev.accelProfiles );
    writeChoice( dpy, id, propClickMethod, s.clickMethod, dev.clickMethods );
    writeBool( dpy, id, propMiddleEmulation, s.middleEmulation );

    // "disabled" (index 0) is someone else's (a key that turns the
    // touchpad off): left as it is
    if ( dev.features & HasDisableOnExternalMouse ) {
	Bytes v = readBytes( dpy, id, propSendEventsMode );
	if ( v.size() > unsigned( sendEventsOnExternalMouse ) && !v[0]
	     && ( v[sendEventsOnExternalMouse] != 0 ) != s.disableOnExternalMouse ) {
	    v[sendEventsOnExternalMouse] = s.disableOnExternalMouse ? 1 : 0;
	    writeBytes( dpy, id, propSendEventsMode, v );
	}
    }
}

int applyAll( Display *dpy )
{
    KConfig config( "touchpadrc", true, false );
    const DeviceList list = touchpads( dpy );
    int n = 0;
    for ( DeviceList::ConstIterator it = list.begin(); it != list.end(); ++it ) {
	if ( !( *it ).libinput )
	    continue;
	apply( dpy, *it, load( &config, defaults( dpy, *it ) ) );
	++n;
    }
    return n;
}

}
