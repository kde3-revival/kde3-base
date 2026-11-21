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

#include <qpopupmenu.h>
#include <qtooltip.h>

#include <kapplication.h>
#include <kglobal.h>
#include <kglobalaccel.h>
#include <kiconloader.h>
#include <kipc.h>
#include <klocale.h>
#include <kpopupmenu.h>
#include <krun.h>
#include <kstandarddirs.h>

#include "global.h"
#include "pulsevolume.h"
#include "volumebutton.h"
#include "volumeosd.h"
#include "volumepopup.h"
#include "volumeapplet.h"

// The volume step of the mouse wheel and the volume keys, in percent.
static const int WheelStep = 5;

// The mixer: PulseAudio Volume Control.
static const char MixerCommand[] = "pavucontrol";

QPtrList<VolumeApplet> VolumeApplet::s_applets;
VolumeOSD *VolumeApplet::s_osd = 0;
VolumeApplet *VolumeApplet::s_keyOwner = 0;

extern "C"
{
    KDE_EXPORT KPanelApplet *init(QWidget *parent, const QString &configFile)
    {
        return new VolumeApplet(configFile, KPanelApplet::Normal, 0,
                                parent, "volumeapplet");
    }
}

VolumeApplet::VolumeApplet(const QString &configFile, Type type, int actions,
                           QWidget *parent, const char *name)
    : KPanelApplet(configFile, type, actions, parent, name),
      m_haveMixer(false),
      m_keys(0)
{
    setBackgroundOrigin(AncestorOrigin);

    m_button = new VolumeButton(this);
    connect(m_button, SIGNAL(leftPressed()), SLOT(togglePopup()));
    connect(m_button, SIGNAL(middleClicked()), SLOT(toggleMute()));
    connect(m_button, SIGNAL(rightPressed(const QPoint &)),
            SLOT(showMenu(const QPoint &)));
    connect(m_button, SIGNAL(wheelSteps(int)), SLOT(changeVolume(int)));

    m_popup = new VolumePopup(this);
    connect(m_popup, SIGNAL(volumeChanged(int)), SLOT(setVolume(int)));
    connect(m_popup, SIGNAL(muteToggled(bool)), SLOT(setMuted(bool)));
    connect(m_popup, SIGNAL(mixerRequested()), SLOT(runMixer()));

    m_menu = new KPopupMenu(this);
    m_menu->setCheckable(true);
    m_menu->insertTitle(SmallIcon("kmix"), i18n("Volume Control"));
    m_muteId = m_menu->insertItem(i18n("&Mute"), this, SLOT(toggleMute()));
    m_deviceMenu = new QPopupMenu(m_menu);
    m_deviceMenu->setCheckable(true);
    connect(m_deviceMenu, SIGNAL(aboutToShow()),
            SLOT(deviceMenuAboutToShow()));
    connect(m_deviceMenu, SIGNAL(activated(int)), SLOT(deviceSelected(int)));
    m_deviceMenuId = m_menu->insertItem(i18n("Output &Device"), m_deviceMenu);
    m_menu->insertSeparator();
    m_mixerId = m_menu->insertItem(SmallIcon("kmix"),
                                   i18n("&Open Volume Control"),
                                   this, SLOT(runMixer()));
    connect(m_menu, SIGNAL(aboutToShow()), SLOT(menuAboutToShow()));
    // also in the menu of the applet's handle
    setCustomMenu(m_menu);

    checkMixer();

    m_pulse = new PulseVolume(this);
    connect(m_pulse, SIGNAL(changed()), SLOT(updateState()));
    updateState();

    s_applets.append(this);
    if (!s_keyOwner)
    {
        takeKeys();
    }
}

VolumeApplet::~VolumeApplet()
{
    s_applets.removeRef(this);

    if (s_keyOwner == this)
    {
        // Ungrab them before another applet grabs them again.
        m_keys->setEnabled(false);
        delete m_keys;
        m_keys = 0;
        s_keyOwner = 0;

        if (!s_applets.isEmpty())
        {
            s_applets.first()->takeKeys();
        }
    }

    // With the last applet: the library may be unloaded after it.
    if (s_applets.isEmpty())
    {
        delete s_osd;
        s_osd = 0;
    }
}

// As wide (or high, on a vertical panel) as the icon with a small margin,
// like a system tray icon, not a square as large as the panel.
static int appletExtent(int panelSize)
{
    return QMIN(panelSize, 22 + 6);
}

int VolumeApplet::widthForHeight(int height) const
{
    return appletExtent(height);
}

int VolumeApplet::heightForWidth(int width) const
{
    return appletExtent(width);
}

void VolumeApplet::resizeEvent(QResizeEvent *)
{
    m_button->setGeometry(rect());
}

void VolumeApplet::checkMixer()
{
    m_haveMixer = !KStandardDirs::findExe(MixerCommand).isEmpty();
}

