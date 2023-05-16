/*
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Copyright (C) 2011-2023 Red Hat, Inc.
 *
 * Author: Tony Asleson <tasleson@redhat.com>
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
