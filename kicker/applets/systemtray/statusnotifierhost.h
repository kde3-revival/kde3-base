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

#ifndef STATUSNOTIFIERHOST_H
#define STATUSNOTIFIERHOST_H

#include <qimage.h>
#include <qobject.h>
#include <qptrlist.h>
#include <qstring.h>
#include <qstringlist.h>
#include <qvaluelist.h>

/*
 * The StatusNotifierItem side of the system tray: the icons of programs
 * which talk to the tray over D-Bus (org.kde.StatusNotifierItem, used by
 * libappindicator, Qt 5/6, Chromium, Electron and others) instead of
 * embedding an X window.
 *
 * One StatusNotifierHost per process, shared by the tray applets in it
 * (ref() and deref()).  It is the org.kde.StatusNotifierWatcher of the
 * session when no other program is (and takes over when the other one
 * goes away), registers itself as a host with whichever watcher there is,
 * and follows the properties and the signals of the items.
 *
 * All the D-Bus traffic is asynchronous (GDBus on the GLib main loop, which
 * Qt runs on); nothing that comes back after the host is gone is used.
 */

// An image of an item, as the item sent it.
typedef QValueList<QImage> StatusNotifierImages;

struct StatusNotifierItemData
{
    StatusNotifierItemData() : itemIsMenu(false) {}

    QString id;
    QString title;
    QString category;
    QString status;         // Passive, Active or NeedsAttention
    QString iconThemePath;
    QString iconName;
    QString overlayIconName;
    QString attentionIconName;
    StatusNotifierImages iconImages;
    StatusNotifierImages overlayImages;
    StatusNotifierImages attentionImages;
    QString toolTipTitle;
    QString toolTipText;
    QString menuPath;       // empty: no com.canonical.dbusmenu menu
    bool itemIsMenu;
};

// An entry of a com.canonical.dbusmenu menu, with its entries.  (Not a
// QValueList of nodes: its end node would hold a node, which would hold a
// list...)
struct StatusNotifierMenuNode
{
    enum Toggle { NoToggle, CheckMark, Radio };

    StatusNotifierMenuNode()
        : id(0), enabled(true), separator(false), submenu(false),
          toggle(NoToggle), checked(false)
    {
        children.setAutoDelete(true);
    }

    int id;
    QString label;          // with the dbusmenu mnemonic ("_File")
    QString iconName;
    QImage icon;            // from icon-data
    QString shortcut;       // in QKeySequence's format ("Ctrl+Q")
    bool enabled;
    bool separator;
    bool submenu;           // children-display "submenu"
    Toggle toggle;
    bool checked;
    QPtrList<StatusNotifierMenuNode> children;      // the visible ones, owned

private:
    StatusNotifierMenuNode(const StatusNotifierMenuNode &);
    StatusNotifierMenuNode &operator=(const StatusNotifierMenuNode &);
};

class StatusNotifierHostPrivate;

class StatusNotifierHost : public QObject
{
    Q_OBJECT

public:
    // The host of this process, made on the first call; 0 without a
    // session bus.  Each successful ref() needs a deref().
    static StatusNotifierHost *ref();
    void deref();

    // The items, in the order they came.
    QStringList items() const;
    // The data of an item; 0 if there is no such item (any more).
    const StatusNotifierItemData *item(const QString &key) const;

    // Returns the serial of activateFailed() if the item has no Activate.
    int activate(const QString &key, int x, int y);
    void secondaryActivate(const QString &key, int x, int y);
    void scroll(const QString &key, int delta, bool horizontal);
    void contextMenu(const QString &key, int x, int y);

    // Asks for the menu (or the submenu parentId) of an item: menuReady()
    // or menuFailed() follows with the returned serial.
    int fetchMenu(const QString &key, int parentId);
    // Tells the item about an entry of its menu: "clicked", "opened",
    // "closed".
    void menuEvent(const QString &key, int id, const char *event);

signals:
    void itemAdded(const QString &key);
    void itemRemoved(const QString &key);
    void itemChanged(const QString &key);
    // Activate is not implemented by the item: show its menu instead.
    void activateFailed(const QString &key, int serial, int x, int y);
    void menuReady(const QString &key, int serial, const StatusNotifierMenuNode &menu);
    void menuFailed(const QString &key, int serial);

private slots:
    void refreshPending();
    void askWatcher();

private:
    StatusNotifierHost();
    ~StatusNotifierHost();

    friend class StatusNotifierHostPrivate;
    StatusNotifierHostPrivate *d;
};

#endif
