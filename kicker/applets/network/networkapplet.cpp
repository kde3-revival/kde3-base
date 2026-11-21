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

#include <qstylesheet.h>
#include <qtimer.h>
#include <qtooltip.h>

#include <kapplication.h>
#include <kconfig.h>
#include <kglobal.h>
#include <kiconloader.h>
#include <klocale.h>
#include <kmessagebox.h>
#include <kpassivepopup.h>
#include <kpopupmenu.h>
#include <krun.h>
#include <kstandarddirs.h>
#include <kwin.h>

#include "global.h"
#include "networkbutton.h"
#include "networkdialogs.h"
#include "networkicons.h"
#include "networkpopup.h"
#include "networkapplet.h"

// The icon size of the panel button (that of the system tray icons).
static const int PanelIconSize = 22;

// The connecting animation: the time per frame.
static const int FrameInterval = 400;

// While the popup is open, a new scan after this many milliseconds.
static const int ScanInterval = 20000;

// A failure to connect within this many milliseconds of the user
// declining to give the password is not shown.
static const int CancelGrace = 30000;

// The settings editor of NetworkManager.
static const char EditorCommand[] = "nm-connection-editor";

// The settings shared by the applets of all panels and processes.
static const char ConfigFile[] = "networkappletrc";

QPtrList<NetworkApplet> NetworkApplet::s_applets;

extern "C"
{
    KDE_EXPORT KPanelApplet *init(QWidget *parent, const QString &configFile)
    {
        KGlobal::locale()->insertCatalogue("networkapplet");
        return new NetworkApplet(configFile, KPanelApplet::Normal, 0,
                                 parent, "networkapplet");
    }
}

NetworkApplet::NetworkApplet(const QString &configFile, Type type, int actions,
                             QWidget *parent, const char *name)
    : KPanelApplet(configFile, type, actions, parent, name),
      m_frame(0),
      m_notifications(true),
      m_airplane(false),
      m_wifiUsable(false)
{
    setBackgroundOrigin(AncestorOrigin);
    readConfig();

    m_button = new NetworkButton(this);
    connect(m_button, SIGNAL(leftPressed()), SLOT(togglePopup()));
    connect(m_button, SIGNAL(rightPressed(const QPoint &)),
            SLOT(showMenu(const QPoint &)));

    m_popup = new NetworkPopup(this);
    connect(m_popup, SIGNAL(networkClicked(const QString &)),
            SLOT(connectTo(const QString &)));
    connect(m_popup, SIGNAL(wifiToggled(bool)), SLOT(setWifiEnabled(bool)));
    connect(m_popup, SIGNAL(airplaneToggled(bool)), SLOT(setAirplaneMode(bool)));
    connect(m_popup, SIGNAL(hiddenRequested()), SLOT(connectHidden()));
    connect(m_popup, SIGNAL(disconnectRequested()), SLOT(disconnectWifi()));
    connect(m_popup, SIGNAL(settingsRequested()), SLOT(runEditor()));

    m_menu = new KPopupMenu(this);
    m_menu->setCheckable(true);
    m_menu->insertTitle(SmallIcon("network"), i18n("Network"));
    m_wifiId = m_menu->insertItem(i18n("Enable &Wi-Fi"));
    m_airplaneId = m_menu->insertItem(i18n("&Airplane Mode"));
    m_networkingId = m_menu->insertItem(i18n("Enable &Networking"));
    m_menu->insertSeparator();
    m_hiddenId = m_menu->insertItem(i18n("Connect to &Hidden Network..."));
    m_disconnectId = m_menu->insertItem(SmallIcon("connect_no"),
                                        i18n("&Disconnect Wi-Fi"));
    m_menu->insertSeparator();
    m_notifyId = m_menu->insertItem(i18n("Show N&otifications"));
    m_editorId = m_menu->insertItem(SmallIcon("configure"),
                                    i18n("Network &Settings..."));
    connect(m_menu, SIGNAL(aboutToShow()), SLOT(menuAboutToShow()));
    connect(m_menu, SIGNAL(activated(int)), SLOT(menuActivated(int)));
    // also in the menu of the applet's handle
    setCustomMenu(m_menu);

    m_animation = new QTimer(this);
    connect(m_animation, SIGNAL(timeout()), SLOT(animate()));

    m_scanTimer = new QTimer(this);
    connect(m_scanTimer, SIGNAL(timeout()), SLOT(scan()));

    s_applets.append(this);

    m_backend = NetworkBackend::ref();
    connect(m_backend, SIGNAL(changed()), SLOT(updateState()));
    connect(m_backend, SIGNAL(secretRequested(int, const QString &, bool)),
            SLOT(secretRequested(int, const QString &, bool)));
    connect(m_backend, SIGNAL(secretCancelled(int)), SLOT(secretCancelled(int)));
    connect(m_backend, SIGNAL(connected(const QString &, bool)),
            SLOT(connected(const QString &, bool)));
    connect(m_backend, SIGNAL(disconnected(const QString &, bool)),
            SLOT(disconnected(const QString &, bool)));
    connect(m_backend, SIGNAL(connectionFailed(const QString &, const QString &)),
            SLOT(connectionFailed(const QString &, const QString &)));
    connect(m_backend, SIGNAL(error(const QString &)), SLOT(error(const QString &)));
    updateState();
}

