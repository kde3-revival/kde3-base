/*
   This file is part of the KDE3 Fork Project
   Copyright (c) 2012 Serghei Amelian <serghei.amelian@gmail.com>

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

/*
 * The media and their mapping to Medium objects (MIME types, icons,
 * labels) come from the UDisks2 backend of the earlier kde3 tree; its
 * D-Bus layer (the Qt 3 D-Bus binding) is replaced by UDisks2Connection,
 * on GIO.
 */

#include <qapplication.h>
#include <qeventloop.h>

#include <kconfig.h>
#include <kdebug.h>
#include <klocale.h>
#include <kmdcodec.h>

#include "udisks2backend.h"

static const char driveInterface[] = "org.freedesktop.UDisks2.Drive";
static const char blockInterface[] = "org.freedesktop.UDisks2.Block";
static const char filesystemInterface[] = "org.freedesktop.UDisks2.Filesystem";
static const char loopInterface[] = "org.freedesktop.UDisks2.Loop";

static QString humanReadableSize(Q_INT64 size)
{
    const char *units[] = {"B", "KiB", "MiB", "GiB", "TiB", "PiB"};
    int mod = 1024;

    int i;
    for(i = 0; size > mod && i < 5; i++)
        size /= mod;

    return QString("%1%2").arg(size).arg(units[i]);
}

// A file system the system itself has mounted somewhere in its tree
// (/, /boot, /home, /usr, ...), not for the user under /media, /run/media
// or /mnt
static bool isSystemMount(const QStringList &mountPoints)
{
    for(QStringList::ConstIterator it = mountPoints.begin(); it != mountPoints.end(); ++it)
    {
        const QString &mp = *it;
        if(!mp.startsWith("/media/") && !mp.startsWith("/run/media/") && !mp.startsWith("/mnt/") && mp != "/mnt")
            return true;
    }
    return false;
}

/***** UDisks2Backend::Object ***********************/

// What we know of one UDisks2 object: a drive or a block device
struct UDisks2Backend::Object
{
    Object(const QString &p)
        : path(p), shown(false), optical(false), opticalBlank(false), opticalAudio(false), removable(false), ejectable(false),
          canPowerOff(false), mountable(false), size(0), hintIgnore(false), hintSystem(false)
    {
    }

    bool has(const char *interface) const
    {
        return interfaces.contains(QString::fromLatin1(interface));
    }
    bool isDrive() const
    {
        return has(driveInterface);
    }
    bool isLoop() const
    {
        return has(loopInterface);
    }
    bool hasFilesystem() const
    {
        return has(filesystemInterface);
    }
    bool isMounted() const
    {
        return !mountPoints.isEmpty();
    }

    QString path;
    QStringList interfaces;

    // the Medium we have in the media list, if any
    bool shown;
    QString mediumId;

    // drive interface
    bool optical;
    bool opticalBlank;
    bool opticalAudio;
    bool removable;
    bool ejectable;
    bool canPowerOff;
    QString media;

    // block device interface
    bool mountable;
    QString drive;
    QString device;
    QString label;
    QString uuid;
    QString fsType;
    Q_INT64 size;
    bool hintIgnore;
    bool hintSystem;

    // filesystem interface
    QStringList mountPoints;
};

/***** UDisks2Backend *******************************/

UDisks2Backend::UDisks2Backend(MediaList &mediaList) : BackendBase(mediaList), m_allowNotification(false)
{
    m_connection = new UDisks2Connection(this);
    m_objects.setAutoDelete(true);
}

UDisks2Backend::~UDisks2Backend()
{
    // the calls still waiting for their reply must not touch us
    for(QPtrListIterator<bool> it(m_waiting); it.current(); ++it)
        *it.current() = true;

    delete m_connection;

    for(QDictIterator<Object> it(m_objects); it.current(); ++it)
        if(it.current()->shown)
            m_mediaList.removeMedium(it.current()->mediumId, false);
}

bool UDisks2Backend::initialize()
{
    QString error;
    if(!m_connection->open(&error))
    {
        kdDebug(1219) << "UDisks2Backend: " << error << endl;
        return false;
    }

    // the media there are at the start are no news
    m_allowNotification = true;
    return true;
}

