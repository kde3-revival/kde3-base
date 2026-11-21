/*
 * Copyright (C) 2012 Serghei Amelian <serghei.amelian@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

// GLib's headers must come before any Qt header: Qt 3 defines "signals"
// as a macro, which breaks GLib's declarations that use the word.
#include <gio/gio.h>

#include <stdlib.h>

#include <qdir.h>
#include <qfile.h>
#include <qstringlist.h>
#include <qtextstream.h>
#include <kdebug.h>

#include "backlight.h"


static const char loginService[] = "org.freedesktop.login1";
static const char sessionPath[] = "/org/freedesktop/login1/session/auto";
static const char sessionInterface[] = "org.freedesktop.login1.Session";


static int readNumber(const QString &path)
{
    QFile file(path);
    if(!file.open(IO_ReadOnly))
        return -1;
    QTextStream stream(&file);
    bool ok = false;
    int value = stream.readLine().stripWhiteSpace().toInt(&ok);
    return ok ? value : -1;
}


struct Backlight::Private
{
    QString dir;  // e.g. /sys/class/backlight/intel_backlight
    QString name; // e.g. intel_backlight
    int maxLevel;
    int requested; // the last level asked for, while calls are pending
    int pending;
    GCancellable *cancellable;

    static void setDone(GObject *source, GAsyncResult *result, gpointer data)
    {
        GCancellable *cancellable = G_CANCELLABLE(data);
        GError *error = 0;
        GVariant *reply = g_dbus_connection_call_finish(G_DBUS_CONNECTION(source), result, &error);
        if(reply)
            g_variant_unref(reply);
        // when cancelled, the Backlight is gone
        if(!g_cancellable_is_cancelled(cancellable))
        {
            Private *d = static_cast<Private *>(g_object_get_data(G_OBJECT(cancellable), "backlight"));
            d->pending--;
            if(error)
                kdWarning() << "kpowermanager: logind SetBrightness: " << error->message << endl;
        }
        if(error)
            g_error_free(error);
        g_object_unref(cancellable);
    }
};


Backlight::Backlight()
{
    d = new Private;
    d->maxLevel = 0;
    d->requested = 0;
    d->pending = 0;
    d->cancellable = g_cancellable_new();
    g_object_set_data(G_OBJECT(d->cancellable), "backlight", d);

    const char *sysfs = getenv("KPOWERMANAGER_BACKLIGHT_SYSFS");
    QDir dir(sysfs && *sysfs ? QFile::decodeName(sysfs) : QString::fromLatin1("/sys/class/backlight"));
    // the devices are symlinks to directories there
    QStringList devices = dir.entryList(QDir::Dirs, QDir::Name);
    int bestRank = -1;
    for(QStringList::ConstIterator it = devices.begin(); it != devices.end(); ++it)
    {
        if(*it == "." || *it == "..")
            continue;
        QString path = dir.absFilePath(*it);
        QFile typeFile(path + "/type");
        QString type;
        if(typeFile.open(IO_ReadOnly))
            type = QTextStream(&typeFile).readLine().stripWhiteSpace();
        int rank = type == "firmware" ? 3 : type == "platform" ? 2 : type == "raw" ? 1 : 0;
        int max = readNumber(path + "/max_brightness");
        if(max > 0 && rank > bestRank)
        {
            bestRank = rank;
            d->dir = path;
            d->name = *it;
            d->maxLevel = max;
        }
    }

    if(d->maxLevel > 0)
        kdDebug() << "kpowermanager: brightness control through " << d->dir << endl;
    else
        kdDebug() << "kpowermanager: no backlight, the brightness control is disabled" << endl;
}


Backlight::~Backlight()
{
    g_cancellable_cancel(d->cancellable);
    g_object_unref(d->cancellable);
    delete d;
}


int Backlight::minBrightness() const
{
    return 0;
}


int Backlight::maxBrightness() const
{
    return d->maxLevel;
}


int Backlight::brightness() const
{
    if(d->maxLevel <= 0)
        return 0;
    // sysfs follows once logind has done it
    if(d->pending > 0)
        return d->requested;
    int level = readNumber(d->dir + "/brightness");
    return level < 0 ? 0 : level;
}


void Backlight::setBrightness(int level)
{
    if(d->maxLevel <= 0)
        return;
    if(level < 0)
        level = 0;
    if(level > d->maxLevel)
        level = d->maxLevel;

    GError *error = 0;
    GDBusConnection *bus = g_bus_get_sync(G_BUS_TYPE_SYSTEM, 0, &error);
    if(!bus)
    {
        kdWarning() << "kpowermanager: cannot reach the system bus: " << error->message << endl;
        g_error_free(error);
        return;
    }
    // must not take kicker down when the bus goes away
    g_dbus_connection_set_exit_on_close(bus, FALSE);

    d->requested = level;
    d->pending++;
    g_dbus_connection_call(bus, loginService, sessionPath, sessionInterface, "SetBrightness",
                           g_variant_new("(ssu)", "backlight", d->name.latin1(), guint32(level)), 0, G_DBUS_CALL_FLAGS_NONE, 5000,
                           d->cancellable, Private::setDone, g_object_ref(d->cancellable));
    g_object_unref(bus);
}
