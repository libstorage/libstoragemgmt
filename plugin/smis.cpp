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

#include "smis.h"
#include <libstoragemgmt/libstoragemgmt_pool.h>
#include <libstoragemgmt/libstoragemgmt_volumes.h>
#include <libstoragemgmt/libstoragemgmt_initiators.h>

template <class Type> void getPropValue(CIMInstance i, String key, Type &value)
{
    i.getProperty(i.findProperty(CIMName(key))).getValue().get(value);
}

Smis::Smis(String host, Uint16 port, String smisNameSpace,
    String userName, String password, int timeout) : ns(smisNameSpace)
{
    c.setTimeout(timeout);
    c.connect(host, port, userName, password);
}

Smis::~Smis()
{
    c.disconnect();
}

void Smis::setTmo(Uint32 timeout)
{
    c.setTimeout(timeout);
}

Uint32 Smis::getTmo()
{
    return c.getTimeout();
}

lsmPoolPtr * Smis::getStoragePools(Uint32 *count)
{
    lsmPoolPtr *rc = NULL;

    *count = 0;

    Array<CIMInstance> instances = c.enumerateInstances(ns, CIMName("CIM_StoragePool"));

    if (instances.size() > 0) {
        Uint32 num = 0;

        for (Uint32 i = 0; i < instances.size(); ++i) {
            Boolean primordial;
            getPropValue(instances[i], "Primordial", primordial);

            if (!primordial) {
                ++num;
            }
        }

        *count = num;
        rc = lsmPoolRecordAllocArray(num);
        Uint32 addIndex = 0;
        for (Uint32 i = 0; i < instances.size(); ++i) {
            String idValue;
            String nameValue;
            Uint64 spaceValue = 0;
            Uint64 freeValue = 0;
            Boolean primordial;
            getPropValue(instances[i], "Primordial", primordial);

            if (!primordial) {

                getPropValue(instances[i], "PoolID", idValue);
                getPropValue(instances[i], "ElementName", nameValue);
                getPropValue(instances[i], "TotalManagedSpace", spaceValue);
                getPropValue(instances[i], "RemainingManagedSpace", freeValue);

                rc[addIndex++] = lsmPoolRecordAlloc(idValue.getCString(),
                    nameValue.getCString(),
                    spaceValue,
                    freeValue);
            }
        }
    }

    return rc;
}

lsmInitiatorPtr *Smis::getInitiators(Uint32 *count)
{
    //Note: If you want the storage array IQN go after CIM_SCSIProtocolEndpoint.Name
    //return instancePropertyNames("CIM_StorageHardwareID", "StorageID");

    lsmInitiatorPtr *rc = NULL;

    *count = 0;

    Array<CIMInstance> instances = c.enumerateInstances(ns, CIMName("CIM_StorageHardwareID"));

    if (instances.size() > 0) {

        *count = instances.size();
        rc = lsmInitiatorRecordAllocArray(instances.size());

        for (Uint32 i = 0; i < instances.size(); ++i) {
            String storageId;
            Uint16 idType;

            getPropValue(instances[i], "StorageID", storageId);
            getPropValue(instances[i], "IDType", idType);

            rc[i] = lsmInitiatorRecordAlloc((lsmInitiatorType)idType,
                                                storageId.getCString());
        }
    }

    return rc;
}

lsmVolumePtr *Smis::getVolumes(Uint32 *count)
{
    //return instancePropertyNames("CIM_StorageVolume", "ElementName");
    lsmVolumePtr *rc = NULL;

    *count = 0;

    Array<CIMInstance> instances = c.enumerateInstances(ns, CIMName("CIM_StorageVolume"));

    if (instances.size() > 0) {
        *count = instances.size();
        rc = lsmVolumeRecordAllocArray(instances.size());

        for (Uint32 i = 0; i < instances.size(); ++i) {
            rc[i] = getVolume(instances[i]);
        }
    }
    return rc;
}

