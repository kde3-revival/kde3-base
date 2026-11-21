/*****************************************************************

Copyright (c) 1996-2001 the kicker authors. See file AUTHORS.

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

#ifndef __systemtrayapplet_h__
#define __systemtrayapplet_h__

#include <qvaluevector.h>
#include <qstringlist.h>
#include <qevent.h>
#include <qdatetime.h>
#include <qmap.h>
#include <qxembed.h>

#include <dcopobject.h>
#include <kapplication.h>
#include <kpanelapplet.h>

#include "simplebutton.h"
#include "trayitem.h"

class QGridLayout;
class QTimer;
class KWinModule;
class TrayEmbed;
class KDialogBase;
class KActionSelector;
class StatusNotifierHost;

class SystemTrayApplet : public KPanelApplet, public DCOPObject
{
    Q_OBJECT
    K_DCOP
    typedef QValueVector<TrayItem*> TrayItemList;

public:

    SystemTrayApplet(const QString& configFile, Type t = Normal, int actions = 0,
                     QWidget *parent = 0, const char *name = 0);
    ~SystemTrayApplet();

    int widthForHeight(int h) const;
    int heightForWidth(int w) const;

    bool eventFilter(QObject* watched, QEvent* e);

    // where the menus of the icons open
    Direction menuDirection() { return popupDirection(); }

k_dcop:
    void loadSettings();

protected:
    void resizeEvent(QResizeEvent*);
    void moveEvent(QMoveEvent *);
    bool x11Event( XEvent *e );
    void preferences();
    void orientationChange( Orientation );

protected slots:
    void initialize();
    void systemTrayWindowAdded( WId );
    void updateTrayWindows();
    void layoutTray();
    void paletteChange(const QPalette & /* oldPalette */);
    void toggleExpanded();
    void settingsDialogFinished();
    void applySettings();
    void checkAutoRetract();
    void configure() { preferences(); }
    void setBackground();
    void statusNotifierItemAdded(const QString &key);
    void statusNotifierItemRemoved(const QString &key);
    void statusNotifierItemChanged(const QString &key);

private:
    void embedWindow( WId w, bool kde_tray );
    bool isWinManaged( WId w);
    bool shouldHide(const TrayItem *item) const;
    void addItem(TrayItem *item);
    void sortHidden();
    void updateVisibleWins();
    void expand();
    void retract();
    void showExpandButton(bool show);
    void refreshExpandButton();
    int iconSizeFor(int thickness) const;
    void announceIconSize();

    TrayItemList m_shownWins;
    TrayItemList m_hiddenWins;
    QStringList m_hiddenIconList;
    QMap<QString, QString> m_dialogKeys;    // name in the dialog -> hideKey
    KWinModule *kwin_module;
    Atom net_system_tray_selection;
    Atom net_system_tray_opcode;
    bool m_showFrame;
    bool m_showHidden;
    SimpleArrowButton *m_expandButton;
    KDialogBase* m_settingsDialog;
    KActionSelector* m_iconSelector;
    QTimer* m_autoRetractTimer;
    bool m_autoRetract;
    int m_iconSize;         // configured
    int m_currentIconSize;  // on this panel
    int m_announcedIconSize;
    QGridLayout* m_layout;
    StatusNotifierHost *m_statusNotifierHost;
};

class TrayEmbed : public QXEmbed, public TrayItem
{
    Q_OBJECT
public:
    TrayEmbed( bool kdeTray, QWidget* parent = NULL );
    bool kdeTray() const { return kde_tray; }

    QWidget *widget() { return this; }
    QString displayName() const;
    QString hideKey() const;
    bool isListed(const QStringList &hidden) const;
    QPixmap dialogIcon() const;
    void setIconSize(int size);
    void setBackground();

protected:
    bool x11Event(XEvent *e);

private:
    bool kde_tray;
    QTime m_configureTime;  // of the first of the last requests
    int m_configureCount;
};

#endif
