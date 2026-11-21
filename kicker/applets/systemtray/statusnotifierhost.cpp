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

// GIO first: Qt defines "signals", which GLib uses as an identifier.
#include <gio/gio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <qcstring.h>
#include <qmap.h>
#include <qtimer.h>

#include <kdebug.h>

#include <X11/X.h>

#include "statusnotifierhost.h"

// The D-Bus callbacks come from the GLib main loop: Qt has to run on it.
#ifndef QT_GLIB_EVENTLOOP
#error "The system tray needs Qt built with the GLib event loop (QT_GLIB_EVENTLOOP)"
#endif

extern Time qt_x_time;

static const char WatcherName[] = "org.kde.StatusNotifierWatcher";
static const char WatcherPath[] = "/StatusNotifierWatcher";
static const char WatcherInterface[] = "org.kde.StatusNotifierWatcher";
static const char ItemInterface[] = "org.kde.StatusNotifierItem";
static const char DefaultItemPath[] = "/StatusNotifierItem";
static const char MenuInterface[] = "com.canonical.dbusmenu";

// What the items and the other programs send is not trusted: limits for
// all of it.
static const int MaxItems = 256;            // registered, and shown
static const int MaxHosts = 64;
static const int MaxString = 4096;          // characters of a property
static const int MaxImages = 32;            // sizes of an icon
static const int MaxImageSide = 1024;       // pixels
static const gsize MaxImageBytes = 16 << 20;    // all the images of an item
static const int MaxMenuDepth = 16;
static const int MaxMenuNodes = 2000;
static const int MaxMenuLabel = 512;
static const gsize MaxMenuIconBytes = 256 << 10;
static const int MaxMenuIconSide = 512;

// Timeouts of the calls, in milliseconds.
static const int PropertiesTimeout = 10000;
static const int ActivateTimeout = 5000;
static const int MenuTimeout = 10000;

// Property and signal bursts are answered once after this many ms.
static const int RefreshDelay = 50;

static const char WatcherXml[] =
    "<node>"
    " <interface name='org.kde.StatusNotifierWatcher'>"
    "  <method name='RegisterStatusNotifierItem'>"
    "   <arg name='service' type='s' direction='in'/>"
    "  </method>"
    "  <method name='RegisterStatusNotifierHost'>"
    "   <arg name='service' type='s' direction='in'/>"
    "  </method>"
    "  <property name='RegisteredStatusNotifierItems' type='as' access='read'/>"
    "  <property name='IsStatusNotifierHostRegistered' type='b' access='read'/>"
    "  <property name='ProtocolVersion' type='i' access='read'/>"
    "  <signal name='StatusNotifierItemRegistered'><arg type='s'/></signal>"
    "  <signal name='StatusNotifierItemUnregistered'><arg type='s'/></signal>"
    "  <signal name='StatusNotifierHostRegistered'/>"
    "  <signal name='StatusNotifierHostUnregistered'/>"
    " </interface>"
    "</node>";

static StatusNotifierHost *s_host = 0;
static int s_refs = 0;

/******************************************************************************
 * Reading what comes over the bus, with the limits above.
 */

static QString variantString(GVariant *v)
{
    if (!v || !(g_variant_is_of_type(v, G_VARIANT_TYPE_STRING)
                || g_variant_is_of_type(v, G_VARIANT_TYPE_OBJECT_PATH)))
    {
        return QString::null;
    }
    gsize length = 0;
    const char *s = g_variant_get_string(v, &length);
    return QString::fromUtf8(s, int(QMIN(length, gsize(MaxString * 4)))).left(MaxString);
}

// An a(iiay) of ARGB32 images in network byte order.
static void readImages(GVariant *v, StatusNotifierImages &images, gsize &budget)
{
    images.clear();
    if (!v || !g_variant_is_of_type(v, G_VARIANT_TYPE("a(iiay)")))
    {
        return;
    }
    gsize count = g_variant_n_children(v);
    for (gsize i = 0; i < count && images.count() < uint(MaxImages); ++i)
    {
        GVariant *child = g_variant_get_child_value(v, i);
        gint32 w = 0, h = 0;
        GVariant *bytes = 0;
        g_variant_get(child, "(ii@ay)", &w, &h, &bytes);
        gsize length = 0;
        const guchar *data = static_cast<const guchar *>(
            g_variant_get_fixed_array(bytes, &length, 1));
        if (w > 0 && h > 0 && w <= MaxImageSide && h <= MaxImageSide)
        {
            gsize needed = gsize(w) * gsize(h) * 4;
            if (data && length >= needed && needed <= budget)
            {
                budget -= needed;
                QImage image(w, h, 32);
                image.setAlphaBuffer(true);
                for (int y = 0; y < h; ++y)
                {
                    QRgb *line = reinterpret_cast<QRgb *>(image.scanLine(y));
                    const guchar *p = data + gsize(y) * gsize(w) * 4;
                    for (int x = 0; x < w; ++x, p += 4)
                    {
                        line[x] = qRgba(p[1], p[2], p[3], p[0]);
                    }
                }
                images.append(image);
            }
        }
        g_variant_unref(bytes);
        g_variant_unref(child);
    }
}

static void readMenuNode(GVariant *node, StatusNotifierMenuNode &out, int depth, int &budget);

// The children of a menu node (av of (ia{sv}av)).
static void readMenuChildren(GVariant *children, StatusNotifierMenuNode &out, int depth, int &budget)
{
    gsize count = g_variant_n_children(children);
    for (gsize i = 0; i < count && budget > 0; ++i)
    {
        GVariant *boxed = g_variant_get_child_value(children, i);
        GVariant *child = g_variant_get_variant(boxed);
        if (g_variant_is_of_type(child, G_VARIANT_TYPE("(ia{sv}av)")))
        {
            StatusNotifierMenuNode *node = new StatusNotifierMenuNode;
            node->id = -1;
            --budget;
            readMenuNode(child, *node, depth + 1, budget);
            if (node->id != -1)
                out.children.append(node);
            else
                delete node;
        }
        g_variant_unref(child);
        g_variant_unref(boxed);
    }
}

