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

#include <stdlib.h>

#include <qdir.h>
#include <qfile.h>

#include <kconfig.h>
#include <kdebug.h>
#include <klocale.h>
#include <kmdcodec.h>

#include "udisks2backend.h"

static const char driveInterface[] = "org.freedesktop.UDisks2.Drive";
static const char blockInterface[] = "org.freedesktop.UDisks2.Block";
static const char filesystemInterface[] = "org.freedesktop.UDisks2.Filesystem";
static const char encryptedInterface[] = "org.freedesktop.UDisks2.Encrypted";
static const char loopInterface[] = "org.freedesktop.UDisks2.Loop";
static const char drivesPrefix[] = "/org/freedesktop/UDisks2/drives/";
static const char blockDevicesPrefix[] = "/org/freedesktop/UDisks2/block_devices/";

static QString humanReadableSize(Q_INT64 size)
{
    const char *units[] = {"B", "KiB", "MiB", "GiB", "TiB", "PiB"};
    int mod = 1024;

    int i;
    for(i = 0; size > mod && i < 5; i++)
        size /= mod;

    return QString("%1%2").arg(size).arg(units[i]);
}

// A file system mounted somewhere in the system's own tree (/, /boot,
// /home, /usr, ...), not for the user under /media, /run/media or /mnt;
// returns the mount point, or QString::null
static QString systemMountPoint(const QStringList &mountPoints)
{
    for(QStringList::ConstIterator it = mountPoints.begin(); it != mountPoints.end(); ++it)
    {
        const QString &mp = *it;
        if(!mp.startsWith("/media/") && !mp.startsWith("/run/media/") && !mp.startsWith("/mnt/") && mp != "/mnt")
            return mp;
    }
    return QString::null;
}

