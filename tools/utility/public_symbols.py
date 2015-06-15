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

# This is a file that simply dumps the publicly available symbols for the
# python library.  Anything that starts with a '_' is considered private.

import inspect
import lsm

nesting = 0
visited_module = {}
visited_class = {'ABCMeta': True}
visited_function = {}


def o_p(msg):
    print("%s%s" % (' ' * nesting, msg))


def handle_data(c, a):
    d = getattr(c, a[0])
    o_p("DATA: %s %s %s" % (a[0], type(d), a[3]))


def handle_property(c, a):
    o_p("PROPERTY: %s" % (a[0]))


def handle_method(c, a):
    o_p("%s: %s %s" % (a[1].upper(), a[0],
                       inspect.getargspec(getattr(c, a[0]))))


def handle_other(c, a):
    o_p("OTHER: %s: %s" % (a[0], a[1]))


f_map = {'data': handle_data,
         'property': handle_property,
         'method': handle_method,
         'static method': handle_method}


def h_class(c):

    global nesting

    nesting += 4

    info = [x for x in inspect.classify_class_attrs(c)
            if not x[0].startswith('_')]

    s = sorted(info, key=lambda x: (x[1], x[0]))

    for i in s:
        if i[1] in f_map:
            f_map[i[1]](c, i)
        else:
            handle_other(c, i)

    nesting -= 4


def h_module(mod):
    global nesting
    global visited_module
    global visited_class
    global visited_function

    if str(mod.__name__) in visited_module:
        return
    else:
        visited_module[str(mod.__name__)] = True

    class_list = [x for x in inspect.getmembers(mod, inspect.isclass)
                  if not x[0].startswith('_')]

    function_list = [x for x in inspect.getmembers(mod, inspect.isfunction)
                     if not x[0].startswith('_')]

    module_list = [x for x in inspect.getmembers(mod, inspect.ismodule)
                   if not x[0].startswith('_')]

    print '%sModule: %s' % (' ' * nesting, str(mod.__name__))
    nesting += 4
    for m in function_list:
        if not m[0] in visited_function:
            visited_function[m[0]] = True
            print '%sf: %s' % (' ' * nesting, m[0])

    nesting -= 4

    for c in class_list:
        if not c[0] in visited_class:
            visited_class[c[0]] = True
            nesting += 4
            print '%sClass: %s' % (' ' * nesting, c[0])
            h_class(c[1])
            nesting -= 4

    for m in module_list:
        h_module(m[1])

if __name__ == '__main__':
    h_module(lsm)
