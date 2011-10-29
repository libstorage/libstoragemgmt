/*
 * Copyright (C) 2011 Red Hat, Inc.
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 *
 * Author: tasleson
 */

#ifndef __SMIS_H__
#define __SMIS_H__

#include <Pegasus/Common/Config.h>
#include <Pegasus/Client/CIMClient.h>
#include <libstoragemgmt/libstoragemgmt_common.h>
#include <libstoragemgmt/libstoragemgmt_types.h>
#include <libstoragemgmt/libstoragemgmt_plug_interface.h>
#include "./util/misc.h"

#include <map>

PEGASUS_USING_PEGASUS;
PEGASUS_USING_STD;

/**
 * Class for Smis block operations.
 */
class LSM_DLL_LOCAL Smis
{
public:

    /**
     * Class constructor.
     * Once this completes we have a connection to the SMI-S agent/proxy.
     * @param   host    a string representing the IP or host of SMI-S agent
     * @param   port    The server port to connect too.
     * @param   smisNameSpace   The SMI-S namespace to use.
     * @param   userName    User name when using authentication.
     * @param   password    Plain text password.
     * @param   timeout Timeout in ms
     */
    Smis(  String host, Uint16 port, String smisNameSpace,
                                String userName, String password, int timeout);

    /**
     * Class destructor which closes the connection to the SMI-S agent/proxy
     */
    ~Smis();

    /**
     * Set the connection timeout
     * @param timeout in ms
     */
    void setTmo(Uint32 timeout);

    /**
     * Retrieve the connection timeout.
     * @return Number of ms for timeout
     */
    Uint32 getTmo();

    /**
     * Creates a logical unit.
     * @param pool              Pool to create lun from.
     * @param volumeName        Name of new lun
     * @param size              Size of new lun
     * @param provisioning      What type of provisioning requested.
     * @param newVolume         New volume information (NULL if job valid)
     * @param job               Job number if async. operation.
     * @return  LSM_ERR_OK, LSM_ERR_JOB_STARTED or other lsmErrorNumber.
     */
    int createLun( lsmPoolPtr pool, const char *volumeName,
                        Uint64 size, lsmProvisionType provisioning,
                        lsmVolumePtr *newVolume, Uint32 *job);

    /**
     * Creates an initiator record.
     * @param name      Name of the initiator.
     * @param id        Id of the initiator.
     * @param type      Type
     * @param[out] init Newly created initiator structure.
     * @return LSM_ERR_OK, else error reason.
     */
    int createInit( const char *name, const char *id,
                            lsmInitiatorType type, lsmInitiatorPtr *init);

    /**
     * Grants access to a volume to an initiator.
     * @param init      Initiator
     * @param vol       Volume
     * @param access    Type of access
     * @param job       Async. job id.
     * @return LSM_ERR_OK, LSM_ERR_JOB_STARTED else error reason
     */
    int grantAccess( lsmInitiatorPtr init, lsmVolumePtr vol,
                        lsmAccessType access, Uint32 *job);

    /**
     * Removes access to a volume from an initiator.
     * @param init      Initiator
     * @param vol       Volume
     * @return LSM_ERR_OK, LSM_ERR_NO_MAPPING else error reason.
     */
    int removeAccess( lsmInitiatorPtr init, lsmVolumePtr vol );

    /**
     * Replicates a volume
     * @param p                 Pool to create lun from
     * @param repType           Replication type
     * @param volumeSrc         Volume source
     * @param name              Volume Name
     * @param newReplicant      Name of new replicated volume
     * @param job               Job number if async. operation.
     * @return LSM_ERR_OK, LSM_ERR_JOB_STARTED or other lsmErrorNumber.
     */
    int replicateLun( lsmPoolPtr p, lsmReplicationType repType,
                        lsmVolumePtr volumeSrc, const char *name,
                        lsmVolumePtr *newReplicant, Uint32 *job);


    /**
     * Deletes a volume.
     * @param v         Volume to delete.
     * @param job       Async. job id.
     * @return LSM_ERR_OK, LSM_ERR_JOB_STARTED else error reason.
     */
    int deleteVolume( lsmVolumePtr v, uint32_t *job);