// The devices built on top of a block device (dm, md, ...), from sysfs
static QStringList holders(const QString &device)
{
    if(device.isEmpty())
        return QStringList();
    const char *sysfs = getenv("MEDIAMANAGER_UDISKS2_TEST_SYSFS");
    QString base = sysfs && *sysfs ? QFile::decodeName(sysfs) : QString::fromLatin1("/sys/class/block");
    QDir dir(base + "/" + device.section('/', -1) + "/holders");
    QStringList list = dir.entryList(QDir::All | QDir::System | QDir::Hidden);
    list.remove(".");
    list.remove("..");
    return list;
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
    // the cleartext device of an unlocked encrypted volume
    bool isCleartext() const
    {
        return !cryptoBacking.isEmpty() && cryptoBacking != "/";
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
    QString device;    // the preferred device node, for the user
    QString rawDevice; // the kernel's device node, e.g. /dev/dm-0
    QString label;
    QString uuid;
    QString fsType;
    Q_INT64 size;
    bool hintIgnore;
    bool hintSystem;
    QString cryptoBacking;

    // filesystem interface
    QStringList mountPoints;
};

/***** UDisks2Backend::Task *************************/

// One method call of a task
struct UDisks2Backend::Step
{
    Step() : action(Mount)
    {
    }
    Step(Action a, const QString &p, const QString &d) : action(a), path(p), device(d)
    {
    }

    Action action;
    QString path;
    QString device; // for the error messages
};

// A mount, unmount or eject: method calls of UDisks2 made one after the
// other, each when the previous one has succeeded
class UDisks2Backend::Task : public UDisks2ReplyHandler
{
public:
    Task(UDisks2Backend *b, const QValueList<Step> &s) : backend(b), result(0), steps(s), ejected(false)
    {
    }

    ~Task()
    {
        backend->m_connection->forget(this);
    }

    void next()
    {
        if(steps.isEmpty())
        {
            finish(QString::null);
            return;
        }
        current = steps.first();
        steps.remove(steps.begin());

        const char *interface = filesystemInterface;
        const char *method = "Mount";
        switch(current.action)
        {
            case Mount:
                break;
            case Unmount:
                method = "Unmount";
                break;
            case Lock:
                interface = encryptedInterface;
                method = "Lock";
                break;
            case Eject:
                interface = driveInterface;
                method = "Eject";
                break;
            case PowerOff:
                interface = driveInterface;
                method = "PowerOff";
                break;
        }
        backend->m_connection->call(current.path, interface, method, UDisks2Properties(), this);
    }

    virtual void udisks2Reply(const UDisks2Reply &reply)
    {
        bool ok = reply.ok;
        QString error;
        switch(current.action)
        {
            case Mount:
                if(!ok && reply.errorName == "org.freedesktop.UDisks2.Error.AlreadyMounted")
                    ok = true;
                else if(ok)
                    backend->mounted(current.path, reply.value);
                break;
            case Unmount:
                if(!ok && reply.errorName == "org.freedesktop.UDisks2.Error.NotMounted")
                    ok = true;
                else if(ok)
                    backend->unmounted(current.path);
                break;
            case Lock:
                break;
            case Eject:
                if(!ok && !steps.isEmpty() && steps.first().action == PowerOff)
                {
                    // powering off may still do
                    ejectError = errorMessage(Eject, current.device, reply);
                    next();
                    return;
                }
                ejected = ok;
                break;
            case PowerOff:
                // ejected is good enough
                if(!ok && ejected)
                    ok = true;
                else if(!ok && !ejectError.isNull())
                    error = ejectError;
                break;
        }

        if(ok)
            next();
        else
            finish(error.isNull() ? errorMessage(current.action, current.device, reply) : error);
    }

    void finish(const QString &error)
    {
        backend->m_tasks.removeRef(this);
        if(result)
        {
            result->finished(error);
            delete result;
        }
        delete this;
    }

    UDisks2Backend *backend;
    UDisks2Result *result;
    QValueList<Step> steps;
    Step current;
    bool ejected;
    QString ejectError;
};

/***** UDisks2Backend *******************************/

UDisks2Backend::UDisks2Backend(MediaList &mediaList) : BackendBase(mediaList), m_allowNotification(false), m_takingOver(false)
{
    m_connection = new UDisks2Connection(this);
    m_objects.setAutoDelete(true);
}

UDisks2Backend::~UDisks2Backend()
{
    // the tasks still running end now, unsuccessfully
    QPtrList<Task> tasks = m_tasks;
    for(QPtrListIterator<Task> it(tasks); it.current(); ++it)
        it.current()->finish(i18n("The media manager was stopped."));

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

bool UDisks2Backend::owns(const QString &id) const
{
    return findObjectByMediumId(id) != 0;
}

void UDisks2Backend::udisks2InterfacesAdded(const QString &path, const UDisks2Interfaces &interfaces)
{
    // udisksd is coming back: its objects are added once it is all there
    if(!m_connection->hasOwner())
    {
        if(!m_replay.contains(path))
            m_replayPaths.append(path);
        UDisks2Interfaces &buffered = m_replay[path];
        for(UDisks2Interfaces::ConstIterator it = interfaces.begin(); it != interfaces.end(); ++it)
            buffered[it.key()] = it.data();
        return;
    }

    addInterfaces(path, interfaces);
}

void UDisks2Backend::addInterfaces(const QString &path, const UDisks2Interfaces &interfaces)
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

    changed(object);
    if(object->isDrive())
        updateBlocksOf(path);
}

void UDisks2Backend::udisks2InterfacesRemoved(const QString &path, const QStringList &interfaces)
{
    if(m_replay.contains(path))
    {
        // came and went while udisksd restarted
        m_replay.remove(path);
        m_replayPaths.remove(path);
    }

    Object *object = m_objects.find(path);
    if(!object)
        return;

    // udisksd is going away: what it had is probably coming back
    if(!m_connection->hasOwner() && object->shown && !m_quiet.contains(object->mediumId))
        m_quiet.append(object->mediumId);

    for(QStringList::ConstIterator it = interfaces.begin(); it != interfaces.end(); ++it)
    {
        object->interfaces.remove(*it);
        // forget what the interface said
        if(*it == filesystemInterface)
            object->mountPoints.clear();
        else if(*it == blockInterface)
        {
            object->mountable = false;
            object->cryptoBacking = QString::null;
        }
        else if(*it == driveInterface)
            object->opticalBlank = object->opticalAudio = false;
    }

    changed(object);

    // destroy the object if it has no more interfaces
    if(object->interfaces.isEmpty())
        m_objects.remove(path);
}

void UDisks2Backend::udisks2PropertiesChanged(const QString &path, const QString &interface, const UDisks2Properties &properties)
{
    Object *object = m_objects.find(path);
    if(!object || !object->interfaces.contains(interface))
        return;

    setProperties(object, interface, properties);
    changed(object);
    if(interface == driveInterface)
        updateBlocksOf(path);
}

void UDisks2Backend::udisks2OwnerChanged(bool present)
{
    if(!present)
        return;

    // udisksd is back: its objects, the drives first; the media that went
    // away with it come back without "new medium" notifications
    QStringList paths = m_replayPaths;
    QMap<QString, UDisks2Interfaces> replay = m_replay;
    m_replayPaths.clear();
    m_replay.clear();
    for(int pass = 0; pass < 3; ++pass)
    {
        for(QStringList::ConstIterator it = paths.begin(); it != paths.end(); ++it)
        {
            bool drive = (*it).startsWith(drivesPrefix);
            bool block = (*it).startsWith(blockDevicesPrefix);
            if((pass == 0 && drive) || (pass == 1 && block) || (pass == 2 && !drive && !block))
                addInterfaces(*it, replay[*it]);
        }
    }
    m_quiet.clear();
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
            else if("Device" == name)
                o->rawDevice = value.toString();
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

                    QString id = o->mediumId;
                    m_mediaList.removeMedium(id, true);
                    o->shown = false;
                    o->mediumId = QString::null;
                    o->uuid = uuid;
                    takeOver(id);
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
            else if("CryptoBackingDevice" == name)
                o->cryptoBacking = value.toString();
        }

        // properties of filesystem
        else if(interface == filesystemInterface)
        {
            if("MountPoints" == name)
                o->mountPoints = value.toStringList();
        }
    }
}

