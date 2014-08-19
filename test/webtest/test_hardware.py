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
# License along with this library; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
#
# Author: tasleson
import xlrd
import sys
import os
import yaml


class TestArrays(object):
    col_name = ['COMPANY', 'NAMESPACE', 'SMI_VERSION', 'PRODUCT', 'PRINCIPAL',
                'PASSWORD', 'CIM_VERSION', 'IP', 'INTEROP_NS', 'PROTOCOL',
                'PORT']

    skip_these = ['Brocade', 'Cisco']

    @staticmethod
    def should_skip(company):
        if company in TestArrays.skip_these:
            return True
        return False

    @staticmethod
    def uri_password_get(d):
        uri = 'smispy'
        port = "5988"

        if "https" in d['PROTOCOL'].lower():
            uri += "+ssl"
            port = "5989"

            uri += "://%s@%s:%s/?namespace=%s&no_ssl_verify=yes" % \
                   (d["PRINCIPAL"], d["IP"], port, d["NAMESPACE"])
        else:
            uri += "://%s@%s:%s/?namespace=%s" % (d["PRINCIPAL"], d["IP"],
                                                  port, d["NAMESPACE"])

        return uri, d['PASSWORD']

    def parse_csv_file(self, filename):
        rc = []

        with open(filename) as f:
            lines = f.read().splitlines()

        lines = lines[1:]

        for l in lines:
            elem = {}
            values = l.split(',')
            for i in range(0, len(values)):
                elem[self.col_name[i]] = str(values[i])

            if self.should_skip(elem['COMPANY']):
                continue

            rc.append(elem)
        return rc

    def get_data(self, work_sheet, row):
        rc = {}

        for i in range(0, len(self.col_name)):
            rc[self.col_name[i]] = str(work_sheet.cell_value(row, i))
        return rc

    def parse_xls_file(self, filename):
        rc = []

        wb = xlrd.open_workbook(filename, 'rb')
        ws_name = wb.sheet_names()[0]
        ws = wb.sheet_by_name(ws_name)

        # First line is column headers
        for x in range(1, ws.nrows):
            d = self.get_data(ws, x)

            if self.should_skip(d['COMPANY']):
                continue

            rc.append(d)
        return rc

    def providers(self, filename):
        rc = []

        file_name, extension = os.path.splitext(filename)

        if extension.lower() == '.csv':
            rc = self.parse_csv_file(filename)
        else:
            rc = self.parse_xls_file(filename)
        return rc


if __name__ == "__main__":

    if len(sys.argv) != 2:
        print 'Syntax: %s <file>' % (sys.argv[0])
        print 'File is a cimon xls/xlsx/csv file'
        sys.exit(1)

    sys.stdout.write(yaml.dump(TestArrays().providers(sys.argv[1])))
