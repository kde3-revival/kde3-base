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

#include <string.h>

#include <qdir.h>
#include <qfile.h>
#include <qfileinfo.h>
#include <qimage.h>
#include <qkeysequence.h>
#include <qobjectlist.h>
#include <qpainter.h>
#include <qpopupmenu.h>
#include <qregexp.h>
#include <qstylesheet.h>
#include <qtooltip.h>

#include <kglobal.h>
#include <kiconloader.h>
#include <kpopupmenu.h>

#include "global.h"

#include "statusnotifieritem.h"
#include "systemtrayapplet.h"

// Icon files of the items are read only up to this size (an IconName may
// name any file).
static const uint MaxIconFile = 4 << 20;

// How deep the menus go (the host reads no deeper either).
static const int MaxMenuDepth = 16;

static bool isIconFile(const QString &path)
{
    QFileInfo info(path);
    return info.isFile() && info.isReadable() && info.size() <= MaxIconFile;
}

// An icon file named by an item: a PNG of at most 1024 x 1024 pixels (its
// header is read before it is decoded; a program may rewrite it under the
// same name, so it is not cached), or an SVG rendered by KIconLoader.
static QImage loadIconFile(const QString &path, int size)
{
    if (!isIconFile(path))
    {
        return QImage();
    }
    if (path.endsWith(".svg") || path.endsWith(".svgz"))
    {
        QPixmap pixmap = KGlobal::iconLoader()->loadIcon(path, KIcon::Panel, size,
                                                         KIcon::DefaultState, 0, true);
        return pixmap.isNull() ? QImage() : pixmap.convertToImage();
    }

    QFile file(path);
    if (!file.open(IO_ReadOnly))
    {
        return QImage();
    }
    QByteArray data = file.readAll();
    static const uchar signature[] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n' };
    const uchar *bytes = reinterpret_cast<const uchar *>(data.data());
    if (data.size() < 24 || memcmp(bytes, signature, 8) || memcmp(bytes + 12, "IHDR", 4))
    {
        return QImage();
    }
    Q_UINT32 w = (Q_UINT32(bytes[16]) << 24) | (bytes[17] << 16) | (bytes[18] << 8) | bytes[19];
    Q_UINT32 h = (Q_UINT32(bytes[20]) << 24) | (bytes[21] << 16) | (bytes[22] << 8) | bytes[23];
    QImage image;
    if (w == 0 || h == 0 || w > 1024 || h > 1024 || !image.loadFromData(data, "PNG"))
    {
        return QImage();
    }
    return image;
}

// The file of the icon `name` in the IconThemePath `dir` of an item: either
// right in it, or in an icon theme there (dir/hicolor/22x22/apps/name.png),
// or dir is such a theme.  The size nearest to `size`, larger ones first.
static QString scanThemeIcon(const QString &dir, const QString &name, int size);

static QString findThemeIcon(const QString &dir, const QString &name, int size)
{
    // the directories are not read again on each change of the icon
    static QMap<QString, QString> cache;
    QString key = dir + '\n' + name + '\n' + QString::number(size);
    QMap<QString, QString>::ConstIterator it = cache.find(key);
    if (it != cache.end())
    {
        return *it;
    }
    if (cache.count() > 256)
    {
        cache.clear();
    }
    QString path = scanThemeIcon(dir, name, size);
    cache.insert(key, path);
    return path;
}