void UDisks2Backend::udisks2InterfacesAdded(const QString &path, const UDisks2Interfaces &interfaces)
{
    Object *object = m_objects.find(path);
    if(!object)
    {
        object = new Object(path);
        m_objects.insert(path, object);
    }

    for(UDisks2Interfaces::ConstIterator it = interfaces.begin(); it != interfaces.end(); ++it)
    {
        if(!object->interfaces.contains(it.key()))
            object->interfaces.append(it.key());
        setProperties(object, it.key(), it.data());
    }

    update(object);
    if(object->isDrive())
        updateBlocksOf(path);
}

void UDisks2Backend::udisks2InterfacesRemoved(const QString &path, const QStringList &interfaces)
{
    Object *object = m_objects.find(path);
    if(!object)
        return;

    for(QStringList::ConstIterator it = interfaces.begin(); it != interfaces.end(); ++it)
    {
        object->interfaces.remove(*it);
        // forget what the interface said
        if(*it == filesystemInterface)
            object->mountPoints.clear();
        else if(*it == blockInterface)
            object->mountable = false;
        else if(*it == driveInterface)
            object->opticalBlank = object->opticalAudio = false;
    }

    update(object);

    // destroy the object if it has no more interfaces
    if(object->interfaces.isEmpty())
        m_objects.remove(path);
}

void UDisks2Backend::udisks2PropertiesChanged(const QString &path, const QString &interface, const UDisks2Properties &changed)
{
    Object *object = m_objects.find(path);
    if(!object || !object->interfaces.contains(interface))
        return;

    setProperties(object, interface, changed);
    update(object);
    if(interface == driveInterface)
        updateBlocksOf(path);
}

void UDisks2Backend::setProperties(Object *o, const QString &interface, const UDisks2Properties &properties)
{
    for(UDisks2Properties::ConstIterator it = properties.begin(); it != properties.end(); ++it)
    {
        const QString &name = it.key();
        const QVariant &value = it.data();

        // properties of drive
        if(interface == driveInterface)
        {
            if("Optical" == name)
                o->optical = value.toBool();
            else if("OpticalBlank" == name)
                o->opticalBlank = value.toBool();
            else if("OpticalNumAudioTracks" == name)
                o->opticalAudio = (0 < value.toUInt());
            else if("Media" == name)
                o->media = value.toString();
            else if("Removable" == name)
                o->removable = value.toBool();
            else if("Ejectable" == name)
                o->ejectable = value.toBool();
            else if("CanPowerOff" == name)
                o->canPowerOff = value.toBool();
        }

        // properties of block device
        else if(interface == blockInterface)
        {
            if("IdUsage" == name)
                o->mountable = ("filesystem" == value.toString());
            else if("Drive" == name)
                o->drive = value.toString();
            else if("PreferredDevice" == name)
                o->device = value.toString();
            else if("IdLabel" == name)
                o->label = value.toString();
            else if("IdUUID" == name)
            {
                QString uuid = value.toString();
                // the uuid has changed while the medium is listed, e.g. the
                // partition was reformatted: the Medium has to be made anew
                if(o->shown && uuid != o->uuid)
                {
                    // the old uuid is probably lost for good, forget its label
                    KConfig cfg("mediamanagerrc");
                    cfg.setGroup("UserLabels");
                    cfg.deleteEntry(o->mediumId);

                    m_mediaList.removeMedium(o->mediumId, true);
                    o->shown = false;
                    o->mediumId = QString::null;
                }
                o->uuid = uuid;
            }
            else if("IdType" == name)
                o->fsType = value.toString();
            else if("Size" == name)
                o->size = value.toLongLong();
            else if("HintIgnore" == name)
                o->hintIgnore = value.toBool();
            else if("HintSystem" == name)
                o->hintSystem = value.toBool();
        }

        // properties of filesystem
        else if(interface == filesystemInterface)
        {
            if("MountPoints" == name)
                o->mountPoints = value.toStringList();
        }
    }
}

bool UDisks2Backend::isAvailable(const Object *o) const
{
    // a blank or audio disc, its drive stands for it
    if(o->opticalBlank || o->opticalAudio)
        return true;

    if(!o->mountable || !o->hasFilesystem())
        return false;

    // what UDisks2 says users should not see (e.g. recovery partitions)
    if(o->hintIgnore)
        return false;

    // the system's own partitions (/, /boot, /home, ...) while it uses them
    if(o->hintSystem && isSystemMount(o->mountPoints))
        return false;

    return true;
}