NetworkApplet::~NetworkApplet()
{
    s_applets.removeRef(this);

    // NetworkManager gets no password from a dialog going away.
    QMap<int, PasswordDialog *>::Iterator it;
    for (it = m_requests.begin(); it != m_requests.end(); ++it)
    {
        m_backend->cancelSecret(it.key());
        delete it.data();
    }
    m_requests.clear();

    disconnect(m_backend, 0, this, 0);
    NetworkBackend::deref();
}

// As wide (or high, on a vertical panel) as the icon with a small margin,
// like a system tray icon, not a square as large as the panel.
static int appletExtent(int panelSize)
{
    return QMIN(panelSize, PanelIconSize + 6);
}

int NetworkApplet::widthForHeight(int height) const
{
    return appletExtent(height);
}

int NetworkApplet::heightForWidth(int width) const
{
    return appletExtent(width);
}

void NetworkApplet::resizeEvent(QResizeEvent *)
{
    m_button->setGeometry(rect());
}

void NetworkApplet::readConfig()
{
    KConfig config(ConfigFile, true, false);
    config.setGroup("General");
    m_notifications = config.readBoolEntry("Notifications", true);
    m_airplane = config.readBoolEntry("AirplaneMode", false);
}

void NetworkApplet::writeConfig()
{
    KConfig config(ConfigFile, false, false);
    config.setGroup("General");
    config.writeEntry("Notifications", m_notifications);
    config.writeEntry("AirplaneMode", m_airplane);
    config.sync();
}

// The notifications and the password dialogs are the first applet's,
// not one of each panel.
bool NetworkApplet::isNotifier() const
{
    return s_applets.getFirst() == this;
}

// Airplane mode: the user switched the radios off with it (and they are
// still off), or the hardware switch did.
bool NetworkApplet::airplaneMode() const
{
    NetworkBackend::Status s = m_backend->status();
    if (s.kind == NetworkBackend::Unavailable || s.kind == NetworkBackend::NotRunning ||
        (!s.haveWifi && !s.haveWwan))
    {
        return false;
    }
    if (s.haveWifi && !s.wifiHardwareEnabled)
    {
        return true;
    }
    return m_airplane && !(s.haveWifi && s.wifiEnabled) &&
           !(s.haveWwan && s.wwanEnabled);
}

bool NetworkApplet::haveEditor() const
{
    return !KStandardDirs::findExe(EditorCommand).isEmpty();
}

void NetworkApplet::setIcon(const QString &key, const QImage &image)
{
    if (key == m_iconKey)
    {
        return;
    }
    m_iconKey = key;
    m_button->setImage(image);
}

