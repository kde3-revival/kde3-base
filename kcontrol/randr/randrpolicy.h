/*
 * What kded_randr does when the monitors change, without the X server, so
 * that it can be tested against simulated states (tests/randrtest.cpp).
 *
 * - It remembers the arrangement: when the configuration settles and it
 *   was not kded_randr that changed it (a login script running xrandr,
 *   xrandr by hand, the control module, another tool), it is saved as the
 *   profile of the connected monitors, as GNOME's monitors.xml does. When
 *   it starts and the connected monitors have no profile yet, the current
 *   arrangement becomes theirs. It never changes the configuration when it
 *   starts (kded may load it in the middle of a session).
 * - When the connected monitors change for good, it applies their profile,
 *   or one derived from the arrangement they had before (deriveLayout()).
 * - Never while the monitors are in power save (DPMS not On): DisplayPort
 *   monitors disconnect when they sleep and connect again, slowly, when
 *   they wake up. A monitor that is gone counts as unplugged only when it
 *   stayed away for removeGraceMs after it went and after the monitors
 *   were last seen in power save, unless none of the remaining ones is on.
 * - When monitors that went away come back, what they had before is put
 *   back if the driver changed it meanwhile.
 *
 * Copyright (C) 2026 Serghei Amelian
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 */

#ifndef RANDRPOLICY_H
#define RANDRPOLICY_H

#include "randrcore.h"

namespace KRandR {

class HotplugPolicy
{
public:
    enum {
	// monitors gone in power save: look again this often
	asleepPollMs = 5000,
	// a monitor that is gone: the time it gets to come back, from when
	// it went and from when the monitors were last seen in power save
	removeGraceMs = 30000,
	// a configuration changed by someone else is remembered when it did
	// not change for this long (longer than the control module's 15 s
	// confirmation, so that an arrangement that is reverted is not)
	rememberQuietMs = 20000
    };

    struct Action
    {
	enum Type {
	    Nothing,    // (wait for the next event)
	    Wait,       // call decide() again in waitMs, or at the next event
	    Apply       // apply layout, then call applied()
	};
	Action() : type( Nothing ), waitMs( 0 ) {}
	Type type;
	int waitMs;
	Layout layout;
	QString message;    // for the log; empty if nothing new
    };

    HotplugPolicy();

    // [General] AutoRestore of displayrc: off, the module neither applies
    // nor remembers anything
    void setEnabled( bool enabled ) { m_enabled = enabled; }

    // state: the server now; asleep: the monitors are in power save (DPMS
    // not On); now, lastEvent: the time in ms (monotonic) now and of the
    // last RandR event; connectionChanged: an output was connected or
    // disconnected since the last call. The first call is the start.
    Action decide( const ScreenState &state, bool asleep, long now, long lastEvent,
		   bool connectionChanged, ProfileStore &store );

    // after an Apply (also a failed one): the server's state
    void applied( const ScreenState &state );

    // DCOP reconfigure(): the profile of the connected monitors, else a
    // derived layout, now
    Layout reconfigure( const ScreenState &state, const ProfileStore &store );

    bool started() const { return m_started; }
    QString knownSet() const { return m_set; }
    const Layout &knownLayout() const { return m_known; }

private:
    Action wait( int ms, const QString &message );

    bool m_enabled;
    bool m_started;
    QString m_set;          // the connected monitors of m_known
    Layout m_known;         // their arrangement, as last seen or applied
    bool m_reconnected;     // outputs (dis)connected since m_known
    long m_changeSince;     // when the set was first seen changed; -1: not
    bool m_seenAsleep;
    long m_asleepAt;        // when the monitors were last seen in power save
    QString m_lastMessage;
};

} // namespace KRandR

#endif