static QString scanThemeIcon(const QString &dir, const QString &name, int size)
{
    static const char * const extensions[] = { ".png", ".svg", ".svgz", 0 };

    for (int e = 0; extensions[e]; ++e)
    {
        QString path = dir + "/" + name + extensions[e];
        if (isIconFile(path))
            return path;
    }

    QStringList themes;
    themes.append(dir);
    QStringList subdirs = QDir(dir).entryList(QDir::Dirs);
    for (QStringList::ConstIterator it = subdirs.begin(); it != subdirs.end() && themes.count() < 9; ++it)
    {
        if (*it != "." && *it != "..")
            themes.append(dir + "/" + *it);
    }

    QString best;
    int bestScore = 0;
    for (QStringList::ConstIterator theme = themes.begin(); theme != themes.end(); ++theme)
    {
        QStringList sizes = QDir(*theme).entryList(QDir::Dirs);
        int sizeCount = 0;
        for (QStringList::ConstIterator s = sizes.begin(); s != sizes.end() && sizeCount < 32; ++s)
        {
            int score;
            if (*s == "scalable")
            {
                score = 1;
            }
            else
            {
                QRegExp re("^(\\d+)x\\1(@\\d+)?$");
                if (re.search(*s) != 0)
                    continue;
                int side = re.cap(1).toInt();
                if (side <= 0)
                    continue;
                score = side == size ? 0 : side > size ? 2 + side - size : 10000 + size - side;
            }
            ++sizeCount;
            if (!best.isEmpty() && score >= bestScore)
                continue;

            QString sizeDir = *theme + "/" + *s;
            QStringList contexts = QDir(sizeDir).entryList(QDir::Dirs);
            int contextCount = 0;
            for (QStringList::ConstIterator c = contexts.begin();
                 c != contexts.end() && contextCount < 16; ++c)
            {
                if (*c == "." || *c == "..")
                    continue;
                ++contextCount;
                for (int e = 0; extensions[e]; ++e)
                {
                    QString path = sizeDir + "/" + *c + "/" + name + extensions[e];
                    if (isIconFile(path))
                    {
                        best = path;
                        bestScore = score;
                        break;
                    }
                }
                if (best.startsWith(sizeDir + "/"))
                    break;
            }
        }
    }
    return best;
}

// `image` made to fit a size x size square, centered.
static QImage fitImage(const QImage &image, int size)
{
    QImage src = image.convertDepth(32);
    src.setAlphaBuffer(true);
    if (src.width() != size || src.height() != size)
    {
        if (src.width() > size || src.height() > size
            || (src.width() != size && src.height() != size))
        {
            src = src.smoothScale(size, size, QImage::ScaleMin);
        }
    }
    if (src.width() == size && src.height() == size)
    {
        return src;
    }

    QImage square(size, size, 32);
    square.setAlphaBuffer(true);
    square.fill(0);
    int ox = (size - src.width()) / 2;
    int oy = (size - src.height()) / 2;
    for (int y = 0; y < src.height(); ++y)
    {
        const QRgb *from = reinterpret_cast<const QRgb *>(src.scanLine(y));
        QRgb *to = reinterpret_cast<QRgb *>(square.scanLine(y + oy)) + ox;
        for (int x = 0; x < src.width(); ++x)
            to[x] = from[x];
    }
    return square;
}

// `over` drawn on `image` at x, y (both 32 bits with alpha).
static void blend(QImage &image, const QImage &over, int ox, int oy)
{
    for (int y = 0; y < over.height(); ++y)
    {
        if (y + oy < 0 || y + oy >= image.height())
            continue;
        const QRgb *from = reinterpret_cast<const QRgb *>(over.scanLine(y));
        QRgb *to = reinterpret_cast<QRgb *>(image.scanLine(y + oy));
        for (int x = 0; x < over.width(); ++x)
        {
            if (x + ox < 0 || x + ox >= image.width())
                continue;
            QRgb s = from[x];
            QRgb &d = to[x + ox];
            int sa = qAlpha(s);
            int da = qAlpha(d);
            int a = sa + da * (255 - sa) / 255;
            if (a == 0)
            {
                d = 0;
                continue;
            }
            int r = (qRed(s) * sa + qRed(d) * da * (255 - sa) / 255) / a;
            int g = (qGreen(s) * sa + qGreen(d) * da * (255 - sa) / 255) / a;
            int b = (qBlue(s) * sa + qBlue(d) * da * (255 - sa) / 255) / a;
            d = qRgba(r, g, b, a);
        }
    }
}

