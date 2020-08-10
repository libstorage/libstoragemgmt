/* ex: set tabstop=4 expandtab: */
/*
 * Copyright (C) 2011-2013 Red Hat, Inc.
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; If not, see <http://www.gnu.org/licenses/>.
 *
 * Author: tasleson
 */

#ifndef __BLOCKMGMT_H__
#define __BLOCKMGMT_H__

#include <Pegasus/Client/CIMClient.h>
#include <Pegasus/Common/Config.h>

PEGASUS_USING_PEGASUS;
PEGASUS_USING_STD;

/**
 * A simple class used to learn about SMI-S utilizing the openpegasus library.
 * Released in hopes that other may benefit.
 */
class BlockMgmt {
  public:
    /**
     * Class constructor.
     * Once this completes we have a connection to the SMI-S agent/proxy.
     * @param   host    a string representing the IP or host of SMI-S agent
     * @param   port    The server port to connect too.
     * @param   smisNameSpace   The SMI-S namespace to use.
     * @param   userName    User name when using authentication.
     * @param   password    Plain text password.
     */
    BlockMgmt(String host, Uint16 port, String smisNameSpace, String userName,
              String password);

    /**
     * Class destructor which closes the connection to the SMI-S agent/proxy
     */
    ~BlockMgmt();

    /**
     * Creates a logical unit.
     * @param   storagePoolName     Name of storage pool to allocate lun from.
     * @param   name                Name to be given to new lun.
     * @param   size                Size of the new Lun
     * @throws  Exception
     */
    void createLun(String storagePoolName, String name, Uint64 size);

    /**
     * Creates an initiator to reference and use.
     * @param   name                User defined name
     * @param   id                  Initiator id
     * @param   type                Type of id [WWN|IQN]
     * @throws Exception
     */
    void createInit(String name, String id, String type);

    /**
     * Deletes an initiator.
     * @param   id  Initiator ID
     * @throws Exception
     */
    void deleteInit(String id);

    /**
     * Creates a snapshot of a lun (point in time copy)
     * @param   sourceLun   Name of lun to snapshot.
     * @param   destStoragePool Storage pool to create snapshot from.
     * @param   destName    Name of new snapshot.
     * @throws  Exception
     */
    void createSnapShot(String sourceLun, String destStoragePool,
                        String destName);

    /**
     * Deletes a logical unit.
     * @param   name    Name of lun to delete.
     * @throws Exception
     */
    void deleteLun(String name);

    /**
     * Resizes an existing Lun.
     * @param   name    Name of lun to resize
     * @param   size    New size
     * @throws Exception
     */
    void resizeLun(String name, Uint64 size);

    /**
     * Returns an array of Strings which are the names of the storage pools.
     * @return  An Array<String>
     * @throws  Exception
     */
    Array<String> getStoragePools();

    /**
     * Returns an array of Strings which are the names of the logical units
     * @return  An Array<String>
     * @throws  Exception
     */
    Array<String> getLuns();

    /**
     * Returns an array of Strings which are the ID(s) of the initiators
     * @return An Array<String> of initiator IDs
     * @throws Exception
     */
    Array<String> getInitiators();

    /**
     * Grants read/write access for a lun to the specified initiator.
     * @param   initiatorID     The initiator ID
     * @param   lunName         The lun name
     * @throws Exception
     */
    void mapLun(String initiatorID, String lunName);

    /**
     * Removes acces for a lun to the specified initiator
     * @param   initiatorID     The initiator ID
     * @param   lunName         The lun name
     * @throws  Exception
     */
    void unmapLun(String initiatorID, String lunName);

    void jobStatus(String id);

  private:
    enum ElementType {
        UNKNOWN = 0,
        RESERVED = 1,
        STORAGE_VOLUME = 2,
        STORAGE_EXTENT = 3,
        STORAGE_POOL = 4,
        LOGICAL_DISK = 5
    };

    enum DeviceAccess { READ_WRITE = 2, READ_ONLY = 3, NO_ACCESS = 4 };

    enum SyncType { MIRROR = 6, SNAPSHOT = 7, CLONE = 8 };

    enum Mode { SYNC = 2, ASYNC = 3 };

    enum MethodInvoke { INVOKE_OK = 0, INVOKE_ASYNC = 4096 };

    enum OperationalStatus { OK = 2, ERROR = 6, STOPPED = 10, COMPLETE = 17 };

    enum JobState {
        JS_NEW = 2,
        JS_STARTING = 3,
        JS_RUNNING = 4,
        JS_SUSPENDED = 5,
        JS_SHUTTING_DOWN = 6,
        JS_COMPLETED = 7,
        JS_TERMINATED = 8,
        JS_KILLED = 9,
        JS_EXCEPTION = 10
    };

    CIMClient c;
    CIMNamespaceName ns;

    Array<CIMInstance> storagePools();
    Array<String> instancePropertyNames(String className, String prop);

    String getClassValue(CIMInstance &instance, String propName);
    CIMInstance getClassInstance(String className);
    CIMInstance getClassInstance(String className, String propertyName,
                                 String propertyValue);

    Uint32 evalInvoke(Array<CIMParamValue> &out, CIMValue value,
                      String jobKey = "Job");

    void processJob(CIMValue &job);

    void printDebug(const CIMValue &v);

    void printVol(const CIMValue &job);

    CIMInstance getSPC(String initiator, String lun, bool &found);

    bool jobCompletedOk(String jobId);
};

#endif