const UDisks2Backend::Object *UDisks2Backend::driveOf(const Object *o) const
{
    if(o->isDrive())
        return o;
    // the cleartext device of an encrypted volume: the drive of the volume
    if(o->isCleartext())
    {
        const Object *backing = m_objects.find(o->cryptoBacking);
        return backing && !backing->isCleartext() ? driveOf(backing) : 0;
    }
    const Object *drive = m_objects.find(o->drive);
    return drive && drive->isDrive() ? drive : 0;
}

UDisks2Backend::Object *UDisks2Backend::cleartextOf(const Object *o) const
{
    for(QDictIterator<Object> it(m_objects); it.current(); ++it)
        if(it.current()->cryptoBacking == o->path)
            return it.current();
    return 0;
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

    // the system's own file systems (/, /boot, /home, ...) while it uses them
    if(!systemMountPoint(o->mountPoints).isNull())
        return false;

    // a twin with the same uuid is mounted (e.g. the other devices of a
    // multi-device btrfs, or a copy of a disk): that one stands for both
    if(!o->isMounted() && !o->uuid.isEmpty())
    {
        for(QDictIterator<Object> it(m_objects); it.current(); ++it)
        {
            const Object *twin = it.current();
            if(twin != o && twin->uuid == o->uuid && twin->isMounted() && twin->hasFilesystem())
                return false;
        }
    }

    return true;
}

// Something of the object changed: it and its twins (same uuid) need an update
void UDisks2Backend::changed(Object *o)
{
    update(o);
    if(o->uuid.isEmpty())
        return;
    for(QDictIterator<Object> it(m_objects); it.current(); ++it)
        if(it.current() != o && it.current()->uuid == o->uuid)
            update(it.current());
}

