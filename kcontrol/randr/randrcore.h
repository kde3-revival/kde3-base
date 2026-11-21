/*
 * RandR 1.2+ display configuration: the model of the outputs, monitor
 * identification (EDID), profiles (displayrc), the default layout and
 * the plan that turns a layout into RandR requests in a safe order.
 *
 * This part does not talk to the X server (see randrx11.h), so that it
 * can be tested against a simulated server (tests/randrtest.cpp).
 *
 * Copyright (C) 2026 Serghei Amelian
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 */

#ifndef RANDRCORE_H
#define RANDRCORE_H

#include <qmap.h>
#include <qrect.h>
#include <qstring.h>
#include <qstringlist.h>
#include <qvaluelist.h>

class KConfig;

namespace KRandR {

typedef unsigned long XID;

// rotation in degrees counter-clockwise and reflection (RandR's
// RR_Rotate_*, RR_Reflect_*)
enum { Rotate0 = 1, Rotate90 = 2, Rotate180 = 4, Rotate270 = 8,
       ReflectX = 16, ReflectY = 32,
       RotationMask = 0x0f, ReflectMask = 0x30 };

struct Mode
{
    Mode() : id( 0 ), width( 0 ), height( 0 ), rate( 0 ) {}
    XID id;
    int width;
    int height;
    double rate;    // Hz
};

struct Output
{
    Output();

    XID id;
    QString name;           // connector: "DP-5"
    bool connected;
    bool nonDesktop;        // "non-desktop" property (VR headsets)

    // monitor identity, from EDID
    QString key;            // see outputKey()
    QString vendor;         // PNP id: "SAM"
    QString model;          // EDID monitor name: "LS27B61x"
    QString serial;
    int mmWidth;
    int mmHeight;

    QValueList<XID> modes;  // the preferred ones first
    int preferredCount;     // npreferred
    QValueList<XID> crtcs;  // possible CRTCs

    // current state
    XID crtc;               // 0: off
    bool primary;
};

struct Crtc
{
    Crtc() : id( 0 ), x( 0 ), y( 0 ), mode( 0 ), rotation( Rotate0 ),
	     rotations( Rotate0 ), width( 0 ), height( 0 ), transformed( false ) {}
    XID id;
    int x;
    int y;
    XID mode;               // 0: off
    int rotation;
    int rotations;          // supported
    int width;              // rotated size
    int height;
    bool transformed;       // a scaling transform or panning (xrandr
			    // --scale/--panning), which survives a mode set
    QValueList<XID> outputs;
    QValueList<XID> possible;
};

struct ScreenState
{
    ScreenState() : minWidth( 0 ), minHeight( 0 ), maxWidth( 0 ), maxHeight( 0 ),
		    width( 0 ), height( 0 ), mmWidth( 0 ), mmHeight( 0 ),
		    timestamp( 0 ) {}
    int minWidth, minHeight, maxWidth, maxHeight;
    int width, height, mmWidth, mmHeight;
    unsigned long timestamp;    // configTimestamp
    QValueList<Output> outputs;
    QValueList<Crtc> crtcs;
    QMap<XID, Mode> modes;

    const Output *output( XID id ) const;
    const Output *outputByName( const QString &name ) const;
    const Output *outputByKey( const QString &key ) const;
    const Crtc *crtc( XID id ) const;
    const Mode *mode( XID id ) const;

    // connected outputs usable for the desktop
    QValueList<Output> connectedOutputs() const;
    // the identity of the set of connected outputs: their sorted keys
    QString connectedSetKey() const;
};

// the wanted state of one output
struct OutputSetting
{
    OutputSetting() : enabled( false ), width( 0 ), height( 0 ), rate( 0 ),
		      x( 0 ), y( 0 ), rotation( Rotate0 ), primary( false ) {}
    QString key;        // monitor identity (Output::key)
    QString name;       // connector, informative (or used without a key)
    QString model;      // informative
    bool enabled;
    int width;          // mode size, not rotated; 0: preferred mode
    int height;
    double rate;        // 0: highest for that size
    int x;
    int y;
    int rotation;       // with the reflection bits
    bool primary;

