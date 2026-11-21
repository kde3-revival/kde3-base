/* This file is part of the KDE Project

   The D-Bus side of the UDisks2 backend of the media manager, on GIO's
   GDBus.

   This library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Library General Public
   License version 2 as published by the Free Software Foundation.

   This library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Library General Public License for more details.

   You should have received a copy of the GNU Library General Public License
   along with this library; see the file COPYING.LIB.  If not, write to
   the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
   Boston, MA 02110-1301, USA.
*/

// GLib's headers must come before any Qt header: Qt 3 defines "signals"
// as a macro, which breaks GLib's declarations that use the word.
#include <gio/gio.h>

#include <stdlib.h>
#include <string.h>

#include <qfile.h>
#include <qptrlist.h>
#include <kdebug.h>

#include "udisks2dbus.h"

static const char udisksService[] = "org.freedesktop.UDisks2";
static const char udisksPath[] = "/org/freedesktop/UDisks2";
static const char drivesPrefix[] = "/org/freedesktop/UDisks2/drives/";
static const char blockDevicesPrefix[] = "/org/freedesktop/UDisks2/block_devices/";

static QString errorString(GError *err)
{
    QString msg = QString::fromUtf8(err->message);
    g_error_free(err);
    return msg;
}

// A byte string ("ay"), e.g. a mount point: NUL terminated, in the
// encoding of the file names
static QString byteString(GVariant *value)
{
    gsize size = 0;
    const char *bytes = static_cast<const char *>(g_variant_get_fixed_array(value, &size, 1));
    if(!bytes)
        return QString::null;
    gsize len = 0;
    while(len < size && bytes[len])
        ++len;
    return QFile::decodeName(QCString(bytes, len + 1));
}

static bool toVariant(GVariant *value, QVariant &result)
{
    if(g_variant_is_of_type(value, G_VARIANT_TYPE_BOOLEAN))
        result = QVariant(bool(g_variant_get_boolean(value)), 0);
    else if(g_variant_is_of_type(value, G_VARIANT_TYPE_INT32))
        result = QVariant(int(g_variant_get_int32(value)));
    else if(g_variant_is_of_type(value, G_VARIANT_TYPE_UINT32))
        result = QVariant(uint(g_variant_get_uint32(value)));
    else if(g_variant_is_of_type(value, G_VARIANT_TYPE_INT64))
        result = QVariant(Q_LLONG(g_variant_get_int64(value)));
    else if(g_variant_is_of_type(value, G_VARIANT_TYPE_UINT64))
        result = QVariant(Q_ULLONG(g_variant_get_uint64(value)));
    else if(g_variant_is_of_type(value, G_VARIANT_TYPE_STRING) || g_variant_is_of_type(value, G_VARIANT_TYPE_OBJECT_PATH))
        result = QVariant(QString::fromUtf8(g_variant_get_string(value, 0)));
    else if(g_variant_is_of_type(value, G_VARIANT_TYPE_BYTESTRING))
        result = QVariant(byteString(value));
    else if(g_variant_is_of_type(value, G_VARIANT_TYPE_BYTESTRING_ARRAY) || g_variant_is_of_type(value, G_VARIANT_TYPE_STRING_ARRAY))
    {
        bool bytes = g_variant_is_of_type(value, G_VARIANT_TYPE_BYTESTRING_ARRAY);
        QStringList list;
        gsize n = g_variant_n_children(value);
        for(gsize i = 0; i < n; ++i)
        {
            GVariant *child = g_variant_get_child_value(value, i);
            list.append(bytes ? byteString(child) : QString::fromUtf8(g_variant_get_string(child, 0)));
            g_variant_unref(child);
        }
        result = QVariant(list);
    }
    else
        return false;
    return true;
}

