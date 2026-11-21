/*****************************************************************

Copyright (c) 2000 Matthias Elter <elter@kde.org>
Copyright (c) 2004-2005 Aaron Seigo <aseigo@kde.org>

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

#include <assert.h>
#include <unistd.h>
#include <stdlib.h>

#include <qapplication.h>
#include <qdesktopwidget.h>
#include <qdir.h>
#include <qfile.h>
#include <qfileinfo.h>
#include <qguardedptr.h>
#include <qtimer.h>

#include <kaboutdata.h>
#include <kconfig.h>
#include <kdebug.h>
#include <kdesktopfile.h>
#include <kdirwatch.h>
#include <kglobal.h>
#include <kinstance.h>
#include <kio/netaccess.h>
#include <klocale.h>
#include <kmenubar.h>
#include <kmessagebox.h>
#include <kstandarddirs.h>
#include <kwinmodule.h>
#include <dcopclient.h>
#include <dcopref.h>

#include "container_extension.h"
#include "containerarea.h"
#include "extensionSettings.h"
#include "global.h"
#include "kicker.h"
#include "kickerSettings.h"
#include "panelextension.h"
#include "pluginmanager.h"

#include "extensionmanager.h"

ExtensionManager* ExtensionManager::m_self = 0;

ExtensionManager* ExtensionManager::the()
{
    if (!m_self)
    {
        m_self = new ExtensionManager;
    }

    return m_self;
}

ExtensionManager::ExtensionManager()
    : QObject(0, "ExtensionManager"),
      m_menubarPanel(0),
      m_mainPanel(0),
      m_panelCounter(-1),
      m_mirrorGeneration(0),
      m_extensionsLoaded(false),
      m_mirrorsInitialized(false),
      m_mirrorsWereOn(false),
      m_syncingMirrors(false)
{
    m_mirrorSyncTimer = new QTimer(this, "mirrorSyncTimer");
    connect(m_mirrorSyncTimer, SIGNAL(timeout()), SLOT(syncMirrors()));

    // the config files of the main panel's applets: their settings
    // changed, the mirrors take them over
    m_mirrorWatch = new KDirWatch(this, "mirrorWatch");
    connect(m_mirrorWatch, SIGNAL(dirty(const QString&)), SLOT(scheduleMirrorSync()));
    connect(m_mirrorWatch, SIGNAL(created(const QString&)), SLOT(scheduleMirrorSync()));
    connect(m_mirrorWatch, SIGNAL(deleted(const QString&)), SLOT(scheduleMirrorSync()));
}

ExtensionManager::~ExtensionManager()
{
    // the mirrors go first: they refer to the main panel
    QValueList<int> screens = m_mirrors.keys();
    for (QValueList<int>::const_iterator it = screens.constBegin();
         it != screens.constEnd();
         ++it)
    {
        destroyMirror(*it);
    }

    ExtensionList::iterator itEnd = _containers.end();
    for (ExtensionList::iterator it = _containers.begin(); it != itEnd; ++it)
    {
        delete *it;
    }
    _containers.clear();

    delete m_menubarPanel;
    m_menubarPanel = 0;
    delete m_mainPanel;
    m_mainPanel = 0;

    // only now: the panels deleted above may still call the(), which
    // must not make a new manager
    if (this == m_self)
    {
        m_self = 0;
    }
}

void ExtensionManager::initialize()
{
//    kdDebug(1210) << "ExtensionManager::loadContainerConfig()" << endl;
    KConfig* config = KGlobal::config();
    PluginManager* pm = PluginManager::the();

    // set up the "main" panel
    if (config->hasGroup("Main Panel"))
    {
        config->setGroup("Main Panel");
        if (config->hasKey("DesktopFile"))
        {
            m_mainPanel = pm->createExtensionContainer(config->readPathEntry("DesktopFile"),
                                                       true, config->readPathEntry("ConfigFile"),
                                                       "Main Panel");
        }
    }

    if (!m_mainPanel)
    {
        // fall back to a regular ol' PanelExtension
        m_mainPanel = pm->createExtensionContainer(
                            "childpanelextension.desktop",
                            true,
                            QString(kapp->aboutData()->appName()) + "rc",
                            "Main Panel");
    }

    if (!m_mainPanel)
    {
        KMessageBox::error(0, i18n("The KDE panel (kicker) could not load the main panel "
                                   "due to a problem with your installation. "),
                           i18n("Fatal Error!"));
        exit(1);
    }

    configureMenubar(true);

    Kicker::the()->setMainWidget(m_mainPanel);

    m_mainPanel->readConfig();
    m_mainPanel->show();
    kapp->processEvents();

    // read extension list
    config->setGroup("General");
    QStringList elist = config->readListEntry("Extensions2");

    // now restore the extensions
    QStringList::iterator itEnd = elist.end();
    for (QStringList::iterator it = elist.begin(); it !=  elist.end(); ++it)
    {
        // extension id
        QString extensionId(*it);

        // create a matching applet container
        if (extensionId.find("Extension") == -1)
        {
            continue;
        }

        // is there a config group for this extension?
        if (!config->hasGroup(extensionId))
        {
            continue;
        }

        // set config group
        config->setGroup(extensionId);

        ExtensionContainer* e = pm->createExtensionContainer(config->readPathEntry("DesktopFile"),
                                                             true, // is startup
                                                             config->readPathEntry("ConfigFile"),
                                                             extensionId);
        if (e)
        {
            addContainer(e);
            e->readConfig();
            e->show();
            kapp->processEvents();
        }
    }

    pm->clearUntrustedLists();
    connect(Kicker::the(), SIGNAL(configurationChanged()), SLOT(configurationChanged()));

    // the copies of the main panel on the other screens: made once the
    // main panel has its applets (they are loaded from the event loop)
    migrateManualMirrors();
    removeStaleMirrorFiles();
    m_mirrorsWereOn = KickerSettings::mainPanelOnEveryScreen();
    m_extensionsLoaded = true;
    QTimer::singleShot(0, this, SLOT(syncMirrors()));

    DCOPRef r( "ksmserver", "ksmserver" );
    r.send( "resumeStartup", QCString( "kicker" ));
}

void ExtensionManager::configureMenubar(bool duringInit)
{
    KConfig menuConfig( "kdesktoprc", true );
    if( KConfigGroup( &menuConfig, "KDE" ).readBoolEntry("macStyle", false)
        || KConfigGroup( &menuConfig, "Menubar" ).readBoolEntry( "ShowMenubar", false ))
    {
        if (KGlobal::dirs()->findResource("applets", "menuapplet.desktop").isEmpty() ||
            m_menubarPanel)
        {
            return;
        }

        if (duringInit)
        {
            AppletInfo menubarInfo("menuapplet.desktop", QString::null, AppletInfo::Applet);
            if (PluginManager::the()->hasInstance(menubarInfo))
            {
                // it's already there, in the main panel!
                return;
            }
            migrateMenubar();
        }

        AppletInfo info("childpanelextension.desktop",
                        "kicker_menubarpanelrc",
                        AppletInfo::Extension);
        KPanelExtension* menubar = new MenubarExtension(info);
        m_menubarPanel = new ExtensionContainer(menubar, info, "Menubar Panel");
        m_menubarPanel->setPanelOrder(-1);
        m_menubarPanel->readConfig();
        m_menubarPanel->setPosition(KPanelExtension::Top);
        m_menubarPanel->setXineramaScreen(XineramaAllScreens);
        m_menubarPanel->setHideButtons(false, false);

        // this takes care of resizing the panel so it shows with the right height
        updateMenubar();

        m_menubarPanel->show();
        connect(kapp, SIGNAL(kdisplayFontChanged()), SLOT(updateMenubar()));
    }
    else if (m_menubarPanel)
    {
        int screen = m_menubarPanel->xineramaScreen();
        delete m_menubarPanel;
        m_menubarPanel = 0;

        emit desktopIconsAreaChanged(desktopIconsArea(screen), screen);
    }
}

void ExtensionManager::migrateMenubar()
{
    // lame, lame, lame.
    // the menubar applet was just plunked into kicker and not much
    // thought was put into how it should be used. great idea, but no
    // integration. >:-(
    // so now we have to check to see if we HAVE another extension that
    // will have a menubar in it, and if so, abort creating one of our
    // own.
    //
    // the reason i didn't do this as a kconfig_update script is that
    // most people don't use this feature, so no reason to penalize
    // everyone, and moreover the user may added this to their main
    // panel, meaning kickerrc itself would have to be vastly modified
    // with lots of complications. not work it IMHO.

    KConfig* config = KGlobal::config();
    config->setGroup("General");

    if (config->readBoolEntry("CheckedForMenubar", false))
    {
        return;
    }

    if (!locate("config", "kicker_menubarpanelrc").isEmpty())
    {
        // don't overwrite/override something that's already there
        return;
    }

    QStringList elist = config->readListEntry("Extensions2");
    QStringList::iterator itEnd = elist.end();
    for (QStringList::iterator it = elist.begin(); it !=  elist.end(); ++it)
    {
        QString extensionId(*it);

        if (extensionId.find("Extension") == -1)
        {
            continue;
        }

        // is there a config group for this extension?
        if (!config->hasGroup(extensionId))
        {
            continue;
        }

        config->setGroup(extensionId);
        QString extension = config->readPathEntry("ConfigFile");
        KConfig extensionConfig(locate("config", extension));
        extensionConfig.setGroup("General");

        if (extensionConfig.hasKey("Applets2"))
        {
            QStringList containers = extensionConfig.readListEntry("Applets2");
            QStringList::iterator cit = containers.begin();
            QStringList::iterator citEnd = containers.end();
            for (; cit != citEnd; ++cit)
            {
                QString appletId(*cit);

                // is there a config group for this applet?
                if (!extensionConfig.hasGroup(appletId))
                {
                    continue;
                }

                KConfigGroup group(&extensionConfig, appletId.latin1());
                QString appletType = appletId.left(appletId.findRev('_'));

                if (appletType == "Applet")
                {
                    QString appletFile = group.readPathEntry("DesktopFile");
                    if (appletFile.find("menuapplet.desktop") != -1)
                    {
                        QString menubarConfig = locate("config", extension);
                        KIO::NetAccess::copy(menubarConfig,
                                             locateLocal("config",
                                             "kicker_menubarpanelrc"), 0);
                        elist.remove(it);
                        config->setGroup("General");
                        config->writeEntry("Extensions2", elist);
                        config->writeEntry("CheckedForMenubar", true);
                        config->sync();
                        return;
                    }
                }
            }
        }
    }

    config->setGroup("General");
    config->writeEntry("CheckedForMenubar", true);
}

void ExtensionManager::saveContainerConfig()
{
//    kdDebug(1210) << "ExtensionManager::saveContainerConfig()" << endl;

    KConfig *config = KGlobal::config();

    // build the extension list
    QStringList elist;
    ExtensionList::iterator itEnd = _containers.end();
    for (ExtensionList::iterator it = _containers.begin(); it != itEnd; ++it)
    {
        elist.append((*it)->extensionId());
    }

    // write extension list
    config->setGroup("General");
    config->writeEntry("Extensions2", elist);

    config->sync();
}

void ExtensionManager::configurationChanged()
{
    if (m_mainPanel)
    {
        m_mainPanel->readConfig();
    }

    if (m_menubarPanel)
    {
        m_menubarPanel->readConfig();
    }

    ExtensionList::iterator itEnd = _containers.end();
    for (ExtensionList::iterator it = _containers.begin(); it != itEnd; ++it)
    {
        (*it)->readConfig();
    }

    // the screens may have changed (Kicker::slotDesktopResized() ends up
    // here): the mirrors of the screens that are gone are hidden now and
    // deleted from the event loop, not while this signal is delivered
    hideStaleMirrors();
    for (MirrorMap::iterator it = m_mirrors.begin(); it != m_mirrors.end(); ++it)
    {
        if ((*it).panel->isVisible())
        {
            (*it).panel->readConfig();
        }
    }

    if (m_extensionsLoaded)
    {
        m_mirrorSyncTimer->start(0, true);
    }
}

void ExtensionManager::updateMenubar()
{
    if (!m_menubarPanel)
    {
        return;
    }

    //kdDebug(0) << "ExtensionManager::updateMenubar()" << endl;
    // we need to make sure the panel is tall enough to accomodate the
    // menubar! an easy way to find out the height of a menu: make one ;)
    KMenuBar tmpmenu;
    tmpmenu.insertItem("KDE Rocks!");
    m_menubarPanel->setSize(KPanelExtension::SizeCustom,
                            tmpmenu.sizeHint().height());
    m_menubarPanel->writeConfig();

    emit desktopIconsAreaChanged(desktopIconsArea(m_menubarPanel->xineramaScreen()),
                                 m_menubarPanel->xineramaScreen());
}

bool ExtensionManager::isMainPanel(const QWidget* panel) const
{
    return m_mainPanel && m_mainPanel == panel;
}

bool ExtensionManager::isMenuBar(const QWidget* panel) const
{
    return m_menubarPanel == panel;
}

void ExtensionManager::addExtension( const QString& desktopFile )
{
    PluginManager* pm = PluginManager::the();
    ExtensionContainer *e = pm->createExtensionContainer(desktopFile,
                                                         false, // is not startup
                                                         QString::null, // no config
                                                         uniqueId());
    

    kdDebug(1210) << "ExtensionManager::addExtension" << endl;

    if (e)
    {
        e->readConfig();
        // as a new panel, the position will be set to the preferred position
        // we just need to make sure this works with the rest of the panel layout
        e->setPosition(initialPanelPosition(e->position()));
        kdDebug(1210)<<"after e->readConfig(): pos="<<e->position()<<endl;
        addContainer(e);
        e->show();
        e->writeConfig();
        saveContainerConfig();
    }
}

void ExtensionManager::addContainer(ExtensionContainer* e)
{
    if (!e)
    {
        return;
    }

    _containers.append(e);

    connect(e, SIGNAL(removeme(ExtensionContainer*)),
            this, SLOT(removeContainer(ExtensionContainer*)));

    emit desktopIconsAreaChanged(desktopIconsArea(e->xineramaScreen()),
                                 e->xineramaScreen());
}

void ExtensionManager::removeContainer(ExtensionContainer* e)
{
    if (!e)
    {
        return;
    }

    e->removeSessionConfigFile();
    _containers.remove(e);
    e->deleteLater(); // Wait till we return to the main event loop
    saveContainerConfig();

    emit desktopIconsAreaChanged(desktopIconsArea(e->xineramaScreen()),
                                 e->xineramaScreen());
}

void ExtensionManager::removeAllContainers()
{
    while (!_containers.isEmpty())
    {
        ExtensionContainer* e = _containers.first();
        _containers.remove(e);
        e->deleteLater(); // Wait till we return to the main event loop
    }

    saveContainerConfig();
}

QString ExtensionManager::uniqueId()
{
    QString idBase = "Extension_%1";
    QString newId;
    int i = 0;
    bool unique = false;

    while (!unique)
    {
        i++;
        newId = idBase.arg(i);

        unique = true;
        ExtensionList::iterator itEnd = _containers.end();
        for (ExtensionList::iterator it = _containers.begin(); it != itEnd; ++it)
        {
            if ((*it)->extensionId() == newId)
            {
                unique = false;
                break;
            }
        }
    }

    return newId;
}

KPanelExtension::Position ExtensionManager::initialPanelPosition(KPanelExtension::Position preferred)
{
    // Guess a good position
    bool positions[KPanelExtension::Bottom+1];
    for( int i = 0; i <= (int) KPanelExtension::Bottom; ++i )
    {
       positions[i] = true;
    }

    ExtensionList::iterator itEnd = _containers.end();
    for (ExtensionList::iterator it = _containers.begin(); it != itEnd; ++it)
    {
       positions[(int) (*it)->position()] = false;
    }

    KPanelExtension::Position pos = preferred;
    if (positions[(int)pos])
       return pos;

    pos = (KPanelExtension::Position) (pos ^ 1);
    if (positions[(int)pos])
       return pos;

    pos = (KPanelExtension::Position) (pos ^ 3);
    if (positions[(int)pos])
       return pos;

    pos = (KPanelExtension::Position) (pos ^ 1);
    if (positions[(int)pos])
       return pos;

    return preferred;
}

bool ExtensionManager::shouldExclude(int XineramaScreen,
                                     const ExtensionContainer* extension,
                                     const ExtensionContainer* exclude) const
{
    // Rules of Exclusion:
    // 0. Exclude ourselves
    // 1. Exclude panels not on our Xinerama screen
    // 2. Exclude panels on the same side of the screen as ourselves that are above us
    // 3. Exclude panels on the opposite side of the screen. Breaks down if the user
    //    dabbles in insane layouts where a top/bottom or left/right pair overlap?
    // 4. Exclude panels on adjacent sides of the screen that do not overlap with us

    if (exclude->winId() == extension->winId())
    {
        // Rule 0 Exclusion
        return true;
    }

    if (extension->xineramaScreen()!= XineramaAllScreens &&
        exclude->xineramaScreen() != XineramaAllScreens &&
        exclude->xineramaScreen() != XineramaScreen)
    {
        // Rule 1 exclusion
        return true;
    }

    if (!exclude->reserveStrut())
    {
        return true;
    }

    bool lowerInStack = extension->panelOrder() < exclude->panelOrder();
    if (exclude->position() == extension->position())
    {
        // Rule 2 Exclusion
        if (extension->position() == KPanelExtension::Bottom &&
            exclude->geometry().bottom() == extension->geometry().bottom() &&
            !exclude->geometry().intersects(extension->geometry()))
        {
            return false;
        }
        else if (extension->position() == KPanelExtension::Top &&
                 exclude->geometry().top() == extension->geometry().top() &&
                 !exclude->geometry().intersects(extension->geometry()))
        {
            return false;
        }
        else if (extension->position() == KPanelExtension::Left &&
                 exclude->geometry().left() == extension->geometry().left() &&
                 !exclude->geometry().intersects(extension->geometry()))
        {
            return false;
        }
        else if (extension->position() == KPanelExtension::Right &&
                 exclude->geometry().right() == extension->geometry().right() &&
                 !exclude->geometry().intersects(extension->geometry()))
        {
            return false;
        }

        return lowerInStack;
    }

    // Rule 3 exclusion
    if (exclude->orientation() == extension->orientation())
    {
        // on the opposite side of the screen from us.
        return true;
    }

    // Rule 4 exclusion
    if (extension->position() == KPanelExtension::Bottom)
    {
        if (exclude->geometry().bottom() > extension->geometry().top())
        {
            return lowerInStack;
        }
    }
    else if (extension->position() == KPanelExtension::Top)
    {
        if (exclude->geometry().top() < extension->geometry().bottom())
        {
            return lowerInStack;
        }
    }
    else if (extension->position() == KPanelExtension::Left)
    {
        if (exclude->geometry().left() < extension->geometry().right())
        {
            return lowerInStack;
        }
    }
    else /* if (extension->position() == KPanelExtension::Right) */
    {
        if (exclude->geometry().right() > extension->geometry().left())
        {
            return lowerInStack;
        }
    }

    return true;
}

