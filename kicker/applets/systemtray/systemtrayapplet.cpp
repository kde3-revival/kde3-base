/*****************************************************************

Copyright (c) 2000-2001 Matthias Ettrich <ettrich@kde.org>
              2000-2001 Matthias Elter   <elter@kde.org>
              2001      Carsten Pfeiffer <pfeiffer@kde.org>
              2001      Martijn Klingens <mklingens@yahoo.com>
              2004      Aaron J. Seigo   <aseigo@kde.org>

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN
AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

******************************************************************/

#include <qcursor.h>
#include <qlayout.h>
#include <qpopupmenu.h>
#include <qtimer.h>
#include <qpixmap.h>
#include <qevent.h>
#include <qstyle.h>
#include <qpainter.h>

#include <dcopclient.h>
#include <kapplication.h>
#include <klocale.h>
#include <kconfig.h>
#include <kdebug.h>
#include <kglobal.h>
#include <krun.h>
#include <kwinmodule.h>
#include <kdialogbase.h>
#include <kactionselector.h>
#include <kiconloader.h>
#include <kwin.h>

#include "simplebutton.h"

#include "systemtrayapplet.h"
#include "systemtrayapplet.moc"

#include <X11/Xlib.h>
#include <X11/Xatom.h>

extern "C"
{
    KDE_EXPORT KPanelApplet* init(QWidget *parent, const QString& configFile)
    {
        KGlobal::locale()->insertCatalogue("ksystemtrayapplet");
        return new SystemTrayApplet(configFile, KPanelApplet::Normal,
                                    KPanelApplet::Preferences, parent, "ksystemtrayapplet");
    }
}

SystemTrayApplet::SystemTrayApplet(const QString& configFile, Type type, int actions,
                                   QWidget *parent, const char *name)
  : KPanelApplet(configFile, type, actions, parent, name),
    m_showFrame(false),
    m_showHidden(false),
    m_expandButton(0),
    m_settingsDialog(0),
    m_iconSelector(0),
    m_autoRetractTimer(0),
    m_autoRetract(false),
    m_iconSize(22),
    m_currentIconSize(22),
    m_announcedIconSize(0),
    m_layout(0)
{
    loadSettings();

    setBackgroundOrigin(AncestorOrigin);

    kwin_module = new KWinModule(this);

    // kApplication notifies us of settings changes. added to support
    // disabling of frame effect on mouse hover
    kapp->dcopClient()->setNotifications(true);
    connectDCOPSignal("kicker", "kicker", "configurationChanged()", "loadSettings()", false);

    QTimer::singleShot(0, this, SLOT(initialize()));
}

void SystemTrayApplet::initialize()
{
    // register existing tray windows
    const QValueList<WId> systemTrayWindows = kwin_module->systemTrayWindows();
    bool existing = false;
    for (QValueList<WId>::ConstIterator it = systemTrayWindows.begin();
         it != systemTrayWindows.end(); ++it )
    {
        embedWindow(*it, true);
        existing = true;
    }

    showExpandButton(!m_hiddenWins.isEmpty());

    if (existing)
    {
        updateVisibleWins();
        layoutTray();
    }

    // the KWinModule notifies us when tray windows are added or removed
    connect( kwin_module, SIGNAL( systemTrayWindowAdded(WId) ),
             this, SLOT( systemTrayWindowAdded(WId) ) );
    connect( kwin_module, SIGNAL( systemTrayWindowRemoved(WId) ),
             this, SLOT( updateTrayWindows() ) );

    QCString screenstr;
    screenstr.setNum(qt_xscreen());
    QCString trayatom = "_NET_SYSTEM_TRAY_S" + screenstr;

    Display *display = qt_xdisplay();

    net_system_tray_selection = XInternAtom(display, trayatom, false);
    net_system_tray_opcode = XInternAtom(display, "_NET_SYSTEM_TRAY_OPCODE", false);

    // Acquire system tray
    XSetSelectionOwner(display,
                       net_system_tray_selection,
                       winId(),
                       CurrentTime);

    WId root = qt_xrootwin();

    if (XGetSelectionOwner (display, net_system_tray_selection) == winId())
    {
        XClientMessageEvent xev;

        xev.type = ClientMessage;
        xev.window = root;

        xev.message_type = XInternAtom (display, "MANAGER", False);
        xev.format = 32;
        xev.data.l[0] = CurrentTime;
        xev.data.l[1] = net_system_tray_selection;
        xev.data.l[2] = winId();
        xev.data.l[3] = 0;        /* manager specific data */
        xev.data.l[4] = 0;        /* manager specific data */

        XSendEvent (display, root, False, StructureNotifyMask, (XEvent *)&xev);
    }

    announceIconSize();
    setBackground();
}