int Smis::createLun(lsmPoolPtr pool, char *volumeName,
    Uint64 size, lsmProvisionType provisioning,
    lsmVolumePtr *newVolume, Uint32 *job)
{
    Array<CIMParamValue> in;
    Array<CIMParamValue> out;


    if (provisioning != LSM_PROVISION_DEFAULT) {
        return LSM_ERR_UNSUPPORTED_PROVISIONING;
    }

    CIMInstance scs = getClassInstance("CIM_StorageConfigurationService");
    CIMInstance storagePool = getPool(pool);

    in.append(CIMParamValue("ElementName", CIMValue(String(volumeName))));
    in.append(CIMParamValue("ElementType", (Uint16) STORAGE_VOLUME));
    in.append(CIMParamValue("InPool", storagePool.getPath()));
    in.append(CIMParamValue("Size", CIMValue(size)));


    *newVolume = NULL;
    *job = 0;

    return processInvoke(out, c.invokeMethod(ns, scs.getPath(),
        CIMName("CreateOrModifyElementFromStoragePool"),
        in, out), job, newVolume);
}

int Smis::createInit(char *name, char *id, lsmInitiatorType type,
                        lsmInitiatorPtr *init)
{
    Array<CIMParamValue> in;
    Array<CIMParamValue> out;

    CIMInstance hardware = getClassInstance("CIM_StorageHardwareIDManagementService");
    in.append(CIMParamValue("ElementName", String(name)));
    in.append(CIMParamValue("StorageID", String(id)));
      in.append(CIMParamValue("IDType", (Uint16) type));

    int rc = processInvoke(out, c.invokeMethod(ns, hardware.getPath(),
        CIMName("CreateStorageHardwareID"), in, out));

    if( LSM_ERR_OK == rc ) {
        *init = lsmInitiatorRecordAlloc(type, id);
    } else {
        *init = NULL;
    }
    return rc;
}

int Smis::grantAccess(lsmInitiatorPtr i, lsmVolumePtr v, lsmAccessType access,
    Uint32 *job)
{
    Array<CIMParamValue> in;
    Array<CIMParamValue> out;
    Array<String>lunNames;
    Array<String>initPortIDs;
    Array<Uint16>deviceAccess;

    CIMInstance lun = getVolume(v);

    lunNames.append(getClassValue(lun, "Name"));
    initPortIDs.append(String(lsmInitiatorIdGet(i)));

    if (access == LSM_VOLUME_ACCESS_READ_ONLY) {
        deviceAccess.append(READ_ONLY);
    } else {
        deviceAccess.append(READ_WRITE);
    }

    CIMInstance ccs = getClassInstance("CIM_ControllerConfigurationService");

    in.append(CIMParamValue("LUNames", lunNames));
    in.append(CIMParamValue("InitiatorPortIDs", initPortIDs));
    in.append(CIMParamValue("DeviceAccesses", deviceAccess));

    return processInvoke(out, c.invokeMethod(ns, ccs.getPath(),
        CIMName("ExposePaths"),
        in, out));
}

int Smis::removeAccess(lsmInitiatorPtr i, lsmVolumePtr v)
{
    bool found = false;

    //Need to find the SPC for the passed in initiator and volume (lun).
    CIMInstance spc = getSpc(i, v, found);

    if (found) {
        Array<CIMParamValue> in;
        Array<CIMParamValue> out;

        //Let delete the SPC
        CIMInstance ccs = getClassInstance("CIM_ControllerConfigurationService");
        in.append(CIMParamValue("ProtocolController", spc.getPath()));
        in.append(CIMParamValue("DeleteChildrenProtocolControllers", true));
        in.append(CIMParamValue("DeleteUnits", true));

        return processInvoke(out, c.invokeMethod(ns, ccs.getPath(),
            CIMName("DeleteProtocolController"),
            in, out));
    } else {
        return LSM_ERR_NO_MAPPING;
    }
}