void UDisks2Backend::update(Object *o)
{
    if(!isAvailable(o))
    {
        // the medium is not available anymore
        if(o->shown)
        {
            m_mediaList.removeMedium(o->mediumId, true);
            o->shown = false;
            o->mediumId = QString::null;
        }
        return;
    }

    if(!o->shown)
    {
        // the medium became available
        o->mediumId = o->uuid;
        if(o->mediumId.isEmpty())
        {
            // no uuid, make up one
            QString dataForFakeUUID = o->label + o->media + o->fsType + QString::number(o->size) + o->path;
            KMD5 context(dataForFakeUUID.utf8());
            o->mediumId = "FAKE-UUID-" + context.hexDigest();
        }

        Medium *medium = createMedium(o);
        if(m_mediaList.addMedium(medium, m_allowNotification).isNull())
        {
            kdDebug(1219) << "UDisks2Backend: " << o->path << ": there is a medium with the id " << o->mediumId << " already" << endl;
            delete medium;
            o->mediumId = QString::null;
            return;
        }
        o->shown = true;
        return;
    }

    // the medium changed?
    const Medium *current = m_mediaList.findById(o->mediumId);
    if(!current)
        return;
    Medium *medium = createMedium(o);
    static const uint compared[] = {Medium::LABEL,      Medium::MOUNTABLE, Medium::DEVICE_NODE, Medium::MOUNT_POINT, Medium::FS_TYPE,
                                    Medium::MOUNTED,    Medium::BASE_URL,  Medium::MIME_TYPE,   Medium::ICON_NAME};
    for(uint i = 0; i < sizeof(compared) / sizeof(compared[0]); ++i)
    {
        if(medium->properties()[compared[i]] != current->properties()[compared[i]])
        {
            m_mediaList.changeMediumState(*medium, false);
            break;
        }
    }
    delete medium;
}

void UDisks2Backend::updateBlocksOf(const QString &drivePath)
{
    for(QDictIterator<Object> it(m_objects); it.current(); ++it)
        if(it.current()->drive == drivePath)
            update(it.current());
}

Medium *UDisks2Backend::createMedium(const Object *o) const
{
    if(o->mountable && o->hasFilesystem())
        return o->isLoop() ? createLoopMedium(o) : createMountableMedium(o);
    return createBlankOrAudioMedium(o);
}

Medium *UDisks2Backend::createLoopMedium(const Object *o) const
{
    QString name = (o->label.isEmpty() ? QString(o->device).section('/', -1, -1) : o->label);
    QString label = o->label;

    QString mimeType = ("iso9660" == o->fsType ? "media/cdrom" : "media/hdd");

    if(label.isEmpty())
        label = i18n("Loop Device");

    mimeType += (o->isMounted() ? "_mounted" : "_unmounted");

    if(o->label.isEmpty())
        label = QString("%1 %2 (%3)").arg(humanReadableSize(o->size)).arg(label).arg(name);

    Medium *medium = new Medium(o->mediumId, name);
    medium->setLabel(label);
    medium->mountableState(o->device, o->mountPoints.isEmpty() ? QString::null : o->mountPoints.last(), o->fsType, o->isMounted());
    medium->setMimeType(mimeType);

    return medium;
}

