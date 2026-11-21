/*
 * RandR 1.2+ display configuration: reading the state of the X server
 * and applying a plan (randrcore.h) to it.
 *
 * Copyright (C) 2026 Serghei Amelian
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 */

#ifndef RANDRX11_H
#define RANDRX11_H

#include "randrcore.h"

typedef struct _XDisplay Display;

namespace KRandR {

// RandR >= 1.2 on the display?
bool hasRandR12( Display *dpy, int *eventBase = 0 );

// the outputs, CRTCs and modes of the default screen; false without
// RandR 1.2 or on failure. probe: let the server look for monitors
// (XRRGetScreenResources, slow, may flicker) instead of using what it
// knows (XRRGetScreenResourcesCurrent: the server reports hotplugs)
bool queryState( Display *dpy, ScreenState &state, bool probe = false );

// the request sequence of a plan, so that it can be replayed against a
// simulated server in the tests
class PlanExecutor
{
public:
    virtual ~PlanExecutor() {}
    virtual void grab() {}
    virtual void ungrab() {}
    virtual bool setCrtc( const CrtcOp &op ) = 0;
    virtual bool setScreenSize( int w, int h, int mmw, int mmh ) = 0;
    virtual bool setPrimary( XID output ) = 0;
};

// runs plan: disable, screen size, enable, primary; stops at the first
// failure (error set, returns false), or with bestEffort goes on with
// the other requests
bool executePlan( PlanExecutor &ex, const Plan &plan, QString *error = 0,
		  bool bestEffort = false );

// applies layout to the X server with the server grabbed (like xrandr);
// on failure the previous configuration is restored as far as possible
// (if even that fails, error says so). Returns false and sets error
// when the layout cannot be applied. expectSet: the connected monitors
// the layout is for (ScreenState::connectedSetKey()); if others are
// connected when the server is grabbed, nothing is changed.
bool applyLayout( Display *dpy, const Layout &layout, QString *error = 0,
		  const QString &expectSet = QString::null );

// installs an X error handler that ignores errors, for a program that
// queries outputs which may disappear meanwhile (MST hubs) and would
// otherwise be ended by Xlib's default handler
void ignoreXErrors();

} // namespace KRandR

#endif