// A dbusmenu label ("_Open", "a__b") as a Qt one ("&Open", "a_b").
static QString menuLabel(const QString &label)
{
    QString result;
    bool mnemonic = false;
    for (uint i = 0; i < label.length(); ++i)
    {
        QChar c = label[i];
        if (c == '_')
        {
            if (i + 1 < label.length() && label[i + 1] == '_')
            {
                result += '_';
                ++i;
            }
            else if (!mnemonic && i + 1 < label.length())
            {
                result += '&';
                mnemonic = true;
            }
        }
        else if (c == '&')
        {
            result += "&&";
        }
        else
        {
            result += c;
        }
    }
    return result;
}

// What a menu shows, to see whether a new layout changes it.
static QString menuSignature(const StatusNotifierMenuNode &node)
{
    QString s;
    for (QPtrListIterator<StatusNotifierMenuNode> it(node.children); it.current(); ++it)
    {
        const StatusNotifierMenuNode *n = it.current();
        s += QString::number(n->id) + '|' + n->label + '|' + QString::number(n->enabled)
           + QString::number(n->checked) + QString::number(n->separator)
           + QString::number(n->submenu) + QString::number(n->toggle) + '|'
           + n->iconName + '|' + QString::number(n->icon.width()) + '|' + n->shortcut + '\n';
    }
    return s;
}

StatusNotifierWidget::StatusNotifierWidget(StatusNotifierHost *host, const QString &key,
                                           SystemTrayApplet *applet, QWidget *parent)
    : QWidget(parent, "StatusNotifierWidget"),
      m_host(host),
      m_key(key),
      m_applet(applet),
      m_size(22),
      m_menuSerial(-1),
      m_activateSerial(-1),
      m_nextMenuId(1)
{
    setBackgroundOrigin(AncestorOrigin);
    setFixedSize(m_size, m_size);

    connect(m_host, SIGNAL(activateFailed(const QString &, int, int, int)),
            SLOT(activateFailed(const QString &, int, int, int)));
    connect(m_host, SIGNAL(menuReady(const QString &, int, const StatusNotifierMenuNode &)),
            SLOT(menuReady(const QString &, int, const StatusNotifierMenuNode &)));
    connect(m_host, SIGNAL(menuFailed(const QString &, int)),
            SLOT(menuFailed(const QString &, int)));

    itemChanged();
}

void StatusNotifierWidget::itemChanged()
{
    const StatusNotifierItemData *data = m_host->item(m_key);
    if (!data)
    {
        return;
    }
    m_data = *data;
    m_pixmap = render(m_size);
    updateToolTip();
    update();
}

QString StatusNotifierWidget::displayName() const
{
    return m_data.title.isEmpty() ? m_data.id : m_data.title;
}

QString StatusNotifierWidget::hideKey() const
{
    return m_data.id.isEmpty() ? m_data.title : m_data.id;
}

bool StatusNotifierWidget::isListed(const QStringList &hidden) const
{
    return (!m_data.id.isEmpty() && hidden.contains(m_data.id))
        || (!m_data.title.isEmpty() && hidden.contains(m_data.title));
}

bool StatusNotifierWidget::isPassive() const
{
    return m_data.status == "Passive";
}

QPixmap StatusNotifierWidget::dialogIcon() const
{
    return render(22);
}

void StatusNotifierWidget::setIconSize(int size)
{
    if (size == m_size)
    {
        return;
    }
    m_size = size;
    setFixedSize(size, size);
    m_pixmap = render(size);
    update();
}

