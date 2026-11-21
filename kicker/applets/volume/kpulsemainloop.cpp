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

#include <errno.h>
#include <limits.h>
#include <poll.h>

#include <qsocketnotifier.h>
#include <qtimer.h>

#include <pulse/timeval.h>

#include "kpulsemainloop.h"
#include "kpulsemainloop_p.h"

// How long to wait before trying again to delete freed events while a
// callback runs a nested event loop.
static const int NestedCleanupDelay = 100;

static inline pa_io_event *ioHandle(KPulseIoEvent *e)
{
    return reinterpret_cast<pa_io_event *>(e);
}

static inline KPulseIoEvent *ioEvent(pa_io_event *e)
{
    return reinterpret_cast<KPulseIoEvent *>(e);
}

static inline pa_time_event *timeHandle(KPulseTimeEvent *e)
{
    return reinterpret_cast<pa_time_event *>(e);
}

static inline KPulseTimeEvent *timeEvent(pa_time_event *e)
{
    return reinterpret_cast<KPulseTimeEvent *>(e);
}

static inline pa_defer_event *deferHandle(KPulseDeferEvent *e)
{
    return reinterpret_cast<pa_defer_event *>(e);
}

static inline KPulseDeferEvent *deferEvent(pa_defer_event *e)
{
    return reinterpret_cast<KPulseDeferEvent *>(e);
}

// ---- the pa_mainloop_api functions -------------------------------------

extern "C"
{

static pa_io_event *kpulse_io_new(pa_mainloop_api *api, int fd,
                                  pa_io_event_flags_t flags,
                                  pa_io_event_cb_t callback, void *userdata)
{
    KPulseMainLoop *loop = KPulseMainLoop::fromApi(api);
    KPulseIoEvent *e = new KPulseIoEvent(loop, fd, flags, callback, userdata);
    loop->addEvent(e);
    return ioHandle(e);
}

static void kpulse_io_enable(pa_io_event *e, pa_io_event_flags_t flags)
{
    ioEvent(e)->setFlags(flags);
}

static void kpulse_io_free(pa_io_event *e)
{
    ioEvent(e)->dispose();
}

static void kpulse_io_set_destroy(pa_io_event *e,
                                  pa_io_event_destroy_cb_t callback)
{
    ioEvent(e)->setDestroy(callback);
}

static pa_time_event *kpulse_time_new(pa_mainloop_api *api,
                                      const struct timeval *tv,
                                      pa_time_event_cb_t callback,
                                      void *userdata)
{
    KPulseMainLoop *loop = KPulseMainLoop::fromApi(api);
    KPulseTimeEvent *e = new KPulseTimeEvent(loop, callback, userdata);
    loop->addEvent(e);
    e->restart(tv);
    return timeHandle(e);
}

static void kpulse_time_restart(pa_time_event *e, const struct timeval *tv)
{
    timeEvent(e)->restart(tv);
}

static void kpulse_time_free(pa_time_event *e)
{
    timeEvent(e)->dispose();
}

static void kpulse_time_set_destroy(pa_time_event *e,
                                    pa_time_event_destroy_cb_t callback)
{
    timeEvent(e)->setDestroy(callback);
}

static pa_defer_event *kpulse_defer_new(pa_mainloop_api *api,
                                        pa_defer_event_cb_t callback,
                                        void *userdata)
{
    KPulseMainLoop *loop = KPulseMainLoop::fromApi(api);
    KPulseDeferEvent *e = new KPulseDeferEvent(loop, callback, userdata);
    loop->addEvent(e);
    // like pa_mainloop, a new defer event is enabled
    e->setEnabled(true);
    return deferHandle(e);
}

static void kpulse_defer_enable(pa_defer_event *e, int enable)
{
    deferEvent(e)->setEnabled(enable != 0);
}

static void kpulse_defer_free(pa_defer_event *e)
{
    deferEvent(e)->dispose();
}

static void kpulse_defer_set_destroy(pa_defer_event *e,
                                     pa_defer_event_destroy_cb_t callback)
{
    deferEvent(e)->setDestroy(callback);
}

static void kpulse_quit(pa_mainloop_api *, int)
{
    // The Qt event loop is not ours to stop.
}

}