SystemTrayApplet::~SystemTrayApplet()
{
    for (TrayItemList::const_iterator it = m_hiddenWins.constBegin();
         it != m_hiddenWins.constEnd();
         ++it)
    {
        delete *it;
    }

    for (TrayItemList::const_iterator it = m_shownWins.constBegin();
         it != m_shownWins.constEnd();
         ++it)
    {
        delete *it;
    }

    KGlobal::locale()->removeCatalogue("ksystemtrayapplet");
}

bool SystemTrayApplet::x11Event( XEvent *e )
{
#define SYSTEM_TRAY_REQUEST_DOCK    0
#define SYSTEM_TRAY_BEGIN_MESSAGE   1
#define SYSTEM_TRAY_CANCEL_MESSAGE  2
    if ( e->type == ClientMessage ) {
        if ( e->xclient.message_type == net_system_tray_opcode &&
             e->xclient.data.l[1] == SYSTEM_TRAY_REQUEST_DOCK) {
            if( isWinManaged( (WId)e->xclient.data.l[2] ) ) // we already manage it
                return true;
            embedWindow( e->xclient.data.l[2], false );
            layoutTray();
            return true;
        }
    }
    return KPanelApplet::x11Event( e ) ;
}

void SystemTrayApplet::preferences()
{
    if (m_settingsDialog)
    {
        m_settingsDialog->show();
        m_settingsDialog->raise();
        return;
    }

    m_settingsDialog = new KDialogBase(0, "systrayconfig",
                                       false, i18n("Configure System Tray"),
                                       KDialogBase::Ok | KDialogBase::Apply | KDialogBase::Cancel,
                                       KDialogBase::Ok, true);
    m_settingsDialog->resize(450, 400);
    connect(m_settingsDialog, SIGNAL(applyClicked()), this, SLOT(applySettings()));
    connect(m_settingsDialog, SIGNAL(okClicked()), this, SLOT(applySettings()));
    connect(m_settingsDialog, SIGNAL(finished()), this, SLOT(settingsDialogFinished()));

    m_iconSelector = new KActionSelector(m_settingsDialog);
    m_iconSelector->setAvailableLabel(i18n("Visible icons:"));
    m_iconSelector->setSelectedLabel(i18n("Hidden icons:"));
    m_iconSelector->setShowUpDownButtons(false);
    m_settingsDialog->setMainWidget(m_iconSelector);

    QListBox *shownListBox = m_iconSelector->availableListBox();
    QListBox *hiddenListBox = m_iconSelector->selectedListBox();

    // An icon is listed by its name; what is kept for it is its hideKey()
    // (the Id of a StatusNotifierItem).  Passive StatusNotifierItems are
    // hidden whatever the list says, but can still be put on it.
    m_dialogKeys.clear();
    for (int list = 0; list < 2; ++list)
    {
        const TrayItemList &items = list == 0 ? m_shownWins : m_hiddenWins;
        for (TrayItemList::const_iterator it = items.begin(); it != items.end(); ++it)
        {
            QString name = (*it)->displayName();
            if (name.isEmpty() || m_dialogKeys.contains(name))
            {
                continue;
            }
            m_dialogKeys.insert(name, (*it)->hideKey());
            QListBox *box = (*it)->isListed(m_hiddenIconList) ? hiddenListBox : shownListBox;
            box->insertItem((*it)->dialogIcon(), name);
        }
    }

    m_settingsDialog->show();
}

void SystemTrayApplet::settingsDialogFinished()
{
    m_settingsDialog->delayedDestruct();
    m_settingsDialog = 0;
    m_iconSelector = 0;
}

