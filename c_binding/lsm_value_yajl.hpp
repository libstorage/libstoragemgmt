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

#ifdef HAVE_YAJL_YAJL_VERSION_H
#include <yajl/yajl_version.h>
#endif

#if defined(HAVE_YAJL_YAJL_VERSION_H) && YAJL_MAJOR > 1
#define LSM_NEW_YAJL
#endif

Value::Value(void):t(null_t)
{
}

Value::Value(bool v):t(boolean_t), s((v) ? "true" : "false")
{
}

Value::Value(uint32_t v):t(numeric_t), s(to_string(v))
{
}

Value::Value(int32_t v):t(numeric_t), s(to_string(v))
{
}

Value::Value(uint64_t v):t(numeric_t), s(to_string(v))
{
}

Value::Value(int64_t v):t(numeric_t), s(to_string(v))
{
}

Value::Value(value_type type, const std::string & v):t(type), s(v)
{
}

Value::Value(const std::vector < Value > &v):t(array_t), array(v)
{
}

Value::Value(const char *v)
{
    if (v) {
        t = string_t;
        s = std::string(v);
    } else {
        t = null_t;
    }
}

Value::Value(const std::string & v):t(string_t), s(v)
{
}

Value::Value(const std::map < std::string, Value > &v):t(object_t), obj(v)
{
}

std::string Value::serialize(void)
{
    const unsigned char *buf;
    std::string json;

#ifdef LSM_NEW_YAJL
    size_t len;
    yajl_gen g = yajl_gen_alloc(NULL);
    if (g) {
        /* These could fail, but we will continue regardless */
        yajl_gen_config(g, yajl_gen_beautify, 1);
        yajl_gen_config(g, yajl_gen_indent_string, "  ");
    }
#else
    unsigned int len;
    yajl_gen_config conf = { 1, "  " };
    yajl_gen g = yajl_gen_alloc(&conf, NULL);
#endif

    if (g) {
        marshal(g);

        if (yajl_gen_status_ok == yajl_gen_get_buf(g, &buf, &len)) {
            json = std::string((const char *) buf);
        }
        yajl_gen_free(g);
    }
    return json;
}

Value::value_type Value::valueType() const
{
    return t;
}

Value & Value::operator[](const std::string & key) {
    if (t == object_t) {
        return obj[key];
    }
    throw ValueException("Value not object");
}

Value & Value::operator[](uint32_t i) {
    if (t == array_t) {
        return array[i];
    }
    throw ValueException("Value not array");
}

bool Value::hasKey(const std::string & k)
{
    if (t == object_t) {
        std::map < std::string, Value >::iterator iter = obj.find(k);
        if (iter != obj.end() && iter->first == k) {
            return true;
        }
    }
    return false;
}