// A node of a dbusmenu layout; out.id stays -1 for an invisible one.
static void readMenuNode(GVariant *node, StatusNotifierMenuNode &out, int depth, int &budget)
{
    gint32 id = 0;
    GVariant *properties = 0;
    GVariant *children = 0;
    g_variant_get(node, "(i@a{sv}@av)", &id, &properties, &children);

    bool visible = true;
    GVariantIter iter;
    const char *name = 0;
    GVariant *value = 0;
    g_variant_iter_init(&iter, properties);
    while (g_variant_iter_next(&iter, "{&sv}", &name, &value))
    {
        if (!strcmp(name, "type"))
        {
            out.separator = variantString(value) == "separator";
        }
        else if (!strcmp(name, "label"))
        {
            out.label = variantString(value).left(MaxMenuLabel);
        }
        else if (!strcmp(name, "enabled") && g_variant_is_of_type(value, G_VARIANT_TYPE_BOOLEAN))
        {
            out.enabled = g_variant_get_boolean(value);
        }
        else if (!strcmp(name, "visible") && g_variant_is_of_type(value, G_VARIANT_TYPE_BOOLEAN))
        {
            visible = g_variant_get_boolean(value);
        }
        else if (!strcmp(name, "icon-name"))
        {
            out.iconName = variantString(value).left(256);
        }
        else if (!strcmp(name, "icon-data") && g_variant_is_of_type(value, G_VARIANT_TYPE_BYTESTRING))
        {
            gsize length = 0;
            const char *data = static_cast<const char *>(
                g_variant_get_fixed_array(value, &length, 1));
            // PNG only (as the specification says), and its size read from
            // the header before anything is decoded
            static const guchar signature[] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n' };
            const guchar *bytes = reinterpret_cast<const guchar *>(data);
            QImage image;
            if (data && length >= 24 && length <= MaxMenuIconBytes
                && !memcmp(bytes, signature, 8) && !memcmp(bytes + 12, "IHDR", 4))
            {
                guint32 w = (guint32(bytes[16]) << 24) | (bytes[17] << 16) | (bytes[18] << 8) | bytes[19];
                guint32 h = (guint32(bytes[20]) << 24) | (bytes[21] << 16) | (bytes[22] << 8) | bytes[23];
                if (w > 0 && h > 0 && w <= guint32(MaxMenuIconSide) && h <= guint32(MaxMenuIconSide)
                    && image.loadFromData(bytes, uint(length), "PNG")
                    && image.width() <= MaxMenuIconSide && image.height() <= MaxMenuIconSide)
                {
                    out.icon = image;
                }
            }
        }
        else if (!strcmp(name, "toggle-type"))
        {
            QString type = variantString(value);
            out.toggle = type == "checkmark" ? StatusNotifierMenuNode::CheckMark
                       : type == "radio" ? StatusNotifierMenuNode::Radio
                       : StatusNotifierMenuNode::NoToggle;
        }
        else if (!strcmp(name, "toggle-state") && g_variant_is_of_type(value, G_VARIANT_TYPE_INT32))
        {
            out.checked = g_variant_get_int32(value) == 1;
        }
        else if (!strcmp(name, "children-display"))
        {
            out.submenu = variantString(value) == "submenu";
        }
        else if (!strcmp(name, "shortcut") && g_variant_is_of_type(value, G_VARIANT_TYPE("aas"))
                 && g_variant_n_children(value) > 0)
        {
            // the first of the key combinations: ["Control", "q"]
            GVariant *keys = g_variant_get_child_value(value, 0);
            QStringList parts;
            gsize count = QMIN(g_variant_n_children(keys), gsize(8));
            for (gsize i = 0; i < count; ++i)
            {
                GVariant *key = g_variant_get_child_value(keys, i);
                QString part = variantString(key).left(32);
                g_variant_unref(key);
                if (part == "Control")
                    part = "Ctrl";
                else if (part == "Super")
                    part = "Meta";
                parts.append(part);
            }
            g_variant_unref(keys);
            out.shortcut = parts.join("+");
        }
        g_variant_unref(value);
    }

    if (visible)
    {
        out.id = id;
        if (depth < MaxMenuDepth)
        {
            readMenuChildren(children, out, depth, budget);
        }
        if (!out.children.isEmpty())
        {
            out.submenu = true;
        }
    }

    g_variant_unref(children);
    g_variant_unref(properties);
}

/******************************************************************************
 * The private part: the watcher, the host and the items.
 */

class StatusNotifierHostPrivate
{
public:
    struct Item
    {
        Item() : signalId(0), serial(0), failures(0), pending(false), inFlight(false), valid(false) {}
        QString service;        // unique bus name
        QString path;
        guint signalId;
        int serial;             // of the newest GetAll
        int failures;           // of GetAll before the first answer
        bool pending;           // a refresh is due
        bool inFlight;          // a GetAll is under way
        bool valid;             // the properties came at least once
        StatusNotifierItemData data;
    };

    // What an asynchronous call needs when its answer comes.
    struct Call
    {
        Call(StatusNotifierHostPrivate *d_, const QString &key_, int serial_ = 0)
            : d(d_), key(key_), serial(serial_), x(0), y(0) {}
        StatusNotifierHostPrivate *d;   // valid unless the call was cancelled
        QString key;
        int serial;
        int x, y;
    };

    StatusNotifierHostPrivate(StatusNotifierHost *host);
    ~StatusNotifierHostPrivate();

    void start();

    // host
    void addEntry(const QString &entry);
    void addItem(const QString &entry, const QString &service, const QString &path);
    void removeEntry(const QString &entry);
    void removeItem(const QString &key);
    void refresh(const QString &key);
    void scheduleRefresh(const QString &key);
    void readProperties(const QString &key, int serial, GVariant *reply);
    void watcherAppeared(const char *owner);
    void askWatcher();
    void watcherVanished();
    void call(const QString &key, const char *method, GVariant *parameters);

    // watcher
    void registerItem(GDBusMethodInvocation *invocation, const char *sender, const QString &service);
    void registerHost(GDBusMethodInvocation *invocation, const char *sender);
    void unregisterWatchedItem(const QString &entry);
    void nameVanished(const QString &name);
    GVariant *watcherProperty(const char *property);
    void emitWatcherSignal(const char *signal, GVariant *parameters);

