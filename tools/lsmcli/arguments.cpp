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

#include "arguments.h"
#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <stdarg.h>
#include <unistd.h>
#include <iomanip>
#include <sstream>
#include <algorithm>


namespace LSM {

#define _(text) text
const char *list_types[] = { LIST_TYPE_VOL, LIST_TYPE_POOL, LIST_TYPE_INIT };
const std::vector<std::string> listTypes(list_types, list_types + 3);

const char *init_types[] = { INIT_TYPE_WWPN, INIT_TYPE_WWNN, INIT_TYPE_ISCSI,
                                INIT_TYPE_HN };
const std::vector<std::string> initTypes(init_types, init_types + 4);

const char *prov_types[] = { PROV_TYPE_DEFAULT, PROV_TYPE_THIN, PROV_TYPE_FULL};
const std::vector<std::string> provTypes(prov_types, prov_types + 3);

const char *rep_types[]  = { REP_TYPE_SNAPSHOT, REP_TYPE_CLONE, REP_TYPE_COPY, REP_TYPE_MIRROR };
const std::vector<std::string> repTypes(rep_types, rep_types + 4);

const char *access_types[] = { ACCESS_TYPE_RW, ACCESS_TYPE_RO };
const std::vector<std::string> accessTypes(access_types, access_types + 2);

lsmInitiatorType Arguments::initiatorType() const
{
    lsmInitiatorType rc = LSM_INITIATOR_OTHER;
    if( type.value ==  INIT_TYPE_WWPN ) {
        rc = LSM_INITIATOR_PORT_WWN;
    } else if( type.value == INIT_TYPE_WWNN ) {
        rc = LSM_INITIATOR_NODE_WWN;
    } else if( type.value == INIT_TYPE_ISCSI ) {
        rc = LSM_INITIATOR_ISCSI;
    } else if( type.value == INIT_TYPE_HN ) {
        rc = LSM_INITIATOR_HOSTNAME;
    }
    return rc;
}

lsmProvisionType Arguments::provisionType() const
{
    lsmProvisionType rc = LSM_PROVISION_UNKNOWN;
    if( provisioning.value == PROV_TYPE_DEFAULT ) {
       rc = LSM_PROVISION_DEFAULT;
    } else if( provisioning.value == PROV_TYPE_THIN ) {
       rc = LSM_PROVISION_THIN;
    } else if( provisioning.value == PROV_TYPE_FULL) {
       rc = LSM_PROVISION_FULL;
    }
    return rc;
}

lsmReplicationType Arguments::replicationType() const
{
    lsmReplicationType rc = LSM_VOLUME_REPLICATE_UNKNOWN;

    if( type.value == REP_TYPE_SNAPSHOT ) {
        rc = LSM_VOLUME_REPLICATE_SNAPSHOT;
    } else if ( type.value == REP_TYPE_CLONE) {
        rc = LSM_VOLUME_REPLICATE_CLONE;
    } else if ( type.value == REP_TYPE_COPY) {
        rc = LSM_VOLUME_REPLICATE_COPY;
    } else if ( type.value == REP_TYPE_MIRROR ) {
        rc = LSM_VOLUME_REPLICATE_MIRROR;
    }
    return rc;
}

lsmAccessType Arguments::accessType() const
{
    lsmAccessType rc = LSM_VOLUME_ACCESS_NONE;
    if( access.value == ACCESS_TYPE_RW ) {
        rc = LSM_VOLUME_ACCESS_READ_WRITE;
    } else {
        rc = LSM_VOLUME_ACCESS_READ_ONLY;
    }
    return rc;
}

void syntaxError(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
    exit(1);
}

void usage()
{
        printf(_("Usage: %s [OPTIONS]... [COMAND]...\n"), "lsmcli");
        fputs(_("\
Manage storage in external storage arrays.\n\
\n\
"), stdout);
        fputs(_("\
Options include:\n\
  -u, --uri                     uniform resource identifier (LSMCLI_URI) \n\
  -P, --prompt                  prompt for password (LSMCLI_PASSWORD)\n\
  -H,                           print sizes in human readable format\n\
                                (e.g., MiB, GiB, TiB)\n\
  -t, --terse=SEP               print output in terse form with \"SEP\" as a \n\
                                record separator\n\
"), stdout);
        fputs(_("\
Commands include:\n\
  -l                            List records of type [VOLUMES|INITIATORS|POOLS]\n\
      --create-initiator=NAME   Create an initiator record requires:\n\
                                --id <initiator id>\n\
                                --type [WWPN|WWNN|ISCSI|HOSTNAME]\n\
"), stdout);
        fputs(_("\
      --create-volume=NAME      requires:\n\
                                --size <volume size> Can use M, G, T\n\
                                --pool <pool id>\n\
                                --provisioning [DEFAULT|THIN|FULL]\n\
      --delete-volume=ID        deletes a volume given its volume id\n\
"), stdout);
        fputs(_("\
  -r, --replicate=VOLUME_ID     replicates a volume, requires:\n\
                                --type [SNAPSHOT|CLONE|COPY|MIRROR]\n\
                                --pool <pool id>\n\
                                --name <human name>\n\
      --access-grant=INIT_ID    grants access to an initiator to a volume\n\
                                requires:\n\
                                --volume <volume id>\n\
                                --access [RO|RW], read-only or read-write\n\
      --access-revoke=INIT_ID   removes access for an initiator to a volume\n\
                                requires:\n\
                                --volume <volume id>\n\
    , --resize-volume=VOLUME_ID resizes a volume, requires:\n\
                                --size <new size>\n\
"), stdout);
        fputs(_("\
  -v, --version                 print version information and exit\n\
  -h, --help                    print help text\n\n\n\
Please report bugs to libstoragemgmt-devel@lists.sourceforge.net\n\
"), stdout);

    exit(1);
}

void version()
{
    printf("lsmcli version %s, built on %s\n\n", "0.01", __DATE__ ", " __TIME__);
    printf("Copyright 2011 Red Hat, Inc.\n");
    exit(0);
}

static std::string join(std::vector<std::string> s, std::string del)
{
    std::string rc = "";

    for( size_t i = 0; i < s.size(); ++i) {
        rc += s[i];
        if( i + 1 < s.size()) {
            rc +=del;
        }
    }

    return rc;
}

std::string validateDomain( std::string option, std::string  value,
                        const std::vector<std::string> &domain)
{
    std::string arg(value);
    std::transform(arg.begin(), arg.end(), arg.begin(), ::toupper);

    for( size_t i = 0; i < domain.size(); ++i ) {
        if( arg == domain[i] ) {
            return arg;
        }
    }
    syntaxError("option (%s) with value (%s) not in set [%s]\n", option.c_str(),
                value.c_str(), join(domain, "|").c_str());

    return "Never get here!";
}

void setCommand( Arguments &args, const std::string &cs, commandTypes c,
                    std::string value)
{
    if( args.c != NONE ) {
        syntaxError(" only one command can be specified at a time, "
                        "previous is (%s)\n", args.commandStr.c_str());
    } else {
        args.commandStr = cs;
        args.c = c;
        args.commandValue = value;
    }
}

static struct option long_options[] = {
    /* These options set a flag. */
    {"uri", required_argument, 0, 'u'},                 //0
    {"list", required_argument, 0, 'l'},                //1
    {"create-initiator", required_argument, 0, 0},      //2
    {"create-volume", required_argument, 0, 0},         //3
    {"delete-initiator", required_argument, 0, 0},      //4 //Future use
    {"delete-volume", required_argument, 0, 0},         //5
    {"replicate", required_argument, 0, 'r'},           //6
    {"access-grant", required_argument, 0, 0},          //7
    {"access-revoke", required_argument, 0, 0},         //8
    {"terse", required_argument, 0, 0},                 //9
    {"help", no_argument, 0, 'h'},                      //10
    {"prompt", no_argument, 0, 'P'},                    //11
    {"version", no_argument, 0, 'v'},                   //12
    {"size", required_argument, 0, 0},                  //13
    {"type", required_argument, 0, 0},                  //14
    {"provisioning", required_argument, 0, 0},          //15
    {"access", required_argument, 0, 0},                //16
    {"volume", required_argument, 0, 0},                //17
    {"id", required_argument, 0, 0},                    //18
    {"pool", required_argument, 0, 0},                  //19
    {"name", required_argument, 0, 0},                  //20
    {"resize-volume", required_argument, 0, 0},         //21
    {0, 0, 0, 0}
};

void parseArguments(int argc, char **argv, Arguments &args) {
    int c;

    while (1) {

        int long_opt_index = 0;

        c = getopt_long(argc, argv, "u:PHr:vht:l:",
            long_options, &long_opt_index);

        /* Detect the end of the options. */
        if (c == -1)
            break;

        switch (c) {
            case 0: {
                /* If this option set a flag, do nothing else now. */
                if (long_options[long_opt_index].flag != 0)
                    break;

                if( long_opt_index <= ACCESS_REVOKE ) {
                    setCommand(args, long_options[long_opt_index].name,
                                (commandTypes)long_opt_index, optarg);
                } else {
                    switch (long_opt_index) {
                        case (13): {
                            uint64_t s = 0;

                            if( ! sizeArg(optarg, &s) ) {
                                syntaxError("--size %s not in the form "
                                            "<num>|<num>[M|G|T]\n", optarg);
                            }
                            args.size.set(optarg);
                            break;
                        }
                        case (14): {
                            args.type.set(optarg);
                            break;
                        }
                        case (15): {
                            args.provisioning.set(optarg);
                            break;
                        }
                        case (16): {
                            args.access.set(optarg);
                            break;
                        }
                        case (17): {
                            args.volume.set(optarg);
                            break;
                        }
                        case (18): {
                            args.id.set(optarg);
                            break;
                        }
                        case (19): {
                            args.pool.set(optarg);
                            break;
                        }
                        case (20): {
                            args.name.set(optarg);
                            break;
                        }
                        case (21): {
                            setCommand(args, long_options[long_opt_index].name,
                                (commandTypes)long_opt_index, optarg);
                            break;
                        }
                    }
                }
                break;
            }
            case('u'): {
                args.uri.set(optarg);
                break;
            }
            case 'l': {
                setCommand(args, "l", LIST, validateDomain("-l", optarg,
                            listTypes));
                break;
            }
            case 'h': {
                usage();
                break;
            }
            case 'H': {
                args.human.set(true);
                break;
            }
            case 'P': {
                args.prompt.set(true);
                break;
            }
            case 't': {
                args.terse.set(optarg);
                break;
            }
            case 'v': {
                version();
                break;
            }
            case 'r': {
                setCommand(args, "r", REPLICATE, optarg);
                break;
            }
            case '?': {
                break;
            }
            default: {
                syntaxError("Code bug, missing handler for option %c\n", c);
            }
        }
    }
}

//Everything parsed, lets see if it logically makes sense.
void requiredArguments( Arguments &args)
{
    if( args.c == NONE ) {
        syntaxError("No command specified. -h for help\n");
    } else {
        switch ( args.c ) {
            case (CREATE_VOL) : {
                if( args.size.present && args.pool.present &&
                    args.provisioning.present ) {

                    //Verify provisioning
                    validateDomain("--provisioning", args.provisioning.value,
                        provTypes);
                } else {
                    syntaxError("--%s requires --size, --pool and "
                                "--provisioning!\n", args.commandStr.c_str());
                }
                break;
            }
            case ( CREATE_INIT ) : {
                if( args.id.present && args.type.present ) {
                    //Make sure type is correct.
                    validateDomain("--type", args.type.value, initTypes);
                } else {
                    syntaxError("--%s requires --id and --type\n",
                            args.commandStr.c_str());
                }
                break;
            }
            case ( REPLICATE ) : {
                if( args.type.present && args.pool.present &&
                    args.name.present) {

                    validateDomain("--type", args.type.value, repTypes);

                } else {
                    syntaxError("-%s requires --type and --pool and --name \n",
                            args.commandStr.c_str());
                }
                break;
            }
            case ( ACCESS_GRANT ) : {
                if( args.volume.present && args.access.present ) {
                    validateDomain("--access", args.access.value, accessTypes);
                } else {
                    syntaxError("--%s requires --volume and --access \n",
                            args.commandStr.c_str());
                }
                break;
            }
            case ( ACCESS_REVOKE ) : {
                if( args.volume.present ) {

                } else {
                    syntaxError("--%s requires --volume\n",
                                    args.commandStr.c_str());
                }
                break;
            }
            case ( RESIZE_VOLUME ) : {
                if( !args.size.present ) {
                    syntaxError("--%s requires --size\n",
                                    args.commandStr.c_str());
                }
                break;
            }
            case ( NONE ):
            case ( DELETE_INIT ):
            case ( DELETE_VOL ):
            case ( LIST ) : {
                break;
            }

        }

        //Check other values.
        if( !args.uri.present ) {
            char *uri_env = getenv("LSMCLI_URI");
            if( uri_env ) {
                args.uri.set(uri_env);
            }
        }

        if( !args.uri.present ) {
            syntaxError("uri missing, please use -u "
                    "or export LSMCLI_URI=<uri>\n");
        }

        //Not prompting for password, then check for ENV.
        if( !args.prompt.present ) {
            char *pw = getenv("LSMCLI_PASSWORD");
            if( pw ) {
                args.password.set(std::string(pw));
            }
        } else {
            args.password.set(std::string(getpass("Password: ")));
        }
    }
}

void processCommandLine( int argc, char **argv, Arguments &args )
{
    parseArguments(argc, argv, args);
    requiredArguments(args);
}

int sizeArg(const char *s, uint64_t *size)
{
    char units = 'M';
    int rc = 0;

    if( s != NULL && size != NULL &&
        (2 == sscanf(s,"%"PRIu64"%c", size, &units)) &&
        (units == 'M' || units == 'G' || units == 'T') ) {

        if( units == 'M') {
            *size *= MiB;
        } else if (units == 'G') {
            *size *= GiB;
        } else if (units == 'T') {
            *size *= TiB;
        }
        rc = 1;
    }
    return rc;
}

std::string sizeHuman(bool human, uint64_t size)
{
    std::string units ="";
    double s = size;

    if( human ) {
        if( size >= TiB ) {
            units=" TiB";
            s /= (double)TiB;

        } else if( size >= GiB ) {
            units=" GiB";
            s /= (double)GiB;

        } else if( size >= MiB ) {
            units=" MiB";
            s /= (double)MiB;
        }
    }

    std::ostringstream o;
    if( human && (size >= MiB) ) {
        o << std::setiosflags(std::ios::fixed)  << std::setprecision(2) << s;
    } else {
        o << size;
    }

    return o.str() + units;
}

} //Namespace
