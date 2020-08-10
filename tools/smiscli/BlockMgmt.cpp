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

#include "BlockMgmt.h"
#include <unistd.h>

template <class Type>
void getPropValue(CIMInstance i, String key, Type &value) {
    i.getProperty(i.findProperty(CIMName(key))).getValue().get(value);
}

BlockMgmt::BlockMgmt(String host, Uint16 port, String smisNameSpace,
                     String userName, String password)
    : ns(smisNameSpace) {
    c.connect(host, port, userName, password);
}

BlockMgmt::~BlockMgmt() { c.disconnect(); }

Array<String> BlockMgmt::getStoragePools() {
    return instancePropertyNames("CIM_StoragePool", "ElementName");
}

Array<String> BlockMgmt::getInitiators() {
    // Note: If you want the storage array IQN go after
    // CIM_SCSIProtocolEndpoint.Name
    return instancePropertyNames("CIM_StorageHardwareID", "StorageID");
}

CIMInstance BlockMgmt::getSPC(String initiator, String lun, bool &found) {
    CIMInstance rc;

    CIMInstance init =
        getClassInstance("CIM_StorageHardwareID", "StorageID", initiator);

    Array<CIMObject> auth_priviledge =
        c.associators(ns, init.getPath(), "CIM_AuthorizedSubject");

    for (Uint32 i = 0; i < auth_priviledge.size(); ++i) {
        Array<CIMObject> spc = c.associators(ns, auth_priviledge[i].getPath(),
                                             "CIM_AuthorizedTarget");

        // Make sure that we have associations for authorized targets and
        // controllers.
        if (spc.size() > 0) {
            Array<CIMObject> logicalDevice = c.associators(
                ns, spc[0].getPath(), "CIM_ProtocolControllerForUnit");

            if (logicalDevice.size() > 0) {
                CIMInstance volume =
                    c.getInstance(ns, logicalDevice[0].getPath().toString());

                String name;
                getPropValue(volume, "ElementName", name);

                if (name == lun) {
                    found = true;
                    return (CIMInstance)spc[0];
                }
            }
        }
    }
    found = false;
    return rc;
}

void BlockMgmt::mapLun(String initiatorID, String lunName) {
    Array<CIMParamValue> in;
    Array<CIMParamValue> out;
    Array<String> lunNames;
    Array<String> initPortIDs;
    Array<String> deviceNumbers;
    Array<Uint16> deviceAccess;

    CIMInstance lun =
        getClassInstance("CIM_StorageVolume", "ElementName", lunName);

    lunNames.append(getClassValue(lun, "Name"));
    initPortIDs.append(initiatorID);
    deviceAccess.append(READ_WRITE); // Hard coded to Read Write

    CIMInstance ccs = getClassInstance("CIM_ControllerConfigurationService");

    in.append(CIMParamValue("LUNames", lunNames));
    in.append(CIMParamValue("InitiatorPortIDs", initPortIDs));
    in.append(CIMParamValue("DeviceAccesses", deviceAccess));

    evalInvoke(out, c.invokeMethod(ns, ccs.getPath(), CIMName("ExposePaths"),
                                   in, out));
}

void BlockMgmt::unmapLun(String initiatorID, String lunName) {
    bool found = false;

    // Need to find the SPC for the passed in initiator and volume (lun).
    CIMInstance spc = getSPC(initiatorID, lunName, found);

    if (found) {
        Array<CIMParamValue> in;
        Array<CIMParamValue> out;

        // Let delete the SPC
        CIMInstance ccs =
            getClassInstance("CIM_ControllerConfigurationService");
        in.append(CIMParamValue("ProtocolController", spc.getPath()));
        in.append(CIMParamValue("DeleteChildrenProtocolControllers", true));
        in.append(CIMParamValue("DeleteUnits", true));

        evalInvoke(out, c.invokeMethod(ns, ccs.getPath(),
                                       CIMName("DeleteProtocolController"), in,
                                       out));
    } else {
        throw Exception("No mapping found");
    }
}