    /**
     * Re-sizes a volume.
     * @param[in] volume            Volume to resize.
     * @param[in] newSize           New size for the volume
     * @param[out] resizedVolume    Pointer to the newly resized volume
     * @param[out] job
     * @return LSM_ERR_OK, LSM_ERR_JOB_STARTED else error reason.
     */
    int resizeVolume(lsmVolumePtr volume,
                                uint64_t newSize, lsmVolumePtr *resizedVolume,
                                uint32_t *job);

    /**
     * Returns an array of lsmPoolPtr
     * @param   count   Number of pools
     * @return  An array of lsmPoolPtr's
     * @throws  Exception on error
     */
    lsmPoolPtr *getStoragePools(Uint32 *count);

    /**
     * Returns an array of lsmVolumePtr
     * @param   count   Number of pools
     * @return  An array of lsmVolumePtrs
     * @throws  Exception on error
     */
    lsmVolumePtr *getVolumes(Uint32 *count);

    /**
     * Returns an array of
     * @return An array of lsmPoolPtr's
     * @throws Exception on error
     */
    lsmInitiatorPtr *getInitiators(Uint32 *count);

    /**
     * Grants read/write access for a lun to the specified initiator.
     * @param   initiatorID     The initiator ID
     * @param   lunName         The lun name
     * @throws Exception
     */
    void mapLun(String initiatorID, String lunName);

    /**
     * Retrieve the status for a previous submitted job.
     * @param[in]  jobNumber           Job number
     * @param[out]  status              Job status
     * @param[out]  percentComplete     Percent complete
     * @param[out]  vol                 Volume pointer if job is related to one.
     * @return LSM_ERR_OK else error reason.
     */
    int jobStatusVol(Uint32 jobNumber, lsmJobStatus *status,
                        Uint8 *percentComplete, lsmVolumePtr *vol);

    /**
     * Frees the resources for a job.
     * @param[in] jobNumber     Job number to free.
     * @return LSM_ERR_OK else error reason.
     */
    int jobFree(Uint32 jobNumber);

private:

    class Job
    {
    public:
        CIMValue cimJob;
    };

    enum ElementType { UNKNOWN = 0, RESERVED = 1, STORAGE_VOLUME = 2,
                       STORAGE_EXTENT = 3, STORAGE_POOL = 4, LOGICAL_DISK = 5
                     };

    enum DeviceAccess { READ_WRITE = 2, READ_ONLY = 3,
                        NO_ACCESS = 4
                      };

    enum SyncType { MIRROR = 6, SNAPSHOT = 7, CLONE = 8 };

    enum Mode { SYNC = 2, ASYNC = 3 };

    enum MethodInvoke { INVOKE_OK = 0, INVOKE_ASYNC = 4096 };

    enum OperationalStatus { OK = 2, ERROR = 6, STOPPED = 10, COMPLETE = 17 };

    CIMClient   c;
    CIMNamespaceName    ns;
    LSM::JobControl<Job> jobs;

    int jobStatus(uint32_t jobNumber);

    Array<CIMInstance>  storagePools();
    Array<String> instancePropertyNames( String className, String prop );

    String getClassValue(CIMInstance &instance, String propName);
    CIMInstance getClassInstance(String className);
    CIMInstance getClassInstance(String className, String propertyName,
                                 String propertyValue );

    int processInvoke( Array<CIMParamValue> &out, CIMValue value,
                        Uint32 *jobid = NULL, lsmVolumePtr *v = NULL );

    CIMValue getParamValue(Array<CIMParamValue> o, String key);
    String getParamValueDebug( Array<CIMParamValue> o );


    lsmVolumePtr getVolume(const CIMInstance &i);
    bool getVolume(CIMInstance &vol, const CIMValue &job);
    bool getVolumeInstance(String name, CIMInstance &i);
    CIMInstance getVolume(lsmVolumePtr v);


    CIMInstance getPool(lsmPoolPtr p);
    CIMInstance getSpc( lsmInitiatorPtr initiator, lsmVolumePtr v, bool &found );
};

#endif