QRect ExtensionManager::workArea(int XineramaScreen, const ExtensionContainer* extension)
{
    if (!extension)
    {
        return Kicker::the()->kwinModule()->workArea(XineramaScreen);
    }

    QValueList<WId> list;

    ExtensionList::iterator itEnd = _containers.end();
    ExtensionList::iterator it = _containers.begin();

    // If the hide mode is Manual, exclude the struts of
    // panels below this one in the list. Else exclude the
    // struts of all panels.
    if (extension->reserveStrut() &&
        extension != m_menubarPanel &&
        extension->hideMode() == ExtensionContainer::ManualHide)
    {
        if (m_mainPanel && shouldExclude(XineramaScreen, extension, m_mainPanel))
        {
            list.append(m_mainPanel->winId());
        }

        for (; it != itEnd; ++it)
        {
            if (shouldExclude(XineramaScreen, extension, *it))
            {
                list.append((*it)->winId());
            }
        }

        for (MirrorMap::const_iterator mit = m_mirrors.constBegin();
             mit != m_mirrors.constEnd();
             ++mit)
        {
            if (shouldExclude(XineramaScreen, extension, (*mit).panel))
            {
                list.append((*mit).panel->winId());
            }
        }
    }
    else
    {
        // auto hide panel? just ignore everything else for now.
        if (extension == m_menubarPanel)
        {
            list.append(m_menubarPanel->winId());
        }

        if (m_mainPanel)
        {
            list.append(m_mainPanel->winId());
        }

        for (; it != itEnd; ++it)
        {
            list.append((*it)->winId());
        }

        for (MirrorMap::const_iterator mit = m_mirrors.constBegin();
             mit != m_mirrors.constEnd();
             ++mit)
        {
            list.append((*mit).panel->winId());
        }
    }

    QRect workArea;
    if (XineramaScreen == XineramaAllScreens)
    {
         /* special value for all screens */
         workArea = Kicker::the()->kwinModule()->workArea(list);
    }
    else
    {
        workArea = Kicker::the()->kwinModule()->workArea(list, XineramaScreen)
                   .intersect(QApplication::desktop()->screenGeometry(XineramaScreen));
    }

    return workArea;
}