void BlockMgmt::printDebug(const CIMValue &v) {
    String id;
    String name;
    Uint64 blockSize;
    Uint64 numberOfBlocks;

    CIMInstance i = c.getInstance(ns, v.toString());

    getPropValue(i, "DeviceID", id);
    getPropValue(i, "ElementName", name);
    getPropValue(i, "BlockSize", blockSize);
    getPropValue(i, "NumberOfBlocks", numberOfBlocks);

    std::cout << "ID = " << id << " name = " << name
              << " blocksize = " << blockSize
              << " # of blocks = " << numberOfBlocks << std::endl;
}

void BlockMgmt::createLun(String storagePoolName, String name, Uint64 size) {
    Array<CIMParamValue> in;
    Array<CIMParamValue> out;

    CIMInstance scs = getClassInstance("CIM_StorageConfigurationService");
    CIMInstance storagePool =
        getClassInstance("CIM_StoragePool", "ElementName", storagePoolName);

    std::cout << "pool = " << storagePool.getPath().toString() << std::endl;

    in.append(CIMParamValue("ElementName", CIMValue(name)));
    in.append(CIMParamValue("ElementType", (Uint16)STORAGE_VOLUME));
    in.append(CIMParamValue("InPool", storagePool.getPath()));
    in.append(CIMParamValue("Size", CIMValue(size)));

    Uint32 result = evalInvoke(
        out, c.invokeMethod(ns, scs.getPath(),
                            CIMName("CreateOrModifyElementFromStoragePool"), in,
                            out));

    if (result == 0) {
        for (Uint8 i = 0; i < out.size(); i++) {
            if (out[i].getParameterName() == "TheElement") {
                printDebug(out[i].getValue());
            }
        }
    }
}

void BlockMgmt::createInit(String name, String id, String type) {
    Array<CIMParamValue> in;
    Array<CIMParamValue> out;

    CIMInstance hardware =
        getClassInstance("CIM_StorageHardwareIDManagementService");
    in.append(CIMParamValue("ElementName", String(name)));
    in.append(CIMParamValue("StorageID", (CIMValue(id))));

    if (type == "WWN") {
        in.append(CIMParamValue("IDType", (Uint16)2));
    } else {
        in.append(CIMParamValue("IDType", (Uint16)5));
    }

    evalInvoke(out,
               c.invokeMethod(ns, hardware.getPath(),
                              CIMName("CreateStorageHardwareID"), in, out));
}

void BlockMgmt::deleteInit(String id) {
    Array<CIMParamValue> in;
    Array<CIMParamValue> out;

    CIMInstance init =
        getClassInstance("CIM_StorageHardwareID", "StorageID", id);

    CIMInstance hardware =
        getClassInstance("CIM_StorageHardwareIDManagementService");
    in.append(CIMParamValue("HardwareID", init.getPath()));

    evalInvoke(out,
               c.invokeMethod(ns, hardware.getPath(),
                              CIMName("DeleteStorageHardwareID"), in, out));
}

void BlockMgmt::createSnapShot(String sourceLun, String destStoragePool,
                               String destName) {
    Array<CIMParamValue> in;
    Array<CIMParamValue> out;

    CIMInstance rs = getClassInstance("CIM_ReplicationService");
    CIMInstance pool =
        getClassInstance("CIM_StoragePool", "ElementName", destStoragePool);
    CIMInstance lun =
        getClassInstance("CIM_StorageVolume", "ElementName", sourceLun);

    in.append(CIMParamValue("ElementName", CIMValue(destName)));
    in.append(CIMParamValue("SyncType", Uint16(SNAPSHOT)));
    in.append(CIMParamValue("Mode", Uint16(ASYNC)));
    in.append(CIMParamValue("SourceElement", lun.getPath()));
    in.append(CIMParamValue("TargetPool", pool.getPath()));

    evalInvoke(out, c.invokeMethod(ns, rs.getPath(),
                                   CIMName("CreateElementReplica"), in, out));
}

