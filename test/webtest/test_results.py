#!/usr/bin/env python2

# Copyright (C) 2014 Red Hat, Inc.
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

# Takes a directory of output from the script test_automated.py and creates
# a summary html table.

from bs4 import BeautifulSoup as bs
from htmltag import HTML, div, table, thead, tbody, tr, td, span, th, foo, \
    head, body, html, title, style, link, fail_test, notsupported, pass_test, \
    pre, h1

import time

import yaml
import sys
import os


def _header_entry(odd, data):
    cl = ""
    if odd:
        cl = "odd"
    return div(span(data), _class=cl)


def _table_header(rd):
    count = 1

    rc = th()
    for i in rd:
        rc += th(_header_entry(count % 2, i), _class="skew")
        count += 1

    return thead(tr(HTML(str(rc))))


def _body_entry(cl, data):
    if data == 'P':
        data = pass_test(data)
    elif data == 'F':
        data = fail_test(data)
    elif data == 'U':
        data = notsupported(data)
    return td(data, _class=cl)


def _table_body(rd):
    row = ""
    rows = ""

    for r in rd:
        count = 1
        row = _body_entry("operation", r[0])

        for i in r[1:]:
            cl = ""
            if count % 2:
                cl = "odd"
            row += _body_entry(cl, HTML(i))
            count += 1

        rows += tr(HTML(row))

    return tbody(HTML(rows))


def _angled_html_table(column_headers, row_headers, row_data):
    rc = div(table(), _class="angled_table")
    return rc


def has_errors(run_list):
    for i in run_list:
        if not i['rc']:
            return True
    return False


def get_result(r, method):

    if 'RESULTS' in r and 'methods_called' in r['RESULTS']:
        if method in r['RESULTS']['methods_called']:
            if has_errors(r['RESULTS']['methods_called'][method]):
                return 'F'
            else:
                return 'P'
        else:
            return 'U'
    else:
        return '*'


def to_html(results):
    PREAMBLE_FILE = os.getenv('LSM_PREAMBLE_FILE', "")
    preamble = ""
    methods = ['capabilities',
               'systems', 'plugin_info', 'pools', 'job_status', 'job_free',
               'iscsi_chap_auth',
               'volumes', 'volume_create', 'volume_delete', 'volume_resize',
               'volume_replicate', 'volume_replicate_range_block_size',
               'volume_replicate_range', 'volume_enable', 'volume_disable',
               'disks', 'target_ports',
               'volume_mask',
               'volume_unmask',
               'volume_child_dependency',
               'volume_child_dependency_rm',
               'access_groups',
               'access_groups_granted_to_volume',
               'access_group_create',
               'access_group_delete',
               'volumes_accessible_by_access_group',
               'access_groups_granted_to_volume',
               'access_group_initiator_add',
               'access_group_initiator_delete',
               'fs',
               'fs_create',
               'fs_delete',
               'fs_resize',
               'fs_clone',
               'fs_file_clone',
               'fs_snapshots',
               'fs_snapshot_create',
               'fs_snapshot_delete',
               'fs_snapshot_restore',
               'fs_child_dependency',
               'fs_child_dependency_rm',
               'export_auth',
               'exports',
               'export_fs',
               'export_remove'
               ]

    ch = []
    row_data = []

    if os.path.isfile(PREAMBLE_FILE):
        with open(PREAMBLE_FILE, 'r') as pm:
            preamble = pm.read()

    #Build column header
    for r in results:
        ch.append(r['SYSTEM']['ID'])

    # Add overall pass/fail for unit tests
    pass_fail = ['Overall Pass/Fail result']
    for r in results:
        if r['META']['ec'] == '0':
            pass_fail.append('P')
        else:
            pass_fail.append('F')
    row_data.append(pass_fail)

    # Append on link for error log
    error_log = ['Error log (click +)']
    for r in results:
        error_log.append('<a href="%s">+</a>' %
                         ('./' + os.path.basename(r['META']['error_file'])))
    row_data.append(error_log)

    for m in methods:
        row = [m]

        for r in results:
            row.append(get_result(r, m))

        row_data.append(row)

    # Build HTML
    text = '<!DOCTYPE html>'
    text += str(html(
                head(link(rel="stylesheet", type="text/css",
                          href="../../test.css"),
                     title("libStorageMgmt test results"), ),
                body(
                    HTML(h1("%s Results generated @ %s") %
                         (preamble, time.strftime("%c"))),
                    div(table(_table_header(ch), _table_body(row_data)),
                        _class="angled_table"),
                    div(pre(
                        "                  Legend\n"
                        "                  P = Pass (Method called and returned without error)\n"
                        "                  F = Fail (Method call returned an error)\n"
                        "                  U = Unsupported or unable to test due to other errors\n"
                        "                  * = Unable to connect to array or provider totally unsupported\n"
                        "                  + = hyper link to error log\n\n\n",
                        HTML('                  Source code for plug-in for this test run <a href=./smis.py.html>is here. </a>'))))
                ))

    return bs(text).prettify()


def process_result_files(path, ext):
    results = []
    for f in os.listdir(path):
        cur_file = os.path.join(path, f)
        f_name, f_ext = os.path.splitext(f)

        # Look at the files to determine if we were unable to communicate
        # with the array
        if f_ext.lower() == ext.lower():
            with open(cur_file, 'r') as array_data:
                r = yaml.safe_load(array_data.read())

            with open(os.path.join(path, (f_name + ".ec"))) as meta:
                m = yaml.safe_load(meta.read())

            results.append(dict(SYSTEM=dict(ID=f_name), RESULTS=r, META=m))

    #Sort the list by ID and then generate the html
    sorted_results = sorted(results, key=lambda k: k['SYSTEM']['ID'])
    print to_html(sorted_results)


if __name__ == '__main__':
    if len(sys.argv) != 2:
        print 'Syntax: %s <directory>' % (sys.argv[0])
        sys.exit(1)

    process_result_files(sys.argv[1], '.out')