Medium *UDisks2Backend::createMountableMedium(const Object *o) const
{
    // no drive, e.g. a RAID or an encrypted volume: as if it were a disk
    static const Object noDrive(QString::null);
    const Object *drive = m_objects.find(o->drive);
    if(!drive)
        drive = &noDrive;

    QString name = (o->label.isEmpty() ? QString(o->device).section('/', -1, -1) : o->label);
    QString label = o->label;

    QString mimeType;
    QString iconName;

    // optical media
    if(drive->optical)
    {
        if("optical_cd_r" == drive->media.left(12))
            mimeType = "media/cdwriter";
        else if("optical_dvd" == drive->media.left(11))
            mimeType = "media/dvd";
        else
            mimeType = "media/cdrom";
        if(label.isEmpty())
            label = i18n("Optical Media");
    }

    // removable media
    else if(drive->removable)
    {
        mimeType = "media/removable";
        if(label.isEmpty())
            label = i18n("Removable Media");
    }

    // other media
    else
    {
        mimeType = "media/hdd";
        if(label.isEmpty())
            label = i18n("Hard Disk");
    }

    if("thumb" == drive->media)
        iconName = "usbpendrive";
    else if("flash_cf" == drive->media)
        iconName = "compact_flash";
    else if("flash_ms" == drive->media)
        iconName = "memory_stick";
    else if("flash_sm" == drive->media)
        iconName = "smart_media";
    else if("flash_sd" == drive->media.left(8))
        iconName = "sd_mmc";
    else if("flash_mmc" == drive->media)
        iconName = "sd_mmc";
    else if("floppy" == drive->media)
        iconName = "3floppy";
    else if("floppy_zip" == drive->media)
        iconName = "zip";

    mimeType += (o->isMounted() ? "_mounted" : "_unmounted");

    if(!iconName.isEmpty())
        iconName += (o->isMounted() ? "_mount" : "_unmount");

    if(o->label.isEmpty())
        label = QString("%1 %2 (%3)").arg(humanReadableSize(o->size)).arg(label).arg(name);

    Medium *medium = new Medium(o->mediumId, name);
    medium->setLabel(label);
    medium->mountableState(o->device, o->mountPoints.isEmpty() ? QString::null : o->mountPoints.last(), o->fsType, o->isMounted());
    medium->setMimeType(mimeType);
    medium->setIconName(iconName);

    return medium;
}

Medium *UDisks2Backend::createBlankOrAudioMedium(const Object *o) const
{
    QString name;
    QString mimeType;

    // blank cd or dvd
    if(o->opticalBlank)
    {
        if("optical_dvd" == o->media.left(11))
        {
            name = i18n("Blank DVD");
            mimeType = "media/blankdvd";
        }
        else
        {
            name = i18n("Blank CD");
            mimeType = "media/blankcd";
        }
    }

    // audio cd
    else if(o->opticalAudio)
    {
        name = i18n("Audio CD");
        mimeType = "media/audiocd";
    }

    Medium *medium = new Medium(o->mediumId, name);
    medium->setLabel(name);
    medium->unmountableState("");
    medium->setMimeType(mimeType);

    return medium;
}

UDisks2Backend::Object *UDisks2Backend::findObjectByMediumId(const QString &id) const
{
    for(QDictIterator<Object> it(m_objects); it.current(); ++it)
        if(it.current()->shown && id == it.current()->mediumId)
            return it.current();
    return 0;
}

// Waits for the reply in a local event loop, as the DCOP calls return
// the outcome; the call has no timeout, as polkit may ask for a
// password.  Returns false if the backend was deleted meanwhile, then
// nothing of it may be touched any more.
bool UDisks2Backend::call(const QString &path, const char *interface, const char *method, const UDisks2Properties &options,
                          UDisks2Reply &reply)
{
    bool gone = false;
    m_waiting.append(&gone);

    m_connection->call(path, interface, method, options, &reply);
    while(!reply.done)
        qApp->eventLoop()->processEvents(QEventLoop::AllEvents | QEventLoop::WaitForMore);

    if(gone)
        return false;
    m_waiting.removeRef(&gone);
    return true;
}

QString UDisks2Backend::errorMessage(Action action, const QString &device, const UDisks2Reply &reply)
{
    const QString &name = reply.errorName;
    QString reason;

    if(name.startsWith("org.freedesktop.UDisks2.Error.NotAuthorized"))
    {
        if(name.endsWith("Dismissed"))
            reason = i18n("The authentication was cancelled.");
        else if(name.endsWith("CanObtain"))
            reason = i18n("You are not authorized to do this without authenticating, "
                          "and no polkit authentication agent runs in this session to ask for a password.");
        else
            reason = i18n("You are not authorized to do this (the system's polkit policy does not allow it).");
    }
    else if(name == "org.freedesktop.UDisks2.Error.DeviceBusy" || reply.errorMessage.contains("target is busy"))
        reason = i18n("The device is busy: some program still uses files on it. Close them and try again.");
    else if(name == "org.freedesktop.DBus.Error.ServiceUnknown" || name == "org.freedesktop.DBus.Error.NoReply")
        reason = i18n("UDisks2 (udisksd) is not running: %1").arg(reply.errorMessage);
    else
        reason = reply.errorMessage;

    switch(action)
    {
        case Mount:
            return i18n("Unable to mount \"%1\".\nReason: %2").arg(device).arg(reason);
        case Unmount:
            return i18n("Unable to unmount \"%1\".\nReason: %2").arg(device).arg(reason);
        case Eject:
            return i18n("Unable to eject \"%1\".\nReason: %2").arg(device).arg(reason);
        case PowerOff:
            break;
    }
    return i18n("Unable to power off \"%1\".\nReason: %2").arg(device).arg(reason);
}