void SystemTrayApplet::applySettings()
{
    if (!m_iconSelector)
    {
        return;
    }

    KConfig *conf = config();
    conf->setGroup("HiddenTrayIcons");
    QString name;

    // use the following snippet of code someday to implement ordering
    // of icons
    /*
    m_visibleIconList.clear();
    QListBoxItem* item = m_iconSelector->availableListBox()->firstItem();
    for (; item; item = item->next())
    {
        m_visibleIconList.append(item->text());
    }
    conf->writeEntry("Visible", m_visibleIconList);
    selection.clear();*/

    // The entries of the icons which are not there now stay.
    QStringList hidden;
    for (QStringList::ConstIterator it = m_hiddenIconList.begin(); it != m_hiddenIconList.end(); ++it)
    {
        bool present = false;
        for (int list = 0; list < 2 && !present; ++list)
        {
            const TrayItemList &items = list == 0 ? m_shownWins : m_hiddenWins;
            for (TrayItemList::const_iterator item = items.begin(); item != items.end(); ++item)
            {
                if ((*item)->isListed(QStringList(*it)))
                {
                    present = true;
                    break;
                }
            }
        }
        if (!present)
        {
            hidden.append(*it);
        }
    }

    QListBoxItem* item = m_iconSelector->selectedListBox()->firstItem();
    for (; item; item = item->next())
    {
        QString key = m_dialogKeys.contains(item->text()) ? m_dialogKeys[item->text()] : item->text();
        if (!hidden.contains(key))
        {
            hidden.append(key);
        }
    }
    m_hiddenIconList = hidden;
    conf->writeEntry("Hidden", m_hiddenIconList);
    conf->sync();

    sortHidden();
}

// Each icon among the shown or the hidden ones, as the list of hidden icons
// and its status say; the order of the icons stays.
void SystemTrayApplet::sortHidden()
{
    TrayItemList::iterator it = m_shownWins.begin();
    while (it != m_shownWins.end())
    {
        if (shouldHide(*it))
        {
            m_hiddenWins.append(*it);
            it = m_shownWins.erase(it);
        }
        else
        {
            ++it;
        }
    }

    it = m_hiddenWins.begin();
    while (it != m_hiddenWins.end())
    {
        if (!shouldHide(*it))
        {
            m_shownWins.append(*it);
            it = m_hiddenWins.erase(it);
        }
        else
        {
            ++it;
        }
    }

    showExpandButton(!m_hiddenWins.isEmpty());

    updateVisibleWins();
    layoutTray();
}

void SystemTrayApplet::checkAutoRetract()
{
    if (!m_autoRetractTimer)
    {
        return;
    }

    if (!geometry().contains(mapFromGlobal(QCursor::pos())))
    {
        m_autoRetractTimer->stop();
        if (m_autoRetract)
        {
            m_autoRetract = false;

            if (m_showHidden)
            {
                retract();
            }
        }
        else
        {
            m_autoRetract = true;
            m_autoRetractTimer->start(2000, true);
        }

    }
    else
    {
        m_autoRetract = false;
        m_autoRetractTimer->start(250, true);
    }
}

void SystemTrayApplet::showExpandButton(bool show)
{
    if (show)
    {
        if (!m_expandButton)
        {
            m_expandButton = new SimpleArrowButton(this);
            m_expandButton->installEventFilter(this);
            refreshExpandButton();

            if (orientation() == Vertical)
            {
                m_expandButton->setFixedSize(width() - 4,
                                             m_expandButton->sizeHint()
                                                            .height());
            }
            else
            {
                m_expandButton->setFixedSize(m_expandButton->sizeHint()
                                                            .width(),
                                             height() - 4);
            }
            connect(m_expandButton, SIGNAL(clicked()),
                    this, SLOT(toggleExpanded()));

            m_autoRetractTimer = new QTimer(this);
            connect(m_autoRetractTimer, SIGNAL(timeout()),
                    this, SLOT(checkAutoRetract()));
        }
        else
        {
            refreshExpandButton();
        }

        m_expandButton->show();
    }
    else if (m_expandButton)
    {
        m_expandButton->hide();
    }
}

void SystemTrayApplet::orientationChange( Orientation /*orientation*/ )
{
    refreshExpandButton();
}

void SystemTrayApplet::loadSettings()
{
    // set our defaults
    setFrameStyle(NoFrame);
    m_showFrame = false;

    KConfig *conf = config();
    conf->setGroup("General");

    if (conf->readBoolEntry("ShowPanelFrame", false))
    {
        setFrameStyle(Panel | Sunken);
    }

    conf->setGroup("HiddenTrayIcons");
    m_hiddenIconList = conf->readListEntry("Hidden");

    //Note This setting comes from kdeglobal.
    conf->setGroup("System Tray");
    m_iconSize = QMIN(QMAX(conf->readNumEntry("systrayIconWidth", 22), 8), 128);

    // after the start: the icons as the list says now
    if (m_layout)
    {
        sortHidden();
    }
}

