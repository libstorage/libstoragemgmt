/*
 * Copyright 2011, Red Hat, Inc.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "smis.h"

Smis::Smis(String host, Uint16 port, String smisNameSpace,
                     String userName, String password, int timeout):ns(smisNameSpace)
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
    lsmPoolPtr *rc;

    *count = 0;

    Array<CIMInstance> instances = c.enumerateInstances(ns, CIMName("CIM_StoragePool"));

    if( instances.size() > 0 ) {

        *count = instances.size();
        rc = lsmPoolRecordAllocArray(instances.size());

        for(Uint32 i = 0; i < instances.size(); ++i) {
            Uint32 idIndex = instances[i].findProperty(CIMName("PoolID"));
            Uint32 nameIndex = instances[i].findProperty(CIMName("ElementName"));
            Uint32 spaceIndex = instances[i].findProperty(CIMName("TotalManagedSpace"));
            Uint32 freeIndex = instances[i].findProperty(CIMName("RemainingManagedSpace"));
            String idValue;
            String nameValue;
            Uint64 spaceValue = 0;
            Uint64 freeValue = 0;

            instances[i].getProperty(idIndex).getValue().get(idValue);
            instances[i].getProperty(nameIndex).getValue().get(nameValue);
            instances[i].getProperty(spaceIndex).getValue().get(spaceValue);
            instances[i].getProperty(freeIndex).getValue().get(freeValue);

            rc[i] = lsmPoolRecordAlloc( idValue.getCString(),
                                        nameValue.getCString(),
                                        spaceValue,
                                        freeValue);

        }
    }

    return rc;
}

/*
Array<String> Smis::getStoragePools()
{
    return instancePropertyNames("CIM_StoragePool", "ElementName");
}
 */

Array<String> Smis::getInitiators()
{
    //Note: If you want the storage array IQN go after CIM_SCSIProtocolEndpoint.Name
    return instancePropertyNames("CIM_StorageHardwareID", "StorageID");
}

void Smis::mapLun(String initiatorID, String lunName)
{
    Array<CIMParamValue> in;
    Array<CIMParamValue> out;
    Array<String>lunNames;
    Array<String>initPortIDs;
    Array<String>deviceNumbers;
    Array<Uint16>deviceAccess;

    CIMInstance lun = getClassInstance("CIM_StorageVolume", "ElementName",
                                       lunName);

    lunNames.append(getClassValue(lun, "Name"));
    initPortIDs.append(initiatorID);
    deviceAccess.append(READ_WRITE);   //Hard coded to Read Write

    CIMInstance ccs = getClassInstance("CIM_ControllerConfigurationService");

    in.append(CIMParamValue("LUNames", lunNames));
    in.append(CIMParamValue("InitiatorPortIDs", initPortIDs));
    in.append(CIMParamValue("DeviceAccesses", deviceAccess));

    evalInvoke(out, c.invokeMethod(ns, ccs.getPath(),
                                   CIMName("ExposePaths"),
                                   in, out));
}

void Smis::createLun( String storagePoolName, String name, Uint64 size)
{
    Array<CIMParamValue> in;
    Array<CIMParamValue> out;

    CIMInstance scs = getClassInstance("CIM_StorageConfigurationService");
    CIMInstance storagePool = getClassInstance("CIM_StoragePool", "ElementName",
                              storagePoolName);

    in.append(CIMParamValue("ElementName", CIMValue(name)));
    in.append(CIMParamValue("ElementType", (Uint16)STORAGE_VOLUME));
    in.append(CIMParamValue("InPool", storagePool.getPath()));
    in.append(CIMParamValue("Size", CIMValue(size)));

    evalInvoke(out, c.invokeMethod(ns, scs.getPath(),
                                   CIMName("CreateOrModifyElementFromStoragePool"),
                                   in, out));
}

void Smis::createSnapShot( String sourceLun, String destStoragePool,
                                String destName)
{
    Array<CIMParamValue> in;
    Array<CIMParamValue> out;

    CIMInstance rs = getClassInstance("CIM_ReplicationService");
    CIMInstance pool = getClassInstance("CIM_StoragePool", "ElementName",
                                        destStoragePool);
    CIMInstance lun = getClassInstance("CIM_StorageVolume", "ElementName",
                                       sourceLun);

    in.append(CIMParamValue("ElementName", CIMValue(destName)));
    in.append(CIMParamValue("SyncType", Uint16(SNAPSHOT)));
    in.append(CIMParamValue("Mode", Uint16(ASYNC)));
    in.append(CIMParamValue("SourceElement", lun.getPath()));
    in.append(CIMParamValue("TargetPool", pool.getPath()));

    evalInvoke(out, c.invokeMethod(ns, rs.getPath(),
                                   CIMName("CreateElementReplica"), in, out));
}

