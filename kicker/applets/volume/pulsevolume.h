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

#ifndef PULSEVOLUME_H
#define PULSEVOLUME_H

#include <qmap.h>
#include <qobject.h>
#include <qstring.h>
#include <qvaluelist.h>

#include <pulse/context.h>
#include <pulse/introspect.h>
#include <pulse/subscribe.h>
#include <pulse/volume.h>

class QTimer;
class KPulseMainLoop;

/**
 * The sinks of the sound server and the volume of the default one,
 * followed live.  Connects on creation and again a few seconds after the
 * connection failed or was lost, for as long as it lives.  Everything is
 * asynchronous: the setters send the request and update the state they
 * report at once, the server's answer comes with changed().
 */
class PulseVolume : public QObject
{
    Q_OBJECT

public:
    struct Sink
    {
        Sink() : index(PA_INVALID_INDEX), muted(false) { pa_cvolume_init(&volume); }

        uint32_t index;
        QString name;
        QString description;
        pa_cvolume volume;
        bool muted;
    };

    PulseVolume(QObject *parent = 0, const char *name = 0);
    virtual ~PulseVolume();

    /** Whether there is a connection to the server, ready to be used. */
    bool isConnected() const { return m_ready; }

    /** The sinks, in the order of their indices. */
    QValueList<Sink> sinks() const;

    /** The default sink, or 0 when there is none (yet). */
    const Sink *defaultSink() const;

    /**
     * The volume of the default sink in percent, the average of its
     * channels (above 100 when amplified), or -1 without a default sink.
     */
    int volume() const;

    /** Whether the default sink is muted. */
    bool isMuted() const;

    /**
     * Sets the volume of the default sink, keeping the balance of the
     * channels: they are scaled so that their average is @p percent.
     */
    void setVolume(int percent);

    void setMuted(bool muted);

    /** Makes the sink called @p name the default one. */
    void setDefaultSink(const QString &name);

signals:
    /** Something above changed: sent once per pass of the event loop. */
    void changed();

private slots:
    void connectToServer();
    void emitChanged();

private:
    void disconnectFromServer();
    void scheduleChanged();
    void contextStateChanged();
    void subscriptionEvent(pa_subscription_event_type_t type, uint32_t index);
    void updateSink(const pa_sink_info *info);
    void updateServer(const pa_server_info *info);

    static void contextStateCallback(pa_context *c, void *userdata);
    static void subscribeCallback(pa_context *c,
                                  pa_subscription_event_type_t type,
                                  uint32_t index, void *userdata);
    static void sinkInfoCallback(pa_context *c, const pa_sink_info *info,
                                 int eol, void *userdata);
    static void serverInfoCallback(pa_context *c, const pa_server_info *info,
                                   void *userdata);

    KPulseMainLoop *m_loop;
    pa_context *m_context;
    bool m_ready;
    bool m_failureReported;
    QTimer *m_retryTimer;
    bool m_changePending;

    QMap<uint32_t, Sink> m_sinks;
    QString m_defaultSinkName;
};

#endif