void SystemTrayApplet::systemTrayWindowAdded( WId w )
{
    if (isWinManaged(w))
    {
        // we already manage it
        return;
    }

    embedWindow(w, true);
    updateVisibleWins();
    layoutTray();

    if (m_showFrame && frameStyle() == NoFrame)
    {
        setFrameStyle(Panel|Sunken);
    }
}

void SystemTrayApplet::embedWindow( WId w, bool kde_tray )
{
    TrayEmbed* emb = new TrayEmbed(kde_tray, this);
    emb->setAutoDelete(false);

    if (kde_tray)
    {
        static Atom hack_atom = XInternAtom( qt_xdisplay(), "_KDE_SYSTEM_TRAY_EMBEDDING", False );
        XChangeProperty( qt_xdisplay(), w, hack_atom, hack_atom, 32, PropModeReplace, NULL, 0 );
        emb->embed(w);
        XDeleteProperty( qt_xdisplay(), w, hack_atom );
    }
    else
    {
        emb->embed(w);
    }

    if (emb->embeddedWinId() == 0)  // error embedding
    {
        delete emb;
        return;
    }

    connect(emb, SIGNAL(embeddedWindowDestroyed()), SLOT(updateTrayWindows()));
    addItem(emb);
}

void SystemTrayApplet::addItem(TrayItem *item)
{
    // Whatever size the program asks for: the size of the tray's icons.
    item->setIconSize(m_currentIconSize);

    if (shouldHide(item))
    {
        item->widget()->hide();
        m_hiddenWins.append(item);
        showExpandButton(true);
    }
    else
    {
        item->widget()->hide();
        item->setBackground();
        item->widget()->show();
        m_shownWins.append(item);
    }
}

bool SystemTrayApplet::isWinManaged(WId w)
{
    for (int list = 0; list < 2; ++list)
    {
        const TrayItemList &items = list == 0 ? m_shownWins : m_hiddenWins;
        for (TrayItemList::const_iterator it = items.begin(); it != items.end(); ++it)
        {
            TrayEmbed *emb = dynamic_cast<TrayEmbed *>(*it);
            if (emb && emb->embeddedWinId() == w) // we already manage it
            {
                return true;
            }
        }
    }

    return false;
}

bool SystemTrayApplet::shouldHide(const TrayItem *item) const
{
    return item->isPassive() || item->isListed(m_hiddenIconList);
}

void SystemTrayApplet::updateVisibleWins()
{
    TrayItemList::const_iterator lastEmb = m_hiddenWins.end();
    TrayItemList::const_iterator emb = m_hiddenWins.begin();

    if (m_showHidden)
    {
        for (; emb != lastEmb; ++emb)
        {
            (*emb)->widget()->hide();
            (*emb)->setBackground();
            (*emb)->widget()->show();
        }
    }
    else
    {
        for (; emb != lastEmb; ++emb)
        {
            (*emb)->widget()->hide();
        }
    }
}

void SystemTrayApplet::toggleExpanded()
{
    if (m_showHidden)
    {
        retract();
    }
    else
    {
        expand();
    }
}

void SystemTrayApplet::refreshExpandButton()
{
    if (!m_expandButton)
    {
        return;
    }

    Qt::ArrowType a;

    if (orientation() == Vertical)
        a = m_showHidden ? Qt::DownArrow : Qt::UpArrow;
    else
        a = (m_showHidden ^ kapp->reverseLayout()) ? Qt::RightArrow : Qt::LeftArrow;
    
    m_expandButton->setArrowType(a);
}

void SystemTrayApplet::expand()
{
    m_showHidden = true;
    refreshExpandButton();

    updateVisibleWins();
    layoutTray();

    if (m_autoRetractTimer)
    {
        m_autoRetractTimer->start(250, true);
    }
}

void SystemTrayApplet::retract()
{
    if (m_autoRetractTimer)
    {
        m_autoRetractTimer->stop();
    }

    m_showHidden = false;
    refreshExpandButton();

    updateVisibleWins();
    layoutTray();
}