void BlockMgmt::resizeLun(String name, Uint64 size) {
    Array<CIMParamValue> in;
    Array<CIMParamValue> out;

    CIMInstance scs = getClassInstance("CIM_StorageConfigurationService");
    CIMInstance lun =
        getClassInstance("CIM_StorageVolume", "ElementName", name);

    in.append(CIMParamValue("TheElement", CIMValue(lun.getPath())));
    in.append(CIMParamValue("Size", CIMValue(size)));

    evalInvoke(out,
               c.invokeMethod(ns, scs.getPath(),
                              CIMName("CreateOrModifyElementFromStoragePool"),
                              in, out));
}

void BlockMgmt::deleteLun(String name) {
    Array<CIMParamValue> in;
    Array<CIMParamValue> out;

    CIMInstance scs = getClassInstance("CIM_StorageConfigurationService");
    CIMInstance lun =
        getClassInstance("CIM_StorageVolume", "ElementName", name);

    in.append(CIMParamValue("TheElement", lun.getPath()));
    evalInvoke(out, c.invokeMethod(ns, scs.getPath(),
                                   CIMName("ReturnToStoragePool"), in, out));
}

Uint32 BlockMgmt::evalInvoke(Array<CIMParamValue> &out, CIMValue value,
                             String jobKey) {
    Uint32 result = 0;
    value.get(result);
    CIMValue job;

    if (result) {
        String params;

        for (Uint8 i = 0; i < out.size(); i++) {
            params = params + " (key:value)(" + out[i].getParameterName() +
                     ":" + out[i].getValue().toString() + ")";

            if (result == INVOKE_ASYNC) {
                if (out[i].getParameterName() == jobKey) {
                    job = out[i].getValue();
                }
            }
        }

        if (result == INVOKE_ASYNC) {
            std::cout << "Params = " << params;
            processJob(job);
        } else {
            throw Exception(value.toString().append(params));
        }
    }

    return result;
}

void BlockMgmt::printVol(const CIMValue &job) {
    CIMInstance j = c.getInstance(ns, job.toString());

    /*Array<CIMObject> as = c.associators(ns, j.getPath(), NULL, NULL, "",
    false, false, NULL);

    for( Uint32 i = 0; i < as.size(); ++i ) {

    } */
}

void BlockMgmt::jobStatus(String id) {
    Array<Uint16> values;
    CIMObject status = c.getInstance(ns, id);

    status.getProperty(status.findProperty("OperationalStatus"))
        .getValue()
        .get(values);

    if (values.size()) {
        std::cout << "Operational status: ";

        for (Uint32 i = 0; i < values.size(); ++i) {
            std::cout << values[i] << " ";
        }
        std::cout << std::endl;
    } else {
        std::cout << "Operational status is empty!" << std::endl;
    }

    std::cout << "Percent complete= "
              << status.getProperty(status.findProperty("PercentComplete"))
                     .getValue()
                     .toString()
              << std::endl;

    std::cout << "Job state= "
              << status.getProperty(status.findProperty("JobState"))
                     .getValue()
                     .toString()
              << std::endl;
}

bool BlockMgmt::jobCompletedOk(String jobId) {
    Array<Uint16> values;
    bool rc = false;

    CIMObject status = c.getInstance(ns, jobId);

    status.getProperty(status.findProperty("OperationalStatus"))
        .getValue()
        .get(values);

    if (values.size() > 0) {
        if (values.size() == 1) {
            // Based on the documentations on page 257 this should only
            // occur when the job was stopped.
            std::cout << "Error: Operational status = " << values[0]
                      << std::endl;
        } else if (values.size() > 1) {
            if ((values[0] == OK || values[1] == OK) &&
                (values[0] == COMPLETE || values[1] == COMPLETE)) {
                rc = true;
                std::cout << "Success: Operational status = " << values[0]
                          << ", " << values[1] << std::endl;
            } else {
                std::cout << "Error: Operational status = " << values[0] << ", "
                          << values[1] << std::endl;
            }
        }
    } else {
        std::cout << "No operational status available!" << std::endl;
    }
    return rc;
}