int ExtensionManager::nextPanelOrder()
{
    ++m_panelCounter;
    return m_panelCounter;
}

void ExtensionManager::reduceArea(QRect &area, const ExtensionContainer *extension) const
{
    if (!extension ||
        extension->hideMode() == ExtensionContainer::AutomaticHide ||
        !extension->reserveStrut())
    {
        return;
    }

    QRect geom = extension->initialGeometry(extension->position(), extension->alignment(),
                                            extension->xineramaScreen());

    // reduce given area (QRect) to the space not covered by the given extension
    // As simplification: the length of the extension is not taken into account
    // which means that even a small extension e.g. on the left side of the desktop
    // will remove the available area with its with

    switch (extension->position())
    {
        case KPanelExtension::Left:
        {
            area.setLeft(QMAX(area.left(), geom.right()));
            break;
        }
        case KPanelExtension::Right:
        {
            area.setRight(QMIN(area.right(), geom.left()));
            break;
        }
        case KPanelExtension::Top:
        {
            area.setTop(QMAX(area.top(), geom.bottom()));
            break;
        }
        case KPanelExtension::Bottom:
        {
            area.setBottom(QMIN(area.bottom(), geom.top()));
            break;
        }
        default: ;  // ignore KPanelExtension::Floating ... at least for now
    }
}