// ---- KPulseEvent --------------------------------------------------------

KPulseEvent::KPulseEvent(KPulseMainLoop *loop)
    : QObject(loop),
      m_loop(loop),
      m_userdata(0),
      m_freed(false)
{
}

void KPulseEvent::dispose()
{
    if (m_freed)
    {
        return;
    }

    m_freed = true;
    stop();
    m_loop->freeEvent(this);
}

// ---- KPulseIoEvent ------------------------------------------------------

KPulseIoEvent::KPulseIoEvent(KPulseMainLoop *loop, int fd,
                             pa_io_event_flags_t flags,
                             pa_io_event_cb_t callback, void *userdata)
    : KPulseEvent(loop),
      m_fd(fd),
      m_flags(PA_IO_EVENT_NULL),
      m_callback(callback),
      m_destroy(0)
{
    m_userdata = userdata;
    m_notifiers[QSocketNotifier::Read] = 0;
    m_notifiers[QSocketNotifier::Write] = 0;
    m_notifiers[QSocketNotifier::Exception] = 0;
    setFlags(flags);
}

void KPulseIoEvent::setFlags(pa_io_event_flags_t flags)
{
    if (isFreed())
    {
        return;
    }

    m_flags = flags;

    // Hang-ups and errors make the descriptor readable and writable for
    // select(), so they are noticed with the read and write notifiers and
    // told apart by dispatch().  The exception notifier is only for
    // events that ask for nothing else.
    bool wanted[3];
    wanted[QSocketNotifier::Read] = flags & PA_IO_EVENT_INPUT;
    wanted[QSocketNotifier::Write] = flags & PA_IO_EVENT_OUTPUT;
    wanted[QSocketNotifier::Exception] =
        (flags & (PA_IO_EVENT_HANGUP | PA_IO_EVENT_ERROR)) &&
        !(flags & (PA_IO_EVENT_INPUT | PA_IO_EVENT_OUTPUT));

    for (int type = QSocketNotifier::Read;
         type <= QSocketNotifier::Exception; ++type)
    {
        QSocketNotifier *n = m_notifiers[type];
        if (!n)
        {
            if (!wanted[type])
            {
                continue;
            }

            n = new QSocketNotifier(m_fd, QSocketNotifier::Type(type), this);
            m_notifiers[type] = n;
            switch (type)
            {
                case QSocketNotifier::Read:
                    connect(n, SIGNAL(activated(int)), SLOT(readActivated()));
                    break;
                case QSocketNotifier::Write:
                    connect(n, SIGNAL(activated(int)), SLOT(writeActivated()));
                    break;
                default:
                    connect(n, SIGNAL(activated(int)),
                            SLOT(exceptionActivated()));
                    break;
            }
        }
        else if (n->isEnabled() != wanted[type])
        {
            n->setEnabled(wanted[type]);
        }
    }
}

void KPulseIoEvent::stop()
{
    for (int type = QSocketNotifier::Read;
         type <= QSocketNotifier::Exception; ++type)
    {
        if (m_notifiers[type])
        {
            m_notifiers[type]->setEnabled(false);
        }
    }
}

void KPulseIoEvent::callDestroy()
{
    if (m_destroy)
    {
        m_destroy(m_loop->api(), ioHandle(this), m_userdata);
    }
}

void KPulseIoEvent::readActivated()
{
    dispatch(PA_IO_EVENT_INPUT);
}

void KPulseIoEvent::writeActivated()
{
    dispatch(PA_IO_EVENT_OUTPUT);
}

void KPulseIoEvent::exceptionActivated()
{
    dispatch(PA_IO_EVENT_ERROR);
}