void SystemTrayApplet::updateTrayWindows()
{
    for (int list = 0; list < 2; ++list)
    {
        TrayItemList &items = list == 0 ? m_shownWins : m_hiddenWins;
        TrayItemList::iterator it = items.begin();
        while (it != items.end())
        {
            TrayEmbed *emb = dynamic_cast<TrayEmbed *>(*it);
            WId wid = emb ? emb->embeddedWinId() : 0;
            if (emb &&
                ((wid == 0) ||
                 (emb->kdeTray() &&
                  !kwin_module->systemTrayWindows().contains(wid))))
            {
                emb->deleteLater();
                it = items.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    showExpandButton(!m_hiddenWins.isEmpty());
    updateVisibleWins();
    layoutTray();
}

// The side of the icons on a panel this thick: the configured size (22
// pixels), smaller only where the panel is too small for it, as KDE's own
// icons of tray size (the volume applet).  Whatever an embedded window or a
// StatusNotifierItem asks for, it gets this.
int SystemTrayApplet::iconSizeFor(int thickness) const
{
    return QMAX(QMIN(m_iconSize, thickness - 2), 8);
}

// Tells the embedded clients the size of their icons and the orientation of
// the tray (_NET_SYSTEM_TRAY_ICON_SIZE and _NET_SYSTEM_TRAY_ORIENTATION of
// the system tray specification; GTK draws its icons at that size).
void SystemTrayApplet::announceIconSize()
{
    if (m_announcedIconSize == m_currentIconSize + (orientation() == Vertical ? 1000 : 0))
    {
        return;
    }
    m_announcedIconSize = m_currentIconSize + (orientation() == Vertical ? 1000 : 0);

    Display *display = qt_xdisplay();
    static Atom iconSizeAtom = XInternAtom(display, "_NET_SYSTEM_TRAY_ICON_SIZE", False);
    static Atom orientationAtom = XInternAtom(display, "_NET_SYSTEM_TRAY_ORIENTATION", False);
    long size = m_currentIconSize;
    long orient = orientation() == Vertical ? 1 : 0;
    XChangeProperty(display, winId(), iconSizeAtom, XA_CARDINAL, 32, PropModeReplace,
                    reinterpret_cast<unsigned char *>(&size), 1);
    XChangeProperty(display, winId(), orientationAtom, XA_CARDINAL, 32, PropModeReplace,
                    reinterpret_cast<unsigned char *>(&orient), 1);
}

bool SystemTrayApplet::eventFilter(QObject* watched, QEvent* e)
{
    if (watched == m_expandButton)
    {
        QPoint p;
        if (e->type() == QEvent::ContextMenu)
        {
            p = static_cast<QContextMenuEvent*>(e)->globalPos();
        }
        else if (e->type() == QEvent::MouseButtonPress)
        {
            QMouseEvent* me = static_cast<QMouseEvent*>(e);
            if (me->button() == Qt::RightButton)
            {
                p = me->globalPos();
            }
        }

        if (!p.isNull())
        {
            QPopupMenu* contextMenu = new QPopupMenu(this);
            contextMenu->insertItem(SmallIcon("configure"), i18n("Configure System Tray..."),
                                    this, SLOT(configure()));

            contextMenu->exec(static_cast<QContextMenuEvent*>(e)->globalPos());

            delete contextMenu;
            return true;
        }
    }

    return false;
}

int SystemTrayApplet::widthForHeight(int h) const
{
    if (orientation() == Qt::Vertical)
    {
        return width();
    }

    int currentHeight = height();
    int minHeight = iconSizeFor(h) + 4;
    if (currentHeight != h && currentHeight != minHeight)
    {
        SystemTrayApplet* me = const_cast<SystemTrayApplet*>(this);
        me->setMinimumSize(0, 0);
        me->setMaximumSize(32767, 32767);
        me->setFixedHeight(h);
    }

    return sizeHint().width(); 
}

int SystemTrayApplet::heightForWidth(int w) const
{
    if (orientation() == Qt::Horizontal)
    {
        return height();
    }

    int currentWidth = width();
    int minSize = iconSizeFor(w) + 4;
    if (currentWidth != w && currentWidth != minSize)
    {
        SystemTrayApplet* me = const_cast<SystemTrayApplet*>(this);
        me->setMinimumSize(0, 0);
        me->setMaximumSize(32767, 32767);
        me->setFixedWidth(w);
    }

    return sizeHint().height(); 
}

void SystemTrayApplet::moveEvent( QMoveEvent* )
{
    setBackground();
}


void SystemTrayApplet::resizeEvent( QResizeEvent* e )
{
    KPanelApplet::resizeEvent(e);
    
    layoutTray();
    // we need to give ourselves a chance to adjust our size before calling this
    QTimer::singleShot(0, this, SIGNAL(updateLayout()));
}

void SystemTrayApplet::layoutTray()
{
    setUpdatesEnabled(false);

    int iconCount = m_shownWins.count();

    if (m_showHidden)
    {
        iconCount += m_hiddenWins.count();
    }

    /* heightWidth = height or width in pixels (depends on orientation())
     * nbrOfLines = number of rows or cols (depends on orientation())
     * line = what line to draw an icon in */
    int i = 0, line, nbrOfLines, heightWidth;
    bool showExpandButton = m_expandButton && m_expandButton->isVisibleTo(this);

    // every icon gets the same square
    int thickness = orientation() == Vertical ? width() : height();
    int iconSize = iconSizeFor(thickness);
    if (iconSize != m_currentIconSize)
    {
        m_currentIconSize = iconSize;
        for (int list = 0; list < 2; ++list)
        {
            const TrayItemList &items = list == 0 ? m_shownWins : m_hiddenWins;
            for (TrayItemList::const_iterator it = items.begin(); it != items.end(); ++it)
            {
                (*it)->setIconSize(iconSize);
            }
        }
    }
    announceIconSize();

    // a smaller margin on panels too small for it
    int margin = thickness >= iconSize + 4 ? 2 : 1;
    delete m_layout;
    m_layout = new QGridLayout(this, 1, 1, margin, 2);

    if (m_expandButton)
    {
        if (orientation() == Vertical)
        {
            m_expandButton->setFixedSize(width() - 4, m_expandButton->sizeHint().height());
        }
        else
        {
            m_expandButton->setFixedSize(m_expandButton->sizeHint().width(), height() - 4);
        }
    }

    // col = column or row, depends on orientation(),
    // the opposite direction of line
    int col = 0;

    // 
    // The margin and spacing specified in the layout implies that:
    // [-- 2 pixels --] [-- first icon --] [-- 2 pixels --] ... [-- 2 pixels --] [-- last icon --] [-- 2 pixels --]
    //
    // So, if we say that iconWidth is the icon width plus the 2 pixels spacing, then the available width for the icons
    // is the widget width minus 2 pixels margin. Forgetting these 2 pixels broke the layout algorithm in KDE <= 3.5.9.
    //
    // This fix makes the workaround in the heightForWidth() and widthForHeight() methods unneeded.
    //

    if (orientation() == Vertical)
    {
        int iconWidth = iconSize + 2; // +2 for the spacing of the layout
        heightWidth = width() - 2 * margin + 2;
        // to avoid nbrOfLines=0 we ensure heightWidth >= iconWidth!
        heightWidth = heightWidth < iconWidth ? iconWidth : heightWidth;
        nbrOfLines = heightWidth / iconWidth;
        
        if (showExpandButton)
        {
            m_layout->addMultiCellWidget(m_expandButton,
                                         0, 0,
                                         0, nbrOfLines - 1,
                                         Qt::AlignHCenter | Qt::AlignVCenter);
            col = 1;
        }

        if (m_showHidden)
        {
            TrayItemList::const_iterator lastEmb = m_hiddenWins.end();
            for (TrayItemList::const_iterator emb = m_hiddenWins.begin();
                 emb != lastEmb; ++emb)
            {
                line = i % nbrOfLines;
                (*emb)->widget()->hide();
                (*emb)->widget()->show();
                m_layout->addWidget((*emb)->widget(), col, line,
                                    Qt::AlignHCenter | Qt::AlignVCenter);

                if (line + 1 == nbrOfLines)
                {
                    ++col;
                }

                ++i;
            }
        }

        TrayItemList::const_iterator lastEmb = m_shownWins.end();
        for (TrayItemList::const_iterator emb = m_shownWins.begin();
             emb != lastEmb; ++emb)
        {
            line = i % nbrOfLines;
            (*emb)->widget()->hide();
            (*emb)->widget()->show();
            m_layout->addWidget((*emb)->widget(), col, line,
                                Qt::AlignHCenter | Qt::AlignVCenter);

            if (line + 1 == nbrOfLines)
            {
                ++col;
            }

            ++i;
        }
    }
    else // horizontal
    {
        int iconHeight = iconSize + 2; // +2 for the spacing of the layout
        heightWidth = height() - 2 * margin + 2;
        heightWidth = heightWidth < iconHeight ? iconHeight : heightWidth; // to avoid nbrOfLines=0
        nbrOfLines = heightWidth / iconHeight;

        if (showExpandButton)
        {
            m_layout->addMultiCellWidget(m_expandButton,
                                         0, nbrOfLines - 1,
                                         0, 0,
                                         Qt::AlignHCenter | Qt::AlignVCenter);
            col = 1;
        }

        if (m_showHidden)
        {
            TrayItemList::const_iterator lastEmb = m_hiddenWins.end();
            for (TrayItemList::const_iterator emb = m_hiddenWins.begin(); emb != lastEmb; ++emb)
            {
                line = i % nbrOfLines;
                (*emb)->widget()->hide();
                (*emb)->widget()->show();
                m_layout->addWidget((*emb)->widget(), line, col,
                                    Qt::AlignHCenter | Qt::AlignVCenter);

                if (line + 1 == nbrOfLines)
                {
                    ++col;
                }

                ++i;
            }
        }

        TrayItemList::const_iterator lastEmb = m_shownWins.end();
        for (TrayItemList::const_iterator emb = m_shownWins.begin();
             emb != lastEmb; ++emb)
        {
            line = i % nbrOfLines;
            (*emb)->widget()->hide();
            (*emb)->widget()->show();
            m_layout->addWidget((*emb)->widget(), line, col,
                                Qt::AlignHCenter | Qt::AlignVCenter);

            if (line + 1 == nbrOfLines)
            {
                ++col;
            }

            ++i;
        }
    }

    setUpdatesEnabled(true);
    updateGeometry();
    setBackground();
}

void SystemTrayApplet::paletteChange(const QPalette & /* oldPalette */)
{
    setBackground();
}

void SystemTrayApplet::setBackground()
{
    TrayItemList::const_iterator lastEmb;
    
    lastEmb = m_shownWins.end();
    for (TrayItemList::const_iterator emb = m_shownWins.begin(); emb != lastEmb; ++emb)
        (*emb)->setBackground();
    
    lastEmb = m_hiddenWins.end();
    for (TrayItemList::const_iterator emb = m_hiddenWins.begin(); emb != lastEmb; ++emb)
        (*emb)->setBackground();
}


TrayEmbed::TrayEmbed( bool kdeTray, QWidget* parent )
    : QXEmbed( parent ), kde_tray( kdeTray ), m_configureCount( 0 )
{
    hide();
    setBackground();
}

QString TrayEmbed::displayName() const
{
    return KWin::windowInfo(embeddedWinId()).name();
}

QString TrayEmbed::hideKey() const
{
    return displayName();
}

bool TrayEmbed::isListed(const QStringList &hidden) const
{
    return hidden.contains(displayName());
}

QPixmap TrayEmbed::dialogIcon() const
{
    return KWin::icon(embeddedWinId(), 22, 22, true);
}

// The embedded window is as large as this widget (QXEmbed), whatever its
// size hints ask for: GTK's icons ask for more than the tray size.
void TrayEmbed::setIconSize(int size)
{
    setFixedSize(size, size);
}

bool TrayEmbed::x11Event(XEvent *e)
{
    // A client asking for another size again and again: QXEmbed answers each
    // request with a ConfigureNotify (the size stays); a client which asks
    // again on each of them would keep both busy.  More than 20 requests a
    // second are left unanswered for the rest of the second.
    if (e->type == ConfigureRequest && e->xconfigurerequest.window == embeddedWinId())
    {
        if (m_configureCount == 0 || m_configureTime.elapsed() > 1000)
        {
            m_configureTime.start();
            m_configureCount = 0;
        }
        if (++m_configureCount > 20)
        {
            return true;
        }
    }
    return QXEmbed::x11Event(e);
}

void TrayEmbed::setBackground()
{
    const QPixmap *pbg = parentWidget()->backgroundPixmap();
    
    if (pbg)
    {
        QPixmap bg(width(), height());
        bg.fill(parentWidget(), pos());
        setPaletteBackgroundPixmap(bg);
        setBackgroundOrigin(WidgetOrigin);
    }
    else
        unsetPalette();
    
    if (!isHidden())
    {
        hide(); 
        show();
    }
    //XClearArea(x11Display(), embeddedWinId(), 0, 0, 0, 0, True);
}

