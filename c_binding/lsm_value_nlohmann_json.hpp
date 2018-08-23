/*
 * Copyright (C) 2011-2014,2018 Red Hat, Inc.
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
 *         Gris Ge <fge@redhat.com>
 */

Value::Value(void):j(json())
{
}

Value::Value(bool v):j(json(v))
{
}

Value::Value(uint32_t v):j(json(v))
{
}

Value::Value(int32_t v):j(json(v))
{
}

Value::Value(uint64_t v):j(json(v))
{
}

Value::Value(int64_t v):j(json(v))
{
}

Value::Value(const std::vector < Value > &v): array(v)
{
    j = json::array();
    for (Value e : v) {
        j.push_back(e._getJson());
    }
}

Value::Value(const char *v)
{
    if (v == NULL) {
        j = json();
        s = std::string();
    } else {
        j = json(v);
        s = std::string(v);
    }
}

Value::Value(const std::string & v):j(json(v)), s(v)
{
}

Value::Value(const std::map < std::string, Value > &v):obj(v)
{
    j = json::object();
    for (auto const& e : v) {
        j[e.first.c_str()] = e.second._getJson();
    }
}

std::string Value::serialize(void)
{
    return j.dump();
}

Value::value_type Value::valueType() const
{
    switch (j.type()) {
    case json::value_t::null:
        return Value::null_t;
    case json::value_t::boolean:
        return Value::boolean_t;
    case json::value_t::string:
        return Value::string_t;
    case json::value_t::number_integer:
    case json::value_t::number_unsigned:
        return Value::numeric_t;
    case json::value_t::object:
        return Value::object_t;
    case json::value_t::array:
        return Value::array_t;
    default:
        std::string em = std::string("Unknown value_type ") +
            std::string(j.type_name());
        throw ValueException(em.c_str());
    }
}

Value & Value::operator[](const std::string & key) {
    if (j.is_object()) {
        auto rc = obj.find(key);
        if (rc == obj.end()) {
            std::string em = std::string("Specified key '") + key +
                std::string("' not found");
            throw ValueException(em.c_str());
        }
        return rc->second;
    }
    throw ValueException("Value is not object");
}

Value & Value::operator[](uint32_t i) {
    if (j.is_array()) {
        if (i >= j.size()) {
            throw ValueException("Value array: out of index");
        }
        return array[i];
    }
    throw ValueException("Value not array");
}

bool Value::hasKey(const std::string & k)
{
    if (j.is_object()) {
        auto rc = j.find(k);
        if (rc != j.end()) {
            return true;
        }
    }
    return false;
}

bool Value::isValidRequest()
{
    return (j.is_object() && hasKey("method") &&
            hasKey("id") && hasKey("params"));
}

Value Value::getValue(const char *key)
{
    if (hasKey(key)) {
        return obj[key];
    }
    return Value();
}

bool Value::asBool()
{
    if (j.is_boolean()) {
        return j.get<bool>();
    }
    throw ValueException("Value not boolean");
}

int32_t Value::asInt32_t()
{
    if (j.is_number()) {
        if ((j > INT32_MAX) || (j < INT32_MIN)) {
            std::string em = std::string("Value '") + j.dump() +
                std::string("' overflows int32_t");
            throw ValueException(em.c_str());
        }
        return j.get<int32_t>();
    }
    throw ValueException("Value not numeric");
}

int64_t Value::asInt64_t()
{
    if (j.is_number()) {
        if ((j > INT64_MAX) || (j < INT64_MIN)) {
            std::string em = std::string("Value '") + j.dump() +
                std::string("' overflows int64_t");
            throw ValueException(em.c_str());
        }
        return j.get<int64_t>();
    }
    throw ValueException("Value not numeric");
}

uint32_t Value::asUint32_t()
{
    if (j.is_number()) {
        if (j > UINT32_MAX) {
            std::string em = std::string("Value '") + j.dump() +
                std::string("' overflows uint32_t");
            throw ValueException(em.c_str());
        }
        return j.get<uint32_t>();
    }
    throw ValueException("Value not numeric");
}

uint64_t Value::asUint64_t()
{
    if (j.is_number()) {
        // nlohmann::json only support u64 at this moment.
        return j.get<uint64_t>();
    }
    throw ValueException("Value not numeric");
}

std::string Value::asString()
{
    if (j.is_string()) {
        return s;
    } else if (j.is_null()) {
        return std::string();
    }
    throw ValueException("Value not string");
}

const char *Value::asC_str()
{
    if (j.is_string()) {
        return s.c_str();
    } else if (j.is_null()) {
        return NULL;
    }
    throw ValueException("Value not string");
}

std::map < std::string, Value > Value::asObject()
{
    if (j.is_object()) {
        return obj;
    }
    throw ValueException("Value not object");
}

std::vector < Value > Value::asArray()
{
    if (j.is_array()) {
        return array;
    }
    throw ValueException("Value not array");
}

json Value::_getJson() const {
    return j;
}

std::string Payload::serialize(Value & v)
{
    return v.serialize();
}

Value Payload::deserialize(const std::string & json_str)
{
    try {
        json j = json::parse(json_str);
        if (j.is_object()) {
            std::map<std::string, Value> vm;
            for (json::iterator it = j.begin(); it != j.end(); ++it) {
                vm.emplace(it.key(), Payload::deserialize(it.value().dump()));
            }
            return Value(vm);
        } else if (j.is_array()) {
            std::vector<Value> vv;
            for (json::iterator it = j.begin(); it != j.end(); ++it) {
                vv.push_back(Payload::deserialize((*it).dump()));
            }
            return Value(vv);
        } else if (j.is_number()) {
            if (j.dump().c_str()[0] == '-')
                return Value(j.get<int64_t>());
            else
                return Value(j.get<uint64_t>());
        } else if (j.is_string()) {
            return Value(j.get<std::string>());
        } else if (j.is_null()) {
            return Value();
        } else if (j.is_boolean()) {
            return Value(j.get<bool>());
        } else {
            std::string em = std::string("Unknown value_type ") +
                std::string(j.type_name());
            throw ValueException(em.c_str());
        }
    } catch (const json::exception &je) {
        throw ValueException(je.what());
    }
}