void KPulseIoEvent::dispatch(pa_io_event_flags_t trigger)
{
    // Another callback in this pass of the event loop may have freed the
    // event or changed what it waits for.
    if (isFreed())
    {
        return;
    }

    unsigned int asked = trigger;
    if (trigger == PA_IO_EVENT_ERROR)
    {
        asked = PA_IO_EVENT_HANGUP | PA_IO_EVENT_ERROR;
    }
    if (!(m_flags & asked))
    {
        return;
    }

    // select() tells only readable, writable or exceptional; ask poll()
    // what the descriptor is up to, for PA_IO_EVENT_HANGUP and
    // PA_IO_EVENT_ERROR, and to drop a stale notification (the data read
    // meanwhile by another callback in this pass).
    struct pollfd pfd;
    pfd.fd = m_fd;
    pfd.events = 0;
    if (m_flags & PA_IO_EVENT_INPUT)
    {
        pfd.events |= POLLIN;
    }
    if (m_flags & PA_IO_EVENT_OUTPUT)
    {
        pfd.events |= POLLOUT;
    }
    pfd.revents = 0;

    int ret;
    do
    {
        ret = ::poll(&pfd, 1, 0);
    }
    while (ret < 0 && errno == EINTR);

    unsigned int events = PA_IO_EVENT_NULL;
    if (ret < 0)
    {
        // cannot tell; report what select() said
        events = trigger;
    }
    else if (ret > 0)
    {
        if (pfd.revents & POLLIN)
        {
            events |= PA_IO_EVENT_INPUT;
        }
        if (pfd.revents & POLLOUT)
        {
            events |= PA_IO_EVENT_OUTPUT;
        }
        if (pfd.revents & POLLHUP)
        {
            events |= PA_IO_EVENT_HANGUP;
        }
        if (pfd.revents & (POLLERR | POLLNVAL))
        {
            events |= PA_IO_EVENT_ERROR;
        }
    }
    else if (trigger == PA_IO_EVENT_ERROR)
    {
        // out-of-band data or the like, which poll() was not asked about
        events = PA_IO_EVENT_ERROR;
    }

    // As pa_mainloop does: what was asked for, and hang-ups and errors
    // always.
    events &= m_flags | PA_IO_EVENT_HANGUP | PA_IO_EVENT_ERROR;
    if (events == PA_IO_EVENT_NULL)
    {
        return;
    }

    m_loop->enterCallback();
    m_callback(m_loop->api(), ioHandle(this), m_fd,
               pa_io_event_flags_t(events), m_userdata);
    m_loop->leaveCallback();
}

// ---- KPulseTimeEvent ----------------------------------------------------

KPulseTimeEvent::KPulseTimeEvent(KPulseMainLoop *loop,
                                 pa_time_event_cb_t callback, void *userdata)
    : KPulseEvent(loop),
      m_callback(callback),
      m_destroy(0)
{
    m_userdata = userdata;
    m_time.tv_sec = 0;
    m_time.tv_usec = 0;
    m_timer = new QTimer(this);
    connect(m_timer, SIGNAL(timeout()), SLOT(timeout()));
}

void KPulseTimeEvent::restart(const struct timeval *tv)
{
    if (isFreed())
    {
        return;
    }

    if (!tv)
    {
        m_timer->stop();
        return;
    }

    m_time = *tv;

    // The time is absolute (wall clock: libpulse uses its monotonic
    // clock only with its own main loops); a time passed fires as soon
    // as possible.  Round up, so as not to fire early.
    struct timeval now;
    pa_gettimeofday(&now);

    int msec = 0;
    if (pa_timeval_cmp(&m_time, &now) > 0)
    {
        pa_usec_t usec = pa_timeval_diff(&m_time, &now);
        pa_usec_t rounded = (usec + PA_USEC_PER_MSEC - 1) / PA_USEC_PER_MSEC;
        msec = rounded > pa_usec_t(INT_MAX) ? INT_MAX : int(rounded);
    }

    m_timer->start(msec, true);
}

void KPulseTimeEvent::stop()
{
    m_timer->stop();
}

void KPulseTimeEvent::callDestroy()
{
    if (m_destroy)
    {
        m_destroy(m_loop->api(), timeHandle(this), m_userdata);
    }
}

void KPulseTimeEvent::timeout()
{
    if (isFreed())
    {
        return;
    }

    // a copy: the callback may restart the event
    struct timeval tv = m_time;

    m_loop->enterCallback();
    m_callback(m_loop->api(), timeHandle(this), &tv, m_userdata);
    m_loop->leaveCallback();
}

