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
#include <qptrlist.h>

#include "backendbase.h"
#include "udisks2dbus.h"

/**
 * Lists the drives, partitions, optical discs and loop devices UDisks2
 * (org.freedesktop.UDisks2 on the system bus) knows, keeps the list up
 * to date and mounts, unmounts and ejects through UDisks2.
 */
class UDisks2Backend : public BackendBase, private UDisks2Listener
{
public:
    UDisks2Backend(MediaList &mediaList);
    ~UDisks2Backend();

    /** Connects to UDisks2 and lists the media; false if it is not there. */
    bool initialize();

    // these return QString::null on success, else an error message
    QString mount(const QString &id);
    QString unmount(const QString &id);
    QString eject(const QString &id);

private:
    struct Object;
    enum Action { Mount, Unmount, Eject, PowerOff };

    virtual void udisks2InterfacesAdded(const QString &path, const UDisks2Interfaces &interfaces);
    virtual void udisks2InterfacesRemoved(const QString &path, const QStringList &interfaces);
    virtual void udisks2PropertiesChanged(const QString &path, const QString &interface, const UDisks2Properties &changed);

    void setProperties(Object *object, const QString &interface, const UDisks2Properties &properties);
    void update(Object *object);
    void updateBlocksOf(const QString &drivePath);
    bool isAvailable(const Object *object) const;
    Medium *createMedium(const Object *object) const;
    Medium *createLoopMedium(const Object *object) const;
    Medium *createMountableMedium(const Object *object) const;
    Medium *createBlankOrAudioMedium(const Object *object) const;
    Object *findObjectByMediumId(const QString &id) const;

    bool call(const QString &path, const char *interface, const char *method, const UDisks2Properties &options,
              UDisks2Reply &reply);
    static QString errorMessage(Action action, const QString &device, const UDisks2Reply &reply);

    UDisks2Connection *m_connection;
    QDict<Object> m_objects;
    bool m_allowNotification;
    // the calls waiting for their reply learn here that we are gone
    QPtrList<bool> m_waiting;
};

#endif
