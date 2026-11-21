/*
    Copyright (C) 2026 Serghei Amelian

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
    02110-1301, USA.
*/

#ifndef KPULSEMAINLOOP_H
#define KPULSEMAINLOOP_H

#include <qobject.h>
#include <qptrlist.h>

#include <pulse/mainloop-api.h>

class KPulseEvent;

/**
 * A pa_mainloop_api on the Qt event loop of the calling thread, for the
 * asynchronous libpulse API without pa_threaded_mainloop or
 * pa_glib_mainloop:
 *
 * @code
 * KPulseMainLoop *loop = new KPulseMainLoop(this);
 * pa_context *c = pa_context_new(loop->api(), "My Application");
 * @endcode
 *
 * I/O events are watched with QSocketNotifier, time events with
 * single-shot QTimers and defer events with zero timers while they are
 * enabled.  Events are freed on the next pass of the event loop, so an
 * event may be freed from any callback, its own included; the destroy
 * callbacks are called then.
 *
 * Free the contexts and streams using the API before the main loop;
 * events still alive then are freed (their destroy callbacks called) by
 * the destructor.  quit() does nothing: the Qt event loop is not the
 * library's to stop.
 */
class KPulseMainLoop : public QObject
{
    Q_OBJECT

public:
    KPulseMainLoop(QObject *parent = 0, const char *name = 0);
    virtual ~KPulseMainLoop();

    /**
     * The API to give to pa_context_new() and friends.  It belongs to
     * this object and is valid as long as it lives.
     */
    pa_mainloop_api *api() { return &m_api; }

    /** @internal */
    static KPulseMainLoop *fromApi(pa_mainloop_api *api);
    /** @internal */
    void addEvent(KPulseEvent *event);
    /** @internal */
    void freeEvent(KPulseEvent *event);
    /** @internal Counts the callbacks running, see deleteFreedEvents(). */
    void enterCallback() { ++m_callbackDepth; }
    /** @internal */
    void leaveCallback() { --m_callbackDepth; }

private slots:
    void deleteFreedEvents();

private:
    pa_mainloop_api m_api;
    QPtrList<KPulseEvent> m_events;
    QPtrList<KPulseEvent> m_freedEvents;
    bool m_cleanupPending;
    int m_callbackDepth;
};

#endif
