#include "lsm_convert.hpp"

static bool isExpectedObject(Value &obj, std::string class_name)
{
    if (obj.valueType() == Value::object_t) {
        std::map<std::string, Value> i = obj.asObject();
        std::map<std::string, Value>::iterator iter = i.find("class");
        if (iter != i.end() && iter->second.asString() == class_name) {
            return true;
        }
    }
    return false;
}

lsmVolume *valueToVolume(Value &vol)
{
    lsmVolume *rc = NULL;

    if (isExpectedObject(vol, "Volume")) {
        std::map<std::string, Value> v = vol.asObject();
        rc = lsmVolumeRecordAlloc(
            v["id"].asString().c_str(),
            v["name"].asString().c_str(),
            v["vpd83"].asString().c_str(),
            v["block_size"].asUint64_t(),
            v["num_of_blocks"].asUint64_t(),
            v["status"].asUint32_t()
            );
    }

    return rc;
}

Value volumeToValue(lsmVolume *vol)
{
    std::map<std::string, Value> v;

    v["class"] = Value("Volume");
    v["id"] = Value(vol->id);
    v["name"] = Value(vol->name);
    v["vpd83"] = Value(vol->vpd83);
    v["block_size"] = Value(vol->blockSize);
    v["num_of_blocks"] = Value(vol->numberOfBlocks);
    v["status"] = Value(vol->status);
    return Value(v);
}

lsmInitiator *valueToInitiator(Value &init)
{
    lsmInitiator *rc = NULL;

    if (isExpectedObject(init, "Initiator")) {
        std::map<std::string, Value> i = init.asObject();
        rc = lsmInitiatorRecordAlloc(
            (lsmInitiatorType) i["type"].asInt32_t(),
            i["id"].asString().c_str()
            );
    }
    return rc;

}

Value initiatorToValue(lsmInitiator *init)
{
    std::map<std::string, Value> i;
    i["class"] = Value("Initiator");
    i["type"] = Value((int32_t) init->idType);
    i["id"] = Value(init->id);
    return Value(i);
}

lsmPool *valueToPool(Value &pool)
{
    lsmPool *rc = NULL;

    if (isExpectedObject(pool, "Pool")) {
        std::map<std::string, Value> i = pool.asObject();
        rc = lsmPoolRecordAlloc(
            i["id"].asString().c_str(),
            i["name"].asString().c_str(),
            i["total_space"].asUint64_t(),
            i["free_space"].asUint64_t()
            );
    }
    return rc;
}

Value poolToValue(lsmPool *pool)
{
    std::map<std::string, Value> p;
    p["class"] = Value("Pool");
    p["id"] = Value(pool->id);
    p["name"] = Value(pool->name);
    p["total_space"] = Value(pool->totalSpace);
    p["free_space"] = Value(pool->freeSpace);
    return Value(p);
}