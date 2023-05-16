#!/usr/bin/env python2
#
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# Copyright (C) 2011-2023 Red Hat, Inc.

# This is a file that simply outputs html to std out which can then
# be redirected to a file to be used on the project website for supported
# array features.

import sys
import os.path


def process_file(cap_file):
    h = open(cap_file, 'r')
    data = h.readlines()
    d = {}

    for l in data:
        (k, v) = l[0:-1].split(":")
        if v == "SUPPORTED":
            d[k] = True
        else:
            d[k] = False
    return os.path.basename(cap_file), d


def create_html(data):
    print('''
<!DOCTYPE HTML PUBLIC "-//W3C//DTD HTML 4.01 Transitional//EN">
<html>
<head>
<meta http-equiv="Content-Type" content="text/html; charset=ISO-8859-1">
<title>Array support</title>
<script type='text/javascript' src='https://www.google.com/jsapi'></script>
<script type='text/javascript'>

google.load('visualization', '1', {packages:['table']});
google.setOnLoadCallback(drawTable);
function drawTable() {
    var data = new google.visualization.DataTable(); ''')

    array_vendors = [x for x, v in data]

    cap_keys = list(data[0][1].keys())
    cap_keys.sort()

    print('''data.addColumn('string', 'Capability/Feature');''')

    #Create vendor columns
    for a in array_vendors:
        print("data.addColumn('boolean', '" + a + "');")

    #Create plugin column
    print("data.addRows([")

    #Output the rows
    for c in cap_keys:
        line = "['%s'" % c

        for v, d in data:
            line += ",%s" % str(d[c]).lower()

        line += "],"
        print(line)
    print("]);")

    print('''
    var table = new google.visualization.Table(document.getElementById('table_div'));
        table.draw(data, {showRowNumber: false});
      }
    </script>
  </head>

  <body>
    <h2>Supported API calls for each plug-in </h2>

    <h3>Notes</h3>
    <ul>
        <li> This data is generated from querying the capabilities of the plug-in.
        <li> Plug-in must pass rudimentary tests for each advertised feature to be included here.
        <li> At this time all plug-ins listed are included in the install packages as they are all open source.
        <li> Columns are sort-able, click header to sort.
    </ul>

    <div id='table_div'></div>
  </body>
</html>
''')


if __name__ == '__main__':
    arrays = []

    if len(sys.argv) < 2:
        print('syntax: web_cap.py <array_cap_1.txt> <array_cap_N.txt>\n\n')
        print('HOWTO: \n' \
            '1. Take the output of lsmcli --capabilities <system>\n' \
            '2. Dump to a file for 1 or more arrays.  \n' \
            '3. Then supply each file name on the command line for this \n' \
            '   utility and the html output will be dumped to STDOUT\n\n')
        print('Note: The file name is used as the column header for output.')
    for f in sys.argv[1:]:
        arrays.append(process_file(f))

    if len(arrays):
        create_html(arrays)