int Smis::replicateLun(lsmPoolPtr p, lsmReplicationType repType,
    lsmVolumePtr volumeSrc, char *name,
    lsmVolumePtr *newReplicant, Uint32 *job)
{

    Array<CIMParamValue> in;
    Array<CIMParamValue> out;
    Uint16 sync = 0;

    CIMInstance rs = getClassInstance("CIM_ReplicationService");
    CIMInstance pool = getPool(p);
    CIMInstance lun = getVolume(volumeSrc);

    in.append(CIMParamValue("ElementName", CIMValue(String(name))));

    switch (repType) {
    case(LSM_VOLUME_REPLICATE_CLONE): sync = CLONE;
        break;
    case(LSM_VOLUME_REPLICATE_MIRROR): sync = MIRROR;
        break;
    default:
        sync = SNAPSHOT;
        break;
    }
    in.append(CIMParamValue("SyncType", sync));


    in.append(CIMParamValue("Mode", Uint16(ASYNC)));
    in.append(CIMParamValue("SourceElement", lun.getPath()));
    in.append(CIMParamValue("TargetPool", pool.getPath()));

    *newReplicant = NULL;
    *job = 0;

    return processInvoke(out, c.invokeMethod(ns, rs.getPath(),
        CIMName("CreateElementReplica"), in, out), job, newReplicant);
}

int Smis::resizeVolume(lsmVolumePtr volume, uint64_t newSize,
    lsmVolumePtr *resizedVolume, uint32_t *job)
{
    Array<CIMParamValue> in;
    Array<CIMParamValue> out;

    CIMInstance scs = getClassInstance("CIM_StorageConfigurationService");
    CIMInstance lun = getVolume(volume);

    in.append(CIMParamValue("TheElement", CIMValue(lun.getPath())));
    in.append(CIMParamValue("Size", CIMValue((Uint64) newSize)));

    return processInvoke(out, c.invokeMethod(ns, scs.getPath(),
        CIMName("CreateOrModifyElementFromStoragePool"), in, out), job,
        resizedVolume);
}

int Smis::deleteVolume(lsmVolumePtr v, uint32_t *jobId)
{
    Array<CIMParamValue> in;
    Array<CIMParamValue> out;

    CIMInstance scs = getClassInstance("CIM_StorageConfigurationService");
    CIMInstance lun = getVolume(v);

    in.append(CIMParamValue("TheElement", lun.getPath()));
    return processInvoke(out, c.invokeMethod(ns, scs.getPath(),
        CIMName("ReturnToStoragePool"), in, out),
        jobId);
}

int Smis::processInvoke(Array<CIMParamValue> &out, CIMValue value,
    Uint32 *jobId, lsmVolumePtr *v)
{
    int rc = LSM_ERR_PLUGIN_ERROR;
    Uint32 result = 0;
    value.get(result);

    if (0 == result) {
        if (v) {
            //Operation is finished, return the new volume information
            CIMInstance i = c.getInstance(ns,
                getParamValue(out, "TheElement").toString());
            *v = getVolume(i);
        }
        rc = LSM_ERR_OK;
    } else if (INVOKE_ASYNC == result) {
        if (jobId) {
            Job j;
            j.cimJob = getParamValue(out, "Job");
            *jobId = jobs.insert(j);
            rc = LSM_ERR_JOB_STARTED;
        } else {
            //This really should never happen :-)
            rc = LSM_ERR_INTERNAL_ERROR;
        }
    } else {
        throw Exception(value.toString().append(getParamValueDebug(out)));
    }
    return rc;
}

int Smis::jobFree(Uint32 jobNumber)
{
    if (jobs.present(jobNumber)) {
        jobs.remove(jobNumber);
    }
    return LSM_ERR_INVALID_JOB_NUM;
}

