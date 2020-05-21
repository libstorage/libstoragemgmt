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

#ifndef LSM_PLUGIN_IPC_HPP
#define LSM_PLUGIN_IPC_HPP

#include "libstoragemgmt/libstoragemgmt_common.h"
#include <map>

template <typename K, typename V> class LSM_DLL_LOCAL static_map {
  private:
    std::map<K, V> _m;

  public:
    static_map(const K &key, const V &val) { _m[key] = val; }

    static_map<K, V> &operator()(const K &key, const V &val) {
        _m[key] = val;
        return *this;
    }

    operator std::map<K, V>() { return _m; }
};

#endif