void BlockMgmt::processJob(CIMValue &job) {
    std::cout << std::endl << "job started= " << job.toString() << std::endl;

    while (true) {
        Uint16 jobState;
        Boolean autodelete = false;

        CIMObject status = c.getInstance(ns, job.toString());
        status.getProperty(status.findProperty("JobState"))
            .getValue()
            .get(jobState);

        switch (jobState) {
        case (JS_NEW):
        case (JS_STARTING):
            break;
        case (JS_RUNNING):
            // Dump percentage
            std::cout << "Percent complete= "
                      << status
                             .getProperty(
                                 status.findProperty("PercentComplete"))
                             .getValue()
                             .toString()
                      << std::endl;
            break;
        case (JS_COMPLETED):
            // Check operational status.
            std::cout << "Job is complete!" << std::endl;

            jobCompletedOk(job.toString());

            status.getProperty(status.findProperty("DeleteOnCompletion"))
                .getValue()
                .get(autodelete);

            if (!autodelete) {
                // We are done, delete job instance.
                try {
                    c.deleteInstance(ns, job.toString());
                    std::cout << "Deleted job!" << std::endl;
                } catch (Exception &e) {
                    std::cout << "Warning: error when deleting job! "
                              << e.getMessage() << std::endl;
                }
            }
            return;
        default:
            std::cout << "Unexpected job state " << jobState << std::endl;
            return;
            break;
        }

        sleep(1);
    }

    /*
        status.getProperty(status.findProperty("OperationalStatus")).getValue().get(values);
        if( values.size() > 0 ) {
            if( values.size() == 1 ) {
                //Based on the documentations on page 257 this should only
                //occur when the job was stopped.


            } else if( values.size() > 1 ) {
                if( (values[0] == OK || values[1] == OK) &&
                    (values[0] == COMPLETE || values[0] == COMPLETE)) {

                }
            }
        }

        if( values[0] == OK || values[1] == OK ) {
            //Array of values may have 1 or 2 values according to mof
            if( values.size() == 2 ) {
                Boolean autodelete = false;
                status.getProperty(status.findProperty("DeleteOnCompletion")).getValue().get(autodelete);

                if( !autodelete ) {
                    //We are done, delete job instance.
                    try {
                        c.deleteInstance(ns, job.toString());
                    } catch (Exception &e) {
                        std::cout   << "Warning: error when deleting job! "
                                    << e.getMessage() << std::endl;
                    }
                }

                if( values[1] == COMPLETE) {
                    std::cout << "Job complete!" << std::endl;




                } else if ( values[1] == STOPPED) {
                    std::cout << "Job stopped!" << std::endl;
                } else if( values[1] == ERROR ) {
                    std::cout << "Job errored!" << std::endl;
                }
                return;
            }

            std::cout   << "Percent complete= "
                        <<
    status.getProperty(status.findProperty("PercentComplete")).getValue().toString()
                        << std::endl;

            sleep(1);
        } else {
            throw Exception("Job " + job.toString() + " encountered an error!");
        }
    }

    */
}

Array<String> BlockMgmt::getLuns() {
    return instancePropertyNames("CIM_StorageVolume", "ElementName");
}

Array<CIMInstance> BlockMgmt::storagePools() {
    return c.enumerateInstances(ns, CIMName("CIM_StoragePool"));
}

Array<String> BlockMgmt::instancePropertyNames(String className, String prop) {
    Array<String> names;

    Array<CIMInstance> instances = c.enumerateInstances(ns, CIMName(className));

    for (Uint32 i = 0; i < instances.size(); ++i) {
        Uint32 propIndex = instances[i].findProperty(CIMName(prop));
        names.append(instances[i].getProperty(propIndex).getValue().toString());
    }
    return names;
}

String BlockMgmt::getClassValue(CIMInstance &instance, String propName) {
    return instance.getProperty(instance.findProperty(propName))
        .getValue()
        .toString();
}

CIMInstance BlockMgmt::getClassInstance(String className) {
    Array<CIMInstance> cs = c.enumerateInstances(ns, CIMName(className));

    // Could there be more than one?
    // If there is, what would be a better thing to do, pick one?
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

CIMInstance BlockMgmt::getClassInstance(String className, String propertyName,
                                        String propertyValue) {
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
