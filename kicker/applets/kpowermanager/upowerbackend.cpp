/*
 * Copyright (C) 2012-2014 Serghei Amelian <serghei.amelian@gmail.com>
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
#include <upower.h>

#include <qtimer.h>
#include <kdebug.h>

#include <config.h>

#include "upowerbackend.h"


// how long to wait before trying to reach UPower again
static const int retryDelay = 30000;


struct UPowerBackend::Private
{
    UPowerBackend *q;
    GCancellable *cancellable;
    UpClient *client;
    UpDevice *display;  // UPower's summary of all batteries, ref'd
    GPtrArray *devices; // the UpDevices we follow, ref'd
    PowerSources ps;

    static bool cancelled(GError *error)
    {
        // when cancelled, the backend is gone already
        if(g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        {
            g_error_free(error);
            return true;
        }
        return false;
    }

    void failed(const char *what, GError *error)
    {
        kdWarning() << "kpowermanager: " << what << ": " << error->message << endl;
        g_error_free(error);
        ps = PowerSources();
        q->changed();
        QTimer::singleShot(retryDelay, q, SLOT(initialize()));
    }

    static void clientReady(GObject *, GAsyncResult *result, gpointer data)
    {
        GError *error = 0;
        UpClient *client = up_client_new_finish(result, &error);
        if(!client)
        {
            if(!cancelled(error))
                static_cast<Private *>(data)->failed("cannot reach UPower", error);
            return;
        }

        Private *d = static_cast<Private *>(data);
        d->client = client;
        g_signal_connect(client, "device-added", G_CALLBACK(deviceAdded), d);
        g_signal_connect(client, "device-removed", G_CALLBACK(deviceRemoved), d);
        g_signal_connect(client, "notify::on-battery", G_CALLBACK(propertyChanged), d);
#ifdef HAVE_UP_CLIENT_GET_DEVICES_ASYNC
        up_client_get_devices_async(client, d->cancellable, devicesReady, d);
#else
        d->setDevices(up_client_get_devices2(client));
#endif
    }

#ifdef HAVE_UP_CLIENT_GET_DEVICES_ASYNC
    static void devicesReady(GObject *source, GAsyncResult *result, gpointer data)
    {
        GError *error = 0;
        GPtrArray *devices = up_client_get_devices_finish(UP_CLIENT(source), result, &error);
        if(!devices)
        {
            if(cancelled(error))
                return;
            Private *d = static_cast<Private *>(data);
            d->dropClient();
            d->failed("cannot list the power devices", error);
            return;
        }
        static_cast<Private *>(data)->setDevices(devices);
    }
#endif

    void setDevices(GPtrArray *list)
    {
        if(!list)
        {
            dropClient();
            failed("cannot list the power devices", g_error_new_literal(G_IO_ERROR, G_IO_ERROR_FAILED, "no reply"));
            return;
        }
        for(guint i = 0; i < list->len; i++)
            addDevice(UP_DEVICE(g_ptr_array_index(list, i)));
        g_ptr_array_unref(list);

        // the composite battery (for laptops with two batteries); a quick
        // synchronous call, UPower has just answered
        display = up_client_get_display_device(client);
        if(display)
            g_signal_connect(display, "notify", G_CALLBACK(propertyChanged), this);

        ps.daemon.connected = true;
        kdDebug() << "kpowermanager: upower backend has been initialized" << endl;
        update();
    }

    void addDevice(UpDevice *device)
    {
        // "device-added" may come for a device of the list as well
        for(guint i = 0; i < devices->len; i++)
            if(g_strcmp0(up_device_get_object_path(UP_DEVICE(g_ptr_array_index(devices, i))), up_device_get_object_path(device)) == 0)
                return;
        g_ptr_array_add(devices, g_object_ref(device));
        g_signal_connect(device, "notify", G_CALLBACK(propertyChanged), this);
    }

    static void deviceAdded(UpClient *, UpDevice *device, gpointer data)
    {
        Private *d = static_cast<Private *>(data);
        d->addDevice(device);
        d->update();
    }

    static void deviceRemoved(UpClient *, const gchar *path, gpointer data)
    {
        Private *d = static_cast<Private *>(data);
        for(guint i = 0; i < d->devices->len; i++)
        {
            UpDevice *device = UP_DEVICE(g_ptr_array_index(d->devices, i));
            if(g_strcmp0(up_device_get_object_path(device), path) == 0)
            {
                g_signal_handlers_disconnect_by_data(device, d);
                g_ptr_array_remove_index(d->devices, i); // unrefs it
                break;
            }
        }
        d->update();
    }

    static void propertyChanged(GObject *, GParamSpec *, gpointer data)
    {
        static_cast<Private *>(data)->update();
    }

    // Recomputes the power sources from the devices: the line power
    // devices and the batteries that power the machine (not of a mouse or
    // a UPS); UPower's display device sums those up
    void update()
    {
        if(!ps.daemon.connected)
            return;

        PowerSources fresh;
        fresh.daemon.connected = true;
        fresh.powerLine.online = false;
        bool haveBattery = false;
        double capacity = 0;
        for(guint i = 0; i < devices->len; i++)
        {
            UpDevice *device = UP_DEVICE(g_ptr_array_index(devices, i));
            guint kind = UP_DEVICE_KIND_UNKNOWN;
            gboolean online = FALSE, powerSupply = FALSE, isPresent = FALSE;
            g_object_get(device, "kind", &kind, "online", &online, "power-supply", &powerSupply, "is-present", &isPresent, NULL);

            if(kind == UP_DEVICE_KIND_LINE_POWER)
            {
                fresh.powerLine.present = true;
                fresh.powerLine.online = fresh.powerLine.online || online;
            }
            else if(kind == UP_DEVICE_KIND_BATTERY && powerSupply && isPresent)
            {
                // the capacity (wear) is not summed up: the first battery's
                if(!haveBattery)
                    g_object_get(device, "capacity", &capacity, NULL);
                haveBattery = true;
            }
        }

        if(haveBattery)
        {
            // the sum of the batteries, or the battery if there is only one
            UpDevice *battery = display;
            gboolean displayPresent = FALSE;
            if(battery)
                g_object_get(battery, "is-present", &displayPresent, NULL);
            if(!displayPresent)
                battery = 0;
            for(guint i = 0; !battery && i < devices->len; i++)
            {
                UpDevice *device = UP_DEVICE(g_ptr_array_index(devices, i));
                guint kind = UP_DEVICE_KIND_UNKNOWN;
                gboolean powerSupply = FALSE, isPresent = FALSE;
                g_object_get(device, "kind", &kind, "power-supply", &powerSupply, "is-present", &isPresent, NULL);
                if(kind == UP_DEVICE_KIND_BATTERY && powerSupply && isPresent)
                    battery = device;
            }

            guint state = UP_DEVICE_STATE_UNKNOWN;
            gint64 timeToEmpty = 0, timeToFull = 0;
            g_object_get(battery, "percentage", &fresh.battery.percentage, "state", &state, "time-to-empty", &timeToEmpty, "time-to-full",
                         &timeToFull, "energy-rate", &fresh.battery.energyRate, NULL);
            fresh.battery.present = true;
            fresh.battery.capacity = capacity;
            fresh.battery.state = state <= PowerSources::Battery::PendingDischarge ? PowerSources::Battery::State(state)
                                                                                   : PowerSources::Battery::Unknown;
            fresh.battery.timeToEmpty = timeToEmpty;
            fresh.battery.timeToFull = timeToFull;
        }

        // no line power device (e.g. a desktop): on mains unless UPower
        // says the machine runs on battery
        if(!fresh.powerLine.present)
            fresh.powerLine.online = !up_client_get_on_battery(client);

        ps = fresh;
        q->changed();
    }

    void dropClient()
    {
        for(guint i = 0; i < devices->len; i++)
            g_signal_handlers_disconnect_by_data(g_ptr_array_index(devices, i), this);
        g_ptr_array_set_size(devices, 0);
        if(display)
        {
            g_signal_handlers_disconnect_by_data(display, this);
            g_object_unref(display);
            display = 0;
        }
        if(client)
        {
            g_signal_handlers_disconnect_by_data(client, this);
            g_object_unref(client);
            client = 0;
        }
        ps.daemon.connected = false;
    }
};


UPowerBackend::UPowerBackend()
{
    d = new Private;
    d->q = this;
    d->cancellable = g_cancellable_new();
    d->client = 0;
    d->display = 0;
    d->devices = g_ptr_array_new_with_free_func(g_object_unref);

    // The shared system bus connection libupower-glib uses _exit()s the
    // process when the bus goes away (e.g. the D-Bus daemon is restarted);
    // that must not take kicker down with it.
    GDBusConnection *bus = g_bus_get_sync(G_BUS_TYPE_SYSTEM, 0, 0);
    if(bus)
    {
        g_dbus_connection_set_exit_on_close(bus, FALSE);
        g_object_unref(bus);
    }

    initialize();
}


UPowerBackend::~UPowerBackend()
{
    // the callbacks of the cancelled calls still run and must not touch d
    g_cancellable_cancel(d->cancellable);
    g_object_unref(d->cancellable);
    d->dropClient();
    g_ptr_array_unref(d->devices);
    delete d;
}


const PowerSources &UPowerBackend::powerSources() const
{
    return d->ps;
}


void UPowerBackend::initialize()
{
    if(d->client)
        return;
    up_client_new_async(d->cancellable, Private::clientReady, d);
}


void UPowerBackend::changed()
{
    emit powerSourcesChanged(d->ps);
}


#include "upowerbackend.moc"