QRect ExtensionManager::desktopIconsArea(int screen) const
{
    // This is pretty broken, mixes Xinerama and non-Xinerama multihead
    // and generally doesn't seem to be required anyway => ignore screen.
//    QRect area = QApplication::desktop()->screenGeometry(screen);
    QRect area = QApplication::desktop()->geometry();

    reduceArea(area, m_mainPanel);
    reduceArea(area, m_menubarPanel);

    for (ExtensionList::const_iterator it = _containers.constBegin();
         it != _containers.constEnd();
         ++it)
    {
        reduceArea(area, (*it));
    }

    // not the mirrors: this area is the whole desktop's, a mirror would
    // only cut it further where the screens differ in size

    kdDebug(1210) << "ExtensionManager::desktopIconsArea() = " << area
                  << " screen = " << screen << endl;
    return area;
}

void ExtensionManager::extensionSizeChanged(ExtensionContainer *extension)
{
  // we have to recalc the available space for desktop icons
  if (!extension)
  {
      return;
  }

  emit desktopIconsAreaChanged(desktopIconsArea(extension->xineramaScreen()),
                               extension->xineramaScreen());
}

//
// The main panel on every screen
//
// A mirror is a panel of its own (an ExtensionContainer with a
// PanelExtension) made from files generated out of the main panel's:
// its panel settings, its Applets2 list and the groups of the
// containers, and a copy of every applet's config file.  It is not in
// _containers, so it is not in Extensions2, in the Remove Panel menu or
// in the Control Center.  Its own config is never written back: moving,
// removing and configuring, done on a mirror, are done on the main panel
// (see ContainerArea::setMirrorSource()), and when the main panel's files
// change, the mirrors are made again from them.
//