QImage StatusNotifierWidget::loadIcon(const QString &name, const StatusNotifierImages &images,
                                      int size) const
{
    QImage image;
    if (!name.isEmpty())
    {
        if (name.startsWith("/"))
        {
            image = loadIconFile(name, size);
        }
        else if (name.find('/') < 0 && name != "." && name != "..")
        {
            if (!m_data.iconThemePath.isEmpty() && m_data.iconThemePath.startsWith("/"))
            {
                QString path = findThemeIcon(m_data.iconThemePath, name, size);
                if (!path.isEmpty())
                    image = loadIconFile(path, size);
            }
            if (image.isNull())
            {
                QPixmap pixmap = KGlobal::iconLoader()->loadIcon(name, KIcon::Panel, size,
                                                                 KIcon::DefaultState, 0, true);
                if (!pixmap.isNull())
                    image = pixmap.convertToImage();
            }
        }
    }

    if (image.isNull() && !images.isEmpty())
    {
        // the smallest one at least as large as the square, else the largest
        const QImage *best = 0;
        for (StatusNotifierImages::ConstIterator it = images.begin(); it != images.end(); ++it)
        {
            int side = QMIN((*it).width(), (*it).height());
            if (!best)
            {
                best = &*it;
                continue;
            }
            int bestSide = QMIN(best->width(), best->height());
            if (bestSide < size ? side > bestSide : (side >= size && side < bestSide))
                best = &*it;
        }
        image = *best;
    }
    return image;
}

QPixmap StatusNotifierWidget::render(int size) const
{
    QImage image;
    if (m_data.status == "NeedsAttention")
    {
        image = loadIcon(m_data.attentionIconName, m_data.attentionImages, size);
    }
    if (image.isNull())
    {
        image = loadIcon(m_data.iconName, m_data.iconImages, size);
    }
    if (image.isNull())
    {
        image = KGlobal::iconLoader()->loadIcon("unknown", KIcon::Panel, size).convertToImage();
    }
    if (image.isNull())
    {
        return QPixmap();
    }
    image = fitImage(image, size);

    int overlaySize = size / 2;
    QImage overlay = loadIcon(m_data.overlayIconName, m_data.overlayImages, overlaySize);
    if (!overlay.isNull() && overlaySize > 0)
    {
        overlay = fitImage(overlay, overlaySize);
        blend(image, overlay, size - overlaySize, size - overlaySize);
    }

    QPixmap pixmap;
    pixmap.convertFromImage(image);
    return pixmap;
}

void StatusNotifierWidget::updateToolTip()
{
    QString title = m_data.toolTipTitle;
    if (title.isEmpty())
    {
        title = displayName();
    }

    // The text may have some HTML; only the line breaks of it are kept.
    QString text = m_data.toolTipText;
    text.replace(QRegExp("<br\\s*/?>", false), "\n");
    text.replace(QRegExp("<[^>]*>"), "");
    text.replace("&lt;", "<").replace("&gt;", ">").replace("&quot;", "\"")
        .replace("&apos;", "'").replace("&nbsp;", " ").replace("&amp;", "&");
    text = text.stripWhiteSpace();

    QString tip = "<qt><nobr><b>" + QStyleSheet::escape(title) + "</b></nobr>";
    if (!text.isEmpty())
    {
        tip += "<br>" + QStyleSheet::escape(text).replace("\n", "<br>");
    }
    tip += "</qt>";

    QToolTip::remove(this);
    if (!title.isEmpty() || !text.isEmpty())
    {
        QToolTip::add(this, tip);
    }
}

void StatusNotifierWidget::paintEvent(QPaintEvent *)
{
    if (m_pixmap.isNull())
    {
        return;
    }
    QPainter p(this);
    p.drawPixmap((width() - m_pixmap.width()) / 2, (height() - m_pixmap.height()) / 2, m_pixmap);
}

void StatusNotifierWidget::mousePressEvent(QMouseEvent *e)
{
    e->accept();
}