static QImage themeIcon(const char *name)
{
    return KGlobal::iconLoader()
        ->loadIcon(name, KIcon::NoGroup, PanelIconSize, KIcon::DefaultState, 0, true)
        .convertToImage();
}

void NetworkApplet::updateState()
{
    NetworkBackend::Status s = m_backend->status();
    bool airplane = airplaneMode();

    // the icon
    bool animated = false;
    switch (s.kind)
    {
        case NetworkBackend::Unavailable:
        case NetworkBackend::NotRunning:
            setIcon("error", NetworkIcons::wifi(PanelIconSize, 0, false,
                                                NetworkIcons::ErrorBadge));
            break;
        case NetworkBackend::NetworkingOff:
            setIcon("off", NetworkIcons::wifi(PanelIconSize, 0, false,
                                              NetworkIcons::OffBadge));
            break;
        case NetworkBackend::Wireless:
        {
            int bars = NetworkIcons::bars(s.strength);
            setIcon(QString("wifi%1").arg(bars), NetworkIcons::wifi(PanelIconSize, bars));
            break;
        }
        case NetworkBackend::Wired:
        case NetworkBackend::Other:
            setIcon("wired", themeIcon("network_local"));
            break;
        case NetworkBackend::Connecting:
            if (s.wifiConnecting)
            {
                animated = true;
                if (!m_animation->isActive())
                {
                    m_frame = 0;
                    animate();
                    m_animation->start(FrameInterval);
                }
            }
            else
            {
                setIcon("wired-connecting", themeIcon("connect_creating"));
            }
            break;
        case NetworkBackend::Disconnected:
            if (airplane)
            {
                setIcon("airplane", NetworkIcons::airplane(PanelIconSize));
            }
            else if (s.haveWifi && s.wifiEnabled)
            {
                setIcon("wifi0", NetworkIcons::wifi(PanelIconSize, 0));
            }
            else if (s.haveWifi)
            {
                setIcon("wifi-off", NetworkIcons::wifi(PanelIconSize, 0, false,
                                                       NetworkIcons::OffBadge));
            }
            else
            {
                setIcon("disconnected", themeIcon("connect_no"));
            }
            break;
    }
    if (!animated)
    {
        m_animation->stop();
    }

    // the tooltip
    QStringList tip;
    switch (s.kind)
    {
        case NetworkBackend::Unavailable:
            tip << i18n("No connection to the system bus");
            break;
        case NetworkBackend::NotRunning:
            tip << i18n("NetworkManager is not running");
            break;
        case NetworkBackend::NetworkingOff:
            tip << i18n("Networking is disabled");
            break;
        case NetworkBackend::Wireless:
            tip << i18n("Wi-Fi: %1").arg(s.name);
            tip << i18n("Signal: %1%").arg(s.strength);
            break;
        case NetworkBackend::Wired:
            tip << i18n("Wired: %1").arg(s.name);
            break;
        case NetworkBackend::Other:
            tip << i18n("Connected: %1").arg(s.name);
            break;
        case NetworkBackend::Connecting:
            tip << i18n("Connecting to %1...").arg(s.name);
            break;
        case NetworkBackend::Disconnected:
            tip << i18n("Not connected");
            if (airplane)
            {
                tip << i18n("Airplane mode");
            }
            else if (s.haveWifi && !s.wifiEnabled)
            {
                tip << i18n("Wi-Fi is off");
            }
            break;
    }
    if (!s.address.isEmpty())
    {
        tip << i18n("Address: %1").arg(s.address);
    }
    if (s.speed > 0 && (s.kind == NetworkBackend::Wireless ||
                        s.kind == NetworkBackend::Wired))
    {
        tip << i18n("Speed: %1 Mbit/s").arg(s.speed);
    }
    QString tipText = tip.join("\n");
    if (tipText != m_tip)
    {
        m_tip = tipText;
        QToolTip::remove(m_button);
        QToolTip::add(m_button, tipText);
    }

    m_popup->setState(s, m_backend->networks(), m_backend->isScanning(), airplane,
                      haveEditor());

    // Wi-Fi switched on while the list is open: look for the networks now,
    // not at the next round of the timer
    bool wifiUsable = s.kind != NetworkBackend::Unavailable &&
                      s.kind != NetworkBackend::NotRunning &&
                      s.kind != NetworkBackend::NetworkingOff &&
                      s.haveWifi && s.wifiEnabled && s.wifiHardwareEnabled;
    if (wifiUsable && !m_wifiUsable && m_popup->isVisible())
    {
        QTimer::singleShot(0, this, SLOT(scan()));
    }
    m_wifiUsable = wifiUsable;
}

