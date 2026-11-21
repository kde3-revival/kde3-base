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

#ifndef NETWORKAPPLET_H
#define NETWORKAPPLET_H

#include <qdatetime.h>
#include <qmap.h>
#include <qptrlist.h>
#include <qstring.h>

#include <kpanelapplet.h>

#include "networkbackend.h"

class QTimer;
class KPopupMenu;
class NetworkButton;
class NetworkPopup;
class PasswordDialog;

/**
 * A panel button for NetworkManager, made for Wi-Fi: the icon shows the
 * connection (the Wi-Fi signal, a cable, a connection being made, none,
 * airplane mode), the tooltip its details.  The left button opens the
 * list of the Wi-Fi networks in range, one click connecting to one (with
 * a password asked for a new protected network); the right button has
 * the switches and the settings editor (nm-connection-editor).
 *
 * The applet is also the secret agent of the session, asking for the
 * Wi-Fi passwords NetworkManager needs, and shows the connections and
 * disconnections next to the panel.
 */
class NetworkApplet : public KPanelApplet
{
    Q_OBJECT

public:
    NetworkApplet(const QString &configFile, Type t = Normal, int actions = 0,
                  QWidget *parent = 0, const char *name = 0);
    virtual ~NetworkApplet();

    virtual int widthForHeight(int height) const;
    virtual int heightForWidth(int width) const;

protected:
    virtual void resizeEvent(QResizeEvent *e);

private slots:
    void updateState();
    void animate();
    void togglePopup();
    void scan();
    void showMenu(const QPoint &pos);
    void menuAboutToShow();
    void menuActivated(int id);

    void connectTo(const QString &ssid);
    void connectHidden();
    void disconnectWifi();
    void setWifiEnabled(bool on);
    void setAirplaneMode(bool on);
    void runEditor();

    void secretRequested(int request, const QString &ssid, bool retry);
    void secretCancelled(int request);
    void passwordEntered();
    void passwordCancelled();
    void newPasswordEntered();
    void hiddenEntered();

    void connected(const QString &name, bool wifi);
    void disconnected(const QString &name, bool wifi);
    void connectionFailed(const QString &name, const QString &reason);
    void error(const QString &message);

private:
    bool isNotifier() const;
    bool airplaneMode() const;
    bool haveEditor() const;
    void setIcon(const QString &key, const QImage &image);
    void notify(const QString &caption, const QString &text, const QPixmap &icon);
    void readConfig();
    void writeConfig();

    NetworkBackend *m_backend;
    NetworkButton *m_button;
    NetworkPopup *m_popup;
    KPopupMenu *m_menu;
    QTimer *m_animation;
    QTimer *m_scanTimer;
    int m_frame;
    QString m_iconKey;
    QString m_tip;

    int m_wifiId;
    int m_airplaneId;
    int m_networkingId;
    int m_disconnectId;
    int m_hiddenId;
    int m_notifyId;
    int m_editorId;

    bool m_notifications;
    bool m_airplane;

    // the password dialogs of the agent's requests
    QMap<int, PasswordDialog *> m_requests;
    // the network the user did not give a password for, when
    QString m_cancelled;
    QTime m_cancelTime;

    static QPtrList<NetworkApplet> s_applets;
};

#endif