void StatusNotifierWidget::mouseReleaseEvent(QMouseEvent *e)
{
    if (!rect().contains(e->pos()))
    {
        return;
    }
    QPoint global = e->globalPos();
    switch (e->button())
    {
    case LeftButton:
        if (m_data.itemIsMenu && !m_data.menuPath.isEmpty())
            showMenu();
        else
            m_activateSerial = m_host->activate(m_key, global.x(), global.y());
        break;
    case MidButton:
        m_host->secondaryActivate(m_key, global.x(), global.y());
        break;
    case RightButton:
        if (!m_data.menuPath.isEmpty())
            showMenu();
        else
            m_host->contextMenu(m_key, global.x(), global.y());
        break;
    default:
        break;
    }
}

void StatusNotifierWidget::wheelEvent(QWheelEvent *e)
{
    m_host->scroll(m_key, e->delta(), e->orientation() == Horizontal);
    e->accept();
}

void StatusNotifierWidget::activateFailed(const QString &key, int serial, int x, int y)
{
    // (another tray applet's icon of the same item may have been clicked)
    if (key != m_key || serial != m_activateSerial)
    {
        return;
    }
    m_activateSerial = -1;
    // libappindicator has no Activate: its menu, as in other trays
    if (!m_data.menuPath.isEmpty())
        showMenu();
    else
        m_host->contextMenu(m_key, x, y);
}

void StatusNotifierWidget::showMenu()
{
    if (m_menu)
    {
        m_menu->close();
    }
    m_menuSerial = m_host->fetchMenu(m_key, 0);
}

void StatusNotifierWidget::menuFailed(const QString &key, int serial)
{
    if (key != m_key)
    {
        return;
    }
    if (serial == m_menuSerial)
    {
        m_menuSerial = -1;
    }
    m_submenuFetches.remove(serial);
}

void StatusNotifierWidget::menuReady(const QString &key, int serial, const StatusNotifierMenuNode &node)
{
    if (key != m_key)
    {
        return;
    }

    if (m_submenuFetches.contains(serial))
    {
        // the contents of a submenu, asked for when it was opened
        QPopupMenu *submenu = m_submenuFetches[serial];
        m_submenuFetches.remove(serial);
        if (!submenu)
            return;
        QString signature = menuSignature(node);
        if (m_submenuContents[submenu] == signature)
            return;
        int depth = 1;
        for (QObject *o = submenu->parent(); o && o != m_menu; o = o->parent())
            ++depth;
        // its submenus go with it, also open ones
        QObjectList *nested = submenu->queryList("QPopupMenu", 0, false, true);
        if (nested)
        {
            for (QObjectListIt it(*nested); it.current(); ++it)
            {
                m_submenuIds.remove(it.current());
                m_submenuContents.remove(it.current());
            }
            delete nested;
        }
        nested = submenu->queryList("QPopupMenu", 0, false, false);
        if (nested)
        {
            QObjectList direct = *nested;
            delete nested;
            for (QObjectListIt it(direct); it.current(); ++it)
                delete it.current();
        }
        submenu->clear();
        fillMenu(submenu, node, depth);
        m_submenuContents[submenu] = signature;
        return;
    }

    if (serial != m_menuSerial)
    {
        return;
    }
    m_menuSerial = -1;
    if (node.children.isEmpty())
    {
        return;
    }

    if (m_menu)
    {
        m_menu->close();
        delete static_cast<QPopupMenu *>(m_menu);
    }
    m_submenuIds.clear();
    m_submenuContents.clear();
    m_submenuFetches.clear();
    m_menuIds.clear();

    KPopupMenu *menu = new KPopupMenu(this);
    m_menu = menu;
    fillMenu(menu, node, 0);
    connect(menu, SIGNAL(aboutToHide()), SLOT(rootAboutToHide()));

    KickerLib::adjustPopupSize(menu);
    menu->popup(KickerLib::popupPosition(m_applet->menuDirection(), menu, this));
    m_host->menuEvent(m_key, 0, "opened");
}

