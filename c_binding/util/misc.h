/*
 * Copyright (C) 2011-2013 Red Hat, Inc.
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
 * License along with this library; If not, see <http://www.gnu.org/licenses/>.
 *
 * Author: tasleson
 */

#ifndef MISC_H
#define MISC_H
#include <map>
#include <stdint.h>
#include <string>

#include <libstoragemgmt/libstoragemgmt_common.h>

namespace LSM {

/**
 * Retrieve the first value that matches key from a query string
 * @param query_string  Query String to parse.
 * @param key           Key to look for
 * @return empty string if key not found, else returns value.
 */
LSM_DLL_LOCAL std::string getValue(std::string query_string, std::string key);

/**
 * Simple class used to store jobs in an associative array.
 * @param t     Type you want to associate with an integer.
 * @return
 */
template <class Type> class LSM_DLL_LOCAL JobControl {

  public:
    /**
     * Ctor
     */
    JobControl() : ticket(0) {}

    /**
     * Adds an item to the collection.
     * @param t         Type to add.
     * @return          Index it was inserted at.
     */
    int insert(Type t) {
        jobs[++ticket] = t;
        return ticket;
    }

    /**
     * Returns item at index num.
     * @param num       Key
     * @return Item
     */
    Type get(uint32_t num) { return getInt(num)->second; }

    /**
     * Checks to see if an item is present.
     * @param num       Key
     * @return  true if key exists, else false.
     */
    bool present(uint32_t num) { return (getInt(num) != jobs.end()); }

    /**
     * Removes an item.
     * @param num       Key
     */
    void remove(uint32_t num) { jobs.erase(getInt(num)); }

  private:
    typename std::map<uint32_t, Type>::iterator getInt(uint32_t num) {
        typename std::map<uint32_t, Type>::iterator i = jobs.find(num);
        return i;
    }
    uint32_t ticket;
    std::map<uint32_t, Type> jobs;
};

} // namespace LSM

#endif /* MISC_H */
