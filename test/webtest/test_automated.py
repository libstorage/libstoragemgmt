#!/usr/bin/env python

import test_hardware
import sys
from subprocess import Popen, PIPE
from multiprocessing import Process
import yaml


def call(command):
    """
    Call an executable and return a tuple of exitcode, stdout, stderr
    """
    process = Popen(command, stdout=PIPE, stderr=PIPE)
    out = process.communicate()
    return process.returncode, out[0], out[1]


def run_test(cmdline, output_dir, sys_id, uri, password ):

    exec_array = [cmdline, '-q', '--uri', uri,
                                     '--password', password]

    (ec, out, error) = call(exec_array)

    # Save the output to a temp dir
    fn = "%s/%s" % (output_dir, sys_id.replace(' ', '_'))

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
    if len(sys.argv) != 4:
        print('Syntax: %s <cimon_file> <plugin unit test> <output directory>'
              % (sys.argv[0]))
        sys.exit(1)
    else:
        run = True
        process_list = []
        results = []
        array_cimons = test_hardware.TestArrays().providers(sys.argv[1])

        for system in array_cimons:
            (uri, password) = test_hardware.TestArrays.uri_password_get(system)
            name = system['COMPANY']
            ip = system['IP']
            system_id = "%s-%s" % (name, ip)

            p = Process(target=run_test, args=(sys.argv[2], sys.argv[3],
                                               system_id, uri, password))
            p.start()
            process_list.append(p)

        while len(process_list) > 0:
            for p in process_list:
                p.join(1)
                if not p.is_alive():
                    process_list.remove(p)
                    break