// The properties in an a{sv}
static UDisks2Properties toProperties(GVariant *dict)
{
    UDisks2Properties props;
    GVariantIter iter;
    const gchar *name;
    GVariant *value;
    g_variant_iter_init(&iter, dict);
    while(g_variant_iter_next(&iter, "{&sv}", &name, &value))
    {
        QVariant v;
        if(toVariant(value, v))
            props.insert(QString::fromLatin1(name), v);
        g_variant_unref(value);
    }
    return props;
}

// The cached properties of the proxy of one interface
static UDisks2Properties proxyProperties(GDBusProxy *proxy)
{
    UDisks2Properties props;
    gchar **names = g_dbus_proxy_get_cached_property_names(proxy);
    for(gchar **name = names; name && *name; ++name)
    {
        GVariant *value = g_dbus_proxy_get_cached_property(proxy, *name);
        if(!value)
            continue;
        QVariant v;
        if(toVariant(value, v))
            props.insert(QString::fromLatin1(*name), v);
        g_variant_unref(value);
    }
    g_strfreev(names);
    return props;
}

static UDisks2Interfaces objectInterfaces(GDBusObject *object)
{
    UDisks2Interfaces interfaces;
    GList *list = g_dbus_object_get_interfaces(object);
    for(GList *l = list; l; l = l->next)
    {
        GDBusProxy *proxy = G_DBUS_PROXY(l->data);
        interfaces.insert(QString::fromLatin1(g_dbus_proxy_get_interface_name(proxy)), proxyProperties(proxy));
    }
    g_list_free_full(list, g_object_unref);
    return interfaces;
}

static QString objectPath(GDBusObject *object)
{
    return QString::fromLatin1(g_dbus_object_get_object_path(object));
}

struct UDisks2Connection::Private
{
    // A call waiting for its reply
    struct Pending
    {
        Private *owner;               // 0 once the connection is gone
        UDisks2ReplyHandler *handler; // 0 once forgotten
    };

    UDisks2Listener *listener;
    GCancellable *cancellable;
    GDBusConnection *bus;
    GDBusObjectManager *manager;
    gulong handlers[6];
    QPtrList<Pending> pending;

    static void objectAdded(GDBusObjectManager *, GDBusObject *object, gpointer data)
    {
        Private *d = static_cast<Private *>(data);
        d->listener->udisks2InterfacesAdded(objectPath(object), objectInterfaces(object));
    }

    static void objectRemoved(GDBusObjectManager *, GDBusObject *object, gpointer data)
    {
        Private *d = static_cast<Private *>(data);
        // the object still has its interfaces here
        d->listener->udisks2InterfacesRemoved(objectPath(object), objectInterfaces(object).keys());
    }

    static void interfaceAdded(GDBusObjectManager *, GDBusObject *object, GDBusInterface *interface, gpointer data)
    {
        Private *d = static_cast<Private *>(data);
        GDBusProxy *proxy = G_DBUS_PROXY(interface);
        UDisks2Interfaces interfaces;
        interfaces.insert(QString::fromLatin1(g_dbus_proxy_get_interface_name(proxy)), proxyProperties(proxy));
        d->listener->udisks2InterfacesAdded(objectPath(object), interfaces);
    }

    static void interfaceRemoved(GDBusObjectManager *, GDBusObject *object, GDBusInterface *interface, gpointer data)
    {
        Private *d = static_cast<Private *>(data);
        QStringList interfaces;
        interfaces.append(QString::fromLatin1(g_dbus_proxy_get_interface_name(G_DBUS_PROXY(interface))));
        d->listener->udisks2InterfacesRemoved(objectPath(object), interfaces);
    }

    static void propertiesChanged(GDBusObjectManagerClient *, GDBusObjectProxy *object, GDBusProxy *interface,
                                  GVariant *changed, const gchar *const *, gpointer data)
    {
        Private *d = static_cast<Private *>(data);
        d->listener->udisks2PropertiesChanged(objectPath(G_DBUS_OBJECT(object)),
                                              QString::fromLatin1(g_dbus_proxy_get_interface_name(interface)),
                                              toProperties(changed));
    }