bool Value::isValidRequest()
{
    return (t == Value::object_t && hasKey("method") &&
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
    if (t == boolean_t) {
        return (s == "true");
    }
    throw ValueException("Value not boolean");
}

int32_t Value::asInt32_t()
{
    if (t == numeric_t) {
        int32_t rc;

        if (sscanf(s.c_str(), "%d", &rc) > 0) {
            return rc;
        }
        throw ValueException("Value not int32");
    }
    throw ValueException("Value not numeric");
}

int64_t Value::asInt64_t()
{
    if (t == numeric_t) {
        int64_t rc;
        if (sscanf(s.c_str(), "%lld", (long long int *) &rc) > 0) {
            return rc;
        }
        throw ValueException("Not an integer");
    }
    throw ValueException("Value not numeric");
}

uint32_t Value::asUint32_t()
{
    if (t == numeric_t) {
        uint32_t rc;
        if (sscanf(s.c_str(), "%u", &rc) > 0) {
            return rc;
        }
        throw ValueException("Not an integer");
    }
    throw ValueException("Value not numeric");
}

uint64_t Value::asUint64_t()
{
    if (t == numeric_t) {
        uint64_t rc;
        if (sscanf(s.c_str(), "%llu", (long long unsigned int *) &rc) > 0) {
            return rc;
        }
        throw ValueException("Not an integer");
    }
    throw ValueException("Value not numeric");
}

std::string Value::asString()
{
    if (t == string_t) {
        return s;
    } else if (t == null_t) {
        return std::string();
    }
    throw ValueException("Value not string");
}

const char *Value::asC_str()
{
    if (t == string_t) {
        return s.c_str();
    } else if (t == null_t) {
        return NULL;
    }
    throw ValueException("Value not string");
}

std::map < std::string, Value > Value::asObject()
{
    if (t == object_t) {
        return obj;
    }
    throw ValueException("Value not object");
}

std::vector < Value > Value::asArray()
{
    if (t == array_t) {
        return array;
    }
    throw ValueException("Value not array");
}

void Value::marshal(yajl_gen g)
{
    switch (t) {
    case (null_t):
        {
            if (yajl_gen_status_ok != yajl_gen_null(g)) {
                throw ValueException("yajl_gen_null failure");
            }
            break;
        }
    case (boolean_t):
        {
            if (yajl_gen_status_ok != yajl_gen_bool(g, (s == "true") ? 1 : 0)) {
                throw ValueException("yajl_gen_bool failure");
            }
            break;
        }
    case (string_t):
        {
            if (yajl_gen_status_ok !=
                yajl_gen_string(g, (const unsigned char *) s.c_str(),
                                s.size())) {
                throw ValueException("yajl_gen_string failure");
            }
            break;
        }
    case (numeric_t):
        {
            if (yajl_gen_status_ok != yajl_gen_number(g, s.c_str(), s.size())) {
                throw ValueException("yajl_gen_number failure");
            }
            break;
        }
    case (object_t):
        {

            if (yajl_gen_status_ok != yajl_gen_map_open(g)) {
                throw ValueException("yajl_gen_map_open failure");
            }

            std::map < std::string, Value >::iterator iter;

            for (iter = obj.begin(); iter != obj.end(); iter++) {
                if (yajl_gen_status_ok != yajl_gen_string(g,
                                                          (const unsigned
                                                           char *) iter->first.
                                                          c_str(),
                                                          iter->first.size())) {
                    throw ValueException("yajl_gen_string failure");
                }
                iter->second.marshal(g);
            }

            if (yajl_gen_status_ok != yajl_gen_map_close(g)) {
                throw ValueException("yajl_gen_map_close failure");
            }
            break;
        }
    case (array_t):
        {
            if (yajl_gen_status_ok != yajl_gen_array_open(g)) {
                throw ValueException("yajl_gen_array_open failure");
            }

            for (unsigned int i = 0; i < array.size(); ++i) {
                array[i].marshal(g);
            }

            if (yajl_gen_status_ok != yajl_gen_array_close(g)) {
                throw ValueException("yajl_gen_array_close failure");
            }
            break;
        }
    }
}

class LSM_DLL_LOCAL ParseElement {
  public:

    enum parse_type {
        null, boolean, string, number, begin_map, end_map,
        begin_array, end_array, map_key, unknown
    };

    ParseElement():t(unknown) {
    } ParseElement(parse_type type):t(type) {
    }

  ParseElement(parse_type type, std::string value):t(type), v(value) {
    }
    parse_type t;
    std::string v;

    std::string to_string() {
        return "type " +::to_string(t) + ": value" + v;
    }
};

#ifdef LSM_NEW_YAJL
#define YAJL_SIZE_T size_t
#else
#define YAJL_SIZE_T unsigned int
#endif

static int handle_value(void *ctx, ParseElement::parse_type type)
{
    std::list < ParseElement > *l = (std::list < ParseElement > *)ctx;
    l->push_back(ParseElement(type));
    return 1;
}

static int handle_value(void *ctx, ParseElement::parse_type type,
                        const char *s, size_t len)
{
    std::list < ParseElement > *l = (std::list < ParseElement > *)ctx;
    l->push_back(ParseElement(type, std::string(s, len)));
    return 1;
}

static int handle_null(void *ctx)
{
    return handle_value(ctx, ParseElement::null);
}

static int handle_boolean(void *ctx, int boolean)
{
    std::string b = (boolean) ? "true" : "false";
    return handle_value(ctx, ParseElement::boolean, b.c_str(), b.size());
}

static int handle_number(void *ctx, const char *s, YAJL_SIZE_T len)
{
    return handle_value(ctx, ParseElement::number, s, len);
}

static int handle_string(void *ctx, const unsigned char *stringVal,
                         YAJL_SIZE_T len)
{
    return handle_value(ctx, ParseElement::string,
                        (const char *) stringVal, len);
}

static int handle_map_key(void *ctx, const unsigned char *stringVal,
                          YAJL_SIZE_T len)
{
    return handle_value(ctx, ParseElement::map_key,
                        (const char *) stringVal, len);
}

static int handle_start_map(void *ctx)
{
    return handle_value(ctx, ParseElement::begin_map);
}

static int handle_end_map(void *ctx)
{
    return handle_value(ctx, ParseElement::end_map);
}

static int handle_start_array(void *ctx)
{
    return handle_value(ctx, ParseElement::begin_array);
}

static int handle_end_array(void *ctx)
{
    return handle_value(ctx, ParseElement::end_array);
}

static yajl_callbacks callbacks = {
    handle_null,
    handle_boolean,
    NULL,
    NULL,
    handle_number,
    handle_string,
    handle_start_map,
    handle_map_key,
    handle_end_map,
    handle_start_array,
    handle_end_array
};

static ParseElement get_next(std::list < ParseElement > &l)
{
    ParseElement rc = l.front();
    l.pop_front();
    return rc;
}

static Value ParseElements(std::list < ParseElement > &l);

static Value HandleArray(std::list < ParseElement > &l)
{
    std::vector < Value > values;

    ParseElement cur;

    if (!l.empty()) {
        do {
            cur = l.front();

            if (cur.t != ParseElement::end_array) {
                values.push_back(ParseElements(l));
            } else {
                get_next(l);
            }

        } while (!l.empty() && cur.t != ParseElement::end_array);
    }

    return Value(values);
}

static Value HandleObject(std::list < ParseElement > &l)
{
    std::map < std::string, Value > values;
    ParseElement cur;

    if (!l.empty()) {
        do {
            cur = get_next(l);

            if (cur.t == ParseElement::map_key) {
                values[cur.v] = ParseElements(l);
            } else if (cur.t != ParseElement::end_map) {
                throw ValueException("Unexpected state: " + cur.to_string());
            }
        } while (!l.empty() && cur.t != ParseElement::end_map);
    }
    return Value(values);
}

static Value ParseElements(std::list < ParseElement > &l)
{
    if (!l.empty()) {
        ParseElement cur = get_next(l);

        switch (cur.t) {
        case (ParseElement::null):
        case (ParseElement::boolean):
        case (ParseElement::string):
        case (ParseElement::number):
            {
                return Value((Value::value_type) cur.t, cur.v);
                break;
            }
        case (ParseElement::begin_map):
            {
                return HandleObject(l);
                break;
            }
        case (ParseElement::end_map):
            {
                throw ValueException("Unexpected end_map");
                break;
            }
        case (ParseElement::begin_array):
            {
                return HandleArray(l);
                break;
            }
        case (ParseElement::end_array):
            {
                throw ValueException("Unexpected end_array");
                break;
            }
        case (ParseElement::map_key):
            {
                throw ValueException("Unexpected map_key");
                break;
            }
        case (ParseElement::unknown):
            {
                throw ValueException("Unexpected unknown");
                break;
            }
        }
    }
    return Value();
}

std::string Payload::serialize(Value & v)
{
    return v.serialize();
}

Value Payload::deserialize(const std::string & json)
{
    yajl_handle hand;
    yajl_status stat;
    std::list < ParseElement > l;

#ifdef LSM_NEW_YAJL
    hand = yajl_alloc(&callbacks, NULL, (void *) &l);
    yajl_config(hand, yajl_allow_comments, 1);
#else
    yajl_parser_config cfg = { 1, 1 };
    hand = yajl_alloc(&callbacks, &cfg, NULL, (void *) &l);
#endif

    if (hand) {
        stat =
            yajl_parse(hand, (const unsigned char *) json.c_str(), json.size());
        yajl_free(hand);

        if (stat == yajl_status_ok) {
            return ParseElements(l);
        } else {
            throw ValueException("In-valid json");
        }
    }
    return Value();
}
