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

#include <qcheckbox.h>
#include <qlabel.h>
#include <qlayout.h>
#include <qlistbox.h>
#include <qpainter.h>
#include <qpushbutton.h>
#include <qstyle.h>

#include <kdialog.h>
#include <kglobal.h>
#include <kiconloader.h>
#include <klocale.h>
#include <kstringhandler.h>

#include "networkicons.h"
#include "networkpopup.h"

// A click outside the popup closes it; one on the panel button reaching
// the button within this many milliseconds does not open it again.
static const int ReopenDelay = 300;

// The size of the signal icons and the room around the parts of a row.
static const int SignalSize = 22;
static const int Margin = 4;

// Rows shown without scrolling.
static const int VisibleRows = 8;

/**
 * A network in the list: the signal, the SSID, a line with the state and
 * the security, and a lock for the networks with a password.
 */
class NetworkItem : public QListBoxItem
{
public:
    NetworkItem(QListBox *list, const NetworkBackend::Network &network)
        : QListBoxItem(list), m_network(network)
    {
        setText(network.ssid);
    }

    const NetworkBackend::Network &network() const { return m_network; }

    virtual int height(const QListBox *list) const
    {
        return rowHeight(list->font());
    }

    static int rowHeight(const QFont &font)
    {
        QFontMetrics fm(font);
        QFontMetrics small(smallFont(font));
        return QMAX(SignalSize, fm.lineSpacing() + small.lineSpacing()) + 2 * Margin;
    }

    virtual int width(const QListBox *list) const
    {
        QFont bold = list->font();
        bold.setBold(true);
        QFontMetrics fm(bold);
        return Margin + SignalSize + Margin + fm.width(m_network.ssid) + Margin +
               16 + Margin;
    }

protected:
    virtual void paint(QPainter *p)
    {
        const QListBox *list = listBox();
        int w = list->visibleWidth();
        if (list->contentsWidth() > w)
        {
            w = list->contentsWidth();
        }
        int h = height(list);

        int bars = NetworkIcons::bars(m_network.strength);
        p->drawPixmap(Margin, (h - SignalSize) / 2, signalIcon(bars));

        int right = w - Margin;
        if (m_network.security != NetworkBackend::Open &&
            m_network.security != NetworkBackend::EnhancedOpen)
        {
            QPixmap lock = SmallIcon("encrypted");
            right -= lock.width();
            p->drawPixmap(right, (h - lock.height()) / 2, lock);
            right -= Margin;
        }

        int x = Margin + SignalSize + Margin;
        int textWidth = QMAX(right - x, 10);

        QFont font = list->font();
        font.setBold(m_network.active);
        QFontMetrics fm(font);
        QFont small = smallFont(list->font());
        QFontMetrics smallFm(small);

        int top = (h - fm.lineSpacing() - smallFm.lineSpacing()) / 2;
        p->setFont(font);
        p->drawText(x, top, textWidth, fm.lineSpacing(), Qt::AlignLeft | Qt::AlignVCenter,
                    KStringHandler::rPixelSqueeze(m_network.ssid, fm, textWidth));

        QStringList details;
        if (m_network.active)
        {
            details << i18n("Connected");
        }
        else if (m_network.activating)
        {
            details << i18n("Connecting...");
        }
        else if (m_network.known)
        {
            details << i18n("a known Wi-Fi network", "Saved");
        }
        if (!m_network.securityText.isEmpty())
        {
            details << m_network.securityText;
        }
        else if (m_network.security == NetworkBackend::Open)
        {
            details << i18n("an unencrypted Wi-Fi network", "Open");
        }
        details << i18n("signal strength", "%1%").arg(m_network.strength);
        p->setFont(small);
        p->drawText(x, top + fm.lineSpacing(), textWidth, smallFm.lineSpacing(),
                    Qt::AlignLeft | Qt::AlignVCenter,
                    KStringHandler::rPixelSqueeze(
                        details.join(QString(" ") + QChar(0xb7) + ' '), smallFm,
                        textWidth));
        p->setFont(list->font());
    }

private:
    static QFont smallFont(const QFont &font)
    {
        QFont f = font;
        if (f.pointSize() > 0)
        {
            f.setPointSize(QMAX(f.pointSize() - 1, 6));
        }
        else
        {
            f.setPixelSize(QMAX(f.pixelSize() - 2, 8));
        }
        return f;
    }

