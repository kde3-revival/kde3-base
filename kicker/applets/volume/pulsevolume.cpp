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

#include <qtimer.h>

#include <kdebug.h>

#include <pulse/error.h>
#include <pulse/proplist.h>

#include "kpulsemainloop.h"
#include "pulsevolume.h"

// How long to wait before connecting again, in milliseconds.
static const int RetryDelay = 3000;

static void unrefOperation(pa_operation *op)
{
    // The callbacks are called without the reference; nothing waits for
    // an operation here.
    if (op)
    {
        pa_operation_unref(op);
    }
}

static int volumeToPercent(pa_volume_t v)
{
    return int((uint64_t(v) * 100 + PA_VOLUME_NORM / 2) / PA_VOLUME_NORM);
}

PulseVolume::PulseVolume(QObject *parent, const char *name)
    : QObject(parent, name),
      m_context(0),
      m_ready(false),
      m_failureReported(false),
      m_changePending(false)
{
    m_loop = new KPulseMainLoop(this);

    m_retryTimer = new QTimer(this);
    connect(m_retryTimer, SIGNAL(timeout()), SLOT(connectToServer()));

    connectToServer();
}

PulseVolume::~PulseVolume()
{
    // no changed() any more
    m_changePending = true;
    disconnectFromServer();
}

void PulseVolume::connectToServer()
{
    disconnectFromServer();

    pa_proplist *props = pa_proplist_new();
    pa_proplist_sets(props, PA_PROP_APPLICATION_NAME, "Volume Control");
    pa_proplist_sets(props, PA_PROP_APPLICATION_ID, "org.kde.kicker.volume");
    pa_proplist_sets(props, PA_PROP_APPLICATION_ICON_NAME, "kmix");
    m_context = pa_context_new_with_proplist(m_loop->api(), 0, props);
    pa_proplist_free(props);

    if (!m_context)
    {
        kdWarning(1210) << "Volume Control: cannot create a PulseAudio context"
                        << endl;
        m_retryTimer->start(RetryDelay, true);
        return;
    }

    pa_context_set_state_callback(m_context, contextStateCallback, this);

    // No autospawn: a panel applet trying every few seconds should not
    // start sound servers.
    if (pa_context_connect(m_context, 0, PA_CONTEXT_NOAUTOSPAWN, 0) < 0)
    {
        if (!m_failureReported)
        {
            kdWarning(1210) << "Volume Control: cannot connect to the sound server: "
                            << pa_strerror(pa_context_errno(m_context)) << endl;
            m_failureReported = true;
        }
        disconnectFromServer();
        m_retryTimer->start(RetryDelay, true);
    }
}

void PulseVolume::disconnectFromServer()
{
    m_retryTimer->stop();

    if (m_context)
    {
        // pa_context_disconnect() would call the state callback.
        pa_context_set_state_callback(m_context, 0, 0);
        pa_context_set_subscribe_callback(m_context, 0, 0);
        pa_context_disconnect(m_context);
        pa_context_unref(m_context);
        m_context = 0;
    }

    if (m_ready || !m_sinks.isEmpty() || !m_defaultSinkName.isEmpty())
    {
        m_ready = false;
        m_sinks.clear();
        m_defaultSinkName = QString::null;
        scheduleChanged();
    }
}

void PulseVolume::contextStateCallback(pa_context *, void *userdata)
{
    static_cast<PulseVolume *>(userdata)->contextStateChanged();
}

void PulseVolume::contextStateChanged()
{
    switch (pa_context_get_state(m_context))
    {
        case PA_CONTEXT_READY:
        {
            kdDebug(1210) << "Volume Control: connected to "
                          << pa_context_get_server(m_context) << endl;
            m_ready = true;
            m_failureReported = false;

            pa_context_set_subscribe_callback(m_context, subscribeCallback,
                                              this);
            unrefOperation(pa_context_subscribe(m_context,
                pa_subscription_mask_t(PA_SUBSCRIPTION_MASK_SINK |
                                       PA_SUBSCRIPTION_MASK_SERVER),
                0, 0));
            unrefOperation(pa_context_get_server_info(m_context,
                                                      serverInfoCallback,
                                                      this));
            unrefOperation(pa_context_get_sink_info_list(m_context,
                                                         sinkInfoCallback,
                                                         this));
            scheduleChanged();
            break;
        }

        case PA_CONTEXT_FAILED:
        case PA_CONTEXT_TERMINATED:
        {
            if (!m_failureReported)
            {
                kdWarning(1210) << "Volume Control: "
                                << (m_ready ? "lost the connection to"
                                            : "cannot connect to")
                                << " the sound server: "
                                << pa_strerror(pa_context_errno(m_context))
                                << endl;
                m_failureReported = true;
            }

            m_ready = false;
            m_sinks.clear();
            m_defaultSinkName = QString::null;
            scheduleChanged();

            // The context is freed by the retry, not here in its own
            // callback.
            m_retryTimer->start(RetryDelay, true);
            break;
        }

        default:
            break;
    }
}

void PulseVolume::subscribeCallback(pa_context *,
                                    pa_subscription_event_type_t type,
                                    uint32_t index, void *userdata)
{
    static_cast<PulseVolume *>(userdata)->subscriptionEvent(type, index);
}