static const QStringList& mirroredPanelKeys(const ExtensionSettings& settings)
{
    static QStringList keys;
    if (keys.isEmpty())
    {
        // every ExtensionSettings entry but these two
        KConfigSkeletonItem::List items = settings.items();
        for (KConfigSkeletonItem::List::const_iterator it = items.constBegin();
             it != items.constEnd();
             ++it)
        {
            QString key = (*it)->key();
            if (key != "IExist" && key != "XineramaScreen")
            {
                keys.append(key);
            }
        }
    }

    return keys;
}

static QString mirrorEntriesText(const QMap<QString, QString>& entries,
                                 bool layoutHints = true)
{
    QString text;
    for (QMap<QString, QString>::const_iterator it = entries.constBegin();
         it != entries.constEnd();
         ++it)
    {
        // the size hints are written whenever the main panel saves its
        // layout; the mirror's applets work theirs out anyway
        if (!layoutHints &&
            (it.key() == "WidthForHeightHint" || it.key() == "HeightForWidthHint"))
        {
            continue;
        }

        text += it.key() + '=' + it.data() + '\n';
    }

    return text;
}

// The entries of a config file as an application sees them, with those
// of the system's files below the user's (KConfig::entryMap() leaves the
// system's out: a default panel layout the user never touched).
static QMap<QString, QMap<QString, QString> > readConfigCascade(const QString& name)
{
    QMap<QString, QMap<QString, QString> > groups;
    QStringList files = KGlobal::dirs()->findAllResources("config", name);
    // the user's first: read the other way round, the later wins
    for (QStringList::const_iterator it = files.fromLast(); it != files.end(); --it)
    {
        KConfig file(*it, true, false, "");
        QStringList names = file.groupList();
        for (QStringList::const_iterator git = names.constBegin(); git != names.constEnd(); ++git)
        {
            QMap<QString, QString> entries = file.entryMap(*git);
            for (QMap<QString, QString>::const_iterator eit = entries.constBegin();
                 eit != entries.constEnd();
                 ++eit)
            {
                groups[*git][eit.key()] = eit.data();
            }
        }

        if (it == files.begin())
        {
            break;
        }
    }

    return groups;
}

QString ExtensionManager::mirrorFilePrefix()
{
    // kickermirror_, or kicker-screen-1mirror_ with multihead
    return QString::fromLatin1(KGlobal::instance()->instanceName()) + "mirror_";
}

bool ExtensionManager::isMirrorConfigFile(const QString& configFile)
{
    return configFile.startsWith(mirrorFilePrefix());
}

bool ExtensionManager::isMirror(const QWidget* panel) const
{
    const ExtensionContainer* e = dynamic_cast<const ExtensionContainer*>(panel);
    return e && isMirrorConfigFile(e->info().configFile());
}

PanelExtension* ExtensionManager::mainPanelExtension() const
{
    return m_mainPanel ? dynamic_cast<PanelExtension*>(m_mainPanel->extension()) : 0;
}

ContainerArea* ExtensionManager::mainPanelArea() const
{
    PanelExtension* panel = mainPanelExtension();
    return panel ? panel->containerArea() : 0;
}

QPoint ExtensionManager::insertionPointFor(const QWidget* origin,
                                           const QPoint& globalPos) const
{
    if (!origin || !m_mainPanel || globalPos.isNull())
    {
        return globalPos;
    }

    const QWidget* mirror = origin->topLevelWidget();
    if (!isMirror(mirror))
    {
        return globalPos;
    }

    // the same place along the main panel (the screens and so the
    // panels may differ in length)
    QRect from = mirror->geometry();
    QRect to = m_mainPanel->geometry();
    if (from.width() <= 0 || from.height() <= 0 || to.width() <= 0 || to.height() <= 0)
    {
        return to.center();
    }

    QPoint p = globalPos - from.topLeft();
    int x;
    int y;
    if (m_mainPanel->orientation() == Qt::Horizontal)
    {
        x = p.x() * to.width() / from.width();
        y = p.y();
    }
    else
    {
        x = p.x();
        y = p.y() * to.height() / from.height();
    }

    x = QMIN(QMAX(x, 0), to.width() - 1);
    y = QMIN(QMAX(y, 0), to.height() - 1);
    return to.topLeft() + QPoint(x, y);
}