    bool operator==( const OutputSetting &o ) const;
};

typedef QValueList<OutputSetting> Layout;

// --- EDID ------------------------------------------------------------------

struct EdidInfo
{
    EdidInfo() : valid( false ), product( 0 ), serialNumber( 0 ) {}
    bool valid;
    QString vendor;         // PNP id
    unsigned int product;
    unsigned int serialNumber;
    QString name;           // descriptor 0xfc
    QString serial;         // descriptor 0xff
};

EdidInfo parseEdid( const unsigned char *data, int len );

// the identity of a monitor: "SAM-71ea-H4TTB00049" from its EDID (serial
// string, else the serial number when not 0), or "connector-DP-5" without
// a usable EDID
QString edidKey( const EdidInfo &edid, const QString &connector );

// gives every connected output a unique key: two monitors with the same
// EDID identity (no serial) get their connector appended ("...@DP-4")
void assignKeys( ScreenState &state, const QMap<XID, EdidInfo> &edids );

// --- modes and layouts -------------------------------------------------------

bool isInternalPanel( const QString &connector );
// a natural order of connector names: "DP-2" < "DP-10"
bool connectorLessThan( const QString &a, const QString &b );

// the mode of output for size width x height and refresh rate (closest
// rate; 0 = highest); width 0: the preferred mode
const Mode *findMode( const ScreenState &state, const Output &output,
		      int width, int height, double rate );
const Mode *preferredMode( const ScreenState &state, const Output &output );

// the rotated size of an enabled setting (0x0 if no mode)
void settingSize( const OutputSetting &s, int &w, int &h );

// the layout the server has now (connected outputs only)
Layout currentLayout( const ScreenState &state );

// all connected outputs side by side, left to right: those enabled now
// in their current left-to-right order with their current mode and
// rotation, then the others at their preferred mode (internal panel
// first, then by connector), as many as there are CRTCs for; the
// primary output stays primary if connected, else the first one is
Layout defaultLayout( const ScreenState &state );

// moves the layout so that its top left corner is 0,0
void normalizeLayout( Layout &layout );

// after the size of monitor index changed from before (its rotated
// rectangle; empty if it was off): the monitors that were at its right
// or below it move with its right or bottom edge, so that monitors
// side by side stay side by side
void adjustNeighbours( Layout &layout, int index, int beforeX, int beforeY,
		       int beforeW, int beforeH );

// puts the enabled monitor index to the right of the other enabled ones,
// top aligned
void placeAtRight( Layout &layout, int index );

// the rectangle of an enabled monitor (its rotated size)
QRect monitorRect( const OutputSetting &s );

// places the enabled monitor index next to the others, as close as
// possible to where it is now: edge to edge with one of them, without
// overlapping any, its other edges aligned with that one's when they
// are closer than threshold (layout pixels)
void snapMonitor( Layout &layout, int index, int threshold );

// checks: at least one enabled output, no overlaps; message on failure
bool validateLayout( const Layout &layout, QString *error = 0 );

// --- plan --------------------------------------------------------------------

struct CrtcOp
{
    CrtcOp() : crtc( 0 ), x( 0 ), y( 0 ), mode( 0 ), rotation( Rotate0 ),
	       resetTransform( false ) {}
    XID crtc;
    int x, y;
    XID mode;           // 0: disable
    int rotation;       // with the reflection bits
    bool resetTransform;// identity transform and no panning first
    QValueList<XID> outputs;
};

struct Plan
{
    Plan() : resize( false ), width( 0 ), height( 0 ), mmWidth( 0 ), mmHeight( 0 ),
	     primary( 0 ), setPrimary( false ) {}
    QString error;                  // not empty: cannot be applied
    QValueList<CrtcOp> disable;     // first: CRTCs that are switched off
    bool resize;                    // then the screen size
    int width, height, mmWidth, mmHeight;
    QValueList<CrtcOp> enable;      // then the CRTCs that are (re)configured
    XID primary;
    bool setPrimary;

    bool isEmpty() const
    { return disable.isEmpty() && !resize && enable.isEmpty() && !setPrimary; }
    QString describe( const ScreenState &state ) const;
};

// RandR requests that take the server from state to layout: CRTCs that
// are not used any more, that would not fit into the new screen size or
// that get other outputs are disabled first, then the screen size is
// set, then the CRTCs are configured; outputs of the layout that are not
// connected are ignored, connected outputs missing in the layout are
// switched off
Plan makePlan( const ScreenState &state, const Layout &layout );

// --- profiles (displayrc) --------------------------------------------------

class ProfileStore
{
public:
    // config: displayrc, not deleted
    ProfileStore( KConfig *config );

    bool hasProfile( const QString &setKey ) const;
    Layout profile( const QString &setKey ) const;
    void saveProfile( const QString &setKey, const Layout &layout );
    void removeProfile( const QString &setKey );
    QStringList profiles() const;

    static QString groupName( const QString &setKey );

private:
    KConfig *m_config;
};

// [General] AutoRestore of displayrc ("Restore the monitor arrangement
// automatically", default on; the older ApplyOnHotplug when not set):
// off, the arrangement is neither applied at login or hotplug nor
// remembered, only the control module changes it
bool autoRestoreEnabled( KConfig *displayrc );

// the saved profile for the connected outputs of state, if any; else
// defaultLayout() when useDefault
Layout layoutFor( const ScreenState &state, const ProfileStore &store,
		  bool useDefault, bool *fromProfile = 0 );

// a layout for a set of monitors that has no profile, from what is known
// about them: the reference is the layout (previous, the one on screen
// before the monitors changed, or a saved profile) that has the most of
// the connected monitors; those keep their place, mode, rotation and the
// primary role relative to each other (gaps left by monitors that are
// gone are closed, the top left corner moves to 0,0). Monitors it does
// not have go to the right: those on now in their order on screen, then
// the others (internal panel first, then by connector), with the mode a
// profile has for them, else the current or preferred one. The primary
// one: the reference's, else previous's, else the server's, else the
// first one. Monitors that the reference or previous has switched off
// stay off. Without any reference: defaultLayout(); empty if no valid
// layout can be made from the reference.
Layout deriveLayout( const ScreenState &state, const ProfileStore &store,
		     const Layout &previous );

// the same arrangement: the same monitors on and off, and the enabled
// ones with the same place, size, rotation, primary role and (nearly)
// the same refresh rate; the order does not matter
bool sameArrangement( const Layout &a, const Layout &b );

} // namespace KRandR

#endif