int Smis::jobStatusVol(Uint32 jobNumber, lsmJobStatus *status,
    Uint8 *percentComplete, lsmVolumePtr *vol)
{
    Job j;
    Array<Uint16> values;
    Uint16 pc = 0;

    if (jobs.present(jobNumber)) {
        if (vol) {
            *vol = NULL;
        }

        j = jobs.get(jobNumber);

        CIMObject cimStatus = c.getInstance(ns, j.cimJob.toString());

        cimStatus.getProperty(cimStatus.findProperty("OperationalStatus")).getValue().get(values);

        if (values[0] == OK) {
            //Array of values may have 1 or 2 values according to mof
            if (values.size() == 2) {
                Boolean autodelete = false;
                cimStatus.getProperty(cimStatus.findProperty("DeleteOnCompletion")).getValue().get(autodelete);

                if (!autodelete) {
                    //We are done, delete job instance.
                    //Need to verify if this is really correct.
                    try {
                        c.deleteInstance(ns, cimStatus.getPath());
                    } catch (Exception &e) {
                    }
                }

                if (values[1] == COMPLETE) {
                    //NOTE: Go fetch the new volume information for this!

                    *status = LSM_JOB_COMPLETE;

                    if (vol) {
                        CIMInstance vi;
                        if (getVolume(vi, j.cimJob)) {
                            *vol = getVolume(vi);
                        }
                    }
                } else if (values[1] == STOPPED) {
                    *status = LSM_JOB_STOPPED;
                } else if (values[1] == ERROR) {
                    *status = LSM_JOB_ERROR;
                }

                *percentComplete = 100;
                return LSM_ERR_OK;
            }

            //Job is in progress.
            *status = LSM_JOB_INPROGRESS;
            cimStatus.getProperty(cimStatus.findProperty("PercentComplete")).getValue().get(pc);
            *percentComplete = (pc > 100) ? 100 : pc;

        } else {
            throw Exception("Job " + j.cimJob.toString() + " encountered an error!");
        }
    }
    return LSM_ERR_INVALID_JOB_NUM;
}

Array<CIMInstance> Smis::storagePools()
{
    return c.enumerateInstances(ns, CIMName("CIM_StoragePool"));
}

Array<String> Smis::instancePropertyNames(String className, String prop)
{
    Array<String> names;

    Array<CIMInstance> instances = c.enumerateInstances(ns, CIMName(className));

    for (Uint32 i = 0; i < instances.size(); ++i) {
        Uint32 propIndex = instances[i].findProperty(CIMName(prop));
        names.append(instances[i].getProperty(propIndex).getValue().toString());
    }
    return names;
}

String Smis::getClassValue(CIMInstance &instance, String propName)
{
    return instance.getProperty(instance.findProperty(propName)).getValue().toString();
}

CIMInstance Smis::getClassInstance(String className)
{
    Array<CIMInstance> cs = c.enumerateInstances(ns, CIMName(className));

    //Could there be more than one?
    //If there is, what would be a better thing to do, pick one?
    if (cs.size() != 1) {
        String instances;

        if (cs.size() == 0) {
            instances = "none!";
        } else {
            instances.append("\n");
            for (Uint32 i = 0; i < cs.size(); ++i) {
                instances.append(cs[i].getPath().toString() + String("\n"));
            }
        }

        throw Exception("Expecting one object instance of " + className +
            "got " + instances);
    }
    return cs[0];
}

CIMInstance Smis::getClassInstance(String className, String propertyName,
    String propertyValue)
{
    Array<CIMInstance> cs = c.enumerateInstances(ns, CIMName(className));

    for (Uint32 i = 0; i < cs.size(); ++i) {
        Uint32 index = cs[i].findProperty(propertyName);
        if (cs[i].getProperty(index).getValue().toString() == propertyValue) {
            return cs[i];
        }
    }
    throw Exception("Instance of class name: " + className + " property=" +
        propertyName + " value= " + propertyValue + " not found.");
}

