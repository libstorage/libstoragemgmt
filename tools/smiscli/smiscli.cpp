/*
 * Copyright 2011, Red Hat, Inc.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/*
 * g++ -DPEGASUS_PLATFORM_LINUX_X86_64_GNU -g -Wall -lpegcommon -lpegclient BlockMgmt.cpp smiscli.cpp -o smiscli
 */

#include "BlockMgmt.h"
#include <stdint.h>
#include <getopt.h>
#include <string.h>
#include <stdlib.h>

void dump_strings( Array<String> strings)
{
    for(Uint32 i = 0; i < strings.size(); ++i ) {
        std::cout << strings[i] << std::endl;
    }
}

void usage(String name)
{
    std::cout << name << " is a simple utility to learn smi-s block service "
              "functionality with openpegasus\n\n" << std::endl;
    std::cout << "Syntax: " << name << " host port namespace [create <storage pool> <name> <size>| delete <name> \n"
              "| resize <name> <size> | list [luns|pools]] \n"
              "| map <lun name> <initiator> \n"
              "| snapshot <source lun> <dest pool> <dest name> ]" << std::endl;
    std::cout << "Note: Expects no authentication, if required export DEMO_SMIS_USER and DEMO_SMIS_PASS" << std::endl;
    std::cout << "Built on " << __DATE__  << " @ " << __TIME__ << std::endl;
    std::cout << "\n\nExample:\n" << std::endl;
    std::cout << name << " 192.168.2.25 5988 root/ontap create aggr3 testlun 50000000\n" << std::endl;
    exit(EXIT_FAILURE);
}

struct args {
    String host;
    Uint16 port;
    String ns;
    String username;
    String password;
    String operation;
    Array<String> opArgs;
};

void process_args( int argc, char *argv[] , struct args *cmdline_args)
{
    if( argc < 5 ) {
        usage(argv[0]);
    }

    cmdline_args->host = argv[1];
    cmdline_args->port = (Uint16)atol(argv[2]);
    cmdline_args->ns = argv[3];
    cmdline_args->operation = argv[4];
    cmdline_args->operation.toLower();

    for(int i = 5; i < argc; ++i) {
        cmdline_args->opArgs.append(argv[i]);
    }

    if( getenv("DEMO_SMIS_USER") ) {
        cmdline_args->username = getenv("DEMO_SMIS_USER");

        if( getenv("DEMO_SMIS_PASS") ) {
            cmdline_args->password = getenv("DEMO_SMIS_PASS");
        } else {
            std::cout << "Missing DEMO_SMIS_PASS ?" << std::endl;
            usage(argv[0]);
        }
    }
}

int main(int argc, char *argv[])
{
    struct args arguments;

    try {
        process_args(argc, argv, &arguments);
        BlockMgmt bm(arguments.host, arguments.port, arguments.ns, arguments.username, arguments.password);

        if( arguments.operation == "create" ) {
            if( arguments.opArgs.size() != 3 ) {
                std::cout << "create expects <storage pool> <name> <size>" << std::endl;
                return EXIT_FAILURE;
            }

            bm.createLun(arguments.opArgs[0], arguments.opArgs[1], atoll(arguments.opArgs[2].getCString()));

        } else if ( arguments.operation == "snapshot" ) {
            if( arguments.opArgs.size() != 3 ) {
                std::cout << "snapshot expects <source lun> <dest. storage pool> <dest. name>" << std::endl;
                return EXIT_FAILURE;
            }

            bm.createSnapShot(arguments.opArgs[0], arguments.opArgs[1], arguments.opArgs[2]);
        } else if( arguments.operation == "delete" ) {
            if( arguments.opArgs.size() != 1 ) {
                std::cout << "delete expects <name>" << std::endl;
                return EXIT_FAILURE;
            }

            bm.deleteLun(arguments.opArgs[0]);
        } else if( arguments.operation == "resize" ) {
            if( arguments.opArgs.size() != 2 ) {
                std::cout << "resize expects <name> <size>" << std::endl;
                return EXIT_FAILURE;
            }

            bm.resizeLun(arguments.opArgs[0], atoll(arguments.opArgs[1].getCString()));

        } else if( arguments.operation == "list" ) {
            if( arguments.opArgs.size() != 1 ) {
                std::cout << "list expects one of the following [luns|pools|initiators]" << std::endl;
                return EXIT_FAILURE;
            }

            arguments.opArgs[0].toLower();

            if( arguments.opArgs[0] == "luns" ) {
                dump_strings( bm.getLuns());
            } else if( arguments.opArgs[0] == "pools" ) {
                dump_strings( bm.getStoragePools());
            }

        } else if ( arguments.operation == "map" ) {

            std::cout << "NOT IMPLEMENTED!" << std::endl;
        } else {
            std::cout << "Unsupported operation " << arguments.operation << std::endl;
        }

    } catch (Exception &e) {
        std::cerr << "Error: " << e.getMessage() << std::endl;
        exit(1);
    }
    return 0;
}