    static void nameOwnerChanged(GDBusConnection *, const char *, const char *, const char *,
                                 const char *, GVariant *parameters, gpointer data);
    static void itemSignal(GDBusConnection *, const char *sender, const char *path, const char *,
                           const char *signal, GVariant *, gpointer data);
    static void watcherSignal(GDBusConnection *, const char *, const char *, const char *,
                              const char *signal, GVariant *parameters, gpointer data);
    static void watcherNameAppeared(GDBusConnection *, const char *, const char *owner, gpointer data);
    static void watcherNameVanished(GDBusConnection *, const char *, gpointer data);
    static void watcherNameAcquired(GDBusConnection *, const char *, gpointer data);
    static void watcherNameLost(GDBusConnection *, const char *, gpointer data);
    static void watcherMethod(GDBusConnection *, const char *sender, const char *, const char *,
                              const char *method, GVariant *parameters,
                              GDBusMethodInvocation *invocation, gpointer data);
    static GVariant *watcherGetProperty(GDBusConnection *, const char *, const char *, const char *,
                                        const char *property, GError **, gpointer data);

    static bool finished(GObject *source, GAsyncResult *result, GVariant **reply, GError **error);
    static void registeredItemsReady(GObject *source, GAsyncResult *result, gpointer data);
    static void ownerReady(GObject *source, GAsyncResult *result, gpointer data);
    static void watchedOwnerReady(GObject *source, GAsyncResult *result, gpointer data);
    static void propertiesReady(GObject *source, GAsyncResult *result, gpointer data);
    static void activateReady(GObject *source, GAsyncResult *result, gpointer data);
    static void menuReady(GObject *source, GAsyncResult *result, gpointer data);

    StatusNotifierHost *q;
    GDBusConnection *connection;
    GCancellable *cancellable;
    int serial;
    bool refreshScheduled;

    // host
    QCString hostName;
    guint hostNameId;
    guint watcherWatchId;
    guint watcherSignalId;
    QCString watcherOwner;          // unique name of the watcher, if any
    bool watcherIsSelf;             // that is this host's own watcher
    int watcherRetries;
    guint nameOwnerSignalId;
    QMap<QString, Item> items;      // by key: unique name + path
    QStringList order;              // keys of the valid items
    QMap<QString, QString> entries; // watcher's entry -> key, "" while resolving

    // watcher
    GDBusNodeInfo *watcherInfo;
    guint watcherObjectId;
    guint watcherNameId;
    bool isWatcher;
    QStringList watchedItems;           // entries, in order
    QMap<QString, QString> watchedNames;// entry -> the bus name it lives on
    QStringList hosts;                  // unique names
};

StatusNotifierHostPrivate::StatusNotifierHostPrivate(StatusNotifierHost *host)
    : q(host), connection(0), cancellable(0), serial(0), refreshScheduled(false),
      hostNameId(0), watcherWatchId(0), watcherSignalId(0), watcherIsSelf(false),
      watcherRetries(0), nameOwnerSignalId(0),
      watcherInfo(0), watcherObjectId(0), watcherNameId(0), isWatcher(false)
{
}

void StatusNotifierHostPrivate::start()
{
    // Without a session bus there are no items; no autolaunching of one.
    QCString runtimeBus = QCString(getenv("XDG_RUNTIME_DIR")) + "/bus";
    if (!getenv("DBUS_SESSION_BUS_ADDRESS")
        && (!getenv("XDG_RUNTIME_DIR") || access(runtimeBus.data(), F_OK) != 0))
    {
        return;
    }

    GError *error = 0;
    connection = g_bus_get_sync(G_BUS_TYPE_SESSION, 0, &error);
    if (!connection)
    {
        kdWarning() << "System tray: no session bus: "
                    << (error ? error->message : "") << endl;
        if (error)
            g_error_free(error);
        return;
    }
    // the session bus going away must not end the panel
    g_dbus_connection_set_exit_on_close(connection, FALSE);
    cancellable = g_cancellable_new();

    nameOwnerSignalId = g_dbus_connection_signal_subscribe(
        connection, "org.freedesktop.DBus", "org.freedesktop.DBus", "NameOwnerChanged",
        "/org/freedesktop/DBus", 0, G_DBUS_SIGNAL_FLAGS_NONE, nameOwnerChanged, this, 0);

    // The watcher: ours when no other program is the watcher; queued for
    // the name otherwise, and the watcher when the other one goes.
    watcherInfo = g_dbus_node_info_new_for_xml(WatcherXml, 0);
    if (watcherInfo)
    {
        static const GDBusInterfaceVTable vtable = { watcherMethod, watcherGetProperty, 0, {0} };
        watcherObjectId = g_dbus_connection_register_object(
            connection, WatcherPath, watcherInfo->interfaces[0], &vtable, this, 0, 0);
    }
    if (watcherObjectId)
    {
        watcherNameId = g_bus_own_name_on_connection(
            connection, WatcherName, G_BUS_NAME_OWNER_FLAGS_NONE,
            watcherNameAcquired, watcherNameLost, this, 0);
    }

    // The host: registered with whichever watcher there is.
    hostName = QCString("org.kde.StatusNotifierHost-") + QCString().setNum(getpid());
    hostNameId = g_bus_own_name_on_connection(
        connection, hostName.data(), G_BUS_NAME_OWNER_FLAGS_DO_NOT_QUEUE, 0, 0, 0, 0);
    watcherWatchId = g_bus_watch_name_on_connection(
        connection, WatcherName, G_BUS_NAME_WATCHER_FLAGS_NONE,
        watcherNameAppeared, watcherNameVanished, this, 0);
}

StatusNotifierHostPrivate::~StatusNotifierHostPrivate()
{
    if (!connection)
    {
        return;
    }

    // Nothing that is still under way may come back to us.
    g_cancellable_cancel(cancellable);
    g_object_unref(cancellable);

    for (QMap<QString, Item>::Iterator it = items.begin(); it != items.end(); ++it)
    {
        if ((*it).signalId)
            g_dbus_connection_signal_unsubscribe(connection, (*it).signalId);
    }
    if (watcherSignalId)
        g_dbus_connection_signal_unsubscribe(connection, watcherSignalId);
    if (nameOwnerSignalId)
        g_dbus_connection_signal_unsubscribe(connection, nameOwnerSignalId);
    if (watcherWatchId)
        g_bus_unwatch_name(watcherWatchId);
    if (hostNameId)
        g_bus_unown_name(hostNameId);
    if (watcherNameId)
        g_bus_unown_name(watcherNameId);
    if (watcherObjectId)
        g_dbus_connection_unregister_object(connection, watcherObjectId);
    if (watcherInfo)
        g_dbus_node_info_unref(watcherInfo);

    g_object_unref(connection);
}

