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

// libnm (GLib, GIO) first: Qt defines "signals", which GLib uses as an
// identifier.
#include <gio/gio.h>
// nm-errors.h lacks the C linkage for C++ (libnm 1.58 and earlier).
extern "C" {
#include <nm-errors.h>
}
#include <NetworkManager.h>
#include <nm-secret-agent-old.h>
#include <string.h>

#include <qdatetime.h>
#include <qmap.h>
#include <qstringlist.h>
#include <qtimer.h>

#include <kdebug.h>
#include <klocale.h>

#include "networkbackend.h"

// The callbacks of libnm come from the GLib main loop: Qt has to run on it.
#ifndef QT_GLIB_EVENTLOOP
#error "The network applet needs Qt built with the GLib event loop (QT_GLIB_EVENTLOOP)"
#endif

// The interval of the refresh of the signal strengths, the bit rate and
// the like, which change without a signal of libnm worth following, and
// how long a scan may take before it is no longer shown.
static const int PollInterval = 3000;
static const int ScanTimeout = 15000;

// Another try at the system bus after this many milliseconds.
static const int RetryInterval = 10000;

static const char AgentIdentifier[] = "org.kde.kde3.networkapplet";

/******************************************************************************
 * The secret agent: a subclass of NMSecretAgentOld forwarding the requests
 * to the backend.
 */

struct NetworkAgent
{
    NMSecretAgentOld parent;
    NetworkBackendPrivate *d;
};

struct NetworkAgentClass
{
    NMSecretAgentOldClass parent;
};

static GType network_agent_get_type();

class NetworkBackendPrivate
{
public:
    // A device followed for its state changes.
    struct Device
    {
        Device() : device(0), activated(false) {}
        NMDevice *device;       // referenced
        QString name;           // of its connection, when activated
        bool activated;
    };

    // A connection this applet started, until it is up or failed.
    struct Activation
    {
        Activation() : device(0), connection(0), isNew(false) {}
        NMDevice *device;               // referenced
        NMRemoteConnection *connection; // referenced, when new
        QString name;
        bool isNew;
    };

    // A request of NetworkManager for a password, waiting for the user.
    struct SecretRequest
    {
        SecretRequest()
            : id(0), connection(0), agentOwned(false), callback(0), userData(0) {}
        int id;
        NMConnection *connection;   // referenced
        QCString path;
        QCString setting;
        QString ssid;
        bool agentOwned;            // the profile leaves the password to agents
        NMSecretAgentOldGetSecretsFunc callback;
        gpointer userData;
    };

    // The data of an activation request until its answer.
    struct ActivateCall
    {
        NetworkBackendPrivate *d;
        NMDevice *device;   // referenced
        QString name;
        bool isNew;
    };

    NetworkBackendPrivate(NetworkBackend *backend);
    ~NetworkBackendPrivate();

    void start();
    void stop();
    void schedule();

    void syncDevices();
    void track(NMDevice *device);
    void untrack(Device &device);
    Device *findDevice(NMDevice *device);
    void finishActivations(NMDevice *device, bool success);

    NMDeviceWifi *wifiDevice() const;
    NMAccessPoint *findAp(NMDeviceWifi *wifi, const QString &ssid) const;
    NMRemoteConnection *knownConnection(NMDevice *device,
                                        const QString &ssid) const;
    void activate(NMRemoteConnection *connection, NMDevice *device,
                  NMAccessPoint *ap, const QString &name);
    void addAndActivate(NMConnection *partial, NMDevice *device,
                        NMAccessPoint *ap, const QString &name);
    NMConnection *newWifiConnection(NetworkBackend::Security security,
                                    const QString &password);

    NetworkBackend::Status computeStatus() const;
    NetworkBackend::NetworkList computeNetworks() const;
    QString deviceName(NMDevice *device) const;
    static QString reasonText(guint reason);

    // callbacks
    static void clientReady(GObject *source, GAsyncResult *result, gpointer data);
    static void agentReady(GObject *source, GAsyncResult *result, gpointer data);
    static void changedCb(gpointer data);
    static void notifyCb(GObject *, GParamSpec *, gpointer data);
    static void objectCb(GObject *, GObject *, gpointer data);
    static void permissionCb(NMClient *, guint, guint, gpointer data);
    static void deviceStateCb(NMDevice *device, guint newState, guint oldState,
                              guint reason, gpointer data);
    static void activateDone(GObject *source, GAsyncResult *result, gpointer data);
    static void addAndActivateDone(GObject *source, GAsyncResult *result,
                                   gpointer data);
    static void callDone(GObject *source, GAsyncResult *result, gpointer data);
    static void setPropertyDone(GObject *source, GAsyncResult *result,
                                gpointer data);
    static void disconnectDone(GObject *source, GAsyncResult *result,
                               gpointer data);
    static void scanDone(GObject *source, GAsyncResult *result, gpointer data);
    static void deleteDone(GObject *source, GAsyncResult *result, gpointer data);
    static void updateDone(GObject *source, GAsyncResult *result, gpointer data);

    static void getSecrets(NMSecretAgentOld *agent, NMConnection *connection,
                           const char *path, const char *settingName,
                           const char **hints, NMSecretAgentGetSecretsFlags flags,
                           NMSecretAgentOldGetSecretsFunc callback,
                           gpointer userData);
    static void cancelGetSecrets(NMSecretAgentOld *agent, const char *path,
                                 const char *settingName);
    static void saveSecrets(NMSecretAgentOld *agent, NMConnection *connection,
                            const char *path,
                            NMSecretAgentOldSaveSecretsFunc callback,
                            gpointer userData);
    static void deleteSecrets(NMSecretAgentOld *agent, NMConnection *connection,
                              const char *path,
                              NMSecretAgentOldDeleteSecretsFunc callback,
                              gpointer userData);

    void reportError(GError *error, const QString &what);

    NetworkBackend *q;
    GCancellable *cancellable;
    NMClient *client;
    NMSecretAgentOld *agent;

    QValueList<Device> devices;
    QValueList<Activation> activations;
    QValueList<SecretRequest> requests;
    int nextRequest;

    bool scanning;
    gint64 lastScan;
    QTime scanStarted;

    NetworkBackend::Status status;
    NetworkBackend::NetworkList networks;

    QTimer *updateTimer;
    QTimer *pollTimer;
    QTimer *retryTimer;
};

G_DEFINE_TYPE(NetworkAgent, network_agent, NM_TYPE_SECRET_AGENT_OLD)

static void network_agent_init(NetworkAgent *agent)
{
    agent->d = 0;
}

static void network_agent_class_init(NetworkAgentClass *klass)
{
    NMSecretAgentOldClass *agentClass = NM_SECRET_AGENT_OLD_CLASS(klass);
    agentClass->get_secrets = NetworkBackendPrivate::getSecrets;
    agentClass->cancel_get_secrets = NetworkBackendPrivate::cancelGetSecrets;
    agentClass->save_secrets = NetworkBackendPrivate::saveSecrets;
    agentClass->delete_secrets = NetworkBackendPrivate::deleteSecrets;
}

static NetworkBackendPrivate *agentBackend(NMSecretAgentOld *agent)
{
    return G_TYPE_CHECK_INSTANCE_CAST(agent, network_agent_get_type(),
                                      NetworkAgent)->d;
}

/******************************************************************************
 * Helpers
 */

static QString ssidString(GBytes *ssid)
{
    if (!ssid)
    {
        return QString::null;
    }

    gsize len = 0;
    const guint8 *data = static_cast<const guint8 *>(g_bytes_get_data(ssid, &len));
    if (!data || !len)
    {
        return QString::null;
    }

    // UTF-8, or guessed from the locale's encodings
    char *utf8 = nm_utils_ssid_to_utf8(data, len);
    QString result = QString::fromUtf8(utf8);
    g_free(utf8);
    return result;
}