QString UDisks2Backend::mount(const QString &id)
{
    if(!m_mediaList.findById(id))
        return i18n("No such medium: %1").arg(id);

    Object *o = findObjectByMediumId(id);
    if(!o)
        return i18n("No such UDisks2 object in cache: %1").arg(id);
    if(!o->hasFilesystem())
        return i18n("%1 is not a mountable media.").arg(id);
    if(o->isMounted())
        return QString::null;

    const QString path = o->path;
    const QString device = o->device;

    UDisks2Reply reply;
    if(!call(path, filesystemInterface, "Mount", UDisks2Properties(), reply))
        return i18n("Unable to mount \"%1\".\nReason: %2").arg(device).arg(i18n("The media manager was stopped."));

    if(!reply.ok)
    {
        if(reply.errorName == "org.freedesktop.UDisks2.Error.AlreadyMounted")
            return QString::null;
        return errorMessage(Mount, device, reply);
    }

    // in case the new mount point has not come in yet
    o = m_objects.find(path);
    if(o && !o->isMounted() && !reply.value.isEmpty())
    {
        o->mountPoints.append(reply.value);
        update(o);
    }
    return QString::null;
}

QString UDisks2Backend::unmount(const QString &id)
{
    if(!m_mediaList.findById(id))
        return i18n("No such medium: %1").arg(id);

    Object *o = findObjectByMediumId(id);
    if(!o)
        return i18n("No such UDisks2 object in cache: %1").arg(id);
    if(!o->hasFilesystem() || !o->isMounted())
        return QString::null;

    const QString path = o->path;
    const QString device = o->device;

    UDisks2Reply reply;
    if(!call(path, filesystemInterface, "Unmount", UDisks2Properties(), reply))
        return i18n("Unable to unmount \"%1\".\nReason: %2").arg(device).arg(i18n("The media manager was stopped."));

    if(!reply.ok)
    {
        if(reply.errorName == "org.freedesktop.UDisks2.Error.NotMounted")
            return QString::null;
        return errorMessage(Unmount, device, reply);
    }

    o = m_objects.find(path);
    if(o && o->isMounted())
    {
        o->mountPoints.clear();
        update(o);
    }
    return QString::null;
}

// Ejects the medium (opens the tray, or tells a card reader or USB stick
// that it is about to go), then powers the drive off if it can be, so
// that the device can be pulled out safely
QString UDisks2Backend::eject(const QString &id)
{
    if(!m_mediaList.findById(id))
        return i18n("No such medium: %1").arg(id);

    Object *o = findObjectByMediumId(id);
    if(!o)
        return i18n("No such UDisks2 object in cache: %1").arg(id);

    const Object *drive = o->isDrive() ? o : m_objects.find(o->drive);
    QString device = o->device;
    if(!drive)
        return i18n("Unable to eject \"%1\".\nReason: %2").arg(device).arg(i18n("It is not on a drive that can be ejected."));

    const QString drivePath = drive->path;
    const bool ejectable = drive->ejectable;
    const bool powerOff = drive->canPowerOff && !drive->optical;
    if(device.isEmpty())
        device = id;

    if(!ejectable && !powerOff)
        return i18n("Unable to eject \"%1\".\nReason: %2").arg(device).arg(i18n("The drive can neither be ejected nor powered off."));

    QString error;
    if(ejectable)
    {
        UDisks2Reply reply;
        if(!call(drivePath, driveInterface, "Eject", UDisks2Properties(), reply))
            return i18n("Unable to eject \"%1\".\nReason: %2").arg(device).arg(i18n("The media manager was stopped."));
        if(!reply.ok)
            error = errorMessage(Eject, device, reply);
    }

    if(powerOff)
    {
        UDisks2Reply reply;
        if(!call(drivePath, driveInterface, "PowerOff", UDisks2Properties(), reply))
            return QString::null;
        // ejected is good enough
        if(!reply.ok && (!ejectable || !error.isNull()))
            return error.isNull() ? errorMessage(PowerOff, device, reply) : error;
        if(reply.ok)
            return QString::null;
    }
    return error;
}