// The end of an asynchronous call: false when it was cancelled (the host is
// gone or going: nothing may be touched), *reply is 0 on an error.
bool StatusNotifierHostPrivate::finished(GObject *source, GAsyncResult *result,
                                         GVariant **reply, GError **error)
{
    *reply = g_dbus_connection_call_finish(G_DBUS_CONNECTION(source), result, error);
    if (!*reply && g_error_matches(*error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
    {
        g_error_free(*error);
        *error = 0;
        return false;
    }
    return true;
}

/******************************************************************************
 * The host.
 */

void StatusNotifierHostPrivate::watcherAppeared(const char *owner)
{
    if (watcherSignalId)
    {
        g_dbus_connection_signal_unsubscribe(connection, watcherSignalId);
        watcherSignalId = 0;
    }
    watcherOwner = owner;
    watcherRetries = 0;
    watcherIsSelf = watcherOwner == g_dbus_connection_get_unique_name(connection);

    if (watcherIsSelf)
    {
        // Our own watcher: no round trips through the bus (registerItem()
        // and unregisterWatchedItem() tell the host directly), so nothing
        // is lost while this process is busy (kicker starting).
        QString self = QString::fromUtf8(watcherOwner);
        if (!hosts.contains(self))
        {
            hosts.append(self);
            if (hosts.count() == 1)
                emitWatcherSignal("StatusNotifierHostRegistered", 0);
        }
        const QStringList registered = watchedItems;
        for (QStringList::ConstIterator it = registered.begin(); it != registered.end(); ++it)
        {
            addEntry(*it);
        }
        return;
    }

    watcherSignalId = g_dbus_connection_signal_subscribe(
        connection, owner, WatcherInterface, 0, WatcherPath, 0,
        G_DBUS_SIGNAL_FLAGS_NONE, watcherSignal, this, 0);
    askWatcher();
}

// Registers the host with another program's watcher and asks it for the
// items (again when the answer did not come).
void StatusNotifierHostPrivate::askWatcher()
{
    g_dbus_connection_call(connection, watcherOwner, WatcherPath, WatcherInterface,
                           "RegisterStatusNotifierHost", g_variant_new("(s)", hostName.data()),
                           0, G_DBUS_CALL_FLAGS_NONE, -1, 0, 0, 0);
    g_dbus_connection_call(connection, watcherOwner, WatcherPath, "org.freedesktop.DBus.Properties",
                           "Get", g_variant_new("(ss)", WatcherInterface, "RegisteredStatusNotifierItems"),
                           G_VARIANT_TYPE("(v)"), G_DBUS_CALL_FLAGS_NONE, PropertiesTimeout,
                           cancellable, registeredItemsReady,
                           new Call(this, QString::fromUtf8(watcherOwner)));
}

void StatusNotifierHost::askWatcher()
{
    if (!d->watcherOwner.isEmpty() && !d->watcherIsSelf)
    {
        d->askWatcher();
    }
}

void StatusNotifierHostPrivate::watcherVanished()
{
    // The items stay: they come back with the next watcher, and the ones
    // which go meanwhile are seen going (NameOwnerChanged).
    if (watcherSignalId)
    {
        g_dbus_connection_signal_unsubscribe(connection, watcherSignalId);
        watcherSignalId = 0;
    }
    watcherOwner = QCString();
    watcherIsSelf = false;
}

void StatusNotifierHostPrivate::registeredItemsReady(GObject *source, GAsyncResult *result, gpointer data)
{
    Call *c = static_cast<Call *>(data);
    GVariant *reply = 0;
    GError *error = 0;
    if (finished(source, result, &reply, &error))
    {
        if (c->key != QString::fromUtf8(c->d->watcherOwner))
        {
            // the answer of a watcher which is gone
            if (reply)
                g_variant_unref(reply);
            if (error)
                g_error_free(error);
        }
        else if (reply)
        {
            GVariant *value = 0;
            g_variant_get(reply, "(v)", &value);
            if (g_variant_is_of_type(value, G_VARIANT_TYPE_STRING_ARRAY))
            {
                gsize count = g_variant_n_children(value);
                for (gsize i = 0; i < count && i < gsize(MaxItems); ++i)
                {
                    GVariant *entry = g_variant_get_child_value(value, i);
                    c->d->addEntry(variantString(entry));
                    g_variant_unref(entry);
                }
            }
            g_variant_unref(value);
            g_variant_unref(reply);
        }
        else
        {
            kdWarning() << "System tray: the items of the StatusNotifierWatcher: "
                        << error->message << endl;
            g_error_free(error);
            if (++c->d->watcherRetries <= 5)
                QTimer::singleShot(2000, c->d->q, SLOT(askWatcher()));
        }
    }
    delete c;
}

void StatusNotifierHostPrivate::addEntry(const QString &entry)
{
    if (entry.isEmpty() || entries.contains(entry) || int(entries.count()) >= MaxItems)
    {
        return;
    }

    int slash = entry.find('/');
    QString service = slash < 0 ? entry : entry.left(slash);
    QString path = slash < 0 ? QString(DefaultItemPath) : entry.mid(slash);
    if (!g_dbus_is_name(service.utf8()) || !g_variant_is_object_path(path.utf8()))
    {
        return;
    }

    if (g_dbus_is_unique_name(service.utf8()))
    {
        addItem(entry, service, path);
        return;
    }

    // A well-known name: the item is followed by the unique name owning it.
    entries.insert(entry, QString::null);
    Call *c = new Call(this, entry);
    g_dbus_connection_call(connection, "org.freedesktop.DBus", "/org/freedesktop/DBus",
                           "org.freedesktop.DBus", "GetNameOwner",
                           g_variant_new("(s)", service.utf8().data()), G_VARIANT_TYPE("(s)"),
                           G_DBUS_CALL_FLAGS_NONE, PropertiesTimeout, cancellable, ownerReady, c);
}

void StatusNotifierHostPrivate::ownerReady(GObject *source, GAsyncResult *result, gpointer data)
{
    Call *c = static_cast<Call *>(data);
    GVariant *reply = 0;
    GError *error = 0;
    if (finished(source, result, &reply, &error))
    {
        StatusNotifierHostPrivate *d = c->d;
        const QString &entry = c->key;
        // unregistered meanwhile?
        if (d->entries.contains(entry) && d->entries[entry].isNull())
        {
            d->entries.remove(entry);
            if (reply)
            {
                const char *owner = 0;
                g_variant_get(reply, "(&s)", &owner);
                int slash = entry.find('/');
                d->addItem(entry, QString::fromUtf8(owner),
                           slash < 0 ? QString(DefaultItemPath) : entry.mid(slash));
            }
        }
        if (reply)
            g_variant_unref(reply);
        if (error)
            g_error_free(error);
    }
    delete c;
}

void StatusNotifierHostPrivate::addItem(const QString &entry, const QString &service, const QString &path)
{
    QString key = service + path;
    entries.insert(entry, key);
    if (items.contains(key) || int(items.count()) >= MaxItems)
    {
        return;
    }

    Item &item = items[key];
    item.service = service;
    item.path = path;
    item.signalId = g_dbus_connection_signal_subscribe(
        connection, service.utf8(), ItemInterface, 0, path.utf8(), 0,
        G_DBUS_SIGNAL_FLAGS_NONE, itemSignal, this, 0);
    refresh(key);
}

void StatusNotifierHostPrivate::removeEntry(const QString &entry)
{
    if (!entries.contains(entry))
    {
        return;
    }
    QString key = entries[entry];
    entries.remove(entry);
    if (key.isEmpty())
    {
        return;
    }
    // another entry for the same item (its unique and well-known names)?
    for (QMap<QString, QString>::ConstIterator it = entries.begin(); it != entries.end(); ++it)
    {
        if (*it == key)
            return;
    }
    removeItem(key);
}

void StatusNotifierHostPrivate::removeItem(const QString &key)
{
    QMap<QString, Item>::Iterator it = items.find(key);
    if (it == items.end())
    {
        return;
    }
    if ((*it).signalId)
    {
        g_dbus_connection_signal_unsubscribe(connection, (*it).signalId);
    }
    bool valid = (*it).valid;
    items.remove(it);

    QMap<QString, QString>::Iterator e = entries.begin();
    while (e != entries.end())
    {
        if (*e == key)
        {
            QMap<QString, QString>::Iterator next = e;
            ++next;
            entries.remove(e);
            e = next;
        }
        else
        {
            ++e;
        }
    }

    if (valid)
    {
        order.remove(key);
        emit q->itemRemoved(key);
    }
}

void StatusNotifierHostPrivate::refresh(const QString &key)
{
    Item &item = items[key];
    if (item.inFlight)
    {
        // once more when the answer is there
        item.pending = true;
        return;
    }
    item.pending = false;
    item.inFlight = true;
    item.serial = ++serial;
    g_dbus_connection_call(connection, item.service.utf8(), item.path.utf8(),
                           "org.freedesktop.DBus.Properties", "GetAll",
                           g_variant_new("(s)", ItemInterface), G_VARIANT_TYPE("(a{sv})"),
                           G_DBUS_CALL_FLAGS_NONE, PropertiesTimeout, cancellable,
                           propertiesReady, new Call(this, key, item.serial));
}

void StatusNotifierHostPrivate::scheduleRefresh(const QString &key)
{
    QMap<QString, Item>::Iterator it = items.find(key);
    if (it == items.end())
    {
        return;
    }
    (*it).pending = true;
    if (!refreshScheduled)
    {
        refreshScheduled = true;
        QTimer::singleShot(RefreshDelay, q, SLOT(refreshPending()));
    }
}

void StatusNotifierHost::refreshPending()
{
    d->refreshScheduled = false;
    QStringList due;
    for (QMap<QString, StatusNotifierHostPrivate::Item>::ConstIterator it = d->items.begin();
         it != d->items.end(); ++it)
    {
        if ((*it).pending)
            due.append(it.key());
    }
    for (QStringList::ConstIterator it = due.begin(); it != due.end(); ++it)
    {
        d->refresh(*it);
    }
}

void StatusNotifierHostPrivate::propertiesReady(GObject *source, GAsyncResult *result, gpointer data)
{
    Call *c = static_cast<Call *>(data);
    GVariant *reply = 0;
    GError *error = 0;
    if (finished(source, result, &reply, &error))
    {
        StatusNotifierHostPrivate *d = c->d;
        QMap<QString, Item>::Iterator it = d->items.find(c->key);
        if (it != d->items.end() && (*it).serial == c->serial)
        {
            (*it).inFlight = false;
            bool again = (*it).pending;
            if (reply)
            {
                d->readProperties(c->key, c->serial, reply);
            }
            else if (!(*it).valid)
            {
                // Not there (yet: exported after registering, or the
                // panel busy): a few more tries, then it is not an item.
                kdDebug() << "System tray: no StatusNotifierItem at " << c->key
                          << ": " << error->message << endl;
                if (++(*it).failures <= 3)
                {
                    (*it).pending = true;
                    QTimer::singleShot(1000, d->q, SLOT(refreshPending()));
                    again = false;
                }
                else
                {
                    d->removeItem(c->key);
                    again = false;
                }
            }
            if (again)
                d->scheduleRefresh(c->key);
        }
        if (reply)
            g_variant_unref(reply);
        if (error)
            g_error_free(error);
    }
    delete c;
}

void StatusNotifierHostPrivate::readProperties(const QString &key, int, GVariant *reply)
{
    Item &item = items[key];
    StatusNotifierItemData data;
    gsize budget = MaxImageBytes;

    GVariant *dict = g_variant_get_child_value(reply, 0);
    GVariantIter iter;
    const char *name = 0;
    GVariant *value = 0;
    g_variant_iter_init(&iter, dict);
    while (g_variant_iter_next(&iter, "{&sv}", &name, &value))
    {
        if (!strcmp(name, "Id"))
            data.id = variantString(value);
        else if (!strcmp(name, "Title"))
            data.title = variantString(value);
        else if (!strcmp(name, "Category"))
            data.category = variantString(value);
        else if (!strcmp(name, "Status"))
            data.status = variantString(value);
        else if (!strcmp(name, "IconThemePath"))
            data.iconThemePath = variantString(value);
        else if (!strcmp(name, "IconName"))
            data.iconName = variantString(value);
        else if (!strcmp(name, "OverlayIconName"))
            data.overlayIconName = variantString(value);
        else if (!strcmp(name, "AttentionIconName"))
            data.attentionIconName = variantString(value);
        else if (!strcmp(name, "IconPixmap"))
            readImages(value, data.iconImages, budget);
        else if (!strcmp(name, "OverlayIconPixmap"))
            readImages(value, data.overlayImages, budget);
        else if (!strcmp(name, "AttentionIconPixmap"))
            readImages(value, data.attentionImages, budget);
        else if (!strcmp(name, "Menu"))
        {
            QString path = variantString(value);
            if (path != "/" && path != "/NO_DBUSMENU" && g_variant_is_object_path(path.utf8()))
                data.menuPath = path;
        }
        else if (!strcmp(name, "ItemIsMenu") && g_variant_is_of_type(value, G_VARIANT_TYPE_BOOLEAN))
            data.itemIsMenu = g_variant_get_boolean(value);
        else if (!strcmp(name, "ToolTip") && g_variant_is_of_type(value, G_VARIANT_TYPE("(sa(iiay)ss)")))
        {
            GVariant *title = g_variant_get_child_value(value, 2);
            GVariant *text = g_variant_get_child_value(value, 3);
            data.toolTipTitle = variantString(title);
            data.toolTipText = variantString(text);
            g_variant_unref(title);
            g_variant_unref(text);
        }
        g_variant_unref(value);
    }
    g_variant_unref(dict);

    item.data = data;
    if (!item.valid)
    {
        item.valid = true;
        order.append(key);
        emit q->itemAdded(key);
    }
    else
    {
        emit q->itemChanged(key);
    }
}

void StatusNotifierHostPrivate::itemSignal(GDBusConnection *, const char *sender, const char *path,
                                           const char *, const char *signal, GVariant *, gpointer data)
{
    StatusNotifierHostPrivate *d = static_cast<StatusNotifierHostPrivate *>(data);
    // NewTitle, NewIcon, NewAttentionIcon, NewOverlayIcon, NewToolTip,
    // NewStatus, NewIconThemePath, NewMenu: all the properties again
    if (!strncmp(signal, "New", 3))
    {
        d->scheduleRefresh(QString::fromUtf8(sender) + QString::fromUtf8(path));
    }
}

void StatusNotifierHostPrivate::watcherSignal(GDBusConnection *, const char *, const char *,
                                              const char *, const char *signal,
                                              GVariant *parameters, gpointer data)
{
    StatusNotifierHostPrivate *d = static_cast<StatusNotifierHostPrivate *>(data);
    if (!g_variant_is_of_type(parameters, G_VARIANT_TYPE("(s)")))
    {
        return;
    }
    const char *entry = 0;
    g_variant_get(parameters, "(&s)", &entry);
    if (!strcmp(signal, "StatusNotifierItemRegistered"))
        d->addEntry(QString::fromUtf8(entry));
    else if (!strcmp(signal, "StatusNotifierItemUnregistered"))
        d->removeEntry(QString::fromUtf8(entry));
}

void StatusNotifierHostPrivate::watcherNameAppeared(GDBusConnection *, const char *, const char *owner, gpointer data)
{
    static_cast<StatusNotifierHostPrivate *>(data)->watcherAppeared(owner);
}

void StatusNotifierHostPrivate::watcherNameVanished(GDBusConnection *, const char *, gpointer data)
{
    static_cast<StatusNotifierHostPrivate *>(data)->watcherVanished();
}

void StatusNotifierHostPrivate::nameOwnerChanged(GDBusConnection *, const char *, const char *,
                                                 const char *, const char *,
                                                 GVariant *parameters, gpointer data)
{
    StatusNotifierHostPrivate *d = static_cast<StatusNotifierHostPrivate *>(data);
    if (!g_variant_is_of_type(parameters, G_VARIANT_TYPE("(sss)")))
    {
        return;
    }
    const char *name = 0, *oldOwner = 0, *newOwner = 0;
    g_variant_get(parameters, "(&s&s&s)", &name, &oldOwner, &newOwner);
    if (*newOwner)
    {
        return;
    }
    QString gone = QString::fromUtf8(name);
    d->nameVanished(gone);

    // the items on that connection
    if (gone.startsWith(":"))
    {
        QStringList dead;
        for (QMap<QString, Item>::ConstIterator it = d->items.begin(); it != d->items.end(); ++it)
        {
            if ((*it).service == gone)
                dead.append(it.key());
        }
        for (QStringList::ConstIterator it = dead.begin(); it != dead.end(); ++it)
        {
            d->removeItem(*it);
        }
    }
}

void StatusNotifierHostPrivate::call(const QString &key, const char *method, GVariant *parameters)
{
    QMap<QString, Item>::ConstIterator it = items.find(key);
    if (it == items.end())
    {
        g_variant_unref(g_variant_ref_sink(parameters));
        return;
    }
    g_dbus_connection_call(connection, (*it).service.utf8(), (*it).path.utf8(), ItemInterface,
                           method, parameters, 0, G_DBUS_CALL_FLAGS_NONE, -1, 0, 0, 0);
}

void StatusNotifierHostPrivate::activateReady(GObject *source, GAsyncResult *result, gpointer data)
{
    Call *c = static_cast<Call *>(data);
    GVariant *reply = 0;
    GError *error = 0;
    if (finished(source, result, &reply, &error))
    {
        if (reply)
        {
            g_variant_unref(reply);
        }
        else
        {
            // Not implemented (libappindicator has only a menu); not when
            // the item is just slow.
            if (!g_error_matches(error, G_IO_ERROR, G_IO_ERROR_TIMED_OUT)
                && c->d->items.contains(c->key))
            {
                emit c->d->q->activateFailed(c->key, c->serial, c->x, c->y);
            }
            g_error_free(error);
        }
    }
    delete c;
}

void StatusNotifierHostPrivate::menuReady(GObject *source, GAsyncResult *result, gpointer data)
{
    Call *c = static_cast<Call *>(data);
    GVariant *reply = 0;
    GError *error = 0;
    if (finished(source, result, &reply, &error))
    {
        StatusNotifierHost *q = c->d->q;
        if (!c->d->items.contains(c->key))
        {
            // gone: nobody waits
        }
        else if (reply)
        {
            StatusNotifierMenuNode root;
            GVariant *layout = g_variant_get_child_value(reply, 1);
            int budget = MaxMenuNodes;
            root.id = -1;
            readMenuNode(layout, root, 0, budget);
            g_variant_unref(layout);
            if (root.id == -1)
                emit q->menuFailed(c->key, c->serial);
            else
                emit q->menuReady(c->key, c->serial, root);
        }
        else
        {
            kdDebug() << "System tray: the menu of " << c->key << ": " << error->message << endl;
            emit q->menuFailed(c->key, c->serial);
        }
        if (reply)
            g_variant_unref(reply);
        if (error)
            g_error_free(error);
    }
    delete c;
}

/******************************************************************************
 * The watcher.
 */

void StatusNotifierHostPrivate::watcherNameAcquired(GDBusConnection *, const char *, gpointer data)
{
    StatusNotifierHostPrivate *d = static_cast<StatusNotifierHostPrivate *>(data);
    d->isWatcher = true;
    // our own host is there from the start (IsStatusNotifierHostRegistered
    // decides for some programs whether they use the tray at all)
    QString self = QString::fromUtf8(g_dbus_connection_get_unique_name(d->connection));
    if (!d->hosts.contains(self))
        d->hosts.append(self);
}

void StatusNotifierHostPrivate::watcherNameLost(GDBusConnection *, const char *, gpointer data)
{
    // Another program is the watcher (we wait for the name), or the bus is
    // gone.  The items register with that one.
    StatusNotifierHostPrivate *d = static_cast<StatusNotifierHostPrivate *>(data);
    d->isWatcher = false;
    d->watchedItems.clear();
    d->watchedNames.clear();
    d->hosts.clear();
}

void StatusNotifierHostPrivate::watcherMethod(GDBusConnection *, const char *sender, const char *,
                                              const char *, const char *method, GVariant *parameters,
                                              GDBusMethodInvocation *invocation, gpointer data)
{
    StatusNotifierHostPrivate *d = static_cast<StatusNotifierHostPrivate *>(data);
    if (!d->isWatcher)
    {
        // called by the unique name while another program is the watcher
        g_dbus_method_invocation_return_dbus_error(invocation, "org.freedesktop.DBus.Error.ServiceUnknown",
                                                   "Not the StatusNotifierWatcher");
        return;
    }
    const char *service = 0;
    g_variant_get(parameters, "(&s)", &service);
    if (!strcmp(method, "RegisterStatusNotifierItem"))
        d->registerItem(invocation, sender, QString::fromUtf8(service));
    else if (!strcmp(method, "RegisterStatusNotifierHost"))
        d->registerHost(invocation, sender);
    else
        g_dbus_method_invocation_return_dbus_error(invocation, "org.freedesktop.DBus.Error.UnknownMethod",
                                                   "Unknown method");
}

void StatusNotifierHostPrivate::registerItem(GDBusMethodInvocation *invocation, const char *sender,
                                             const QString &service)
{
    // "/path" (libappindicator): an object of the caller; a bus name: its
    // object /StatusNotifierItem
    QString name = service.startsWith("/") ? QString::fromUtf8(sender) : service;
    QString path = service.startsWith("/") ? service : QString(DefaultItemPath);
    if (!g_dbus_is_name(name.utf8()) || !g_variant_is_object_path(path.utf8()))
    {
        g_dbus_method_invocation_return_dbus_error(invocation, "org.freedesktop.DBus.Error.InvalidArgs",
                                                   "Not a bus name or an object path");
        return;
    }
    QString entry = name + path;
    if (watchedItems.contains(entry))
    {
        g_dbus_method_invocation_return_value(invocation, 0);
        return;
    }
    if (int(watchedItems.count()) >= MaxItems)
    {
        g_dbus_method_invocation_return_dbus_error(invocation, "org.freedesktop.DBus.Error.LimitsExceeded",
                                                   "Too many items");
        return;
    }

    watchedItems.append(entry);
    watchedNames.insert(entry, name);
    g_dbus_method_invocation_return_value(invocation, 0);
    emitWatcherSignal("StatusNotifierItemRegistered", g_variant_new("(s)", entry.utf8().data()));
    if (watcherIsSelf)
    {
        addEntry(entry);
    }

    // A well-known name must have an owner, whose going ends the item.
    if (!g_dbus_is_unique_name(name.utf8()))
    {
        g_dbus_connection_call(connection, "org.freedesktop.DBus", "/org/freedesktop/DBus",
                               "org.freedesktop.DBus", "GetNameOwner",
                               g_variant_new("(s)", name.utf8().data()), G_VARIANT_TYPE("(s)"),
                               G_DBUS_CALL_FLAGS_NONE, PropertiesTimeout, cancellable,
                               watchedOwnerReady, new Call(this, entry));
    }
}

void StatusNotifierHostPrivate::watchedOwnerReady(GObject *source, GAsyncResult *result, gpointer data)
{
    Call *c = static_cast<Call *>(data);
    GVariant *reply = 0;
    GError *error = 0;
    if (finished(source, result, &reply, &error))
    {
        if (reply)
            g_variant_unref(reply);
        else
        {
            c->d->unregisterWatchedItem(c->key);
            g_error_free(error);
        }
    }
    delete c;
}

void StatusNotifierHostPrivate::registerHost(GDBusMethodInvocation *invocation, const char *sender)
{
    QString name = QString::fromUtf8(sender);
    bool first = hosts.isEmpty();
    if (!hosts.contains(name) && int(hosts.count()) < MaxHosts)
    {
        hosts.append(name);
    }
    g_dbus_method_invocation_return_value(invocation, 0);
    if (first && !hosts.isEmpty())
    {
        emitWatcherSignal("StatusNotifierHostRegistered", 0);
    }
}

void StatusNotifierHostPrivate::unregisterWatchedItem(const QString &entry)
{
    if (!watchedItems.contains(entry))
    {
        return;
    }
    watchedItems.remove(entry);
    watchedNames.remove(entry);
    emitWatcherSignal("StatusNotifierItemUnregistered", g_variant_new("(s)", entry.utf8().data()));
    if (watcherIsSelf)
    {
        removeEntry(entry);
    }
}

void StatusNotifierHostPrivate::nameVanished(const QString &name)
{
    if (!isWatcher)
    {
        return;
    }
    QStringList gone;
    for (QMap<QString, QString>::ConstIterator it = watchedNames.begin(); it != watchedNames.end(); ++it)
    {
        if (*it == name)
            gone.append(it.key());
    }
    for (QStringList::ConstIterator it = gone.begin(); it != gone.end(); ++it)
    {
        unregisterWatchedItem(*it);
    }
    if (hosts.contains(name))
    {
        hosts.remove(name);
        if (hosts.isEmpty())
            emitWatcherSignal("StatusNotifierHostUnregistered", 0);
    }
}

GVariant *StatusNotifierHostPrivate::watcherProperty(const char *property)
{
    if (!strcmp(property, "RegisteredStatusNotifierItems"))
    {
        GVariantBuilder builder;
        g_variant_builder_init(&builder, G_VARIANT_TYPE_STRING_ARRAY);
        for (QStringList::ConstIterator it = watchedItems.begin(); it != watchedItems.end(); ++it)
        {
            g_variant_builder_add(&builder, "s", (*it).utf8().data());
        }
        return g_variant_builder_end(&builder);
    }
    if (!strcmp(property, "IsStatusNotifierHostRegistered"))
    {
        return g_variant_new_boolean(!hosts.isEmpty());
    }
    if (!strcmp(property, "ProtocolVersion"))
    {
        return g_variant_new_int32(0);
    }
    return 0;
}

GVariant *StatusNotifierHostPrivate::watcherGetProperty(GDBusConnection *, const char *, const char *,
                                                        const char *, const char *property,
                                                        GError **error, gpointer data)
{
    StatusNotifierHostPrivate *d = static_cast<StatusNotifierHostPrivate *>(data);
    GVariant *value = d->isWatcher ? d->watcherProperty(property) : 0;
    if (!value)
    {
        g_set_error(error, G_DBUS_ERROR, G_DBUS_ERROR_UNKNOWN_PROPERTY, "No property %s", property);
    }
    return value;
}

void StatusNotifierHostPrivate::emitWatcherSignal(const char *signal, GVariant *parameters)
{
    g_dbus_connection_emit_signal(connection, 0, WatcherPath, WatcherInterface, signal, parameters, 0);
}

/******************************************************************************
 * StatusNotifierHost
 */

StatusNotifierHost::StatusNotifierHost()
    : QObject(0, "StatusNotifierHost")
{
    d = new StatusNotifierHostPrivate(this);
    d->start();
}

StatusNotifierHost::~StatusNotifierHost()
{
    delete d;
}

StatusNotifierHost *StatusNotifierHost::ref()
{
    if (!s_host)
    {
        StatusNotifierHost *host = new StatusNotifierHost;
        if (!host->d->connection)
        {
            delete host;
            return 0;
        }
        s_host = host;
    }
    ++s_refs;
    return s_host;
}

void StatusNotifierHost::deref()
{
    if (--s_refs == 0)
    {
        s_host = 0;
        delete this;
    }
}

QStringList StatusNotifierHost::items() const
{
    return d->order;
}

const StatusNotifierItemData *StatusNotifierHost::item(const QString &key) const
{
    QMap<QString, StatusNotifierHostPrivate::Item>::ConstIterator it = d->items.find(key);
    if (it == d->items.end() || !(*it).valid)
    {
        return 0;
    }
    return &(*it).data;
}

int StatusNotifierHost::activate(const QString &key, int x, int y)
{
    QMap<QString, StatusNotifierHostPrivate::Item>::ConstIterator it = d->items.find(key);
    if (it == d->items.end())
    {
        return -1;
    }
    int serial = ++d->serial;
    StatusNotifierHostPrivate::Call *c = new StatusNotifierHostPrivate::Call(d, key, serial);
    c->x = x;
    c->y = y;
    g_dbus_connection_call(d->connection, (*it).service.utf8(), (*it).path.utf8(), ItemInterface,
                           "Activate", g_variant_new("(ii)", x, y), 0, G_DBUS_CALL_FLAGS_NONE,
                           ActivateTimeout, d->cancellable, StatusNotifierHostPrivate::activateReady, c);
    return serial;
}

void StatusNotifierHost::secondaryActivate(const QString &key, int x, int y)
{
    d->call(key, "SecondaryActivate", g_variant_new("(ii)", x, y));
}

void StatusNotifierHost::scroll(const QString &key, int delta, bool horizontal)
{
    d->call(key, "Scroll", g_variant_new("(is)", delta, horizontal ? "horizontal" : "vertical"));
}

void StatusNotifierHost::contextMenu(const QString &key, int x, int y)
{
    d->call(key, "ContextMenu", g_variant_new("(ii)", x, y));
}

int StatusNotifierHost::fetchMenu(const QString &key, int parentId)
{
    QMap<QString, StatusNotifierHostPrivate::Item>::ConstIterator it = d->items.find(key);
    if (it == d->items.end() || (*it).data.menuPath.isEmpty())
    {
        return -1;
    }
    QCString service = (*it).service.utf8();
    QCString path = (*it).data.menuPath.utf8();

    // the item may fill the menu now; it is asked first
    g_dbus_connection_call(d->connection, service, path, MenuInterface, "AboutToShow",
                           g_variant_new("(i)", parentId), 0, G_DBUS_CALL_FLAGS_NONE,
                           -1, 0, 0, 0);

    int serial = ++d->serial;
    const char *noProperties[] = { 0 };
    g_dbus_connection_call(d->connection, service, path, MenuInterface, "GetLayout",
                           g_variant_new("(ii^as)", parentId, -1, noProperties),
                           G_VARIANT_TYPE("(u(ia{sv}av))"), G_DBUS_CALL_FLAGS_NONE, MenuTimeout,
                           d->cancellable, StatusNotifierHostPrivate::menuReady,
                           new StatusNotifierHostPrivate::Call(d, key, serial));
    return serial;
}

void StatusNotifierHost::menuEvent(const QString &key, int id, const char *event)
{
    QMap<QString, StatusNotifierHostPrivate::Item>::ConstIterator it = d->items.find(key);
    if (it == d->items.end() || (*it).data.menuPath.isEmpty())
    {
        return;
    }
    g_dbus_connection_call(d->connection, (*it).service.utf8(), (*it).data.menuPath.utf8(),
                           MenuInterface, "Event",
                           g_variant_new("(isvu)", id, event, g_variant_new_int32(0),
                                         guint32(qt_x_time)),
                           0, G_DBUS_CALL_FLAGS_NONE, -1, 0, 0, 0);
}

#include "statusnotifierhost.moc"
