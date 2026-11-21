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

#ifndef VOLUMEAPPLET_H
#define VOLUMEAPPLET_H

#include <qmap.h>
#include <qptrlist.h>
#include <qstring.h>

#include <kpanelapplet.h>

class QPopupMenu;
class KGlobalAccel;
class KPopupMenu;
class PulseVolume;
class VolumeButton;
class VolumeOSD;
class VolumePopup;

/**
 * A panel button for the volume of the default PulseAudio sink: the
 * left button opens a slider, the wheel changes the volume, the middle
 * button mutes, the right button has a menu for choosing the output
 * device and starting the mixer (pavucontrol).  The volume keys of the
 * keyboard work as the wheel and the middle button.  The changes made
 * with them, the wheel and the mute switches outside the popup are
 * shown on the screen.
 */
class VolumeApplet : public KPanelApplet
{
    Q_OBJECT

public:
    VolumeApplet(const QString &configFile, Type t = Normal, int actions = 0,
                 QWidget *parent = 0, const char *name = 0);
    virtual ~VolumeApplet();

    virtual int widthForHeight(int height) const;
    virtual int heightForWidth(int width) const;

protected:
    virtual void resizeEvent(QResizeEvent *e);

private slots:
    void updateState();
    void togglePopup();
    void toggleMute();
    void setVolume(int percent);
    void setMuted(bool muted);
    void changeVolume(int steps);
    void increaseVolume();
    void decreaseVolume();
    void showMenu(const QPoint &pos);
    void menuAboutToShow();
    void deviceMenuAboutToShow();
    void deviceSelected(int id);
    void runMixer();
    void slotSettingsChanged(int category);

private:
    void checkMixer();
    void showOSD();
    void takeKeys();

    PulseVolume *m_pulse;
    VolumeButton *m_button;
    VolumePopup *m_popup;
    KPopupMenu *m_menu;
    QPopupMenu *m_deviceMenu;
    int m_muteId;
    int m_deviceMenuId;
    int m_mixerId;
    QMap<int, QString> m_deviceNames;
    QString m_iconName;
    QString m_tip;
    bool m_haveMixer;
    KGlobalAccel *m_keys;

    // The applets of this process, which share one on-screen display,
    // and the one of them handling the volume keys.
    static QPtrList<VolumeApplet> s_applets;
    static VolumeOSD *s_osd;
    static VolumeApplet *s_keyOwner;
};

#endif
