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

namespace KRandR {

HotplugPolicy::HotplugPolicy()
    : m_enabled( true ), m_started( false ), m_reconnected( false ),
      m_changeSince( -1 ), m_seenAsleep( false ), m_asleepAt( 0 )
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
	m_started = true;
	m_set = set;
	m_known = currentLayout( state );
	m_reconnected = false;
	m_lastMessage = QString::null;
	if ( m_enabled && !store.hasProfile( set ) && validateLayout( m_known ) ) {
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
	Layout cur = currentLayout( state );
	if ( sameArrangement( cur, m_known ) ) {
	    m_reconnected = false;
	    m_lastMessage = QString::null;
	    return none;
	}
	if ( m_reconnected ) {
	    // monitors went away and came back (power save), and the
	    // driver changed their configuration meanwhile: put back what
	    // they had
	    m_reconnected = false;
	    m_lastMessage = QString::null;
	    if ( !m_enabled ) {
		m_known = cur;
		return none;
	    }
	    Action a;
	    a.type = Action::Apply;
	    a.layout = m_known;
	    a.message = "monitors back, restoring their arrangement: " + set;
	    return a;
	}
	// changed by someone else (xrandr, the control module): remember
	// it when it stays so
	if ( now - lastEvent < rememberQuietMs )
	    return wait( rememberQuietMs - ( now - lastEvent ), QString::null );
	m_known = cur;
	m_lastMessage = QString::null;
	if ( m_enabled && validateLayout( cur ) ) {
	    store.saveProfile( set, cur );
	    none.message = "remembered the arrangement of " + set;
	}
	return none;
    }

    // other monitors
    if ( m_changeSince < 0 )
	m_changeSince = now;
    if ( set.isEmpty() )
	return wait( asleepPollMs, "no monitors, waiting" );
    QStringList had = QStringList::split( '+', m_set ), have = QStringList::split( '+', set );
    bool gone = false;
    for ( QStringList::ConstIterator it = had.begin(); it != had.end(); ++it )
	if ( !have.contains( *it ) )
	    gone = true;
    // monitors gone: they may come back (waking up); if one of the
    // others is on, the user is not left without a picture meanwhile
    if ( gone && anyEnabled( state ) ) {
	long left = removeGraceMs - QMIN( now - m_changeSince, awakeFor );
	if ( left > 0 )
	    return wait( left, "monitors gone, waiting: " + set );
    }

    QString from = m_set;
    Layout previous = m_known;
    m_set = set;
    m_known = currentLayout( state );
    m_changeSince = -1;
    m_reconnected = false;
    m_lastMessage = QString::null;
    if ( !m_enabled )
	return none;
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
    if ( makePlan( state, a.layout ).isEmpty() ) {
	a.type = Action::Nothing;
	a.layout.clear();
	a.message = "monitors changed: " + from + " -> " + set + ", already arranged";
    }
    return a;
}

void HotplugPolicy::applied( const ScreenState &state )
{
    if ( state.connectedSetKey() == m_set )
	m_known = currentLayout( state );
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