void StatusNotifierWidget::fillMenu(QPopupMenu *menu, const StatusNotifierMenuNode &node, int depth)
{
    bool checkable = false;
    for (QPtrListIterator<StatusNotifierMenuNode> it(node.children); it.current(); ++it)
    {
        const StatusNotifierMenuNode &child = *it.current();
        if (child.separator)
        {
            menu->insertSeparator();
            continue;
        }

        QIconSet icon;
        if (!child.icon.isNull())
        {
            QPixmap pixmap;
            pixmap.convertFromImage(fitImage(child.icon, 16));
            icon = QIconSet(pixmap);
        }
        else if (!child.iconName.isEmpty() && child.iconName.find('/') < 0)
        {
            QPixmap pixmap = KGlobal::iconLoader()->loadIcon(child.iconName, KIcon::Small, 16,
                                                             KIcon::DefaultState, 0, true);
            if (!pixmap.isNull())
                icon = QIconSet(pixmap);
        }

        QString label = menuLabel(child.label);
        int id;
        if (child.submenu && depth + 1 < MaxMenuDepth)
        {
            KPopupMenu *submenu = new KPopupMenu(menu);
            fillMenu(submenu, child, depth + 1);
            m_submenuIds[submenu] = child.id;
            m_submenuContents[submenu] = menuSignature(child);
            connect(submenu, SIGNAL(aboutToShow()), SLOT(submenuAboutToShow()));
            connect(submenu, SIGNAL(aboutToHide()), SLOT(submenuAboutToHide()));
            id = icon.isNull() ? menu->insertItem(label, submenu, m_nextMenuId++)
                               : menu->insertItem(icon, label, submenu, m_nextMenuId++);
        }
        else
        {
            // the shortcut is only shown: the program has it
            if (!child.shortcut.isEmpty())
            {
                label += "\t" + QString(QKeySequence(child.shortcut));
            }
            id = icon.isNull() ? menu->insertItem(label, m_nextMenuId++)
                               : menu->insertItem(icon, label, m_nextMenuId++);
            m_menuIds[id] = child.id;
            if (child.submenu)
            {
                // deeper than the menus go
                menu->setItemEnabled(id, false);
                continue;
            }
        }

        menu->setItemEnabled(id, child.enabled);
        if (child.toggle != StatusNotifierMenuNode::NoToggle)
        {
            checkable = true;
            menu->setItemChecked(id, child.checked);
        }
    }
    if (checkable)
    {
        menu->setCheckable(true);
    }
    // once, also when a submenu is filled again
    disconnect(menu, SIGNAL(activated(int)), this, SLOT(menuActivated(int)));
    connect(menu, SIGNAL(activated(int)), SLOT(menuActivated(int)));
}

void StatusNotifierWidget::menuActivated(int id)
{
    if (!m_menuIds.contains(id))
    {
        return;
    }
    m_host->menuEvent(m_key, m_menuIds[id], "clicked");
}

void StatusNotifierWidget::submenuAboutToShow()
{
    const QObject *submenu = sender();
    if (!m_submenuIds.contains(submenu))
    {
        return;
    }
    int id = m_submenuIds[submenu];
    // the item may fill or change it now (AboutToShow, then the layout)
    int serial = m_host->fetchMenu(m_key, id);
    if (serial >= 0)
    {
        m_submenuFetches[serial] = static_cast<QPopupMenu *>(const_cast<QObject *>(submenu));
    }
    m_host->menuEvent(m_key, id, "opened");
}

void StatusNotifierWidget::submenuAboutToHide()
{
    const QObject *submenu = sender();
    if (m_submenuIds.contains(submenu))
    {
        m_host->menuEvent(m_key, m_submenuIds[submenu], "closed");
    }
}

void StatusNotifierWidget::rootAboutToHide()
{
    m_host->menuEvent(m_key, 0, "closed");
    // after the activated() signal which may follow
    if (m_menu)
    {
        m_menu->deleteLater();
    }
}

#include "statusnotifieritem.moc"