void NetworkApplet::animate()
{
    // the bars rising, then all of them again
    m_frame = (m_frame % 4) + 1;
    setIcon(QString("connecting%1").arg(m_frame), NetworkIcons::wifi(PanelIconSize, m_frame));
}

void NetworkApplet::togglePopup()
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

    updateState();
    m_popup->adjustSize();
    m_popup->move(KickerLib::popupPosition(popupDirection(), m_popup, this));
    m_popup->show();

    // The list fills in as the results come.
    scan();
    m_scanTimer->start(ScanInterval);
}

void NetworkApplet::scan()
{
    if (!m_popup->isVisible())
    {
        m_scanTimer->stop();
        return;
    }
    m_backend->requestScan();
}

void NetworkApplet::showMenu(const QPoint &pos)
{
    m_menu->exec(pos);
}

void NetworkApplet::menuAboutToShow()
{
    NetworkBackend::Status s = m_backend->status();
    bool running = s.kind != NetworkBackend::Unavailable &&
                   s.kind != NetworkBackend::NotRunning;
    bool networking = running && s.kind != NetworkBackend::NetworkingOff;
    bool airplane = airplaneMode();

    m_menu->setItemEnabled(m_wifiId, running && s.haveWifi && s.wifiHardwareEnabled);
    m_menu->setItemChecked(m_wifiId, s.wifiEnabled && s.wifiHardwareEnabled);
    m_menu->setItemEnabled(m_airplaneId, running && (s.haveWifi || s.haveWwan));
    m_menu->setItemChecked(m_airplaneId, airplane);
    m_menu->setItemEnabled(m_networkingId, running);
    m_menu->setItemChecked(m_networkingId, m_backend->networkingEnabled());
    m_menu->setItemEnabled(m_hiddenId, networking && s.haveWifi && s.wifiEnabled);
    m_menu->setItemEnabled(m_disconnectId,
                           networking && (s.wifiActive ||
                                          (s.kind == NetworkBackend::Connecting &&
                                           s.wifiConnecting)));
    m_menu->setItemChecked(m_notifyId, m_notifications);
    m_menu->setItemEnabled(m_editorId, haveEditor());
}

void NetworkApplet::menuActivated(int id)
{
    if (id == m_wifiId)
    {
        setWifiEnabled(!m_menu->isItemChecked(id));
    }
    else if (id == m_airplaneId)
    {
        setAirplaneMode(!m_menu->isItemChecked(id));
    }
    else if (id == m_networkingId)
    {
        m_backend->setNetworkingEnabled(!m_backend->networkingEnabled());
    }
    else if (id == m_hiddenId)
    {
        connectHidden();
    }
    else if (id == m_disconnectId)
    {
        disconnectWifi();
    }
    else if (id == m_notifyId)
    {
        m_notifications = !m_notifications;
        writeConfig();
    }
    else if (id == m_editorId)
    {
        runEditor();
    }
}