void VolumeApplet::updateState()
{
    const PulseVolume::Sink *sink = m_pulse->defaultSink();
    int volume = m_pulse->volume();
    bool muted = m_pulse->isMuted();

    QString icon;
    QString tip;
    if (!m_pulse->isConnected())
    {
        icon = "volume-error";
        tip = i18n("Not connected to the sound server");
    }
    else if (!sink)
    {
        icon = "volume-error";
        tip = i18n("No output device");
    }
    else
    {
        icon = muted ? "volume-muted" : "volume";
        tip = muted ? i18n("Volume: %1% (muted)").arg(volume)
                    : i18n("Volume: %1%").arg(volume);
        if (!sink->description.isEmpty())
        {
            tip += '\n' + sink->description;
        }
    }

    if (icon != m_iconName)
    {
        m_iconName = icon;
        KIconLoader loader("volumeapplet");
        m_button->setImage(loader.loadIcon(icon, KIcon::User)
                           .convertToImage());
    }

    if (tip != m_tip)
    {
        m_tip = tip;
        QToolTip::remove(m_button);
        QToolTip::add(m_button, tip);
    }

    m_popup->setState(sink ? volume : -1, muted, m_haveMixer);
}

void VolumeApplet::togglePopup()
{
    if (m_popup->isVisible())
    {
        m_popup->hide();
        return;
    }

    // the press that closed the popup a moment ago
    if (m_popup->justHidden())
    {
        return;
    }

    checkMixer();
    updateState();
    m_popup->adjustSize();
    m_popup->move(KickerLib::popupPosition(popupDirection(), m_popup, this));
    m_popup->show();
}

// The volume keys, handled by one applet of the process, so that a
// press changes the volume once with two panels holding the applet.  An
// applet in another process (an applet proxy) finds them grabbed and
// goes without: KGlobalAccel ignores the failure.
void VolumeApplet::takeKeys()
{
    s_keyOwner = this;

    m_keys = new KGlobalAccel(this);
    KGlobalAccel *keys = m_keys;
#include "volumebindings.cpp"
    m_keys->readSettings();
    m_keys->updateConnections();

    // changed in the Keyboard Shortcuts module
    kapp->addKipcEventMask(KIPC::SettingsChanged);
    connect(kapp, SIGNAL(settingsChanged(int)), SLOT(slotSettingsChanged(int)));
}

void VolumeApplet::slotSettingsChanged(int category)
{
    if (category == KApplication::SETTINGS_SHORTCUTS && m_keys)
    {
        m_keys->readSettings();
        m_keys->updateConnections();
    }
}

void VolumeApplet::showOSD()
{
    // Nothing to show without a device.
    int volume = m_pulse->volume();
    if (volume < 0)
    {
        return;
    }

    if (!s_osd)
    {
        s_osd = new VolumeOSD("volumeosd");
    }
    s_osd->showVolume(volume, m_pulse->isMuted());
}

// The panel button's middle button, the menu and the mute key: shown on
// the screen.
// The popup's switch calls setMuted() and shows nothing more.
void VolumeApplet::toggleMute()
{
    m_pulse->setMuted(!m_pulse->isMuted());
    showOSD();
}

void VolumeApplet::setVolume(int percent)
{
    m_pulse->setVolume(percent);
}

void VolumeApplet::setMuted(bool muted)
{
    m_pulse->setMuted(muted);
}

void VolumeApplet::changeVolume(int steps)
{
    int volume = m_pulse->volume();
    if (volume < 0)
    {
        return;
    }

    // Up to 100%, or to where it is when amplified beyond that.
    int target = volume + steps * WheelStep;
    target = QMIN(target, QMAX(volume, 100));
    target = QMAX(target, 0);
    m_pulse->setVolume(target);
    showOSD();
}

void VolumeApplet::increaseVolume()
{
    changeVolume(1);
}

void VolumeApplet::decreaseVolume()
{
    changeVolume(-1);
}

void VolumeApplet::showMenu(const QPoint &pos)
{
    m_menu->exec(pos);
}

void VolumeApplet::menuAboutToShow()
{
    checkMixer();

    bool haveSink = m_pulse->defaultSink() != 0;
    m_menu->setItemEnabled(m_muteId, haveSink);
    m_menu->setItemChecked(m_muteId, m_pulse->isMuted());
    m_menu->setItemEnabled(m_deviceMenuId, !m_pulse->sinks().isEmpty());
    m_menu->setItemEnabled(m_mixerId, m_haveMixer);
}

void VolumeApplet::deviceMenuAboutToShow()
{
    m_deviceMenu->clear();
    m_deviceNames.clear();

    const PulseVolume::Sink *current = m_pulse->defaultSink();
    QValueList<PulseVolume::Sink> sinks = m_pulse->sinks();
    QValueList<PulseVolume::Sink>::ConstIterator it;
    for (it = sinks.begin(); it != sinks.end(); ++it)
    {
        QString text = (*it).description.isEmpty() ? (*it).name
                                                   : (*it).description;
        text.replace('&', "&&");
        int id = m_deviceMenu->insertItem(text);
        m_deviceMenu->setItemChecked(id, current && current->name == (*it).name);
        m_deviceNames[id] = (*it).name;
    }
}

void VolumeApplet::deviceSelected(int id)
{
    if (m_deviceNames.contains(id))
    {
        m_pulse->setDefaultSink(m_deviceNames[id]);
    }
}

void VolumeApplet::runMixer()
{
    m_popup->hide();
    KRun::runCommand(MixerCommand, MixerCommand, "kmix");
}

#include "volumeapplet.moc"