void ExtensionManager::scheduleMirrorSync()
{
    if (!m_extensionsLoaded)
    {
        // initialize() does the first one
        return;
    }

    // compresses the bursts: a drop or a removal writes several times
    m_mirrorSyncTimer->start(250, true);
}

void ExtensionManager::migrateManualMirrors()
{
    // Panels that somebody put on the other screens by hand before there
    // were mirrors: keep them, and leave the mirrors off.  The option
    // turned on in the Control Center asks about them.
    KConfig* config = KGlobal::config();
    config->setGroup("General");
    if (config->readBoolEntry("CheckedForManualMirrors", false))
    {
        return;
    }

    int mainScreen = m_mainPanel ? m_mainPanel->xineramaScreen()
                                 : QApplication::desktop()->primaryScreen();
    bool manualPanels = false;
    for (ExtensionList::const_iterator it = _containers.constBegin();
         it != _containers.constEnd() && !manualPanels;
         ++it)
    {
        if ((*it)->info().library() != "childpanel_panelextension")
        {
            continue;
        }

        // the screen as written, not as it is now: the other screen may
        // be away (a laptop out of its dock)
        KConfig panelConfig((*it)->info().configFile(), true, false);
        panelConfig.setGroup("General");
        int screen = panelConfig.readNumEntry("XineramaScreen", mainScreen);
        manualPanels = screen != mainScreen && screen != XineramaAllScreens;
    }

    if (manualPanels && KickerSettings::mainPanelOnEveryScreen())
    {
        KickerSettings::setMainPanelOnEveryScreen(false);
        KickerSettings::self()->writeConfig();
    }

    config->setGroup("General");
    config->writeEntry("CheckedForManualMirrors", true);
    config->sync();
}

void ExtensionManager::removeStaleMirrorFiles()
{
    // left over by a kicker that crashed, or written after the end of
    // their mirror by an applet that went late
    QDir dir(KGlobal::dirs()->saveLocation("config"), mirrorFilePrefix() + "*",
             QDir::Unsorted, QDir::Files | QDir::Hidden);
    QStringList files = dir.entryList();
    for (QStringList::const_iterator it = files.constBegin(); it != files.constEnd(); ++it)
    {
        dir.remove(*it);
    }
}

QString ExtensionManager::mirroredDesktopFile(const QString& desktopFile) const
{
    // The applets that can exist only once stay on the main panel alone:
    // the system tray (an X screen has one), the battery, ...  So do those
    // whose desktop file asks for it, the ones that would do twice what
    // must be done once.  This is the place to put in their stead an
    // applet that only shows the main panel's one (a mirrored system
    // tray, say): the desktop file returned here is loaded in the mirror.
    KDesktopFile df(desktopFile, true, "applets");
    if (df.readBoolEntry("X-KDE-UniqueApplet", false) ||
        df.readBoolEntry("X-KDE-MainPanelOnly", false))
    {
        return QString::null;
    }

    return desktopFile;
}

bool ExtensionManager::readMirrorSource(MirrorSource& source) const
{
    QString mainConfigFile = m_mainPanel->info().configFile();
    if (mainConfigFile.isEmpty())
    {
        return false;
    }

    // what the main panel wrote, from the disk: the main panel's
    // ContainerArea writes through a KConfig object of its own
    KConfig config(mainConfigFile, true, false);
    KConfigGroup general(&config, "General");

    // the panel settings, all of them as the main panel sees them: the
    // defaults of its file (a kickerrc of the system, say) are not those
    // of the generated file
    const QStringList& keys = mirroredPanelKeys(m_mainPanel->settings());
    for (QStringList::const_iterator it = keys.constBegin(); it != keys.constEnd(); ++it)
    {
        if (general.hasKey(*it))
        {
            source.panel[*it] = general.readEntry(*it);
        }
    }

    QStringList applets = general.readListEntry("Applets2");
    MirrorSource::Groups mainGroups = readConfigCascade(mainConfigFile);
    QString content;
    for (QStringList::const_iterator it = applets.constBegin(); it != applets.constEnd(); ++it)
    {
        QString appletId = *it;
        if (!mainGroups.contains(appletId))
        {
            continue;
        }

        MirrorSource::Entries entries = mainGroups[appletId];
        QString appletType = appletId.left(appletId.findRev('_'));
        QString configFile;
        if (appletType == "Applet")
        {
            QString desktopFile = mirroredDesktopFile(entries["DesktopFile"]);
            if (desktopFile.isEmpty())
            {
                continue;
            }

            entries["DesktopFile"] = desktopFile;
            configFile = entries["ConfigFile"];
        }

        source.applets.append(appletId);
        source.containers[appletId] = entries;
        content += '[' + appletId + "]\n" + mirrorEntriesText(entries, false);

        if (appletType != "Applet")
        {
            continue;
        }

        // an applet of a mirror always gets a config file of the mirror,
        // also when the main panel's has none (AppletInfo would make up
        // a name outside the mirror's files)
        MirrorSource::Groups groups;
        if (!configFile.isEmpty())
        {
            groups = readConfigCascade(configFile);
            for (MirrorSource::Groups::const_iterator git = groups.constBegin();
                 git != groups.constEnd();
                 ++git)
            {
                // what an applet records as it is used, not settings: the
                // quick launcher's statistics change with every program
                // started, the mirrors would be made anew every time
                if (git.key() == "PopularityData")
                {
                    continue;
                }

                content += '[' + appletId + '/' + git.key() + "]\n" + mirrorEntriesText(*git);
            }

            source.watchedFiles.append(locateLocal("config", configFile));
        }

        // the pager of a mirror shows its own screen
        if (QFileInfo(entries["DesktopFile"]).fileName() == "minipagerapplet.desktop")
        {
            groups["General"]["CurrentScreenOnly"] = "true";
        }

        source.configs[appletId] = groups;
    }

    source.panelSignature = mirrorEntriesText(source.panel);
    source.contentSignature = source.applets.join(",") + '\n' + content;
    return true;
}