    static const QPixmap &signalIcon(int bars)
    {
        static QPixmap icons[5];
        bars = QMIN(QMAX(bars, 0), 4);
        if (icons[bars].isNull())
        {
            icons[bars].convertFromImage(NetworkIcons::wifi(SignalSize, bars));
        }
        return icons[bars];
    }

    NetworkBackend::Network m_network;
};

NetworkPopup::NetworkPopup(QWidget *parent, const char *name)
    : QFrame(parent, name, WStyle_Customize | WType_Popup),
      m_updating(false)
{
    setFrameStyle(QFrame::PopupPanel | QFrame::Raised);
    setLineWidth(style().pixelMetric(QStyle::PM_DefaultFrameWidth, this));

    QVBoxLayout *layout = new QVBoxLayout(this,
                                          frameWidth() + KDialog::marginHint() / 2,
                                          KDialog::spacingHint());

    QHBoxLayout *switches = new QHBoxLayout(layout);
    m_wifi = new QCheckBox(i18n("&Wi-Fi"), this);
    connect(m_wifi, SIGNAL(toggled(bool)), SLOT(wifiClicked(bool)));
    switches->addWidget(m_wifi);
    switches->addStretch();
    m_airplane = new QCheckBox(i18n("&Airplane mode"), this);
    connect(m_airplane, SIGNAL(toggled(bool)), SLOT(airplaneClicked(bool)));
    switches->addWidget(m_airplane);

    m_status = new QLabel(this);
    layout->addWidget(m_status);

    m_list = new QListBox(this);
    m_list->setVScrollBarMode(QScrollView::Auto);
    m_list->setHScrollBarMode(QScrollView::AlwaysOff);
    m_list->setMinimumWidth(300);
    // VisibleRows high, however many there are: the popup does not
    // change its size, which would move it away from the panel.
    m_list->setFixedHeight(VisibleRows * NetworkItem::rowHeight(m_list->font()) +
                           2 * m_list->frameWidth());
    // The row under the mouse is highlighted, as in a menu.
    m_list->setMouseTracking(true);
    m_list->viewport()->setMouseTracking(true);
    connect(m_list, SIGNAL(onItem(QListBoxItem *)),
            SLOT(itemEntered(QListBoxItem *)));
    connect(m_list, SIGNAL(clicked(QListBoxItem *)),
            SLOT(itemClicked(QListBoxItem *)));
    connect(m_list, SIGNAL(returnPressed(QListBoxItem *)),
            SLOT(itemClicked(QListBoxItem *)));
    layout->addWidget(m_list);

    QHBoxLayout *buttons = new QHBoxLayout(layout);
    m_hidden = new QPushButton(i18n("&Hidden Network..."), this);
    connect(m_hidden, SIGNAL(clicked()), SIGNAL(hiddenRequested()));
    buttons->addWidget(m_hidden);
    m_disconnect = new QPushButton(i18n("&Disconnect"), this);
    connect(m_disconnect, SIGNAL(clicked()), SIGNAL(disconnectRequested()));
    buttons->addWidget(m_disconnect);

    m_settings = new QPushButton(SmallIconSet("configure"),
                                 i18n("Network &Settings..."), this);
    connect(m_settings, SIGNAL(clicked()), SIGNAL(settingsRequested()));
    layout->addWidget(m_settings);

    setState(NetworkBackend::Status(), NetworkBackend::NetworkList(), false, false,
             false);
}

