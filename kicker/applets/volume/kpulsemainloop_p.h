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

#ifndef KPULSEMAINLOOP_P_H
#define KPULSEMAINLOOP_P_H

#include <sys/time.h>

#include <qobject.h>

#include <pulse/mainloop-api.h>

class QSocketNotifier;
class QTimer;
class KPulseMainLoop;

// The event objects behind the pa_io_event, pa_time_event and
// pa_defer_event pointers given to libpulse.  An event freed by libpulse
// stays alive, inert, until the main loop deletes it on the next pass of
// the event loop, so that it may be freed from inside its own callback.
class KPulseEvent : public QObject
{
    Q_OBJECT

public:
    KPulseEvent(KPulseMainLoop *loop);

    bool isFreed() const { return m_freed; }
    // Called by the *_free functions (and the destructor of the main
    // loop): stop watching, and have the main loop delete the event.
    void dispose();
    // Called by the main loop right before deleting a freed event.
    virtual void callDestroy() = 0;

protected:
    virtual void stop() = 0;

    KPulseMainLoop *m_loop;
    void *m_userdata;

private:
    bool m_freed;
};

class KPulseIoEvent : public KPulseEvent
{
    Q_OBJECT

public:
    KPulseIoEvent(KPulseMainLoop *loop, int fd, pa_io_event_flags_t flags,
                  pa_io_event_cb_t callback, void *userdata);

    void setFlags(pa_io_event_flags_t flags);
    void setDestroy(pa_io_event_destroy_cb_t callback) { m_destroy = callback; }
    virtual void callDestroy();

protected:
    virtual void stop();

private slots:
    void readActivated();
    void writeActivated();
    void exceptionActivated();

private:
    void dispatch(pa_io_event_flags_t trigger);

    int m_fd;
    pa_io_event_flags_t m_flags;
    pa_io_event_cb_t m_callback;
    pa_io_event_destroy_cb_t m_destroy;
    // indexed by QSocketNotifier::Type, created when first needed
    QSocketNotifier *m_notifiers[3];
};

class KPulseTimeEvent : public KPulseEvent
{
    Q_OBJECT

public:
    KPulseTimeEvent(KPulseMainLoop *loop, pa_time_event_cb_t callback,
                    void *userdata);

    void restart(const struct timeval *tv);
    void setDestroy(pa_time_event_destroy_cb_t callback) { m_destroy = callback; }
    virtual void callDestroy();

protected:
    virtual void stop();

private slots:
    void timeout();

private:
    struct timeval m_time;
    pa_time_event_cb_t m_callback;
    pa_time_event_destroy_cb_t m_destroy;
    QTimer *m_timer;
};

class KPulseDeferEvent : public KPulseEvent
{
    Q_OBJECT

public:
    KPulseDeferEvent(KPulseMainLoop *loop, pa_defer_event_cb_t callback,
                     void *userdata);

    void setEnabled(bool enabled);
    void setDestroy(pa_defer_event_destroy_cb_t callback) { m_destroy = callback; }
    virtual void callDestroy();

protected:
    virtual void stop();

private slots:
    void timeout();

private:
    pa_defer_event_cb_t m_callback;
    pa_defer_event_destroy_cb_t m_destroy;
    QTimer *m_timer;
};

#endif