static QString connectionSsid(NMConnection *connection)
{
    NMSettingWireless *wireless = nm_connection_get_setting_wireless(connection);
    if (!wireless)
    {
        return QString::null;
    }

    const char *mode = nm_setting_wireless_get_mode(wireless);
    if (mode && strcmp(mode, NM_SETTING_WIRELESS_MODE_INFRA) != 0)
    {
        return QString::null;
    }

    return ssidString(nm_setting_wireless_get_ssid(wireless));
}

static bool isActivating(NMDeviceState state)
{
    return state >= NM_DEVICE_STATE_PREPARE && state < NM_DEVICE_STATE_ACTIVATED;
}

static NetworkBackend::Security apSecurity(NMAccessPoint *ap, QString *text)
{
    guint flags = nm_access_point_get_flags(ap);
    guint wpa = nm_access_point_get_wpa_flags(ap);
    guint rsn = nm_access_point_get_rsn_flags(ap);

    NetworkBackend::Security security;
    if ((wpa | rsn) & (NM_802_11_AP_SEC_KEY_MGMT_802_1X |
                       NM_802_11_AP_SEC_KEY_MGMT_EAP_SUITE_B_192))
    {
        security = NetworkBackend::Enterprise;
    }
    else if ((wpa | rsn) & NM_802_11_AP_SEC_KEY_MGMT_PSK)
    {
        security = NetworkBackend::WPAPersonal;
    }
    else if (rsn & NM_802_11_AP_SEC_KEY_MGMT_SAE)
    {
        security = NetworkBackend::WPA3Personal;
    }
    else if (rsn & NM_802_11_AP_SEC_KEY_MGMT_OWE)
    {
        security = NetworkBackend::EnhancedOpen;
    }
    else if (flags & NM_802_11_AP_FLAGS_PRIVACY)
    {
        security = NetworkBackend::WEP;
    }
    else
    {
        security = NetworkBackend::Open;
    }

    if (text)
    {
        QStringList names;
        if (security == NetworkBackend::WEP)
        {
            names << "WEP";
        }
        if (wpa)
        {
            names << "WPA";
        }
        if (rsn & ~(NM_802_11_AP_SEC_KEY_MGMT_SAE | NM_802_11_AP_SEC_KEY_MGMT_OWE |
                    NM_802_11_AP_SEC_KEY_MGMT_OWE_TM))
        {
            names << "WPA2";
        }
        if (rsn & (NM_802_11_AP_SEC_KEY_MGMT_SAE | NM_802_11_AP_SEC_KEY_MGMT_OWE))
        {
            names << "WPA3";
        }
        *text = names.join("/");
        if (security == NetworkBackend::Enterprise)
        {
            *text += ' ' + i18n("Enterprise");
        }
    }

    return security;
}

static QString dbusError(GError *error)
{
    if (g_dbus_error_is_remote_error(error))
    {
        g_dbus_error_strip_remote_error(error);
    }
    return QString::fromUtf8(error->message);
}

/******************************************************************************
 * NetworkBackendPrivate
 */

NetworkBackendPrivate::NetworkBackendPrivate(NetworkBackend *backend)
    : q(backend),
      cancellable(0),
      client(0),
      agent(0),
      nextRequest(1),
      scanning(false),
      lastScan(0)
{
    updateTimer = new QTimer(q);
    QObject::connect(updateTimer, SIGNAL(timeout()), q, SLOT(update()));

    pollTimer = new QTimer(q);
    QObject::connect(pollTimer, SIGNAL(timeout()), q, SLOT(update()));

    retryTimer = new QTimer(q);
    QObject::connect(retryTimer, SIGNAL(timeout()), q, SLOT(retry()));
}

NetworkBackendPrivate::~NetworkBackendPrivate()
{
    stop();
}

void NetworkBackendPrivate::start()
{
    cancellable = g_cancellable_new();
    nm_client_new_async(cancellable, clientReady, this);
}

void NetworkBackendPrivate::stop()
{
    // The callbacks of the calls on their way find them cancelled and
    // leave this alone.
    if (cancellable)
    {
        g_cancellable_cancel(cancellable);
        g_object_unref(cancellable);
        cancellable = 0;
    }

    // NetworkManager gets an error for the passwords never given.
    QValueList<SecretRequest>::Iterator it;
    for (it = requests.begin(); it != requests.end(); ++it)
    {
        GError *error = g_error_new_literal(NM_SECRET_AGENT_ERROR,
                                            NM_SECRET_AGENT_ERROR_AGENT_CANCELED,
                                            "The agent is going away");
        (*it).callback(agent, (*it).connection, 0, error, (*it).userData);
        g_error_free(error);
        g_object_unref((*it).connection);
    }
    requests.clear();

    if (agent)
    {
        reinterpret_cast<NetworkAgent *>(agent)->d = 0;
        nm_secret_agent_old_destroy(agent);
        g_object_unref(agent);
        agent = 0;
    }

    QValueList<Activation>::Iterator ait;
    for (ait = activations.begin(); ait != activations.end(); ++ait)
    {
        g_object_unref((*ait).device);
        if ((*ait).connection)
        {
            g_object_unref((*ait).connection);
        }
    }
    activations.clear();

    QValueList<Device>::Iterator dit;
    for (dit = devices.begin(); dit != devices.end(); ++dit)
    {
        untrack(*dit);
    }
    devices.clear();

    if (client)
    {
        g_signal_handlers_disconnect_by_data(client, this);
        g_object_unref(client);
        client = 0;
    }
}

void NetworkBackendPrivate::schedule()
{
    // Many changes come together: handle them once.
    if (!updateTimer->isActive())
    {
        updateTimer->start(0, true);
    }
}