// ---- KPulseDeferEvent ---------------------------------------------------

KPulseDeferEvent::KPulseDeferEvent(KPulseMainLoop *loop,
                                   pa_defer_event_cb_t callback,
                                   void *userdata)
    : KPulseEvent(loop),
      m_callback(callback),
      m_destroy(0)
{
    m_userdata = userdata;
    m_timer = new QTimer(this);
    connect(m_timer, SIGNAL(timeout()), SLOT(timeout()));
}

void KPulseDeferEvent::setEnabled(bool enabled)
{
    if (isFreed() || enabled == m_timer->isActive())
    {
        return;
    }

    // A zero timer fires on every pass of the event loop, as pa_mainloop
    // dispatches an enabled defer event on each iteration.
    if (enabled)
    {
        m_timer->start(0, false);
    }
    else
    {
        m_timer->stop();
    }
}

void KPulseDeferEvent::stop()
{
    m_timer->stop();
}

void KPulseDeferEvent::callDestroy()
{
    if (m_destroy)
    {
        m_destroy(m_loop->api(), deferHandle(this), m_userdata);
    }
}

void KPulseDeferEvent::timeout()
{
    if (isFreed())
    {
        return;
    }

    m_loop->enterCallback();
    m_callback(m_loop->api(), deferHandle(this), m_userdata);
    m_loop->leaveCallback();
}

// ---- KPulseMainLoop -----------------------------------------------------

KPulseMainLoop::KPulseMainLoop(QObject *parent, const char *name)
    : QObject(parent, name),
      m_cleanupPending(false),
      m_callbackDepth(0)
{
    m_api.userdata = this;
    m_api.io_new = kpulse_io_new;
    m_api.io_enable = kpulse_io_enable;
    m_api.io_free = kpulse_io_free;
    m_api.io_set_destroy = kpulse_io_set_destroy;
    m_api.time_new = kpulse_time_new;
    m_api.time_restart = kpulse_time_restart;
    m_api.time_free = kpulse_time_free;
    m_api.time_set_destroy = kpulse_time_set_destroy;
    m_api.defer_new = kpulse_defer_new;
    m_api.defer_enable = kpulse_defer_enable;
    m_api.defer_free = kpulse_defer_free;
    m_api.defer_set_destroy = kpulse_defer_set_destroy;
    m_api.quit = kpulse_quit;
}

KPulseMainLoop::~KPulseMainLoop()
{
    // no more deleteFreedEvents() calls: the loop below does it
    m_cleanupPending = true;

    // As pa_mainloop_free() does: free what is left, calling the destroy
    // callbacks.  Those may free further events.
    while (!m_events.isEmpty())
    {
        m_events.getFirst()->dispose();
    }

    while (!m_freedEvents.isEmpty())
    {
        KPulseEvent *e = m_freedEvents.take(0);
        e->callDestroy();
        delete e;
    }
}

KPulseMainLoop *KPulseMainLoop::fromApi(pa_mainloop_api *api)
{
    return static_cast<KPulseMainLoop *>(api->userdata);
}

void KPulseMainLoop::addEvent(KPulseEvent *event)
{
    m_events.append(event);
}

void KPulseMainLoop::freeEvent(KPulseEvent *event)
{
    m_events.removeRef(event);
    m_freedEvents.append(event);

    if (!m_cleanupPending)
    {
        m_cleanupPending = true;
        QTimer::singleShot(0, this, SLOT(deleteFreedEvents()));
    }
}

void KPulseMainLoop::deleteFreedEvents()
{
    m_cleanupPending = false;

    // A callback running a nested event loop may still be inside the
    // code of an event freed meanwhile.
    if (m_callbackDepth > 0)
    {
        m_cleanupPending = true;
        QTimer::singleShot(NestedCleanupDelay, this,
                           SLOT(deleteFreedEvents()));
        return;
    }

    while (!m_freedEvents.isEmpty())
    {
        KPulseEvent *e = m_freedEvents.take(0);
        e->callDestroy();
        delete e;
    }
}

#include "kpulsemainloop.moc"
#include "kpulsemainloop_p.moc"
