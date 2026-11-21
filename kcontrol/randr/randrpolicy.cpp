/*
 * What kded_randr does when the monitors change (see randrpolicy.h).
 *
 * Copyright (C) 2026 Serghei Amelian
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 */

#include "randrpolicy.h"

#include <qrect.h>

namespace KRandR {

HotplugPolicy::HotplugPolicy()
    : m_enabled( true ), m_started( false ), m_reconnected( false ),
      m_hasPending( false ), m_changeSince( -1 ), m_seenAsleep( false ), m_asleepAt( 0 ),
      m_lastApply( -restoreAgainMs ), m_startWaitSince( -1 )
{
}

HotplugPolicy::Action HotplugPolicy::wait( int ms, const QString &message )
{
    Action a;
    a.type = Action::Wait;
    a.waitMs = ms > 0 ? ms : 1;
    if ( message != m_lastMessage )
	a.message = message;
    m_lastMessage = message;
    return a;
}

static bool anyEnabled( const ScreenState &state )
{
    QValueList<Output> outs = state.connectedOutputs();
    for ( QValueList<Output>::ConstIterator it = outs.begin(); it != outs.end(); ++it ) {
	const Crtc *c = (*it).crtc ? state.crtc( (*it).crtc ) : 0;
	if ( c && c->mode )
	    return true;
    }
    return false;
}

// an output that layout has on is off now (a driver switches the CRTC of
// a monitor off when it disconnects)
static bool lostOutputs( const Layout &layout, const Layout &cur )
{
    for ( Layout::ConstIterator it = layout.begin(); it != layout.end(); ++it ) {
	if ( !(*it).enabled )
	    continue;
	bool on = false;
	for ( Layout::ConstIterator c = cur.begin(); c != cur.end(); ++c )
	    if ( (*c).key == (*it).key )
		on = (*c).enabled;
	if ( !on )
	    return true;
    }
    return false;
}

// the enabled monitors cover the screen from 0,0 to its size: nothing
// that is gone left its place empty
static bool complete( const ScreenState &state, const Layout &cur )
{
    bool any = false;
    int minX = 0, minY = 0, maxX = 0, maxY = 0;
    for ( Layout::ConstIterator it = cur.begin(); it != cur.end(); ++it ) {
	if ( !(*it).enabled )
	    continue;
	QRect r = monitorRect( *it );
	if ( !any || r.left() < minX ) minX = r.left();
	if ( !any || r.top() < minY ) minY = r.top();
	if ( !any || r.right() + 1 > maxX ) maxX = r.right() + 1;
	if ( !any || r.bottom() + 1 > maxY ) maxY = r.bottom() + 1;
	any = true;
    }
    return any && minX == 0 && minY == 0 && maxX == state.width && maxY == state.height;
}

HotplugPolicy::Action HotplugPolicy::remember( const ScreenState &state, ProfileStore &store )
{
    Action none;
    m_known = currentLayout( state );
    m_set = state.connectedSetKey();
    m_hasPending = false;
    m_lastMessage = QString::null;
    if ( m_enabled && validateLayout( m_known ) ) {
	store.saveProfile( m_set, m_known );
	none.message = "remembered the arrangement of " + m_set;
    }
    return none;
}

HotplugPolicy::Action HotplugPolicy::decide( const ScreenState &state, bool asleep, long now,
					     long lastEvent, bool connectionChanged,
					     ProfileStore &store )
{
    Action none;
    QString set = state.connectedSetKey();
    if ( asleep ) {
	m_seenAsleep = true;
	m_asleepAt = now;
    }
    long awakeFor = m_seenAsleep ? now - m_asleepAt : (long) removeGraceMs;

    // the start (also in the middle of a session): only look; the
    // arrangement is taken when the monitors are awake and settled
    if ( !m_started ) {
	if ( asleep || set.isEmpty() )
	    return wait( asleepPollMs, "monitors in power save, waiting before looking at them" );
	if ( awakeFor < removeGraceMs )
	    return wait( removeGraceMs - awakeFor, "monitors woke up, waiting before looking at them" );
	if ( now - lastEvent < startQuietMs )
	    return wait( startQuietMs - ( now - lastEvent ), QString::null );
	Layout cur = currentLayout( state );
	// a place left empty: a monitor is probably away for a moment
	// (waking up); give it the time to come back
	if ( !complete( state, cur ) ) {
	    if ( m_startWaitSince < 0 )
		m_startWaitSince = now;
	    if ( now - m_startWaitSince < removeGraceMs )
		return wait( asleepPollMs, "a monitor seems to be missing, waiting before looking at them" );
	}
	m_started = true;
	m_set = set;
	m_known = cur;
	m_reconnected = false;
	m_lastMessage = QString::null;
	if ( m_enabled && !store.hasProfile( set ) && complete( state, cur ) &&
	     validateLayout( m_known ) ) {
	    store.saveProfile( set, m_known );
	    none.message = "remembered the arrangement of " + set;
	}
	return none;
    }

    if ( connectionChanged )
	m_reconnected = true;

    // nothing is changed or remembered while the monitors sleep
    if ( asleep ) {
	if ( set != m_set || m_reconnected || !sameArrangement( currentLayout( state ), m_known ) )
	    return wait( asleepPollMs, "monitors in power save, waiting: " + set );
	return none;
    }

    if ( set == m_set ) {
	m_changeSince = -1;
	m_changeSet = QString::null;
	Layout cur = currentLayout( state );
	// the arrangement the monitors should have: a change made by
	// someone else that is not remembered yet, else the known one
	const Layout &target = m_hasPending ? m_pending : m_known;
	if ( sameArrangement( cur, target ) ) {
	    m_reconnected = false;
	    m_lastMessage = QString::null;
	    if ( !m_hasPending )
		return none;
	} else if ( m_reconnected && lostOutputs( target, cur ) ) {
	    // monitors went away and came back (power save), and the
	    // driver switched them off meanwhile: put back what they had,
	    // but not again and again (a monitor that disconnects at each
	    // mode set)
	    m_reconnected = false;
	    m_lastMessage = QString::null;
	    if ( !m_enabled || now - m_lastApply < restoreAgainMs ) {
		Action a = remember( state, store );
		if ( m_enabled )
		    a.message = "monitors back, switched off again soon after an arrangement, "
				"leaving them: " + set;
		return a;
	    }
	    Action a;
	    a.type = Action::Apply;
	    a.layout = target;
	    a.message = "monitors back, restoring their arrangement: " + set;
	    // a change by someone else that the power save interrupted: it
	    // stayed until then
	    if ( m_hasPending && validateLayout( m_pending ) )
		store.saveProfile( set, m_pending );
	    m_lastApply = now;
	    return a;
	} else {
	    // changed by someone else (xrandr, the control module)
	    m_reconnected = false;
	    m_pending = cur;
	    m_hasPending = true;
	}
	// remember it when it stays so
	if ( now - lastEvent < rememberQuietMs )
	    return wait( rememberQuietMs - ( now - lastEvent ), QString::null );
	return remember( state, store );
    }

    // other monitors
    if ( m_changeSince < 0 )
	m_changeSince = now;
    if ( set.isEmpty() )
	return wait( asleepPollMs, "no monitors, waiting" );
    // the arrangement of these monitors when they were first seen, to
    // notice one made by someone else meanwhile
    if ( m_changeSet != set ) {
	m_changeSet = set;
	m_changeLayout = currentLayout( state );
    }
    QStringList had = QStringList::split( '+', m_set ), have = QStringList::split( '+', set );
    bool gone = false;
    for ( QStringList::ConstIterator it = had.begin(); it != had.end(); ++it )
	if ( !have.contains( *it ) )
	    gone = true;
    // monitors gone: they may come back (waking up); if one of the
    // others is on, the user is not left without a picture meanwhile,
    // but just after the monitors woke up the others may still come
    if ( gone ) {
	long left = anyEnabled( state ) ? removeGraceMs - QMIN( now - m_changeSince, awakeFor )
					: removeGraceMs - awakeFor;
	if ( left > 0 )
	    return wait( left, "monitors gone, waiting: " + set );
    }

    QString from = m_set;
    Layout previous = m_hasPending ? m_pending : m_known;
    bool arrangedMeanwhile = !sameArrangement( currentLayout( state ), m_changeLayout );
    m_changeSince = -1;
    m_changeSet = QString::null;
    m_reconnected = false;
    m_lastMessage = QString::null;
    if ( !m_enabled || arrangedMeanwhile ) {
	// arranged by someone else while waiting: that is it
	Action a = remember( state, store );
	if ( m_enabled )
	    a.message = "monitors changed: " + from + " -> " + set +
			", arranged meanwhile; " + a.message;
	return a;
    }
    m_set = set;
    m_known = currentLayout( state );
    m_hasPending = false;
    Action a;
    a.type = Action::Apply;
    if ( store.hasProfile( set ) && !store.profile( set ).isEmpty() ) {
	a.layout = store.profile( set );
	a.message = "monitors changed: " + from + " -> " + set + ", applying their arrangement";
    } else {
	a.layout = deriveLayout( state, store, previous );
	a.message = "monitors changed: " + from + " -> " + set +
		    ", applying an arrangement derived from the previous one";
    }
    if ( a.layout.isEmpty() || makePlan( state, a.layout ).isEmpty() ) {
	a.type = Action::Nothing;
	a.layout.clear();
	a.message = "monitors changed: " + from + " -> " + set + ", leaving the arrangement";
	return a;
    }
    m_lastApply = now;
    return a;
}

void HotplugPolicy::applied( const ScreenState &state )
{
    if ( state.connectedSetKey() == m_set ) {
	m_known = currentLayout( state );
	m_hasPending = false;
    }
}

Layout HotplugPolicy::reconfigure( const ScreenState &state, const ProfileStore &store )
{
    QString set = state.connectedSetKey();
    if ( store.hasProfile( set ) ) {
	Layout l = store.profile( set );
	if ( !l.isEmpty() )
	    return l;
    }
    return deriveLayout( state, store, set == m_set ? m_known : Layout() );
}

} // namespace KRandR
