/*
 * Copyright (C) 2020 Red Hat, Inc.
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

#define JSMN_PARENT_LINKS
#include "jsmn.h"

Value::Value(void) : t(null_t), s("null") {}

Value::Value(bool v) : t(boolean_t), s((v) ? "true" : "false") {}

Value::Value(uint32_t v) : t(numeric_t), s(to_string(v)) {}

Value::Value(int32_t v) : t(numeric_t), s(to_string(v)) {}

Value::Value(uint64_t v) : t(numeric_t), s(to_string(v)) {}

Value::Value(int64_t v) : t(numeric_t), s(to_string(v)) {}

Value::Value(value_type type, const std::string &v) : t(type), s(v) {}

Value::Value(const std::vector<Value> &v) : t(array_t), array(v) {}

Value::Value(const char *v) {
    if (v) {
        t = string_t;
        s = std::string(v);
    } else {
        t = null_t;
        s = "null";
    }
}

Value::Value(const std::string &v) : t(string_t), s(v) {}

Value::Value(const std::map<std::string, Value> &v) : t(object_t), obj(v) {}

std::string Value::serialize(void) {
    switch (t) {
    case (string_t):
        return "\"" + s + "\"";
    case (object_t): {
        std::string obj_s;

        obj_s += "{";

        std::map<std::string, Value>::iterator iter;
        std::map<std::string, Value>::iterator final_iter = obj.end();
        --final_iter;

        for (iter = obj.begin(); iter != obj.end(); iter++) {

            obj_s += "\"" + iter->first + "\": ";
            obj_s += iter->second.serialize();

            if (iter != final_iter) {
                obj_s += ", ";
            }
        }

        obj_s += "}";
        return obj_s;
    }
    case (array_t): {
        std::string obj_s;
        obj_s += "[";

        for (unsigned int i = 0; i < array.size(); ++i) {
            obj_s += array[i].serialize();
            if ((i + 1) < array.size()) {
                obj_s += ", ";
            }
        }

        obj_s += "]";
        return obj_s;
    }
    default:
        return s;
    }
}

Value::value_type Value::valueType() const { return t; }

Value &Value::operator[](const std::string &key) {
    if (t == object_t) {
        return obj[key];
    }
    throw ValueException("Value not object");
}

Value &Value::operator[](uint32_t i) {
    if (t == array_t) {
        return array[i];
    }
    throw ValueException("Value not array");
}

bool Value::hasKey(const std::string &k) {
    if (t == object_t) {
        std::map<std::string, Value>::iterator iter = obj.find(k);
        if (iter != obj.end() && iter->first == k) {
            return true;
        }
    }
    return false;
}

bool Value::isValidRequest() {
    return (t == Value::object_t && hasKey("method") && hasKey("id") &&
            hasKey("params"));
}

Value Value::getValue(const char *key) {
    if (hasKey(key)) {
        return obj[key];
    }
    return Value();
}

bool Value::asBool() {
    if (t == boolean_t) {
        return (s == "true");
    }
    throw ValueException("Value not boolean");
}

int32_t Value::asInt32_t() {
    if (t == numeric_t) {
        int32_t rc;

        if (sscanf(s.c_str(), "%d", &rc) > 0) {
            return rc;
        }
        throw ValueException("Value not int32");
    }
    throw ValueException("Value not numeric");
}

int64_t Value::asInt64_t() {
    if (t == numeric_t) {
        int64_t rc;
        if (sscanf(s.c_str(), "%lld", (long long int *)&rc) > 0) {
            return rc;
        }
        throw ValueException("Not an integer");
    }
    throw ValueException("Value not numeric");
}

uint32_t Value::asUint32_t() {
    if (t == numeric_t) {
        uint32_t rc;
        if (sscanf(s.c_str(), "%u", &rc) > 0) {
            return rc;
        }
        throw ValueException("Not an integer");
    }
    throw ValueException("Value not numeric");
}

uint64_t Value::asUint64_t() {
    if (t == numeric_t) {
        uint64_t rc;
        if (sscanf(s.c_str(), "%llu", (long long unsigned int *)&rc) > 0) {
            return rc;
        }
        throw ValueException("Not an integer");
    }
    throw ValueException("Value not numeric");
}

std::string Value::asString() {
    if (t == string_t) {
        return s;
    } else if (t == null_t) {
        return std::string();
    }
    throw ValueException("Value not string");
}

const char *Value::asC_str() {
    if (t == string_t) {
        return s.c_str();
    } else if (t == null_t) {
        return NULL;
    }
    throw ValueException("Value not string");
}

std::map<std::string, Value> Value::asObject() {
    if (t == object_t) {
        return obj;
    }
    throw ValueException("Value not object");
}

std::vector<Value> Value::asArray() {
    if (t == array_t) {
        return array;
    }
    throw ValueException("Value not array");
}

std::string Payload::serialize(Value &v) { return v.serialize(); }

int inc_token(int current, int amount, int max) {
    if (current + amount >= max) {
        throw ValueException("Ran out of tokens!");
    }
    return amount;
}

Value jsmn_parse(jsmntok_t *tok, int start_tok, int end_tok, const char *j,
                 int *consumed) {

    int i = start_tok;

    int len = tok[i].end - tok[i].start;
    const char *start = j + tok[i].start;

    if (tok[i].type == JSMN_PRIMITIVE) {
        *consumed = 0;
        if (start[0] == 'n') {
            return Value();
        } else if (start[0] == 't') {
            return Value(true);
        } else if (start[0] == 'f') {
            return Value(false);
        } else {
            std::string value(start, len);
            return Value(Value::numeric_t, value);
        }
    } else if (tok[i].type == JSMN_STRING) {
        std::string value(start, len);
        *consumed = 0;
        return Value(value);
    } else if (tok[i].type == JSMN_ARRAY) {
        int num = tok[i].size;

        std::vector<Value> values;
        for (int e = 0; e < num; ++e) {
            i += inc_token(i, 1, end_tok);
            int used = 0;
            values.push_back(jsmn_parse(tok, i, end_tok, j, &used));
            i += inc_token(i, used, end_tok);
        }
        *consumed = i - start_tok;
        return Value(values);
    } else if (tok[i].type == JSMN_OBJECT) {
        std::map<std::string, Value> values;
        int num = tok[i].size;
        // Key, value
        for (int class_mem = 0; class_mem < num; class_mem++) {
            // Get the key
            i += inc_token(i, 1, end_tok);
            if (tok[i].type != JSMN_STRING) {
                throw ValueException("Expecting JSON object key to be string " +
                                     tok[i].type);
            } else {
                std::string key(j + tok[i].start, tok[i].end - tok[i].start);
                i += inc_token(i, 1, end_tok);
                // Get the value
                int used = 0;
                values[key] = jsmn_parse(tok, i, end_tok, j, &used);
                i += inc_token(i, used, end_tok);
            }
        }
        *consumed = i - start_tok;
        return Value(values);
    }
    throw ValueException("Unreachable path!");
}

Value Payload::deserialize(const std::string &json_str) {
    jsmn_parser p;
    jsmntok_t *tok = NULL;
    int rc = 0;
    size_t num_tokens = std::max(size_t(json_str.length() / 10), size_t(500));

    while (1) {
        jsmn_init(&p);
        tok = (jsmntok_t *)malloc(sizeof(*tok) * num_tokens);
        if (tok) {
            rc = jsmn_parse(&p, json_str.c_str(), json_str.length(), tok,
                            num_tokens);

            if (rc < 0) {
                if (JSMN_ERROR_NOMEM == rc) {
                    free(tok);
                    num_tokens *= 2;
                    continue;
                } else {
                    free(tok);
                    throw ValueException("In-valid json");
                }
            }
            break;

        } else {
            // This is what we did when we were using yajl for an allocation
            // error, not sure this is ideal, but typically you never get
            // back NULL on memory allocation anyway.
            return Value();
        }
    }

    int used = 0;
    Value result = jsmn_parse(tok, 0, rc, json_str.c_str(), &used);
    free(tok);
    return result;
}
