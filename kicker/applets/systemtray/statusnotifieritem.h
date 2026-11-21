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

#ifndef STATUSNOTIFIERITEM_H
#define STATUSNOTIFIERITEM_H

#include <qguardedptr.h>
#include <qmap.h>
#include <qwidget.h>

#include "statusnotifierhost.h"
#include "trayitem.h"

class SystemTrayApplet;
class QPopupMenu;

/*
 * The icon of a StatusNotifierItem in the tray: drawn at the size of the
 * tray, with the item's tooltip, clicks, wheel and menu.
 */
class StatusNotifierWidget : public QWidget, public TrayItem
{
    Q_OBJECT

public:
    StatusNotifierWidget(StatusNotifierHost *host, const QString &key,
                         SystemTrayApplet *applet, QWidget *parent);

    QString key() const { return m_key; }
    // The item changed: icon, status, tooltip...
    void itemChanged();

    QWidget *widget() { return this; }
    QString displayName() const;
    QString hideKey() const;
    bool isListed(const QStringList &hidden) const;
    bool isPassive() const;
    QPixmap dialogIcon() const;
    void setIconSize(int size);

protected:
    void paintEvent(QPaintEvent *);
    void mousePressEvent(QMouseEvent *);
    void mouseReleaseEvent(QMouseEvent *);
    void wheelEvent(QWheelEvent *);

private slots:
    void activateFailed(const QString &key, int serial, int x, int y);
    void menuReady(const QString &key, int serial, const StatusNotifierMenuNode &menu);
    void menuFailed(const QString &key, int serial);
    void menuActivated(int id);
    void submenuAboutToShow();
    void submenuAboutToHide();
    void rootAboutToHide();

private:
    QPixmap render(int size) const;
    QImage loadIcon(const QString &name, const StatusNotifierImages &images, int size) const;
    void updateToolTip();
    void showMenu();
    void fillMenu(QPopupMenu *menu, const StatusNotifierMenuNode &node, int depth);

    StatusNotifierHost *m_host;
    QString m_key;
    SystemTrayApplet *m_applet;
    StatusNotifierItemData m_data;
    int m_size;
    QPixmap m_pixmap;

    // the menu shown or asked for
    QGuardedPtr<QPopupMenu> m_menu;
    int m_menuSerial;
    int m_activateSerial;
    int m_nextMenuId;
    QMap<int, int> m_menuIds;                               // Qt id -> dbusmenu id
    QMap<int, QGuardedPtr<QPopupMenu> > m_submenuFetches;  // serial -> submenu
    QMap<const QObject *, int> m_submenuIds;                // submenu -> its id
    QMap<const QObject *, QString> m_submenuContents;       // what it shows
};

#endif