void NetworkApplet::connectTo(const QString &ssid)
{
    NetworkBackend::NetworkList networks = m_backend->networks();
    NetworkBackend::NetworkList::ConstIterator it;
    for (it = networks.begin(); it != networks.end(); ++it)
    {
        if ((*it).ssid == ssid)
        {
            break;
        }
    }
    if (it == networks.end() || (*it).active || (*it).activating)
    {
        return;
    }
    NetworkBackend::Network network = *it;

    m_popup->hide();

    switch (m_backend->connectTo(ssid))
    {
        case NetworkBackend::Started:
            break;
        case NetworkBackend::NeedPassword:
        {
            PasswordDialog *dialog =
                new PasswordDialog(ssid, QString::null,
                                   network.security == NetworkBackend::WPA3Personal,
                                   0, "networkpassword");
            connect(dialog, SIGNAL(okClicked()), SLOT(newPasswordEntered()));
            connect(dialog, SIGNAL(finished()), dialog, SLOT(delayedDestruct()));
            dialog->show();
            KWin::forceActiveWindow(dialog->winId());
            break;
        }
        case NetworkBackend::Unsupported:
        {
            QString text = i18n("<qt>The network <b>%1</b> uses %2, which needs "
                                "to be set up in the network settings.</qt>")
                               .arg(QStyleSheet::escape(ssid))
                               .arg(NetworkBackend::securityName(network.security));
            if (!haveEditor())
            {
                KMessageBox::sorry(0, text, i18n("Network"));
            }
            else if (KMessageBox::questionYesNo(
                         0, text, i18n("Network"),
                         KGuiItem(i18n("Open &Settings"), "configure"),
                         KStdGuiItem::cancel()) == KMessageBox::Yes)
            {
                runEditor();
            }
            break;
        }
        case NetworkBackend::Failed:
            connectionFailed(ssid, i18n("The network was not found."));
            break;
    }
}

void NetworkApplet::newPasswordEntered()
{
    if (sender() && sender()->inherits("PasswordDialog"))
    {
        const PasswordDialog *dialog = static_cast<const PasswordDialog *>(sender());
        m_backend->connectWithPassword(dialog->ssid(), dialog->password());
    }
}

void NetworkApplet::connectHidden()
{
    m_popup->hide();

    HiddenNetworkDialog *dialog = new HiddenNetworkDialog(0, "networkhidden");
    connect(dialog, SIGNAL(okClicked()), SLOT(hiddenEntered()));
    connect(dialog, SIGNAL(finished()), dialog, SLOT(delayedDestruct()));
    dialog->show();
    KWin::forceActiveWindow(dialog->winId());
}

void NetworkApplet::hiddenEntered()
{
    if (sender() && sender()->inherits("HiddenNetworkDialog"))
    {
        const HiddenNetworkDialog *dialog =
            static_cast<const HiddenNetworkDialog *>(sender());
        m_backend->connectHidden(dialog->ssid(), dialog->security(),
                                 dialog->password());
    }
}

void NetworkApplet::disconnectWifi()
{
    m_popup->hide();
    m_backend->disconnectWifi();
}

void NetworkApplet::setWifiEnabled(bool on)
{
    if (on && m_airplane)
    {
        m_airplane = false;
        writeConfig();
    }
    m_backend->setWifiEnabled(on);
}

void NetworkApplet::setAirplaneMode(bool on)
{
    m_airplane = on;
    writeConfig();

    NetworkBackend::Status s = m_backend->status();
    if (s.haveWifi)
    {
        m_backend->setWifiEnabled(!on);
    }
    if (s.haveWwan)
    {
        m_backend->setWwanEnabled(!on);
    }
    updateState();
}

void NetworkApplet::runEditor()
{
    m_popup->hide();
    KRun::runCommand(EditorCommand, EditorCommand, "network");
}