void Smis::resizeLun( String name, Uint64 size)
{
    Array<CIMParamValue> in;
    Array<CIMParamValue> out;

    CIMInstance scs = getClassInstance("CIM_StorageConfigurationService");
    CIMInstance lun = getClassInstance("CIM_StorageVolume", "ElementName",
                                       name);

    in.append(CIMParamValue("TheElement", CIMValue(lun.getPath())));
    in.append(CIMParamValue("Size", CIMValue(size)));

    evalInvoke(out, c.invokeMethod(ns, scs.getPath(),
                                   CIMName("CreateOrModifyElementFromStoragePool"), in, out));
}

void Smis::deleteLun( String name )
{
    Array<CIMParamValue> in;
    Array<CIMParamValue> out;

    CIMInstance scs = getClassInstance("CIM_StorageConfigurationService");
    CIMInstance lun = getClassInstance("CIM_StorageVolume", "ElementName", name);

    in.append(CIMParamValue("TheElement", lun.getPath()));
    evalInvoke(out, c.invokeMethod(ns,scs.getPath(),
                                   CIMName("ReturnToStoragePool"), in, out));
}

void Smis::evalInvoke(Array<CIMParamValue> &out, CIMValue value,
                           String jobKey)
{
    Uint32 result = 0;
    value.get(result);
    CIMValue job;

    if( result ) {
        String params;

        for (Uint8 i = 0; i < out.size(); i++) {
            params = params + " ParamName:value(" + out[i].getParameterName() + ":" +
                     out[i].getValue().toString() + ")";

            if( result == INVOKE_ASYNC ) {
                if(out[i].getParameterName() == jobKey) {
                    job = out[i].getValue();
                }
            }
        }

        if( result == INVOKE_ASYNC ) {
            processJob(job);
        } else {
            throw Exception(value.toString().append(params));
        }
    }
}

void Smis::processJob(CIMValue &job)
{
    std::cout << "job started= " << job.toString() << std::endl;

    while( true ) {
        Array<Uint16> values;

        CIMObject status = c.getInstance(ns, job.toString());

        status.getProperty(status.findProperty("OperationalStatus")).getValue().get(values);

        if( values[0] == OK ) {
            //Array fo values may have 1 or 2 values according to mof
            if( values.size() == 2 ) {
                Boolean autodelete = false;
                status.getProperty(status.findProperty("DeleteOnCompletion")).getValue().get(autodelete);

                if( !autodelete ) {
                    //We are done, delete job instance.
                    try {
                        c.deleteInstance(ns, status.getPath());
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
                        << status.getProperty(status.findProperty("PercentComplete")).getValue().toString()
                        << std::endl;

            sleep(1);
        } else {
            throw Exception("Job " + job.toString() + " encountered an error!");
        }
    }
}

Array<String> Smis::getLuns()
{
    return instancePropertyNames("CIM_StorageVolume", "ElementName");
}

Array<CIMInstance>  Smis::storagePools()
{
    return c.enumerateInstances(ns, CIMName("CIM_StoragePool"));
}

Array<String> Smis::instancePropertyNames( String className, String prop )
{
    Array<String> names;

    Array<CIMInstance> instances = c.enumerateInstances(ns, CIMName(className));

    for(Uint32 i = 0; i < instances.size(); ++i) {
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
    if( cs.size() != 1 ) {
        String instances;

        if( cs.size() == 0 ) {
            instances = "none!";
        } else {
            instances.append("\n");
            for(Uint32 i = 0; i < cs.size(); ++i ) {
                instances.append( cs[i].getPath().toString() + String("\n"));
            }
        }

        throw Exception("Expecting one object instance of " + className +
                        "got " + instances);
    }
    return cs[0];
}

CIMInstance Smis::getClassInstance(String className, String propertyName,
                                        String propertyValue )
{
    Array<CIMInstance> cs = c.enumerateInstances(ns, CIMName(className));

    for( Uint32 i = 0; i < cs.size(); ++i ) {
        Uint32 index = cs[i].findProperty(propertyName);
        if ( cs[i].getProperty(index).getValue().toString() == propertyValue ) {
            return cs[i];
        }
    }
    throw Exception("Instance of class name: " + className + " property=" +
                    propertyName + " value= " + propertyValue + " not found.");
}