ExtensionContainer* ExtensionManager::manualPanelAt(int screen,
                                                    KPanelExtension::Position position) const
{
    for (ExtensionList::const_iterator it = _containers.constBegin();
         it != _containers.constEnd();
         ++it)
    {
        ExtensionContainer* e = *it;
        if (e->info().library() == "childpanel_panelextension" &&
            e->position() == position &&
            (e->xineramaScreen() == screen || e->xineramaScreen() == XineramaAllScreens))
        {
            return e;
        }
    }

    return 0;
}

QValueList<int> ExtensionManager::mirrorScreens(bool askToReplace)
{
    QValueList<int> screens;
    QDesktopWidget* desktop = QApplication::desktop();
    if (!mainPanelArea() || !desktop->isVirtualDesktop() || desktop->numScreens() < 2)
    {
        return screens;
    }

    int mainScreen = m_mainPanel->xineramaScreen();
    if (mainScreen == XineramaAllScreens)
    {
        // it is on every screen already
        return screens;
    }

    if (mainScreen < 0)
    {
        mainScreen = desktop->primaryScreen();
    }

    KPanelExtension::Position position = m_mainPanel->position();
    for (int screen = 0; screen < desktop->numScreens(); ++screen)
    {
        if (screen == mainScreen)
        {
            continue;
        }

        // a panel put there by hand wins, unless the option was just
        // turned on and the user wants it replaced
        bool keep = false;
        while (ExtensionContainer* manual = manualPanelAt(screen, position))
        {
            QGuardedPtr<ExtensionContainer> guard = manual;
            if (!askToReplace ||
                KMessageBox::questionYesNo(0,
                    i18n("Screen %1 already has a panel at the edge where the main "
                         "panel is. Do you want to remove it, with its applets and "
                         "buttons, and show a copy of the main panel there "
                         "instead?").arg(screen + 1),
                    i18n("Main Panel on Every Screen"),
                    KGuiItem(i18n("&Replace"), "editdelete"),
                    KGuiItem(i18n("&Keep"))) != KMessageBox::Yes)
            {
                keep = true;
                break;
            }

            // the dialog ran the event loop: still there?
            if (guard && _containers.contains(manual))
            {
                removeContainer(manual);
            }
        }

        if (!keep)
        {
            screens.append(screen);
        }
    }

    return screens;
}

void ExtensionManager::syncMirrors()
{
    if (!m_mainPanel || !m_extensionsLoaded)
    {
        return;
    }

    // A mirror may be busy: a menu or a dialog of it runs a nested event
    // loop, and Qt 3 also runs the deleteLater()s in nested loops.  The
    // mirrors are deleted from the main loop only.
    // Nor while a mirror slides in or out: that runs processEvents() from
    // its own code (at loop level 1), and this timer may fire in there.
    bool busy = m_syncingMirrors || qApp->loopLevel() > 1 || QApplication::activePopupWidget();
    for (MirrorMap::const_iterator it = m_mirrors.constBegin();
         it != m_mirrors.constEnd() && !busy;
         ++it)
    {
        busy = (*it).panel->isBusy();
    }

    if (busy)
    {
        m_mirrorSyncTimer->start(500, true);
        return;
    }

    m_syncingMirrors = true;

    bool on = KickerSettings::mainPanelOnEveryScreen();
    bool askToReplace = on && !m_mirrorsWereOn && m_mirrorsInitialized;
    m_mirrorsWereOn = on;

    // asks first (a nested event loop), then reads the main panel
    QValueList<int> screens;
    if (on)
    {
        screens = mirrorScreens(askToReplace);
    }

    MirrorSource source;
    if (!screens.isEmpty() && !readMirrorSource(source))
    {
        screens.clear();
    }

    QValueList<int> changed;
    QValueList<int> existing = m_mirrors.keys();
    for (QValueList<int>::const_iterator it = existing.constBegin();
         it != existing.constEnd();
         ++it)
    {
        if (!screens.contains(*it))
        {
            destroyMirror(*it);
            changed.append(*it);
        }
    }

    for (QValueList<int>::const_iterator it = screens.constBegin();
         it != screens.constEnd();
         ++it)
    {
        int screen = *it;
        MirrorMap::iterator mit = m_mirrors.find(screen);
        if (mit == m_mirrors.end())
        {
            createMirror(screen, source);
        }
        else if ((*mit).contentSignature != source.contentSignature)
        {
            // new applets or new settings of them: all made again
            destroyMirror(screen);
            createMirror(screen, source);
        }
        else if ((*mit).panelSignature != source.panelSignature)
        {
            updateMirrorPanel(*mit, screen, source);
        }
        else if (!(*mit).panel->isVisible())
        {
            // hidden by hideStaleMirrors() while its screen was away,
            // and the screen came back before this ran
            (*mit).panel->readConfig();
            (*mit).panel->show();
        }
        else
        {
            continue;
        }

        changed.append(screen);
    }

    if (source.watchedFiles != m_mirrorWatchedFiles)
    {
        for (QStringList::const_iterator it = m_mirrorWatchedFiles.constBegin();
             it != m_mirrorWatchedFiles.constEnd();
             ++it)
        {
            m_mirrorWatch->removeFile(*it);
        }

        m_mirrorWatchedFiles = source.watchedFiles;
        for (QStringList::const_iterator it = m_mirrorWatchedFiles.constBegin();
             it != m_mirrorWatchedFiles.constEnd();
             ++it)
        {
            m_mirrorWatch->addFile(*it);
        }
    }

    m_mirrorsInitialized = true;
    m_syncingMirrors = false;

    for (QValueList<int>::const_iterator it = changed.constBegin();
         it != changed.constEnd();
         ++it)
    {
        emit desktopIconsAreaChanged(desktopIconsArea(*it), *it);
    }

    if (!changed.isEmpty())
    {
        // the taskbars of the main panel and of the mirrors show the
        // windows of their screens while there are mirrors (TaskBar::
        // onlyCurrentScreenWanted()); the signal makes them look again
        kapp->dcopClient()->emitDCOPSignal("kdeTaskBarConfigChanged()", QByteArray());
    }
}

