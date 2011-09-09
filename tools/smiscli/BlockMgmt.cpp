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

#include "BlockMgmt.h"

BlockMgmt::BlockMgmt(String host, Uint16 port, String smisNameSpace,
                     String userName, String password ):ns(smisNameSpace)
{
    c.connect(host, port, userName, password);
}

BlockMgmt::~BlockMgmt()
{
    c.disconnect();
}

Array<String> BlockMgmt::getStoragePools()
{
    return instancePropertyNames("CIM_StoragePool", "ElementName");
}

void BlockMgmt::createLun( String storagePoolName, String name, Uint64 size)
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

void BlockMgmt::createSnapShot( String sourceLun, String destStoragePool,
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

void BlockMgmt::resizeLun( String name, Uint64 size)
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

void BlockMgmt::deleteLun( String name )
{
    Array<CIMParamValue> in;
    Array<CIMParamValue> out;

    CIMInstance scs = getClassInstance("CIM_StorageConfigurationService");
    CIMInstance lun = getClassInstance("CIM_StorageVolume", "ElementName", name);

    in.append(CIMParamValue("TheElement", lun.getPath()));
    evalInvoke(out, c.invokeMethod(ns,scs.getPath(),
                                   CIMName("ReturnToStoragePool"), in, out));
}

void BlockMgmt::evalInvoke(Array<CIMParamValue> &out, CIMValue value,
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

void BlockMgmt::processJob(CIMValue &job)
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

Array<String> BlockMgmt::getLuns()
{
    return instancePropertyNames("CIM_StorageVolume", "ElementName");
}

Array<CIMInstance>  BlockMgmt::storagePools()
{
    return c.enumerateInstances(ns, CIMName("CIM_StoragePool"));
}

Array<String> BlockMgmt::instancePropertyNames( String className, String prop )
{
    Array<String> names;

    Array<CIMInstance> instances = c.enumerateInstances(ns, CIMName(className));

    for(Uint32 i = 0; i < instances.size(); ++i) {
        Uint32 propIndex = instances[i].findProperty(CIMName(prop));
        names.append(instances[i].getProperty(propIndex).getValue().toString());
    }
    return names;
}

CIMInstance BlockMgmt::getClassInstance(String className)
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

CIMInstance BlockMgmt::getClassInstance(String className, String propertyName,
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
