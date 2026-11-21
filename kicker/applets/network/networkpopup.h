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

#ifndef NETWORKPOPUP_H
#define NETWORKPOPUP_H

#include <qdatetime.h>
#include <qframe.h>

#include "networkbackend.h"

class QCheckBox;
class QLabel;
class QListBox;
class QListBoxItem;
class QPushButton;

/**
 * The popup of the panel button: the Wi-Fi and airplane mode switches,
 * the Wi-Fi networks in range, and buttons for a hidden network, for
 * disconnecting and for the settings editor.  A click on a network
 * connects to it.  It closes when clicked outside.
 */
class NetworkPopup : public QFrame
{
    Q_OBJECT

public:
    NetworkPopup(QWidget *parent = 0, const char *name = 0);

    /** Shows the state; @p airplane: airplane mode is on. */
    void setState(const NetworkBackend::Status &status,
                  const NetworkBackend::NetworkList &networks,
                  bool scanning, bool airplane, bool haveEditor);

    /**
     * Whether the popup closed a moment ago, so that the click outside
     * that closed it, on the panel button, does not open it again.
     */
    bool justHidden() const;

signals:
    void networkClicked(const QString &ssid);
    void wifiToggled(bool on);
    void airplaneToggled(bool on);
    void hiddenRequested();
    void disconnectRequested();
    void settingsRequested();

protected:
    virtual void showEvent(QShowEvent *e);
    virtual void hideEvent(QHideEvent *e);
    virtual void keyPressEvent(QKeyEvent *e);

private slots:
    void itemClicked(QListBoxItem *item);
    void itemEntered(QListBoxItem *item);
    void wifiClicked(bool on);
    void airplaneClicked(bool on);

private:
    void setNetworks(const NetworkBackend::NetworkList &networks);

    QCheckBox *m_wifi;
    QCheckBox *m_airplane;
    QLabel *m_status;
    QListBox *m_list;
    QPushButton *m_hidden;
    QPushButton *m_disconnect;
    QPushButton *m_settings;

    NetworkBackend::NetworkList m_networks;
    bool m_updating;
    QTime m_hideTime;
};

#endif