void ExtensionManager::createMirror(int screen, const MirrorSource& source)
{
    // a new name every time: an applet of the mirror before may write its
    // file late, it does not get into this one
    ++m_mirrorGeneration;
    QString prefix = mirrorFilePrefix() + QString("%1_%2").arg(screen).arg(m_mirrorGeneration);

    Mirror mirror;
    mirror.configFile = prefix + "rc";
    mirror.files.append(mirror.configFile);

    {
        KConfig config(mirror.configFile, false, false);
        KConfigGroup general(&config, "General");
        for (MirrorSource::Entries::const_iterator it = source.panel.constBegin();
             it != source.panel.constEnd();
             ++it)
        {
            general.writeEntry(it.key(), it.data());
        }

        general.writeEntry("XineramaScreen", screen);
        general.writeEntry("Applets2", source.applets);

        for (QStringList::const_iterator it = source.applets.constBegin();
             it != source.applets.constEnd();
             ++it)
        {
            MirrorSource::Entries entries = source.containers[*it];
            QMap<QString, MirrorSource::Groups>::const_iterator cit = source.configs.find(*it);
            if (cit != source.configs.constEnd())
            {
                QString appletConfigFile = prefix + '_' + (*it).lower() + "rc";
                entries["ConfigFile"] = appletConfigFile;
                mirror.files.append(appletConfigFile);

                KConfig appletConfig(appletConfigFile, false, false);
                for (MirrorSource::Groups::const_iterator git = (*cit).constBegin();
                     git != (*cit).constEnd();
                     ++git)
                {
                    KConfigGroup group(&appletConfig, git.key());
                    for (MirrorSource::Entries::const_iterator eit = (*git).constBegin();
                         eit != (*git).constEnd();
                         ++eit)
                    {
                        group.writeEntry(eit.key(), eit.data());
                    }
                }
                appletConfig.sync();
            }

            KConfigGroup group(&config, *it);
            for (MirrorSource::Entries::const_iterator eit = entries.constBegin();
                 eit != entries.constEnd();
                 ++eit)
            {
                group.writeEntry(eit.key(), eit.data());
            }
        }

        config.sync();
    }

    ExtensionContainer* e = PluginManager::the()->createExtensionContainer(
                                "childpanelextension.desktop",
                                false,
                                mirror.configFile,
                                QString("Mirror_%1").arg(screen));
    if (!e)
    {
        for (QStringList::const_iterator it = mirror.files.constBegin();
             it != mirror.files.constEnd();
             ++it)
        {
            QFile::remove(locateLocal("config", *it));
        }
        return;
    }

    mirror.panel = e;
    mirror.panelSignature = source.panelSignature;
    mirror.contentSignature = source.contentSignature;
    m_mirrors[screen] = mirror;

    // stacked with the other panels of its screen as the main panel is
    e->setPanelOrder(m_mainPanel->panelOrder());

    e->readConfig();
    e->show();

    // the applets now, not from the event loop: some run a nested event
    // loop as they start (the trash applet's KIO::NetAccess::stat()), and
    // two of those nested in each other, one per mirror, never return
    PanelExtension* panel = dynamic_cast<PanelExtension*>(e->extension());
    if (panel)
    {
        panel->populateNow();
    }
}

void ExtensionManager::updateMirrorPanel(Mirror& mirror, int screen,
                                         const MirrorSource& source)
{
    {
        KConfig config(mirror.configFile, false, false);
        KConfigGroup general(&config, "General");
        const QStringList& keys = mirroredPanelKeys(m_mainPanel->settings());
        for (QStringList::const_iterator it = keys.constBegin(); it != keys.constEnd(); ++it)
        {
            MirrorSource::Entries::const_iterator eit = source.panel.find(*it);
            if (eit == source.panel.constEnd())
            {
                general.deleteEntry(*it);
            }
            else
            {
                general.writeEntry(*it, eit.data());
            }
        }

        general.writeEntry("XineramaScreen", screen);
        config.sync();
    }

    mirror.panelSignature = source.panelSignature;
    mirror.panel->readConfig();
    mirror.panel->show();
}

void ExtensionManager::destroyMirror(int screen)
{
    MirrorMap::iterator it = m_mirrors.find(screen);
    if (it == m_mirrors.end())
    {
        return;
    }

    Mirror mirror = *it;
    m_mirrors.remove(it);

    // now and not with deleteLater(): the applets write their files in
    // their destructors, before the files are removed
    delete mirror.panel;

    for (QStringList::const_iterator fit = mirror.files.constBegin();
         fit != mirror.files.constEnd();
         ++fit)
    {
        QFile::remove(locateLocal("config", *fit));
    }
}

void ExtensionManager::hideStaleMirrors()
{
    QDesktopWidget* desktop = QApplication::desktop();
    for (MirrorMap::iterator it = m_mirrors.begin(); it != m_mirrors.end(); ++it)
    {
        if (!desktop->isVirtualDesktop() || it.key() >= desktop->numScreens())
        {
            (*it).panel->hide();
        }
    }
}

#include "extensionmanager.moc"
