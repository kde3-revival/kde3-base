/* This file is part of the KDE3 Fork Project
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

#ifndef _UDISKS2BACKEND_H_
#define _UDISKS2BACKEND_H_

#include <qdict.h>
#include <qmap.h>
#include <qptrlist.h>
#include <qvaluelist.h>

#include "backendbase.h"
#include "udisks2dbus.h"

/** Gets the outcome of a mount, unmount or eject started with UDisks2Backend::start(). */
class UDisks2Result
{
public:
    virtual ~UDisks2Result() {}
    /** QString::null on success, else an error message for the user. */
    virtual void finished(const QString &error) = 0;
};

/**
 * Lists the drives, partitions, optical discs and loop devices UDisks2
 * (org.freedesktop.UDisks2 on the system bus) knows, keeps the list up
 * to date and mounts, unmounts and ejects through UDisks2.
 *
 * Mounting, unmounting and ejecting are asynchronous: mount(), unmount()
 * and eject() check the request and return a Task, which start() runs;
 * the result comes later, from the main loop (polkit may even ask for a
 * password meanwhile).
 *
 * For testing, MEDIAMANAGER_UDISKS2_TEST_SYSFS replaces /sys/class/block
 * (where the holders of a device, e.g. LVM or RAID, are looked up).
 */
class UDisks2Backend : public BackendBase, private UDisks2Listener
{
public:
    class Task;

    UDisks2Backend(MediaList &mediaList);
    ~UDisks2Backend();

    /** Connects to UDisks2 and lists the media; false if it is not there. */
    bool initialize();

    /** Whether the medium is one of ours (not, e.g., an fstab network share). */
    bool owns(const QString &id) const;

    /**
     * Prepare mounting, unmounting or ejecting a medium.  They return the
     * Task that does it, or 0 with @p error set to a message, or to
     * QString::null if there is nothing to do (e.g. already mounted).
     *
     * Ejecting is "safely removing" the medium's drive: the other file
     * systems of the drive are unmounted, the encrypted volumes locked, the
     * drive ejected and powered off if it can be.  It is refused if a file
     * system of the drive is used by the system, or a device of the drive
     * is a part of a RAID or LVM volume.
     */
    Task *mount(const QString &id, QString *error);
    Task *unmount(const QString &id, QString *error);
    Task *eject(const QString &id, QString *error);

    /** Runs @p task; @p result gets its outcome and is deleted then. */
    void start(Task *task, UDisks2Result *result);

private:
    struct Object;
    struct Step;
    friend class Task;
    enum Action { Mount, Unmount, Lock, Eject, PowerOff };

    virtual void udisks2InterfacesAdded(const QString &path, const UDisks2Interfaces &interfaces);
    virtual void udisks2InterfacesRemoved(const QString &path, const QStringList &interfaces);
    virtual void udisks2PropertiesChanged(const QString &path, const QString &interface, const UDisks2Properties &changed);
    virtual void udisks2OwnerChanged(bool present);

    void addInterfaces(const QString &path, const UDisks2Interfaces &interfaces);
    void setProperties(Object *object, const QString &interface, const UDisks2Properties &properties);
    void changed(Object *object);
    void update(Object *object);
    void takeOver(const QString &id);
    void updateBlocksOf(const QString &drivePath);
    bool isAvailable(const Object *object) const;
    const Object *driveOf(const Object *object) const;
    Object *cleartextOf(const Object *object) const;
    Medium *createMedium(const Object *object) const;
    Medium *createLoopMedium(const Object *object) const;
    Medium *createMountableMedium(const Object *object) const;
    Medium *createBlankOrAudioMedium(const Object *object) const;
    Object *findObjectByMediumId(const QString &id) const;
    bool releaseSteps(const Object *object, QValueList<Step> &steps, QString *error) const;
    void mounted(const QString &path, const QString &mountPoint);
    void unmounted(const QString &path);

    static QString errorMessage(Action action, const QString &device, const UDisks2Reply &reply);

    UDisks2Connection *m_connection;
    QDict<Object> m_objects;
    bool m_allowNotification;
    bool m_takingOver;
    QPtrList<Task> m_tasks;

    // while udisksd restarts: the objects it brings back, to be added at
    // once, and the media that went away with it, to be re-added quietly
    QStringList m_replayPaths;
    QMap<QString, UDisks2Interfaces> m_replay;
    QStringList m_quiet;
};

#endif
