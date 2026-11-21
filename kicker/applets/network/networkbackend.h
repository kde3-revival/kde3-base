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

#ifndef NETWORKBACKEND_H
#define NETWORKBACKEND_H

#include <qobject.h>
#include <qstring.h>
#include <qvaluelist.h>

class NetworkBackendPrivate;

/**
 * NetworkManager, seen through libnm: the state of the connections, the
 * Wi-Fi networks in range, and the requests the applet makes.  The
 * libnm (GLib) code is in networkbackend.cpp only, which includes the
 * libnm headers before any Qt header (Qt defines "signals", which GLib
 * uses as an identifier); this interface has no GLib types.  The
 * callbacks are delivered by the GLib main loop, which Qt runs on
 * (QT_GLIB_EVENTLOOP).
 *
 * One backend serves all applets of a process: it is also the
 * process's secret agent, which NetworkManager asks for a Wi-Fi password
 * it does not have or that was not accepted.  Take it with ref() and
 * give it back with deref().
 *
 * Settings the applet creates keep the Wi-Fi password in the connection
 * profile, where NetworkManager stores it readable by root only, without
 * a keyring.  With the permission to change the connections of the system
 * without asking (polkit settings.modify.system "yes") they are for all
 * users and connect before anyone logs in; otherwise (also "auth", the
 * default of many distributions) they belong to the user, and connect only
 * while the user is logged in.
 */
class NetworkBackend : public QObject
{
    Q_OBJECT

public:
    enum Security
    {
        Open,
        EnhancedOpen,   // OWE: encrypted, without a password
        WEP,
        WPAPersonal,    // WPA/WPA2 with a pre-shared key
        WPA3Personal,   // SAE only
        Enterprise      // 802.1X
    };

    /** A Wi-Fi network in range: the access points of one SSID. */
    struct Network
    {
        Network();
        bool operator==(const Network &o) const;

        QString ssid;
        int strength;           // of the strongest access point, 0-100
        Security security;
        QString securityText;   // "WPA2", "WPA2/WPA3", "WEP", ... or empty
        bool known;             // there is a connection profile for it
        bool active;            // connected to it
        bool activating;        // connecting to it
    };
    typedef QValueList<Network> NetworkList;

    enum Kind
    {
        Unavailable,    // no connection to the system bus
        NotRunning,     // NetworkManager is not running
        NetworkingOff,  // networking disabled in NetworkManager
        Disconnected,
        Connecting,
        Wired,
        Wireless,
        Other           // a connection of another kind (VPN, bridge, ...)
    };

    struct Status
    {
        Status();
        bool operator==(const Status &o) const;
        bool operator!=(const Status &o) const { return !(*this == o); }

        Kind kind;
        bool wifiConnecting;        // with kind Connecting: a Wi-Fi device
        bool haveWifi;              // there is a Wi-Fi device
        bool wifiEnabled;           // Wi-Fi switched on in NetworkManager
        bool wifiHardwareEnabled;   // and not by the rfkill switch
        bool haveWwan;
        bool wwanEnabled;
        bool wifiActive;            // the Wi-Fi device is connected

        QString name;       // SSID or name of the connection
        QString device;     // interface
        QString address;    // IPv4 address with prefix, or IPv6
        int strength;       // Wi-Fi signal, 0-100
        int speed;          // Mbit/s, 0 when unknown
    };

    enum ConnectResult
    {
        Started,        // the connection is coming up
        NeedPassword,   // a new network: call connectWithPassword()
        Unsupported,    // needs the settings editor (802.1X, WEP)
        Failed          // the network is gone, or there is no Wi-Fi device
    };

    static NetworkBackend *ref();
    static void deref();

    Status status() const;
    /** The networks in range: the one in use first, then the known ones,
        then by signal strength. */
    NetworkList networks() const;
    /** A scan asked for by requestScan() is under way. */
    bool isScanning() const;

    /** The permission of the session to create connections for all
        users; without it they are private to this user. */
    bool canModifySystem() const;

    ConnectResult connectTo(const QString &ssid);
    void connectWithPassword(const QString &ssid, const QString &password);
    void connectHidden(const QString &ssid, Security security,
                       const QString &password);
    void disconnectWifi();
    void requestScan();

    void setNetworkingEnabled(bool on);
    void setWifiEnabled(bool on);
    void setWwanEnabled(bool on);
    bool networkingEnabled() const;

    /** The answer to secretRequested(). */
    void answerSecret(int request, const QString &password);
    void cancelSecret(int request);

    static QString securityName(Security security);

signals:
    void changed();

    /** NetworkManager asks for the password of @p ssid; @p retry when the
        one it had was not accepted.  Answer with answerSecret() or
        cancelSecret(). */
    void secretRequested(int request, const QString &ssid, bool retry);
    /** NetworkManager does not need the answer anymore. */
    void secretCancelled(int request);

    void connected(const QString &name, bool wifi);
    void disconnected(const QString &name, bool wifi);
    void connectionFailed(const QString &name, const QString &reason);
    /** A request failed (e.g. not authorized). */
    void error(const QString &message);

private slots:
    void update();
    void retry();

private:
    NetworkBackend();
    virtual ~NetworkBackend();

    friend class NetworkBackendPrivate;
    NetworkBackendPrivate *d;

    static NetworkBackend *s_self;
    static int s_refs;
};

#endif
