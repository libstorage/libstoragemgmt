#!/usr/bin/env python2

# Copyright (C) 2014-2016 Red Hat, Inc.
# This library is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public
# License as published by the Free Software Foundation; either
# version 2.1 of the License, or any later version.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public
# License along with this library; If not, see <http://www.gnu.org/licenses/>.
#
# Author: tasleson

# Takes a csv file of hardware array information and runs the plugin test
# on each of them dumping the results to the specified directory

import test_hardware
import sys
from subprocess import Popen, PIPE
from multiprocessing import Process
import yaml
import time
import os
import signal


def call(command):
    """
    Call an executable and return a tuple of exitcode, stdout, stderr
    """
    process = Popen(command, stdout=PIPE, stderr=PIPE)
    out = process.communicate()
    return process.returncode, out[0], out[1]


def run_test(cmdline, output_dir, sys_id, uri, password):

    exec_array = [cmdline, '-q', '--uri', uri, '--password', password]

    (ec, out, error) = call(exec_array)

    # Save the output to a temp dir
    sys_id = sys_id.replace('/', '-')
    sys_id = sys_id.replace(' ', '_')
    fn = "%s/%s" % (output_dir, sys_id)

    with open(fn + ".out", 'w') as so:
        so.write(out)
        so.flush()

    with open(fn + ".error", 'w') as se:
        se.write(error)
        se.flush()

    # We should probably put more information in here
    with open(fn + ".ec", 'w') as error_file:
        error_file.write(yaml.dump(dict(ec=str(ec),
                                        error_file=fn + ".error",
                                        uri=uri)))
        error_file.flush()


if __name__ == '__main__':
    # We will wait up to 90 minutes or whatever the env variable is
    time_limit_seconds = int(os.getenv('LSM_TEST_TMO_SECS', 90 * 60))

    if len(sys.argv) != 4:
        print('Syntax: %s <array_file> <plugin unit test> <output directory>'
              % (sys.argv[0]))
        sys.exit(1)
    else:
        run = True
        process_list = []
        results = []
        arrays_to_test = test_hardware.TestArrays().providers(sys.argv[1])

        for system in arrays_to_test:
            (u, credentials) = test_hardware.TestArrays.uri_password_get(system)
            name = system['COMPANY']
            ip = system['IP']
            system_id = "%s-%s" % (name, ip)

            p = Process(target=run_test, args=(sys.argv[2], sys.argv[3],
                                               system_id, u, credentials))
            p.name = system_id
            p.start()
            process_list.append(p)

        start = time.time()
        print('Test run started at: %s, time limit is %s minutes' %
              (time.strftime("%c"), str(time_limit_seconds / 60.0)))
        sys.stdout.flush()

        while len(process_list) > 0:
            for p in process_list:
                p.join(1)
                if not p.is_alive():
                    print('%s exited with %s at %s (runtime %s seconds)' %
                          (p.name, str(p.exitcode), time.strftime("%c"),
                           str(time.time() - start)))
                    sys.stdout.flush()
                    process_list.remove(p)
                    break

            current = time.time()
            if (current - start) >= time_limit_seconds:
                print('Test taking too long...')
                sys.stdout.flush()
                for p in process_list:
                    print('Terminating process %s, name %s' %
                          (str(p.pid), p.name))
                    sys.stdout.flush()
                    os.kill(p.pid, signal.SIGKILL)
                break

        print('Test run exiting at: %s' % time.strftime("%c"))
        sys.stdout.flush()