void PulseVolume::subscriptionEvent(pa_subscription_event_type_t type,
                                    uint32_t index)
{
    unsigned int facility = type & PA_SUBSCRIPTION_EVENT_FACILITY_MASK;
    unsigned int kind = type & PA_SUBSCRIPTION_EVENT_TYPE_MASK;

    if (facility == PA_SUBSCRIPTION_EVENT_SINK)
    {
        if (kind == PA_SUBSCRIPTION_EVENT_REMOVE)
        {
            if (m_sinks.contains(index))
            {
                m_sinks.remove(index);
                scheduleChanged();
            }
        }
        else
        {
            unrefOperation(pa_context_get_sink_info_by_index(m_context, index,
                                                             sinkInfoCallback,
                                                             this));
        }
    }
    else if (facility == PA_SUBSCRIPTION_EVENT_SERVER)
    {
        unrefOperation(pa_context_get_server_info(m_context,
                                                  serverInfoCallback, this));
    }
}

void PulseVolume::sinkInfoCallback(pa_context *, const pa_sink_info *info,
                                   int eol, void *userdata)
{
    // eol < 0: the sink is gone already, nothing to do
    if (eol == 0 && info)
    {
        static_cast<PulseVolume *>(userdata)->updateSink(info);
    }
}

void PulseVolume::updateSink(const pa_sink_info *info)
{
    Sink &sink = m_sinks[info->index];
    sink.index = info->index;
    sink.name = QString::fromUtf8(info->name);
    sink.description = QString::fromUtf8(info->description);
    sink.volume = info->volume;
    sink.muted = info->mute != 0;
    scheduleChanged();
}

void PulseVolume::serverInfoCallback(pa_context *, const pa_server_info *info,
                                     void *userdata)
{
    if (info)
    {
        static_cast<PulseVolume *>(userdata)->updateServer(info);
    }
}

void PulseVolume::updateServer(const pa_server_info *info)
{
    QString name = QString::fromUtf8(info->default_sink_name);
    if (name != m_defaultSinkName)
    {
        m_defaultSinkName = name;
        scheduleChanged();
    }
}

void PulseVolume::scheduleChanged()
{
    if (!m_changePending)
    {
        m_changePending = true;
        QTimer::singleShot(0, this, SLOT(emitChanged()));
    }
}

void PulseVolume::emitChanged()
{
    m_changePending = false;
    emit changed();
}

QValueList<PulseVolume::Sink> PulseVolume::sinks() const
{
    return m_sinks.values();
}

const PulseVolume::Sink *PulseVolume::defaultSink() const
{
    if (m_defaultSinkName.isEmpty())
    {
        return 0;
    }

    QMap<uint32_t, Sink>::ConstIterator it;
    for (it = m_sinks.begin(); it != m_sinks.end(); ++it)
    {
        if ((*it).name == m_defaultSinkName)
        {
            return &(*it);
        }
    }

    return 0;
}

int PulseVolume::volume() const
{
    const Sink *sink = defaultSink();
    if (!sink || !pa_cvolume_valid(&sink->volume))
    {
        return -1;
    }

    return volumeToPercent(pa_cvolume_max(&sink->volume));
}

bool PulseVolume::isMuted() const
{
    const Sink *sink = defaultSink();
    return sink && sink->muted;
}

void PulseVolume::setVolume(int percent)
{
    const Sink *sink = defaultSink();
    if (!m_ready || !sink || !pa_cvolume_valid(&sink->volume) || percent < 0)
    {
        return;
    }

    pa_volume_t target =
        pa_volume_t((uint64_t(percent) * PA_VOLUME_NORM + 50) / 100);
    if (target > PA_VOLUME_MAX)
    {
        target = PA_VOLUME_MAX;
    }

    uint32_t index = sink->index;
    pa_cvolume cv = sink->volume;
    if (pa_cvolume_max(&cv) == target)
    {
        return;
    }

    // The loudest channel follows the slider, the others keep their
    // proportion to it (pa_cvolume_scale sets them all when it is 0).
    pa_cvolume_scale(&cv, target);

    unrefOperation(pa_context_set_sink_volume_by_index(m_context, index,
                                                       &cv, 0, 0));

    // Report the new volume now, so that steps made before the server
    // answers (a fast mouse wheel) start from it.
    m_sinks[index].volume = cv;
    scheduleChanged();
}

void PulseVolume::setMuted(bool muted)
{
    const Sink *sink = defaultSink();
    if (!m_ready || !sink || sink->muted == muted)
    {
        return;
    }

    uint32_t index = sink->index;
    unrefOperation(pa_context_set_sink_mute_by_index(m_context, index,
                                                     muted, 0, 0));
    m_sinks[index].muted = muted;
    scheduleChanged();
}

void PulseVolume::setDefaultSink(const QString &name)
{
    if (!m_ready || name.isEmpty() || name == m_defaultSinkName)
    {
        return;
    }

    unrefOperation(pa_context_set_default_sink(m_context, name.utf8(),
                                               0, 0));
    m_defaultSinkName = name;
    scheduleChanged();
}

#include "pulsevolume.moc"
