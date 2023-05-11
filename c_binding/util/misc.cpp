/*
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Copyright (C) 2011-2023 Red Hat, Inc.
 *
 * Author: Tony Asleson <tasleson@redhat.com>
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