void NetworkApplet::secretRequested(int request, const QString &ssid, bool retry)
{
    if (!isNotifier())
    {
        return;
    }

    // the security of the network, for checking the password
    bool sae = false;
    NetworkBackend::NetworkList networks = m_backend->networks();
    NetworkBackend::NetworkList::ConstIterator it;
    for (it = networks.begin(); it != networks.end(); ++it)
    {
        if ((*it).ssid == ssid)
        {
            sae = (*it).security == NetworkBackend::WPA3Personal;
        }
    }

    QString error = retry ? i18n("The password was not accepted. Try again.")
                          : QString::null;
    PasswordDialog *dialog = new PasswordDialog(ssid, error, sae, 0, "networkagent");
    m_requests[request] = dialog;
    connect(dialog, SIGNAL(okClicked()), SLOT(passwordEntered()));
    connect(dialog, SIGNAL(cancelClicked()), SLOT(passwordCancelled()));
    dialog->show();
    KWin::forceActiveWindow(dialog->winId());
}

void NetworkApplet::passwordEntered()
{
    QMap<int, PasswordDialog *>::Iterator it;
    for (it = m_requests.begin(); it != m_requests.end(); ++it)
    {
        if (it.data() == sender())
        {
            int request = it.key();
            PasswordDialog *dialog = it.data();
            m_requests.remove(it);
            m_backend->answerSecret(request, dialog->password());
            dialog->delayedDestruct();
            return;
        }
    }
}

void NetworkApplet::passwordCancelled()
{
    QMap<int, PasswordDialog *>::Iterator it;
    for (it = m_requests.begin(); it != m_requests.end(); ++it)
    {
        if (it.data() == sender())
        {
            int request = it.key();
            PasswordDialog *dialog = it.data();
            m_requests.remove(it);
            m_cancelled = dialog->ssid();
            m_cancelTime.start();
            m_backend->cancelSecret(request);
            dialog->delayedDestruct();
            return;
        }
    }
}

void NetworkApplet::secretCancelled(int request)
{
    if (m_requests.contains(request))
    {
        PasswordDialog *dialog = m_requests[request];
        m_requests.remove(request);
        dialog->delayedDestruct();
    }
}

void NetworkApplet::notify(const QString &caption, const QString &text,
                           const QPixmap &icon)
{
    if (!isNotifier() || !m_notifications)
    {
        return;
    }
    KPassivePopup::message(caption, text, icon, this);
}

void NetworkApplet::connected(const QString &name, bool wifi)
{
    QPixmap icon;
    if (wifi)
    {
        // (the state is updated after this signal)
        icon.convertFromImage(NetworkIcons::wifi(32, 4));
        notify(i18n("Connected"),
               i18n("Connected to the Wi-Fi network %1.").arg(name), icon);
    }
    else
    {
        icon = DesktopIcon("network_local", 32);
        notify(i18n("Connected"), i18n("Connected to %1.").arg(name), icon);
    }
}

void NetworkApplet::disconnected(const QString &name, bool wifi)
{
    QPixmap icon;
    if (wifi)
    {
        icon.convertFromImage(NetworkIcons::wifi(32, 0));
        notify(i18n("Disconnected"),
               i18n("Disconnected from the Wi-Fi network %1.").arg(name), icon);
    }
    else
    {
        icon = DesktopIcon("connect_no", 32);
        notify(i18n("Disconnected"), i18n("Disconnected from %1.").arg(name), icon);
    }
}

void NetworkApplet::connectionFailed(const QString &name, const QString &reason)
{
    // The user said no to the password: no news.
    if (name == m_cancelled && m_cancelTime.isValid() &&
        m_cancelTime.elapsed() < CancelGrace)
    {
        m_cancelled = QString::null;
        return;
    }

    QPixmap icon;
    icon.convertFromImage(NetworkIcons::wifi(32, 0, false, NetworkIcons::ErrorBadge));
    notify(i18n("Connection Failed"),
           i18n("Could not connect to %1.").arg(name) + '\n' + reason, icon);
}

void NetworkApplet::error(const QString &message)
{
    if (!isNotifier())
    {
        return;
    }
    // Shown even with the notifications off: the user asked for something.
    KPassivePopup::message(i18n("Network"), message, DesktopIcon("messagebox_warning", 32),
                           this);
}

#include "networkapplet.moc"
