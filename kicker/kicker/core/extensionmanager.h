/*****************************************************************

Copyright (c) 2000 Matthias Elter

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

#ifndef __extensionmanager_h__
#define __extensionmanager_h__

#include <qmap.h>
#include <qvaluelist.h>
#include <qobject.h>

#include "container_extension.h"

const int XineramaAllScreens = -2;

class ContainerArea;
class KDirWatch;
class PanelExtension;
class QTimer;

class ExtensionManager : public QObject
{
    Q_OBJECT

public:
    static ExtensionManager* the();

    void configureMenubar(bool duringInit);
    void addExtension( const QString &desktopFile );
    bool isMainPanel(const QWidget* panel) const;
    bool isMenuBar(const QWidget* panel) const;
    void addContainer( ExtensionContainer* );
    void removeAllContainers();
    ExtensionList containers() const { return _containers; }

    KPanelExtension::Position initialPanelPosition(KPanelExtension::Position preferred);
    QRect workArea(int XineramaScreen, const ExtensionContainer* container);
    int nextPanelOrder();

    // return the space available for all icons on the desktop
    // subtracts all panels from XineramaScreen's geometry
    QRect desktopIconsArea(int xineramaScreen) const;

    // The copies of the main panel shown on the other screens
    // ("mirrors"), see MainPanelOnEveryScreen.  They are made from
    // generated config files; whatever is done to them goes to the
    // main panel, and they are made again when the main panel changes.
    static bool isMirrorConfigFile(const QString& configFile);
    bool isMirror(const QWidget* panel) const;
    PanelExtension* mainPanelExtension() const;
    ContainerArea* mainPanelArea() const;
    // a point of a mirror moved to the same place of the main panel,
    // for the insertion point of what is added from a mirror's menus
    QPoint insertionPointFor(const QWidget* origin, const QPoint& globalPos) const;

public slots:
    void removeContainer( ExtensionContainer* );
    void initialize();
    void extensionSizeChanged(ExtensionContainer *);
    void scheduleMirrorSync();

signals:
    void desktopIconsAreaChanged(const QRect &, int xineramaScreen);

protected:
    friend class Kicker;

    ExtensionManager();
    ~ExtensionManager();
    QString uniqueId();
    void saveContainerConfig();
    bool shouldExclude(int XineramaScreen,
                       const ExtensionContainer* container,
                       const ExtensionContainer* exclude) const;

protected slots:
    void configurationChanged();
    void updateMenubar();
    void syncMirrors();

private:
    // what a mirror is made of, read from the main panel's files
    struct MirrorSource
    {
        typedef QMap<QString, QString> Entries;
        typedef QMap<QString, Entries> Groups;

        Entries panel;                  // ExtensionSettings of the main panel
        QStringList applets;            // the Applets2 list of a mirror
        Groups containers;              // the group of each of them
        QMap<QString, Groups> configs;  // the config file of each applet
        QStringList watchedFiles;       // the main panel's applet config files
        QString panelSignature;
        QString contentSignature;
    };

    struct Mirror
    {
        Mirror() : panel(0) {}

        ExtensionContainer* panel;
        QString configFile;
        QStringList files;
        QString panelSignature;
        QString contentSignature;
    };

    typedef QMap<int, Mirror> MirrorMap;

    void migrateMenubar();
    void migrateManualMirrors();
    void reduceArea(QRect &area, const ExtensionContainer *panel) const;

    static QString mirrorFilePrefix();
    void removeStaleMirrorFiles();
    QString mirroredDesktopFile(const QString& desktopFile) const;
    bool readMirrorSource(MirrorSource& source) const;
    QValueList<int> mirrorScreens(bool askToReplace);
    ExtensionContainer* manualPanelAt(int screen,
                                      KPanelExtension::Position position) const;
    void createMirror(int screen, const MirrorSource& source);
    void updateMirrorPanel(Mirror& mirror, int screen,
                           const MirrorSource& source);
    void destroyMirror(int screen);
    void hideStaleMirrors();

    ExtensionList _containers;
    ExtensionContainer* m_menubarPanel;
    ExtensionContainer* m_mainPanel;
    int m_panelCounter;
    static ExtensionManager* m_self;

    MirrorMap m_mirrors;
    QTimer* m_mirrorSyncTimer;
    KDirWatch* m_mirrorWatch;
    QStringList m_mirrorWatchedFiles;
    int m_mirrorGeneration;
    bool m_extensionsLoaded;
    bool m_mirrorsInitialized;
    bool m_mirrorsWereOn;
    bool m_syncingMirrors;
};

#endif

