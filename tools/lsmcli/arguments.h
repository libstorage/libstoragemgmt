/*
 * Copyright (C) 2011-2012 Red Hat, Inc.
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
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 *
 * Author: tasleson
 */

#ifndef __ARGUMENTS_H
#define __ARGUMENTS_H

#include <string>
#include <vector>
#include <stdint.h>
#include <libstoragemgmt/libstoragemgmt_types.h>

namespace LSM {

#define LIST_TYPE_VOL       "VOLUMES"
#define LIST_TYPE_POOL      "POOLS"
#define LIST_TYPE_INIT      "INITIATORS"

#define INIT_TYPE_WWPN      "WWPN"
#define INIT_TYPE_WWNN      "WWNN"
#define INIT_TYPE_ISCSI     "ISCSI"
#define INIT_TYPE_HN        "HOSTNAME"

#define PROV_TYPE_DEFAULT   "DEFAULT"
#define PROV_TYPE_THIN      "THIN"
#define PROV_TYPE_FULL      "FULL"

#define REP_TYPE_SNAPSHOT   "SNAPSHOT"
#define REP_TYPE_CLONE      "CLONE"
#define REP_TYPE_COPY       "COPY"
#define REP_TYPE_MIRROR     "MIRROR"

#define ACCESS_TYPE_RW      "RW"
#define ACCESS_TYPE_RO      "RO"

template <class Type>
class Arg {
public:
    bool present;
    Type value;

    Arg() : present(false) {
    }

    void set(Type t) {
        present = true;
        value = t;
    }
};

/**
 * Enumerated commands.  Note: make sure they match array index for long_options
 */
typedef enum {
    NONE = -1,
    LIST = 1,
    CREATE_INIT = 2,
    CREATE_VOL = 3,
    DELETE_INIT = 4,
    DELETE_VOL = 5,
    REPLICATE = 6,
    ACCESS_GRANT = 7,
    ACCESS_REVOKE = 8,
    RESIZE_VOLUME = 21,
} commandTypes;

/**
 * Class the encapsulates the command line arguments.
 */
class Arguments {
public:
    Arguments():c(NONE){}

    /**
     * Uri.
     */
    Arg<std::string> uri;

    /**
     * Prompt for password
     */
    Arg<bool> prompt;

    /**
     * Output sizes as human
     */
    Arg<bool> human;

    /**
     * Use terse output
     */
    Arg<std::string> terse;

    /**
     * Generic identifier, needs command for context.
     */
    Arg<std::string> id;

    /**
     * Generic type, needs command for context.
     */
    Arg<std::string> type;

    /**
     * Generic name, needs command for context.
     */
    Arg<std::string> name;

    /**
     * Size specifier, needs command for context.
     */
    Arg<std::string> size;

    /**
     * Pool specifier, needs command for context.
     */
    Arg<std::string> pool;

    /**
     * Provision specifier, needs command for context.
     */
    Arg<std::string> provisioning;

    /**
     * Access specifier, needs command for context.
     */
    Arg<std::string> access;

     /**
     * Connection password, needs command for context.
     */
    Arg<std::string> password;

    /**
     * Volume specifier, needs command for context.
     */
    Arg<std::string> volume;

    /**
     * Actual command to execute
     */
    commandTypes c;

    /**
     * String representation of command.
     */
    std::string commandStr;

    /**
     * Command value.
     */
    std::string commandValue;

    /**
     * Convert string representation to enum.
     * @return lsmInitiatorType value
     */
    lsmInitiatorType initiatorType() const;

    /**
     * Convert string representation to enum.
     * @return lsmProvisionType value
     */
    lsmProvisionType provisionType() const;

    /**
     * Convert string representation to enum.
     * @return lsmReplicationType value.
     */
    lsmReplicationType replicationType() const;

    /**
     * Convert string representation to enum.
     * @return lsmAccessType value.
     */
    lsmAccessType accessType() const;
};

/**
 * Processes the command line arguments.
 * Note: This function will exit() on missing/bad arguments.
 * @param argc      Command line argument count
 * @param argv      Arguments
 * @param args      Class which holds the parsed arguments.
 */
void processCommandLine( int argc, char **argv, Arguments &args );


const uint64_t MiB = 1048576;       //2**20
const uint64_t GiB = 1073741824;    //2**30
const uint64_t TiB = 1099511627776; //2**40

/**
 * Validates and returns the value of size that the user supplied
 * @param s             String in the form [0-9]+[MGT]
 * @param[out] size     Size in bytes.
 * @return 1 if parsed OK, else 0.
 */
int sizeArg(const char* s, uint64_t *size);

/**
 * Returns a string representation of a size
 * @param human     True use human readable size.
 * @param size      Size to represent
 * @return Size represented as a string.
 */
std::string sizeHuman(bool human, uint64_t size);

} //namespace

#endif