    static void ownerChanged(GObject *, GParamSpec *, gpointer data)
    {
        Private *d = static_cast<Private *>(data);
        d->listener->udisks2OwnerChanged(d->hasOwner());
    }

    bool hasOwner() const
    {
        gchar *owner = g_dbus_object_manager_client_get_name_owner(G_DBUS_OBJECT_MANAGER_CLIENT(manager));
        bool has = owner != 0;
        g_free(owner);
        return has;
    }

    static void deliver(Pending *p, const UDisks2Reply &reply)
    {
        if(p->owner)
            p->owner->pending.removeRef(p);
        if(p->handler)
            p->handler->udisks2Reply(reply);
        delete p;
    }

    static void callDone(GObject *source, GAsyncResult *result, gpointer data)
    {
        Pending *p = static_cast<Pending *>(data);
        UDisks2Reply reply;
        GError *err = 0;
        GVariant *value = g_dbus_connection_call_finish(G_DBUS_CONNECTION(source), result, &err);
        if(value)
        {
            reply.ok = true;
            if(g_variant_is_of_type(value, G_VARIANT_TYPE("(s)")))
            {
                const gchar *s = 0;
                g_variant_get(value, "(&s)", &s);
                reply.value = QString::fromUtf8(s);
            }
            g_variant_unref(value);
        }
        else
        {
            gchar *name = g_dbus_error_get_remote_error(err);
            if(name)
            {
                reply.errorName = QString::fromLatin1(name);
                g_free(name);
                g_dbus_error_strip_remote_error(err);
            }
            reply.errorMessage = errorString(err);
        }
        deliver(p, reply);
    }

    static gboolean notConnected(gpointer data)
    {
        UDisks2Reply reply;
        reply.errorMessage = QString::fromLatin1("not connected to the bus");
        deliver(static_cast<Pending *>(data), reply);
        return FALSE;
    }
};

UDisks2Connection::UDisks2Connection(UDisks2Listener *listener)
{
    d = new Private;
    d->listener = listener;
    d->cancellable = g_cancellable_new();
    d->bus = 0;
    d->manager = 0;
    for(int i = 0; i < 6; ++i)
        d->handlers[i] = 0;
}

UDisks2Connection::~UDisks2Connection()
{
    // the replies of the calls still running come later, to nobody
    for(QPtrListIterator<Private::Pending> it(d->pending); it.current(); ++it)
    {
        it.current()->owner = 0;
        it.current()->handler = 0;
    }
    g_cancellable_cancel(d->cancellable);
    g_object_unref(d->cancellable);
    if(d->manager)
    {
        for(int i = 0; i < 6; ++i)
            if(d->handlers[i])
                g_signal_handler_disconnect(d->manager, d->handlers[i]);
        g_object_unref(d->manager);
    }
    if(d->bus)
        g_object_unref(d->bus);
    delete d;
}