void UDisks2Backend::update(Object *o)
{
    if(!isAvailable(o))
    {
        // the medium is not available anymore
        if(o->shown)
        {
            QString id = o->mediumId;
            m_mediaList.removeMedium(id, true);
            o->shown = false;
            o->mediumId = QString::null;
            takeOver(id);
        }
        return;
    }

    if(!o->shown)
    {
        // the medium became available
        QString id = o->uuid;
        if(id.isEmpty())
        {
            // no uuid, make up one
            QString dataForFakeUUID = o->label + o->media + o->fsType + QString::number(o->size) + o->path;
            KMD5 context(dataForFakeUUID.utf8());
            id = "FAKE-UUID-" + context.hexDigest();
        }

        // a twin with the same uuid is listed already
        if(m_mediaList.findById(id))
            return;

        o->mediumId = id;
        Medium *medium = createMedium(o);
        bool notify = m_allowNotification && !m_takingOver && !m_quiet.contains(id);
        m_mediaList.addMedium(medium, notify);
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

// The medium @p id went away: another object with the same uuid may stand
// in for it now, quietly
void UDisks2Backend::takeOver(const QString &id)
{
    if(m_takingOver)
        return;
    m_takingOver = true;
    for(QDictIterator<Object> it(m_objects); it.current(); ++it)
        if(!it.current()->shown && it.current()->uuid == id)
            update(it.current());
    m_takingOver = false;
}

void UDisks2Backend::updateBlocksOf(const QString &drivePath)
{
    for(QDictIterator<Object> it(m_objects); it.current(); ++it)
    {
        if(it.current()->isDrive())
            continue;
        const Object *drive = driveOf(it.current());
        if(drive && drive->path == drivePath)
            update(it.current());
    }
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
    // no drive, e.g. a RAID: as if it were a disk
    static const Object noDrive(QString::null);
    const Object *drive = driveOf(o);
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
        case Lock:
            return i18n("Unable to lock the encrypted volume \"%1\".\nReason: %2").arg(device).arg(reason);
        case Eject:
            return i18n("Unable to eject \"%1\".\nReason: %2").arg(device).arg(reason);
        case PowerOff:
            break;
    }
    return i18n("Unable to power off \"%1\".\nReason: %2").arg(device).arg(reason);
}

UDisks2Backend::Task *UDisks2Backend::mount(const QString &id, QString *error)
{
    *error = QString::null;
    Object *o = findObjectByMediumId(id);
    if(!o)
        *error = i18n("No such medium: %1").arg(id);
    else if(!o->hasFilesystem())
        *error = i18n("%1 is not a mountable media.").arg(id);
    if(!o || !error->isNull() || o->isMounted())
        return 0;

    QValueList<Step> steps;
    steps.append(Step(Mount, o->path, o->device));
    return new Task(this, steps);
}

UDisks2Backend::Task *UDisks2Backend::unmount(const QString &id, QString *error)
{
    *error = QString::null;
    Object *o = findObjectByMediumId(id);
    if(!o)
    {
        *error = i18n("No such medium: %1").arg(id);
        return 0;
    }
    if(!o->hasFilesystem() || !o->isMounted())
        return 0;

    QValueList<Step> steps;
    steps.append(Step(Unmount, o->path, o->device));
    return new Task(this, steps);
}

// The steps that make a block device of a drive free: its file system
// (or that of its unlocked encrypted volume) unmounted, the volume locked
bool UDisks2Backend::releaseSteps(const Object *b, QValueList<Step> &steps, QString *error) const
{
    QString device = b->device.isEmpty() ? b->path : b->device;

    const Object *fs = b;
    const Object *clear = cleartextOf(b);
    if(clear)
        fs = clear;

    if(fs->hasFilesystem() && fs->isMounted())
    {
        QString mp = systemMountPoint(fs->mountPoints);
        if(!mp.isNull())
        {
            *error = i18n("\"%1\" cannot be removed: it is mounted on %2 and used by the system.").arg(device).arg(mp);
            return false;
        }
        steps.append(Step(Unmount, fs->path, fs->device));
    }

    QStringList users = holders(fs->rawDevice);
    if(!users.isEmpty())
    {
        *error = i18n("\"%1\" cannot be removed: it is in use by %2 (a RAID, LVM or other volume built on it).")
                     .arg(device)
                     .arg(users.join(", "));
        return false;
    }

    if(clear)
        steps.append(Step(Lock, b->path, device));
    return true;
}

// Ejects the medium's drive (opens the tray, or tells a card reader or USB
// stick that it is about to go), then powers it off if it can be, so that
// the device can be pulled out safely; everything of the drive is
// unmounted and locked before
UDisks2Backend::Task *UDisks2Backend::eject(const QString &id, QString *error)
{
    Object *o = findObjectByMediumId(id);
    if(!o)
    {
        *error = i18n("No such medium: %1").arg(id);
        return 0;
    }

    QString device = o->device.isEmpty() ? id : o->device;
    const Object *drive = driveOf(o);
    if(!drive)
    {
        *error = i18n("Unable to eject \"%1\".\nReason: %2").arg(device).arg(i18n("It is not on a drive that can be ejected."));
        return 0;
    }

    const bool powerOff = drive->canPowerOff && !drive->optical;
    if(!drive->ejectable && !powerOff)
    {
        *error = i18n("Unable to eject \"%1\".\nReason: %2").arg(device).arg(i18n("The drive can neither be ejected nor powered off."));
        return 0;
    }

    QValueList<Step> steps;
    for(QDictIterator<Object> it(m_objects); it.current(); ++it)
    {
        const Object *b = it.current();
        if(b->drive == drive->path && !b->isDrive() && !releaseSteps(b, steps, error))
            return 0;
    }

    if(drive->ejectable)
        steps.append(Step(Eject, drive->path, device));
    if(powerOff)
        steps.append(Step(PowerOff, drive->path, device));
    *error = QString::null;
    return new Task(this, steps);
}

void UDisks2Backend::start(Task *task, UDisks2Result *result)
{
    task->result = result;
    m_tasks.append(task);
    task->next();
}

// in case the new mount point has not come in yet
void UDisks2Backend::mounted(const QString &path, const QString &mountPoint)
{
    Object *o = m_objects.find(path);
    if(o && !o->isMounted() && !mountPoint.isEmpty())
    {
        o->mountPoints.append(mountPoint);
        changed(o);
    }
}

void UDisks2Backend::unmounted(const QString &path)
{
    Object *o = m_objects.find(path);
    if(o && o->isMounted())
    {
        o->mountPoints.clear();
        changed(o);
    }
}