CIMValue Smis::getParamValue(Array<CIMParamValue> o, String key)
{
    CIMValue v;

    for (Uint32 i = 0; i < o.size(); ++i) {
        if (o[i].getParameterName() == key) {
            return o[i].getValue();
        }
    }
    return v;
}

String Smis::getParamValueDebug(Array<CIMParamValue> o)
{
    String rc;

    for (Uint32 i = 0; i < o.size(); i++) {
        rc = rc + " key:value(" + o[i].getParameterName() + ":" +
            o[i].getValue().toString() + ")";
    }
    return rc;
}

lsmVolumePtr Smis::getVolume(const CIMInstance &i)
{
    String id;
    String name;
    Array<String> vpd;
    Uint64 blockSize;
    Uint64 numberOfBlocks;
    Array<Uint16> status;
    Uint32 opStatus;
    lsmVolumePtr rc = NULL;

    getPropValue(i, "DeviceID", id);
    getPropValue(i, "ElementName", name);
    getPropValue(i, "OtherIdentifyingInfo", vpd);
    getPropValue(i, "BlockSize", blockSize);
    getPropValue(i, "NumberOfBlocks", numberOfBlocks);
    getPropValue(i, "OperationalStatus", status);

    opStatus = LSM_VOLUME_OP_STATUS_UNKNOWN;
    for (Uint32 j = 0; j < status.size(); ++j) {
        switch (status[j]) {
        case(2): opStatus |= LSM_VOLUME_OP_STATUS_OK;
            break;
        case(3): opStatus |= LSM_VOLUME_OP_STATUS_DEGRADED;
            break;
        case(6): opStatus |= LSM_VOLUME_OP_STATUS_ERROR;
            break;
        case(8): opStatus |= LSM_VOLUME_OP_STATUS_STARTING;
            break;
        case(15): opStatus |= LSM_VOLUME_OP_STATUS_DORMANT;
            break;
        }
    }

    rc = lsmVolumeRecordAlloc(id.getCString(), name.getCString(),
        vpd[0].getCString(), blockSize,
        numberOfBlocks, opStatus);

    return rc;
}

bool Smis::getVolume(CIMInstance &vol, const CIMValue &job)
{
    Array<CIMObject> associations = c.associators(ns, job.toString());

    for (Uint32 i = 0; i < associations.size(); ++i) {
        vol = c.getInstance(ns, associations[i].getPath());
        return true;
    }
    return false;
}

CIMInstance Smis::getPool(lsmPoolPtr p)
{
    return getClassInstance("CIM_StoragePool", "PoolID",
        String(lsmPoolIdGet(p)));
}

CIMInstance Smis::getVolume(lsmVolumePtr v)
{
    return getClassInstance("CIM_StorageVolume", "DeviceID",
        String(lsmVolumeIdGet(v)));
}

CIMInstance Smis::getSpc(lsmInitiatorPtr initiator, lsmVolumePtr v, bool &found)
{
    CIMInstance rc;

    CIMInstance init = getClassInstance("CIM_StorageHardwareID", "StorageID",
        String(lsmInitiatorIdGet(initiator)));

    String vol = lsmVolumeIdGet(v);

    Array<CIMObject> auth_priviledge = c.associators(ns, init.getPath(),
        "CIM_AuthorizedSubject");

    for (Uint32 i = 0; i < auth_priviledge.size(); ++i) {
        Array<CIMObject> spc = c.associators(ns, auth_priviledge[0].getPath(),
            "CIM_AuthorizedTarget");
        Array<CIMObject> logicalDevice = c.associators(ns, spc[0].getPath(),
            "CIM_ProtocolControllerForUnit");

        CIMInstance volume = c.getInstance(ns,
            logicalDevice[0].getPath().toString());

        String volId;
        getPropValue(volume, "DeviceID", volId);

        if (volId == vol) {
            found = true;
            return(CIMInstance) spc[0];
        }
    }
    found = false;
    return rc;
}