void NetworkPopup::setState(const NetworkBackend::Status &status,
                            const NetworkBackend::NetworkList &networks,
                            bool scanning, bool airplane, bool haveEditor)
{
    bool running = status.kind != NetworkBackend::Unavailable &&
                   status.kind != NetworkBackend::NotRunning;
    bool networking = running && status.kind != NetworkBackend::NetworkingOff;
    bool wifiUsable = networking && status.haveWifi && status.wifiEnabled &&
                      status.wifiHardwareEnabled;

    m_updating = true;
    m_wifi->setChecked(status.wifiEnabled && status.wifiHardwareEnabled);
    m_wifi->setEnabled(running && status.haveWifi && status.wifiHardwareEnabled);
    m_airplane->setChecked(airplane);
    m_airplane->setEnabled(running && (status.haveWifi || status.haveWwan));
    m_updating = false;

    QString text;
    switch (status.kind)
    {
        case NetworkBackend::Unavailable:
            text = i18n("No connection to the system bus.");
            break;
        case NetworkBackend::NotRunning:
            text = i18n("NetworkManager is not running.");
            break;
        case NetworkBackend::NetworkingOff:
            text = i18n("Networking is disabled.");
            break;
        default:
            if (!status.haveWifi)
            {
                text = i18n("There is no wireless device.");
            }
            else if (airplane)
            {
                text = i18n("Airplane mode is on.");
            }
            else if (!status.wifiHardwareEnabled)
            {
                text = i18n("Wi-Fi is switched off by a hardware switch.");
            }
            else if (!status.wifiEnabled)
            {
                text = i18n("Wi-Fi is off.");
            }
            else if (scanning)
            {
                text = i18n("Looking for networks...");
            }
            else if (networks.isEmpty())
            {
                text = i18n("No networks found.");
            }
            else
            {
                text = i18n("Choose a network:");
            }
            break;
    }
    m_status->setText(text);

    setNetworks(wifiUsable ? networks : NetworkBackend::NetworkList());
    m_list->setEnabled(wifiUsable);
    m_hidden->setEnabled(wifiUsable);
    m_disconnect->setEnabled(networking && (status.wifiActive ||
                                            (status.kind == NetworkBackend::Connecting &&
                                             status.wifiConnecting)));
    m_settings->setEnabled(haveEditor);
}

void NetworkPopup::setNetworks(const NetworkBackend::NetworkList &networks)
{
    if (networks == m_networks && m_list->count() == networks.count())
    {
        return;
    }
    m_networks = networks;

    // Keep the row under the mouse and the scroll position.
    QString current;
    if (m_list->currentItem() >= 0)
    {
        current = m_list->text(m_list->currentItem());
    }
    int y = m_list->contentsY();

    m_list->setUpdatesEnabled(false);
    m_list->clear();
    NetworkBackend::NetworkList::ConstIterator it;
    for (it = networks.begin(); it != networks.end(); ++it)
    {
        new NetworkItem(m_list, *it);
    }
    if (!current.isEmpty())
    {
        QListBoxItem *item = m_list->findItem(current, ExactMatch | CaseSensitive);
        if (item)
        {
            m_list->setCurrentItem(item);
        }
    }
    m_list->setContentsPos(0, y);
    m_list->setUpdatesEnabled(true);
    m_list->triggerUpdate(true);

}

void NetworkPopup::itemClicked(QListBoxItem *item)
{
    if (!item)
    {
        return;
    }
    emit networkClicked(static_cast<NetworkItem *>(item)->network().ssid);
}

void NetworkPopup::itemEntered(QListBoxItem *item)
{
    m_list->setCurrentItem(item);
}

void NetworkPopup::wifiClicked(bool on)
{
    if (!m_updating)
    {
        emit wifiToggled(on);
    }
}

void NetworkPopup::airplaneClicked(bool on)
{
    if (!m_updating)
    {
        emit airplaneToggled(on);
    }
}

bool NetworkPopup::justHidden() const
{
    return m_hideTime.isValid() && m_hideTime.elapsed() < ReopenDelay;
}

void NetworkPopup::showEvent(QShowEvent *e)
{
    QFrame::showEvent(e);
    m_list->setFocus();
}

void NetworkPopup::hideEvent(QHideEvent *e)
{
    QFrame::hideEvent(e);
    m_hideTime.start();
}

void NetworkPopup::keyPressEvent(QKeyEvent *e)
{
    if (e->key() == Key_Escape)
    {
        close();
        return;
    }

    QFrame::keyPressEvent(e);
}

#include "networkpopup.moc"