bool UDisks2Connection::open(QString *error)
{
    GError *err = 0;

    const char *testBus = getenv("MEDIAMANAGER_UDISKS2_TEST_BUS");
    if(testBus && *testBus)
    {
        kdDebug(1219) << "UDisks2: using the test bus " << testBus << endl;
        d->bus = g_dbus_connection_new_for_address_sync(
            testBus, GDBusConnectionFlags(G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT | G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION),
            0, d->cancellable, &err);
    }
    else
        d->bus = g_bus_get_sync(G_BUS_TYPE_SYSTEM, d->cancellable, &err);
    if(!d->bus)
    {
        *error = errorString(err);
        return false;
    }
    // The connection _exit()s the process when the bus goes away (e.g.
    // the D-Bus daemon is restarted); that must not take kded down.
    g_dbus_connection_set_exit_on_close(d->bus, FALSE);

    d->manager = g_dbus_object_manager_client_new_sync(d->bus, G_DBUS_OBJECT_MANAGER_CLIENT_FLAGS_NONE, udisksService, udisksPath, 0, 0, 0,
                                                       d->cancellable, &err);
    if(!d->manager)
    {
        *error = errorString(err);
        return false;
    }

    if(!d->hasOwner())
    {
        *error = QString::fromLatin1("%1 is not running and could not be started").arg(udisksService);
        return false;
    }

    // the objects there are: drives first, the block devices need them
    GList *objects = g_dbus_object_manager_get_objects(d->manager);
    const char *prefixes[] = {drivesPrefix, blockDevicesPrefix, 0};
    for(int pass = 0; pass < 3; ++pass)
    {
        for(GList *l = objects; l; l = l->next)
        {
            GDBusObject *object = G_DBUS_OBJECT(l->data);
            const char *path = g_dbus_object_get_object_path(object);
            bool drive = g_str_has_prefix(path, drivesPrefix);
            bool block = g_str_has_prefix(path, blockDevicesPrefix);
            if(pass < 2 ? g_str_has_prefix(path, prefixes[pass]) : !drive && !block)
                d->listener->udisks2InterfacesAdded(objectPath(object), objectInterfaces(object));
        }
    }
    g_list_free_full(objects, g_object_unref);

    // and from now on, the changes
    d->handlers[0] = g_signal_connect(d->manager, "object-added", G_CALLBACK(Private::objectAdded), d);
    d->handlers[1] = g_signal_connect(d->manager, "object-removed", G_CALLBACK(Private::objectRemoved), d);
    d->handlers[2] = g_signal_connect(d->manager, "interface-added", G_CALLBACK(Private::interfaceAdded), d);
    d->handlers[3] = g_signal_connect(d->manager, "interface-removed", G_CALLBACK(Private::interfaceRemoved), d);
    d->handlers[4] = g_signal_connect(d->manager, "interface-proxy-properties-changed", G_CALLBACK(Private::propertiesChanged), d);
    d->handlers[5] = g_signal_connect(d->manager, "notify::name-owner", G_CALLBACK(Private::ownerChanged), d);
    return true;
}

bool UDisks2Connection::hasOwner() const
{
    return d->manager && d->hasOwner();
}

void UDisks2Connection::call(const QString &path, const char *interface, const char *method, const UDisks2Properties &options,
                             UDisks2ReplyHandler *handler)
{
    Private::Pending *p = new Private::Pending;
    p->owner = d;
    p->handler = handler;
    d->pending.append(p);

    if(!d->bus)
    {
        // the reply comes from the main loop, as always
        g_idle_add(Private::notConnected, p);
        return;
    }

    GVariantBuilder builder;
    g_variant_builder_init(&builder, G_VARIANT_TYPE_VARDICT);
    for(UDisks2Properties::ConstIterator it = options.begin(); it != options.end(); ++it)
    {
        GVariant *value = 0;
        if((*it).type() == QVariant::Bool)
            value = g_variant_new_boolean((*it).toBool());
        else if((*it).type() == QVariant::String)
            value = g_variant_new_string((*it).toString().utf8().data());
        if(value)
            g_variant_builder_add(&builder, "{sv}", it.key().latin1(), value);
    }

    // no timeout: polkit may ask for a password; the reply comes even if
    // the connection is deleted first (cancelled), but is not delivered
    g_dbus_connection_call(d->bus, udisksService, path.latin1(), interface, method, g_variant_new("(a{sv})", &builder), 0,
                           G_DBUS_CALL_FLAGS_ALLOW_INTERACTIVE_AUTHORIZATION, G_MAXINT, d->cancellable, Private::callDone, p);
}

void UDisks2Connection::forget(UDisks2ReplyHandler *handler)
{
    for(QPtrListIterator<Private::Pending> it(d->pending); it.current(); ++it)
        if(it.current()->handler == handler)
            it.current()->handler = 0;
}