void NetworkBackendPrivate::clientReady(GObject *, GAsyncResult *result, gpointer data)
{
    GError *error = 0;
    NMClient *client = nm_client_new_finish(result, &error);
    if (!client)
    {
        if (!g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        {
            NetworkBackendPrivate *d = static_cast<NetworkBackendPrivate *>(data);
            kdWarning() << "network applet: no connection to NetworkManager: "
                        << error->message << endl;
            d->retryTimer->start(RetryInterval, true);
            d->schedule();
        }
        g_error_free(error);
        return;
    }

    NetworkBackendPrivate *d = static_cast<NetworkBackendPrivate *>(data);
    d->client = client;

    // NMClient follows NetworkManager's restarts by itself: the devices
    // and connections go away and come back.
    g_signal_connect_swapped(client, "notify", G_CALLBACK(changedCb), d);
    const char *const objectSignals[] = {
        NM_CLIENT_DEVICE_ADDED, NM_CLIENT_DEVICE_REMOVED,
        NM_CLIENT_CONNECTION_ADDED, NM_CLIENT_CONNECTION_REMOVED,
        NM_CLIENT_ACTIVE_CONNECTION_ADDED, NM_CLIENT_ACTIVE_CONNECTION_REMOVED
    };
    for (unsigned i = 0; i < sizeof(objectSignals) / sizeof(objectSignals[0]); ++i)
    {
        g_signal_connect(client, objectSignals[i], G_CALLBACK(objectCb), d);
    }
    g_signal_connect(client, NM_CLIENT_PERMISSION_CHANGED,
                     G_CALLBACK(permissionCb), d);

    // The secret agent registers itself whenever NetworkManager runs.
    NetworkAgent *agent = static_cast<NetworkAgent *>(
        g_object_new(network_agent_get_type(),
                     NM_SECRET_AGENT_OLD_IDENTIFIER, AgentIdentifier,
                     NM_SECRET_AGENT_OLD_CAPABILITIES, NM_SECRET_AGENT_CAPABILITY_NONE,
                     (char *)0));
    agent->d = d;
    // Kept from now on: libnm exports the agent before its initialization
    // ends, and stop() must cut it off from this even then.
    d->agent = NM_SECRET_AGENT_OLD(agent);
    g_async_initable_init_async(G_ASYNC_INITABLE(agent), G_PRIORITY_DEFAULT,
                                d->cancellable, agentReady, d);

    d->schedule();
}

void NetworkBackendPrivate::agentReady(GObject *source, GAsyncResult *result,
                                       gpointer data)
{
    GError *error = 0;
    if (!g_async_initable_init_finish(G_ASYNC_INITABLE(source), result, &error))
    {
        // Cancelled: stop() has dealt with the agent already.
        if (!g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        {
            kdWarning() << "network applet: no secret agent: "
                        << error->message << endl;
            NetworkBackendPrivate *d = static_cast<NetworkBackendPrivate *>(data);
            if (d->agent == NM_SECRET_AGENT_OLD(source))
            {
                reinterpret_cast<NetworkAgent *>(d->agent)->d = 0;
                nm_secret_agent_old_destroy(d->agent);
                g_object_unref(d->agent);
                d->agent = 0;
            }
        }
        g_error_free(error);
    }
}

void NetworkBackendPrivate::changedCb(gpointer data)
{
    static_cast<NetworkBackendPrivate *>(data)->schedule();
}

void NetworkBackendPrivate::notifyCb(GObject *, GParamSpec *, gpointer data)
{
    static_cast<NetworkBackendPrivate *>(data)->schedule();
}

void NetworkBackendPrivate::objectCb(GObject *, GObject *, gpointer data)
{
    static_cast<NetworkBackendPrivate *>(data)->schedule();
}

void NetworkBackendPrivate::permissionCb(NMClient *, guint, guint, gpointer data)
{
    static_cast<NetworkBackendPrivate *>(data)->schedule();
}

void NetworkBackendPrivate::syncDevices()
{
    const GPtrArray *present = client && nm_client_get_nm_running(client)
                                   ? nm_client_get_devices(client) : 0;

    // gone
    QValueList<Device>::Iterator it = devices.begin();
    while (it != devices.end())
    {
        bool found = false;
        for (guint i = 0; present && i < present->len; ++i)
        {
            if (g_ptr_array_index(present, i) == (*it).device)
            {
                found = true;
                break;
            }
        }
        if (found)
        {
            ++it;
            continue;
        }

        finishActivations((*it).device, false);
        untrack(*it);
        it = devices.remove(it);
    }

    // new
    for (guint i = 0; present && i < present->len; ++i)
    {
        NMDevice *device = NM_DEVICE(g_ptr_array_index(present, i));
        if (!findDevice(device))
        {
            track(device);
        }
    }
}

void NetworkBackendPrivate::track(NMDevice *device)
{
    if (!NM_IS_DEVICE_WIFI(device) && !NM_IS_DEVICE_ETHERNET(device) &&
        !NM_IS_DEVICE_MODEM(device))
    {
        return;
    }

    Device tracked;
    tracked.device = NM_DEVICE(g_object_ref(device));
    tracked.activated = nm_device_get_state(device) == NM_DEVICE_STATE_ACTIVATED;
    if (tracked.activated)
    {
        tracked.name = deviceName(device);
    }
    devices.append(tracked);

    g_signal_connect(device, "state-changed", G_CALLBACK(deviceStateCb), this);
    g_signal_connect(device, "notify", G_CALLBACK(notifyCb), this);
    if (NM_IS_DEVICE_WIFI(device))
    {
        g_signal_connect(device, "access-point-added", G_CALLBACK(objectCb), this);
        g_signal_connect(device, "access-point-removed", G_CALLBACK(objectCb), this);
    }
}

void NetworkBackendPrivate::untrack(Device &device)
{
    g_signal_handlers_disconnect_by_data(device.device, this);
    g_object_unref(device.device);
    device.device = 0;
}

NetworkBackendPrivate::Device *NetworkBackendPrivate::findDevice(NMDevice *device)
{
    QValueList<Device>::Iterator it;
    for (it = devices.begin(); it != devices.end(); ++it)
    {
        if ((*it).device == device)
        {
            return &(*it);
        }
    }
    return 0;
}

// The activations this applet started on @p device are over.  A new
// connection that never came up (a mistyped password given up on, a
// network gone, one replaced by the next the user chose) is deleted again.
void NetworkBackendPrivate::finishActivations(NMDevice *device, bool success)
{
    NMActiveConnection *active = success ? nm_device_get_active_connection(device) : 0;
    NMRemoteConnection *up = active ? nm_active_connection_get_connection(active) : 0;

    QValueList<Activation>::Iterator it = activations.begin();
    while (it != activations.end())
    {
        if ((*it).device != device)
        {
            ++it;
            continue;
        }

        if ((*it).connection)
        {
            if ((!success || (*it).connection != up) && (*it).isNew && cancellable)
            {
                kdDebug() << "network applet: deleting the new connection "
                          << nm_connection_get_path(NM_CONNECTION((*it).connection))
                          << ", which failed" << endl;
                nm_remote_connection_delete_async((*it).connection, cancellable,
                                                  deleteDone, this);
            }
            g_object_unref((*it).connection);
        }
        g_object_unref((*it).device);
        it = activations.remove(it);
    }
}

void NetworkBackendPrivate::deviceStateCb(NMDevice *device, guint newState,
                                          guint oldState, guint reason,
                                          gpointer data)
{
    NetworkBackendPrivate *d = static_cast<NetworkBackendPrivate *>(data);
    Device *tracked = d->findDevice(device);
    if (!tracked)
    {
        return;
    }

    kdDebug() << "network applet: " << nm_device_get_iface(device) << ": "
              << oldState << " -> " << newState << ", reason " << reason << endl;

    bool wifi = NM_IS_DEVICE_WIFI(device);
    NetworkBackend *q = d->q;
    if (newState == NM_DEVICE_STATE_ACTIVATED)
    {
        tracked->name = d->deviceName(device);
        d->finishActivations(device, true);
        if (!tracked->activated)
        {
            tracked->activated = true;
            emit q->connected(tracked->name, wifi);
        }
    }
    else if (newState == NM_DEVICE_STATE_FAILED)
    {
        QString name;
        QValueList<Activation>::ConstIterator it;
        for (it = d->activations.begin(); it != d->activations.end(); ++it)
        {
            if ((*it).device == device)
            {
                name = (*it).name;
            }
        }
        if (name.isEmpty())
        {
            name = d->deviceName(device);
        }

        tracked->activated = false;
        d->finishActivations(device, false);
        emit q->connectionFailed(name, reasonText(reason));
    }
    else if (newState == NM_DEVICE_STATE_DEACTIVATING ||
             newState < NM_DEVICE_STATE_PREPARE)
    {
        // Not when switching to another network, nor for the stop of a
        // connection being made, which the user did.
        if (newState != NM_DEVICE_STATE_DEACTIVATING &&
            reason != NM_DEVICE_STATE_REASON_NEW_ACTIVATION)
        {
            d->finishActivations(device, false);
        }

        if (tracked->activated)
        {
            tracked->activated = false;
            if (reason != NM_DEVICE_STATE_REASON_NEW_ACTIVATION)
            {
                emit q->disconnected(tracked->name, wifi);
            }
        }
    }

    d->schedule();
}

QString NetworkBackendPrivate::deviceName(NMDevice *device) const
{
    if (NM_IS_DEVICE_WIFI(device))
    {
        NMAccessPoint *ap =
            nm_device_wifi_get_active_access_point(NM_DEVICE_WIFI(device));
        QString ssid = ap ? ssidString(nm_access_point_get_ssid(ap)) : QString::null;
        if (!ssid.isEmpty())
        {
            return ssid;
        }
    }

    NMActiveConnection *active = nm_device_get_active_connection(device);
    if (active)
    {
        NMRemoteConnection *connection = nm_active_connection_get_connection(active);
        QString ssid = connection ? connectionSsid(NM_CONNECTION(connection))
                                  : QString::null;
        if (!ssid.isEmpty())
        {
            return ssid;
        }
        return QString::fromUtf8(nm_active_connection_get_id(active));
    }

    return QString::fromUtf8(nm_device_get_iface(device));
}

QString NetworkBackendPrivate::reasonText(guint reason)
{
    switch (reason)
    {
        case NM_DEVICE_STATE_REASON_NO_SECRETS:
            return i18n("No password was given.");
        case NM_DEVICE_STATE_REASON_SUPPLICANT_DISCONNECT:
            return i18n("The network refused the connection. Is the password right?");
        case NM_DEVICE_STATE_REASON_SUPPLICANT_TIMEOUT:
            return i18n("The network did not answer in time. Is the password right?");
        case NM_DEVICE_STATE_REASON_SUPPLICANT_FAILED:
        case NM_DEVICE_STATE_REASON_SUPPLICANT_CONFIG_FAILED:
            return i18n("The authentication failed.");
        case NM_DEVICE_STATE_REASON_IP_CONFIG_UNAVAILABLE:
        case NM_DEVICE_STATE_REASON_IP_CONFIG_EXPIRED:
        case NM_DEVICE_STATE_REASON_DHCP_FAILED:
            return i18n("No network address was obtained.");
        case NM_DEVICE_STATE_REASON_SSID_NOT_FOUND:
            return i18n("The network was not found.");
        case NM_DEVICE_STATE_REASON_CARRIER:
            return i18n("The cable was unplugged.");
        case NM_DEVICE_STATE_REASON_CONFIG_FAILED:
            return i18n("The device could not be configured.");
        case NM_DEVICE_STATE_REASON_DEPENDENCY_FAILED:
            return i18n("A connection this one depends on failed.");
        default:
            return i18n("The connection failed (reason %1).").arg(reason);
    }
}

NMDeviceWifi *NetworkBackendPrivate::wifiDevice() const
{
    // The first managed Wi-Fi device, the connected one if any.
    NMDeviceWifi *found = 0;
    QValueList<Device>::ConstIterator it;
    for (it = devices.begin(); it != devices.end(); ++it)
    {
        NMDevice *device = (*it).device;
        if (!NM_IS_DEVICE_WIFI(device) ||
            nm_device_get_state(device) <= NM_DEVICE_STATE_UNMANAGED)
        {
            continue;
        }
        if (nm_device_get_state(device) == NM_DEVICE_STATE_ACTIVATED)
        {
            return NM_DEVICE_WIFI(device);
        }
        if (!found)
        {
            found = NM_DEVICE_WIFI(device);
        }
    }
    return found;
}

NMAccessPoint *NetworkBackendPrivate::findAp(NMDeviceWifi *wifi,
                                             const QString &ssid) const
{
    NMAccessPoint *best = 0;
    const GPtrArray *aps = nm_device_wifi_get_access_points(wifi);
    for (guint i = 0; aps && i < aps->len; ++i)
    {
        NMAccessPoint *ap = NM_ACCESS_POINT(g_ptr_array_index(aps, i));
        if (ssidString(nm_access_point_get_ssid(ap)) != ssid)
        {
            continue;
        }
        if (!best || nm_access_point_get_strength(ap) > nm_access_point_get_strength(best))
        {
            best = ap;
        }
    }
    return best;
}

// The connection profile for @p ssid usable on @p device, the one used
// last when there are several.
NMRemoteConnection *NetworkBackendPrivate::knownConnection(NMDevice *device,
                                                           const QString &ssid) const
{
    NMRemoteConnection *best = 0;
    guint64 bestTime = 0;

    const GPtrArray *connections = nm_client_get_connections(client);
    for (guint i = 0; connections && i < connections->len; ++i)
    {
        NMRemoteConnection *remote =
            NM_REMOTE_CONNECTION(g_ptr_array_index(connections, i));
        NMConnection *connection = NM_CONNECTION(remote);
        if (!nm_remote_connection_get_visible(remote) ||
            connectionSsid(connection) != ssid ||
            (device && !nm_device_connection_valid(device, connection)))
        {
            continue;
        }

        NMSettingConnection *s = nm_connection_get_setting_connection(connection);
        guint64 time = s ? nm_setting_connection_get_timestamp(s) : 0;
        if (!best || time > bestTime)
        {
            best = remote;
            bestTime = time;
        }
    }
    return best;
}

NetworkBackend::Status NetworkBackendPrivate::computeStatus() const
{
    NetworkBackend::Status s;
    if (!client)
    {
        s.kind = NetworkBackend::Unavailable;
        return s;
    }
    if (!nm_client_get_nm_running(client))
    {
        s.kind = NetworkBackend::NotRunning;
        return s;
    }

    s.wifiEnabled = nm_client_wireless_get_enabled(client);
    s.wifiHardwareEnabled = nm_client_wireless_hardware_get_enabled(client);
    s.wwanEnabled = nm_client_wwan_get_enabled(client);

    NMDevice *connecting = 0;
    QValueList<Device>::ConstIterator it;
    for (it = devices.begin(); it != devices.end(); ++it)
    {
        NMDevice *device = (*it).device;
        NMDeviceState state = nm_device_get_state(device);
        if (NM_IS_DEVICE_MODEM(device))
        {
            s.haveWwan = true;
            continue;
        }
        if (NM_IS_DEVICE_WIFI(device) && state > NM_DEVICE_STATE_UNMANAGED)
        {
            s.haveWifi = true;
            if (state == NM_DEVICE_STATE_ACTIVATED)
            {
                s.wifiActive = true;
            }
        }
        if (isActivating(state) && (!connecting || NM_IS_DEVICE_WIFI(device)))
        {
            connecting = device;
        }
    }

    if (!nm_client_networking_get_enabled(client))
    {
        s.kind = NetworkBackend::NetworkingOff;
        return s;
    }

    // The connection with the default route, or else any connected
    // Wi-Fi or Ethernet device (a network without a gateway).
    NMActiveConnection *active = nm_client_get_primary_connection(client);
    if (active && nm_active_connection_get_state(active) !=
                      NM_ACTIVE_CONNECTION_STATE_ACTIVATED)
    {
        active = 0;
    }
    NMDevice *device = 0;
    if (active)
    {
        const GPtrArray *activeDevices = nm_active_connection_get_devices(active);
        if (activeDevices && activeDevices->len)
        {
            device = NM_DEVICE(g_ptr_array_index(activeDevices, 0));
        }
    }
    else
    {
        for (it = devices.begin(); it != devices.end(); ++it)
        {
            if (!NM_IS_DEVICE_MODEM((*it).device) &&
                nm_device_get_state((*it).device) == NM_DEVICE_STATE_ACTIVATED)
            {
                device = (*it).device;
                active = nm_device_get_active_connection(device);
                break;
            }
        }
    }

    if (!active || !device)
    {
        if (connecting)
        {
            s.kind = NetworkBackend::Connecting;
            s.wifiConnecting = NM_IS_DEVICE_WIFI(connecting);
            s.name = deviceName(connecting);
            s.device = QString::fromUtf8(nm_device_get_iface(connecting));
        }
        else
        {
            s.kind = NetworkBackend::Disconnected;
        }
        return s;
    }

    s.device = QString::fromUtf8(nm_device_get_iface(device));
    if (NM_IS_DEVICE_WIFI(device) && !nm_active_connection_get_vpn(active))
    {
        s.kind = NetworkBackend::Wireless;
        s.name = deviceName(device);
        NMAccessPoint *ap =
            nm_device_wifi_get_active_access_point(NM_DEVICE_WIFI(device));
        s.strength = ap ? nm_access_point_get_strength(ap) : 0;
        s.speed = nm_device_wifi_get_bitrate(NM_DEVICE_WIFI(device)) / 1000;
    }
    else if (NM_IS_DEVICE_ETHERNET(device) && !nm_active_connection_get_vpn(active))
    {
        s.kind = NetworkBackend::Wired;
        s.name = QString::fromUtf8(nm_active_connection_get_id(active));
        s.speed = nm_device_ethernet_get_speed(NM_DEVICE_ETHERNET(device));
    }
    else
    {
        s.kind = NetworkBackend::Other;
        s.name = QString::fromUtf8(nm_active_connection_get_id(active));
    }

    NMIPConfig *ip = nm_active_connection_get_ip4_config(active);
    const GPtrArray *addresses = ip ? nm_ip_config_get_addresses(ip) : 0;
    if (!addresses || !addresses->len)
    {
        ip = nm_active_connection_get_ip6_config(active);
        addresses = ip ? nm_ip_config_get_addresses(ip) : 0;
    }
    if (addresses && addresses->len)
    {
        NMIPAddress *address = static_cast<NMIPAddress *>(g_ptr_array_index(addresses, 0));
        s.address = QString("%1/%2")
                        .arg(QString::fromLatin1(nm_ip_address_get_address(address)))
                        .arg(nm_ip_address_get_prefix(address));
    }

    return s;
}

NetworkBackend::NetworkList NetworkBackendPrivate::computeNetworks() const
{
    NetworkBackend::NetworkList result;
    if (!client || !nm_client_get_nm_running(client))
    {
        return result;
    }

    NMDeviceWifi *wifi = wifiDevice();
    if (!wifi || !nm_client_wireless_get_enabled(client))
    {
        return result;
    }

    NMDevice *device = NM_DEVICE(wifi);
    NMDeviceState state = nm_device_get_state(device);
    QString activeSsid;
    QString activatingSsid;
    if (state == NM_DEVICE_STATE_ACTIVATED)
    {
        activeSsid = deviceName(device);
    }
    else if (isActivating(state))
    {
        activatingSsid = deviceName(device);
    }

    NMAccessPoint *activeAp = state == NM_DEVICE_STATE_ACTIVATED
                                  ? nm_device_wifi_get_active_access_point(wifi) : 0;

    // the SSIDs with a profile
    QStringList known;
    const GPtrArray *connections = nm_client_get_connections(client);
    for (guint i = 0; connections && i < connections->len; ++i)
    {
        QString ssid = connectionSsid(NM_CONNECTION(g_ptr_array_index(connections, i)));
        if (!ssid.isEmpty())
        {
            known << ssid;
        }
    }

    // the access points, merged by SSID
    QMap<QString, NetworkBackend::Network> bySsid;
    QMap<QString, int> securityStrength;
    const GPtrArray *aps = nm_device_wifi_get_access_points(wifi);
    for (guint i = 0; aps && i < aps->len; ++i)
    {
        NMAccessPoint *ap = NM_ACCESS_POINT(g_ptr_array_index(aps, i));
        QString ssid = ssidString(nm_access_point_get_ssid(ap));
        if (ssid.isEmpty())
        {
            continue;   // hidden
        }

        int strength = nm_access_point_get_strength(ap);
        NetworkBackend::Network &n = bySsid[ssid];
        if (n.ssid.isEmpty())
        {
            n.ssid = ssid;
            n.known = known.contains(ssid);
            n.active = ssid == activeSsid;
            n.activating = ssid == activatingSsid;
        }
        // the access point in use, or the strongest
        if (ap == activeAp)
        {
            n.strength = strength;
            securityStrength[ssid] = 101;
            n.security = apSecurity(ap, &n.securityText);
            continue;
        }
        if (strength > n.strength && !(n.active && activeAp))
        {
            n.strength = strength;
        }
        // the security of the strongest access point
        if (!securityStrength.contains(ssid) || strength > securityStrength[ssid])
        {
            securityStrength[ssid] = strength;
            n.security = apSecurity(ap, &n.securityText);
        }
    }

    QMap<QString, NetworkBackend::Network>::ConstIterator it;
    for (it = bySsid.begin(); it != bySsid.end(); ++it)
    {
        // insertion sort: there are a few dozen at most
        const NetworkBackend::Network &n = it.data();
        int rank = (n.active || n.activating) ? 0 : n.known ? 1 : 2;
        NetworkBackend::NetworkList::Iterator pos = result.begin();
        for (; pos != result.end(); ++pos)
        {
            const NetworkBackend::Network &o = *pos;
            int otherRank = (o.active || o.activating) ? 0 : o.known ? 1 : 2;
            if (rank < otherRank ||
                (rank == otherRank && n.strength > o.strength))
            {
                break;
            }
        }
        result.insert(pos, n);
    }

    return result;
}

NMConnection *NetworkBackendPrivate::newWifiConnection(
    NetworkBackend::Security security, const QString &password)
{
    NMConnection *connection = nm_simple_connection_new();

    NMSettingConnection *s = NM_SETTING_CONNECTION(nm_setting_connection_new());
    // Without the permission to change the connections of the system,
    // one of the user's own.  The password is in the profile either way
    // (NetworkManager keeps it readable by root only).
    if (nm_client_get_permission_result(client,
                                        NM_CLIENT_PERMISSION_SETTINGS_MODIFY_SYSTEM) !=
        NM_CLIENT_PERMISSION_RESULT_YES)
    {
        nm_setting_connection_add_permission(s, "user", g_get_user_name(), 0);
    }
    nm_connection_add_setting(connection, NM_SETTING(s));

    const char *keyMgmt = 0;
    switch (security)
    {
        case NetworkBackend::WPAPersonal:
            keyMgmt = "wpa-psk";
            break;
        case NetworkBackend::WPA3Personal:
            keyMgmt = "sae";
            break;
        case NetworkBackend::EnhancedOpen:
            keyMgmt = "owe";
            break;
        default:
            break;
    }
    if (keyMgmt)
    {
        NMSettingWirelessSecurity *sec =
            NM_SETTING_WIRELESS_SECURITY(nm_setting_wireless_security_new());
        g_object_set(sec, NM_SETTING_WIRELESS_SECURITY_KEY_MGMT, keyMgmt, (char *)0);
        if (!password.isEmpty())
        {
            g_object_set(sec, NM_SETTING_WIRELESS_SECURITY_PSK,
                         password.utf8().data(), (char *)0);
        }
        nm_connection_add_setting(connection, NM_SETTING(sec));
    }

    return connection;
}

void NetworkBackendPrivate::activate(NMRemoteConnection *connection, NMDevice *device,
                                     NMAccessPoint *ap, const QString &name)
{
    ActivateCall *call = new ActivateCall;
    call->d = this;
    call->device = NM_DEVICE(g_object_ref(device));
    call->name = name;
    call->isNew = false;
    nm_client_activate_connection_async(client, NM_CONNECTION(connection), device,
                                        ap ? nm_object_get_path(NM_OBJECT(ap)) : 0,
                                        cancellable, activateDone, call);
}

void NetworkBackendPrivate::addAndActivate(NMConnection *partial, NMDevice *device,
                                           NMAccessPoint *ap, const QString &name)
{
    ActivateCall *call = new ActivateCall;
    call->d = this;
    call->device = NM_DEVICE(g_object_ref(device));
    call->name = name;
    call->isNew = true;
    nm_client_add_and_activate_connection_async(client, partial, device,
                                                ap ? nm_object_get_path(NM_OBJECT(ap)) : 0,
                                                cancellable, addAndActivateDone, call);
}

static void finishActivateCall(NetworkBackendPrivate::ActivateCall *call,
                               NMActiveConnection *active, GError *error)
{
    NetworkBackendPrivate *d = call->d;
    if (!active)
    {
        if (!g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        {
            d->reportError(error, i18n("Could not connect to %1.").arg(call->name));
        }
        g_error_free(error);
    }
    else
    {
        NetworkBackendPrivate::Activation activation;
        activation.device = NM_DEVICE(g_object_ref(call->device));
        activation.name = call->name;
        activation.isNew = call->isNew;
        NMRemoteConnection *connection = nm_active_connection_get_connection(active);
        if (connection && call->isNew)
        {
            activation.connection =
                NM_REMOTE_CONNECTION(g_object_ref(connection));
        }
        d->activations.append(activation);

        // Over before the answer came?
        NMActiveConnectionState state = nm_active_connection_get_state(active);
        if (state == NM_ACTIVE_CONNECTION_STATE_ACTIVATED)
        {
            d->finishActivations(call->device, true);
        }
        else if (state >= NM_ACTIVE_CONNECTION_STATE_DEACTIVATING)
        {
            d->finishActivations(call->device, false);
        }
        g_object_unref(active);
        d->schedule();
    }

    g_object_unref(call->device);
    delete call;
}

void NetworkBackendPrivate::activateDone(GObject *source, GAsyncResult *result,
                                         gpointer data)
{
    GError *error = 0;
    NMActiveConnection *active =
        nm_client_activate_connection_finish(NM_CLIENT(source), result, &error);
    finishActivateCall(static_cast<ActivateCall *>(data), active, error);
}

void NetworkBackendPrivate::addAndActivateDone(GObject *source, GAsyncResult *result,
                                               gpointer data)
{
    GError *error = 0;
    NMActiveConnection *active =
        nm_client_add_and_activate_connection_finish(NM_CLIENT(source), result, &error);
    finishActivateCall(static_cast<ActivateCall *>(data), active, error);
}

void NetworkBackendPrivate::reportError(GError *error, const QString &what)
{
    QString message = dbusError(error);
    kdWarning() << "network applet: " << what << " " << message << endl;
    emit q->error(what + '\n' + message);
}

void NetworkBackendPrivate::callDone(GObject *source, GAsyncResult *result,
                                     gpointer data)
{
    GError *error = 0;
    GVariant *reply = nm_client_dbus_call_finish(NM_CLIENT(source), result, &error);
    if (reply)
    {
        g_variant_unref(reply);
        return;
    }
    if (!g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
    {
        static_cast<NetworkBackendPrivate *>(data)->reportError(
            error, i18n("Could not change the state of the network."));
    }
    g_error_free(error);
}

void NetworkBackendPrivate::setPropertyDone(GObject *source, GAsyncResult *result,
                                            gpointer data)
{
    GError *error = 0;
    if (nm_client_dbus_set_property_finish(NM_CLIENT(source), result, &error))
    {
        return;
    }
    if (!g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
    {
        static_cast<NetworkBackendPrivate *>(data)->reportError(
            error, i18n("Could not switch the wireless device."));
    }
    g_error_free(error);
}

void NetworkBackendPrivate::disconnectDone(GObject *source, GAsyncResult *result,
                                           gpointer data)
{
    GError *error = 0;
    if (nm_device_disconnect_finish(NM_DEVICE(source), result, &error))
    {
        return;
    }
    if (!g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
    {
        static_cast<NetworkBackendPrivate *>(data)->reportError(
            error, i18n("Could not disconnect."));
    }
    g_error_free(error);
}

void NetworkBackendPrivate::scanDone(GObject *source, GAsyncResult *result,
                                     gpointer data)
{
    GError *error = 0;
    if (nm_device_wifi_request_scan_finish(NM_DEVICE_WIFI(source), result, &error))
    {
        return;
    }
    if (!g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
    {
        // Asked too often, or the device is not ready: the last results
        // stay.
        NetworkBackendPrivate *d = static_cast<NetworkBackendPrivate *>(data);
        kdDebug() << "network applet: scan: " << dbusError(error) << endl;
        // Wi-Fi just switched on: the device is not ready yet, and
        // NetworkManager scans by itself once it is.  Still looking, until
        // that scan ends or the timeout.
        if (d->client && nm_client_wireless_get_enabled(d->client) &&
            nm_device_get_state(NM_DEVICE(source)) < NM_DEVICE_STATE_DISCONNECTED)
        {
            g_error_free(error);
            return;
        }
        d->scanning = false;
        d->schedule();
    }
    g_error_free(error);
}

void NetworkBackendPrivate::updateDone(GObject *source, GAsyncResult *result,
                                       gpointer)
{
    GError *error = 0;
    GVariant *reply = nm_remote_connection_update2_finish(NM_REMOTE_CONNECTION(source),
                                                          result, &error);
    if (reply)
    {
        g_variant_unref(reply);
        return;
    }
    // e.g. a profile of the system without the permission: the password
    // is asked for again next time
    if (!g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
    {
        kdWarning() << "network applet: could not keep the password: "
                    << dbusError(error) << endl;
    }
    g_error_free(error);
}

void NetworkBackendPrivate::deleteDone(GObject *source, GAsyncResult *result,
                                       gpointer)
{
    GError *error = 0;
    if (!nm_remote_connection_delete_finish(NM_REMOTE_CONNECTION(source), result,
                                            &error))
    {
        if (!g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        {
            kdWarning() << "network applet: could not delete a connection: "
                        << dbusError(error) << endl;
        }
        g_error_free(error);
    }
}

/******************************************************************************
 * The secret agent's requests
 */

void NetworkBackendPrivate::getSecrets(NMSecretAgentOld *agent,
                                       NMConnection *connection,
                                       const char *path, const char *settingName,
                                       const char **,
                                       NMSecretAgentGetSecretsFlags flags,
                                       NMSecretAgentOldGetSecretsFunc callback,
                                       gpointer userData)
{
    NetworkBackendPrivate *d = agentBackend(agent);

    // Only the password of WPA/WPA2/WPA3-Personal, and only when the
    // user can be asked: the rest is for the settings editor.
    NMSettingWirelessSecurity *sec =
        nm_connection_get_setting_wireless_security(connection);
    const char *keyMgmt = sec ? nm_setting_wireless_security_get_key_mgmt(sec) : 0;
    bool personal = keyMgmt && (strcmp(keyMgmt, "wpa-psk") == 0 ||
                                strcmp(keyMgmt, "sae") == 0);
    QString ssid = connectionSsid(connection);
    if (!d || !personal || ssid.isEmpty() ||
        strcmp(settingName, NM_SETTING_WIRELESS_SECURITY_SETTING_NAME) != 0 ||
        !(flags & NM_SECRET_AGENT_GET_SECRETS_FLAG_ALLOW_INTERACTION))
    {
        GError *error = g_error_new(NM_SECRET_AGENT_ERROR,
                                    NM_SECRET_AGENT_ERROR_NO_SECRETS,
                                    "No secrets for %s of %s", settingName,
                                    nm_connection_get_id(connection));
        callback(agent, connection, 0, error, userData);
        g_error_free(error);
        return;
    }

    // A password NetworkManager keeps in a profile of the system (one for
    // all users) is changed only with the permission to change those:
    // without it NetworkManager throws away the one typed in.  Say so
    // instead of asking for nothing.
    NMSettingSecretFlags pskFlags = NM_SETTING_SECRET_FLAG_NONE;
    nm_setting_get_secret_flags(NM_SETTING(sec), NM_SETTING_WIRELESS_SECURITY_PSK,
                                &pskFlags, 0);
    NMSettingConnection *sc = nm_connection_get_setting_connection(connection);
    bool systemProfile = !sc || nm_setting_connection_get_num_permissions(sc) == 0;
    bool systemOwned = (pskFlags & (NM_SETTING_SECRET_FLAG_AGENT_OWNED |
                                    NM_SETTING_SECRET_FLAG_NOT_SAVED)) == 0;
    if (systemProfile && systemOwned && d->client)
    {
        NMClientPermissionResult allowed = nm_client_get_permission_result(
            d->client, NM_CLIENT_PERMISSION_SETTINGS_MODIFY_SYSTEM);
        if (allowed == NM_CLIENT_PERMISSION_RESULT_NO ||
            allowed == NM_CLIENT_PERMISSION_RESULT_AUTH)
        {
            kdDebug() << "network applet: password of the system connection "
                      << ssid << " requested, not allowed to change it" << endl;
            GError *error = g_error_new(NM_SECRET_AGENT_ERROR,
                                        NM_SECRET_AGENT_ERROR_NO_SECRETS,
                                        "Not allowed to change %s",
                                        nm_connection_get_id(connection));
            callback(agent, connection, 0, error, userData);
            g_error_free(error);
            emit d->q->error(i18n("The Wi-Fi network %1 needs a new password. Its "
                                  "connection is shared by all users of this "
                                  "computer: only an administrator can change "
                                  "the password, in the network settings.")
                                 .arg(ssid));
            return;
        }
    }

    SecretRequest request;
    request.id = d->nextRequest++;
    request.connection = NM_CONNECTION(g_object_ref(connection));
    request.path = path;
    request.setting = settingName;
    request.ssid = ssid;
    request.agentOwned = pskFlags & NM_SETTING_SECRET_FLAG_AGENT_OWNED;
    request.callback = callback;
    request.userData = userData;
    d->requests.append(request);

    bool retry = flags & NM_SECRET_AGENT_GET_SECRETS_FLAG_REQUEST_NEW;
    kdDebug() << "network applet: password requested for " << ssid
              << (retry ? " (again)" : "") << endl;
    emit d->q->secretRequested(request.id, ssid, retry);
}

void NetworkBackendPrivate::cancelGetSecrets(NMSecretAgentOld *agent,
                                             const char *path,
                                             const char *settingName)
{
    NetworkBackendPrivate *d = agentBackend(agent);
    if (!d)
    {
        return;
    }

    // Since libnm 1.24 the callback of a cancelled request is not called.
    QValueList<SecretRequest>::Iterator it = d->requests.begin();
    while (it != d->requests.end())
    {
        if ((*it).path == path && (*it).setting == settingName)
        {
            int id = (*it).id;
            g_object_unref((*it).connection);
            it = d->requests.remove(it);
            emit d->q->secretCancelled(id);
        }
        else
        {
            ++it;
        }
    }
}

// The passwords this agent gives are kept in the profiles by
// NetworkManager (answerSecret() moves the ones left to agents there):
// there is nothing to save or delete here.
void NetworkBackendPrivate::saveSecrets(NMSecretAgentOld *agent,
                                        NMConnection *connection, const char *,
                                        NMSecretAgentOldSaveSecretsFunc callback,
                                        gpointer userData)
{
    callback(agent, connection, 0, userData);
}

void NetworkBackendPrivate::deleteSecrets(NMSecretAgentOld *agent,
                                          NMConnection *connection, const char *,
                                          NMSecretAgentOldDeleteSecretsFunc callback,
                                          gpointer userData)
{
    callback(agent, connection, 0, userData);
}

/******************************************************************************
 * NetworkBackend
 */

NetworkBackend *NetworkBackend::s_self = 0;
int NetworkBackend::s_refs = 0;

NetworkBackend::Network::Network()
    : strength(0),
      security(Open),
      known(false),
      active(false),
      activating(false)
{
}

bool NetworkBackend::Network::operator==(const Network &o) const
{
    return ssid == o.ssid && strength == o.strength && security == o.security &&
           securityText == o.securityText && known == o.known &&
           active == o.active && activating == o.activating;
}

NetworkBackend::Status::Status()
    : kind(Unavailable),
      wifiConnecting(false),
      haveWifi(false),
      wifiEnabled(false),
      wifiHardwareEnabled(false),
      haveWwan(false),
      wwanEnabled(false),
      wifiActive(false),
      strength(0),
      speed(0)
{
}

bool NetworkBackend::Status::operator==(const Status &o) const
{
    return kind == o.kind && wifiConnecting == o.wifiConnecting &&
           haveWifi == o.haveWifi && wifiEnabled == o.wifiEnabled &&
           wifiHardwareEnabled == o.wifiHardwareEnabled &&
           haveWwan == o.haveWwan && wwanEnabled == o.wwanEnabled &&
           wifiActive == o.wifiActive && name == o.name && device == o.device &&
           address == o.address && strength == o.strength && speed == o.speed;
}

NetworkBackend *NetworkBackend::ref()
{
    if (!s_self)
    {
        s_self = new NetworkBackend;
    }
    ++s_refs;
    return s_self;
}

void NetworkBackend::deref()
{
    if (--s_refs == 0)
    {
        delete s_self;
        s_self = 0;
    }
}

NetworkBackend::NetworkBackend()
    : QObject(0, "networkbackend")
{
    d = new NetworkBackendPrivate(this);
    d->start();
    d->pollTimer->start(PollInterval);
}

NetworkBackend::~NetworkBackend()
{
    delete d;
}

void NetworkBackend::retry()
{
    d->stop();
    d->start();
}

void NetworkBackend::update()
{
    d->syncDevices();

    NMDeviceWifi *wifi = d->client ? d->wifiDevice() : 0;
    bool wasScanning = d->scanning;
    if (d->scanning)
    {
        if (!wifi || nm_device_wifi_get_last_scan(wifi) != d->lastScan ||
            d->scanStarted.elapsed() > ScanTimeout)
        {
            d->scanning = false;
        }
    }

    Status status = d->computeStatus();
    NetworkList networks = d->computeNetworks();
    if (status != d->status || networks != d->networks || d->scanning != wasScanning)
    {
        d->status = status;
        d->networks = networks;
        emit changed();
    }
}

NetworkBackend::Status NetworkBackend::status() const
{
    return d->status;
}

NetworkBackend::NetworkList NetworkBackend::networks() const
{
    return d->networks;
}

bool NetworkBackend::isScanning() const
{
    return d->scanning;
}

bool NetworkBackend::canModifySystem() const
{
    return d->client &&
           nm_client_get_permission_result(d->client,
                                           NM_CLIENT_PERMISSION_SETTINGS_MODIFY_SYSTEM) ==
               NM_CLIENT_PERMISSION_RESULT_YES;
}

bool NetworkBackend::networkingEnabled() const
{
    return d->client && nm_client_get_nm_running(d->client) &&
           nm_client_networking_get_enabled(d->client);
}

NetworkBackend::ConnectResult NetworkBackend::connectTo(const QString &ssid)
{
    NMDeviceWifi *wifi = d->client ? d->wifiDevice() : 0;
    NMAccessPoint *ap = wifi ? d->findAp(wifi, ssid) : 0;
    if (!ap)
    {
        return Failed;
    }

    NMDevice *device = NM_DEVICE(wifi);
    NMRemoteConnection *known = d->knownConnection(device, ssid);
    if (known)
    {
        d->activate(known, device, ap, ssid);
        return Started;
    }

    Security security = apSecurity(ap, 0);
    switch (security)
    {
        case Open:
        case EnhancedOpen:
        {
            NMConnection *partial = d->newWifiConnection(security, QString::null);
            d->addAndActivate(partial, device, ap, ssid);
            g_object_unref(partial);
            return Started;
        }
        case WPAPersonal:
        case WPA3Personal:
            return NeedPassword;
        default:
            return Unsupported;
    }
}

void NetworkBackend::connectWithPassword(const QString &ssid, const QString &password)
{
    NMDeviceWifi *wifi = d->client ? d->wifiDevice() : 0;
    NMAccessPoint *ap = wifi ? d->findAp(wifi, ssid) : 0;
    if (!ap)
    {
        emit connectionFailed(ssid, i18n("The network was not found."));
        return;
    }

    // made meanwhile (asked for twice)
    NMRemoteConnection *known = d->knownConnection(NM_DEVICE(wifi), ssid);
    if (known)
    {
        d->activate(known, NM_DEVICE(wifi), ap, ssid);
        return;
    }

    Security security = apSecurity(ap, 0);
    NMConnection *partial = d->newWifiConnection(security, password);
    d->addAndActivate(partial, NM_DEVICE(wifi), ap, ssid);
    g_object_unref(partial);
}

void NetworkBackend::connectHidden(const QString &ssid, Security security,
                                   const QString &password)
{
    NMDeviceWifi *wifi = d->client ? d->wifiDevice() : 0;
    if (!wifi || ssid.isEmpty())
    {
        emit connectionFailed(ssid, i18n("There is no wireless device."));
        return;
    }

    NMDevice *device = NM_DEVICE(wifi);
    NMRemoteConnection *known = d->knownConnection(device, ssid);
    if (known)
    {
        d->activate(known, device, 0, ssid);
        return;
    }

    NMConnection *partial = d->newWifiConnection(security, password);
    NMSettingWireless *wireless = NM_SETTING_WIRELESS(nm_setting_wireless_new());
    QCString utf8 = ssid.utf8();
    GBytes *bytes = g_bytes_new(utf8.data(), utf8.length());
    g_object_set(wireless,
                 NM_SETTING_WIRELESS_SSID, bytes,
                 NM_SETTING_WIRELESS_MODE, NM_SETTING_WIRELESS_MODE_INFRA,
                 NM_SETTING_WIRELESS_HIDDEN, TRUE,
                 (char *)0);
    g_bytes_unref(bytes);
    nm_connection_add_setting(partial, NM_SETTING(wireless));

    d->addAndActivate(partial, device, 0, ssid);
    g_object_unref(partial);
}

void NetworkBackend::disconnectWifi()
{
    NMDeviceWifi *wifi = d->client ? d->wifiDevice() : 0;
    if (wifi)
    {
        nm_device_disconnect_async(NM_DEVICE(wifi), d->cancellable,
                                   NetworkBackendPrivate::disconnectDone, d);
    }
}

void NetworkBackend::requestScan()
{
    NMDeviceWifi *wifi = d->client ? d->wifiDevice() : 0;
    if (!wifi || d->scanning)
    {
        return;
    }

    d->scanning = true;
    d->lastScan = nm_device_wifi_get_last_scan(wifi);
    d->scanStarted.start();
    nm_device_wifi_request_scan_async(wifi, d->cancellable,
                                      NetworkBackendPrivate::scanDone, d);
    emit changed();
}

void NetworkBackend::setNetworkingEnabled(bool on)
{
    if (!d->client)
    {
        return;
    }
    nm_client_dbus_call(d->client, NM_DBUS_PATH, NM_DBUS_INTERFACE, "Enable",
                        g_variant_new("(b)", (gboolean)on), G_VARIANT_TYPE("()"),
                        -1, d->cancellable, NetworkBackendPrivate::callDone, d);
}

void NetworkBackend::setWifiEnabled(bool on)
{
    if (!d->client)
    {
        return;
    }
    nm_client_dbus_set_property(d->client, NM_DBUS_PATH, NM_DBUS_INTERFACE,
                                "WirelessEnabled", g_variant_new_boolean(on), -1,
                                d->cancellable, NetworkBackendPrivate::setPropertyDone,
                                d);
}

void NetworkBackend::setWwanEnabled(bool on)
{
    if (!d->client)
    {
        return;
    }
    nm_client_dbus_set_property(d->client, NM_DBUS_PATH, NM_DBUS_INTERFACE,
                                "WwanEnabled", g_variant_new_boolean(on), -1,
                                d->cancellable, NetworkBackendPrivate::setPropertyDone,
                                d);
}

void NetworkBackend::answerSecret(int request, const QString &password)
{
    QValueList<NetworkBackendPrivate::SecretRequest>::Iterator it;
    for (it = d->requests.begin(); it != d->requests.end(); ++it)
    {
        if ((*it).id == request)
        {
            break;
        }
    }
    if (it == d->requests.end())
    {
        return;
    }
    NetworkBackendPrivate::SecretRequest r = *it;
    d->requests.remove(it);

    GVariantBuilder setting;
    g_variant_builder_init(&setting, NM_VARIANT_TYPE_SETTING);
    g_variant_builder_add(&setting, "{sv}", NM_SETTING_WIRELESS_SECURITY_PSK,
                          g_variant_new_string(password.utf8().data()));
    GVariantBuilder secrets;
    g_variant_builder_init(&secrets, NM_VARIANT_TYPE_CONNECTION);
    g_variant_builder_add(&secrets, "{sa{sv}}",
                          NM_SETTING_WIRELESS_SECURITY_SETTING_NAME, &setting);
    GVariant *result = g_variant_ref_sink(g_variant_builder_end(&secrets));

    r.callback(d->agent, r.connection, result, 0, r.userData);
    g_variant_unref(result);

    // A profile made elsewhere (GNOME's applet, plasma-nm) can leave the
    // password to the agent of the user, who keeps it in a keyring.  This
    // one has none: keep the password in the profile, as for the ones the
    // applet makes, or it is asked for at each connection and the
    // automatic ones fail.
    NMRemoteConnection *remote =
        r.agentOwned && d->client
            ? nm_client_get_connection_by_path(d->client, r.path.data()) : 0;
    if (remote)
    {
        NMConnection *copy = nm_simple_connection_new_clone(NM_CONNECTION(remote));
        NMSettingWirelessSecurity *sec =
            nm_connection_get_setting_wireless_security(copy);
        if (sec)
        {
            kdDebug() << "network applet: keeping the password of " << r.ssid
                      << " in its connection" << endl;
            g_object_set(sec,
                         NM_SETTING_WIRELESS_SECURITY_PSK, password.utf8().data(),
                         NM_SETTING_WIRELESS_SECURITY_PSK_FLAGS,
                         NM_SETTING_SECRET_FLAG_NONE,
                         (char *)0);
            nm_remote_connection_update2(
                remote, nm_connection_to_dbus(copy, NM_CONNECTION_SERIALIZE_ALL),
                NM_SETTINGS_UPDATE2_FLAG_NONE, 0, d->cancellable,
                NetworkBackendPrivate::updateDone, 0);
        }
        g_object_unref(copy);
    }
    g_object_unref(r.connection);
}

void NetworkBackend::cancelSecret(int request)
{
    QValueList<NetworkBackendPrivate::SecretRequest>::Iterator it;
    for (it = d->requests.begin(); it != d->requests.end(); ++it)
    {
        if ((*it).id == request)
        {
            NetworkBackendPrivate::SecretRequest r = *it;
            d->requests.remove(it);

            GError *error = g_error_new_literal(NM_SECRET_AGENT_ERROR,
                                                NM_SECRET_AGENT_ERROR_USER_CANCELED,
                                                "The user cancelled");
            r.callback(d->agent, r.connection, 0, error, r.userData);
            g_error_free(error);
            g_object_unref(r.connection);
            return;
        }
    }
}

QString NetworkBackend::securityName(Security security)
{
    switch (security)
    {
        case Open:
            return i18n("Open");
        case EnhancedOpen:
            return i18n("Enhanced Open");
        case WEP:
            return "WEP";
        case WPAPersonal:
            return i18n("WPA/WPA2 Personal");
        case WPA3Personal:
            return i18n("WPA3 Personal");
        case Enterprise:
            return i18n("WPA Enterprise");
    }
    return QString::null;
}

#include "networkbackend.moc"
