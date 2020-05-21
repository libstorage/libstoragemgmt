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

#include "misc.h"
#include "qparams.h"
#include <string.h>
#include <string>

namespace LSM {

std::string getValue(std::string uri, std::string key) {
    int i;
    std::string rc;
    struct qparam_set *queryParamSet = NULL;

    queryParamSet = qparam_query_parse(uri.c_str());
    if (NULL == queryParamSet) {
        return rc;
    } else {
        for (i = 0; i < queryParamSet->n; ++i) {
            if (strcmp(queryParamSet->p[i].name, key.c_str()) == 0) {
                rc = queryParamSet->p[i].value;
                break;
            }
        }
        free_qparam_set(queryParamSet);
    }
    return rc;
}

} // namespace LSM