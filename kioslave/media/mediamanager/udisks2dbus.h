/* This file is part of the KDE Project

   The D-Bus side of the UDisks2 backend of the media manager.

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

#ifndef _UDISKS2DBUS_H_
#define _UDISKS2DBUS_H_

#include <qmap.h>
#include <qstring.h>
#include <qstringlist.h>
#include <qvariant.h>

/*
 * A plain C++ interface to the objects of org.freedesktop.UDisks2 on the
 * system bus.  The implementation (udisks2dbus.cpp) is the only file of
 * the backend that includes GLib headers: they must come before any Qt
 * header, as Qt 3 defines "signals" as a macro.
 *
 * The objects are watched with GIO's GDBusObjectManagerClient, whose
 * signals are dispatched by the GLib main loop Qt runs on
 * (QT_GLIB_EVENTLOOP).
 *
 * D-Bus values are handed over as QVariants: booleans as Bool, 32 bit
 * integers as Int/UInt, 64 bit ones as LongLong/ULongLong, strings and
 * object paths as String, byte strings ("ay", like the mount points of
 * UDisks2) as String and arrays of strings or byte strings as StringList.
 * Values of other types are left out.
 *
 * Setting MEDIAMANAGER_UDISKS2_TEST_BUS to a D-Bus address makes the
 * backend talk to the bus at that address instead of the system bus, for
 * testing against a fake UDisks2.
 */

/** Property name -> value of one D-Bus interface. */
typedef QMap<QString, QVariant> UDisks2Properties;

/** Interface name -> its properties, of one D-Bus object. */
typedef QMap<QString, UDisks2Properties> UDisks2Interfaces;

/** Receives the changes of the UDisks2 objects. */
class UDisks2Listener
{
public:
    virtual ~UDisks2Listener() {}

    /** An object got new interfaces (or appeared, with all of them). */
    virtual void udisks2InterfacesAdded(const QString &path, const UDisks2Interfaces &interfaces) = 0;
    /** An object lost some interfaces (or disappeared, losing all). */
    virtual void udisks2InterfacesRemoved(const QString &path, const QStringList &interfaces) = 0;
    /** Some properties of an interface of an object changed. */
    virtual void udisks2PropertiesChanged(const QString &path, const QString &interface,
                                          const UDisks2Properties &changed) = 0;
};

/**
 * The outcome of a method call.  call() fills it in when the reply
 * arrives, from the main loop, and sets @p done last.
 */
struct UDisks2Reply
{
    UDisks2Reply() : done(false), ok(false) {}

    bool done;
    bool ok;
    QString value;        // the first value of the reply, if it is a string
    QString errorName;    // the D-Bus error name, e.g. org.freedesktop.UDisks2.Error.NotAuthorized
    QString errorMessage; // the error's message, without the remote error name
};

class UDisks2Connection
{
public:
    UDisks2Connection(UDisks2Listener *listener);
    ~UDisks2Connection();

    /**
     * Connects to the bus and to UDisks2 (starting it if need be), and
     * hands the objects it has to the listener with
     * udisks2InterfacesAdded(): the drives first, then the block devices,
     * then the rest.  Returns false with @p error set if UDisks2 is not
     * there.  Blocks until UDisks2 has answered.
     */
    bool open(QString *error);

    /**
     * Calls @p method of @p interface of the object at @p path, with a
     * single a{sv} argument made of @p options (the call signature of the
     * Filesystem, Drive and Loop methods of UDisks2).  The call does not
     * time out (polkit may ask for a password) and may be interactively
     * authorized.  Returns at once; @p reply is filled in later, from the
     * main loop, and must stay valid until reply->done is set, which
     * happens even if the connection is deleted first.
     */
    void call(const QString &path, const char *interface, const char *method,
              const UDisks2Properties &options, UDisks2Reply *reply);

private:
    UDisks2Connection(const UDisks2Connection &);
    UDisks2Connection &operator=(const UDisks2Connection &);

    struct Private;
    Private *d;
};

#endif
