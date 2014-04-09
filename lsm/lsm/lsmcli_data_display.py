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
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301
# USA
#
# Author: Gris Ge <fge@redhat.com>
import sys
from collections import OrderedDict

from lsm import System, size_bytes_2_size_human


class EnumConvert(object):
    BIT_MAP_STRING_SPLITTER = ','

    @staticmethod
    def _txt_a(txt, append):
        if len(txt):
            return txt + EnumConvert.BIT_MAP_STRING_SPLITTER + append
        else:
            return append

    SYSTEM_STATUS_CONV = {
        System.STATUS_UNKNOWN: 'Unknown',
        System.STATUS_OK: 'OK',
        System.STATUS_ERROR: 'Error',
        System.STATUS_DEGRADED: 'Degraded',
        System.STATUS_PREDICTIVE_FAILURE: 'Predictive failure',
        System.STATUS_STRESSED: 'Stressed',
        System.STATUS_STARTING: 'Starting',
        System.STATUS_STOPPING: 'Stopping',
        System.STATUS_STOPPED: 'Stopped',
        System.STATUS_OTHER: 'Other',
    }

    @staticmethod
    def system_status_to_str(system_status):
        rc = ''
        for cur_sys_status in EnumConvert.SYSTEM_STATUS_CONV.keys():
            if system_status & cur_sys_status:
                rc = EnumConvert._txt_a(rc,
                    EnumConvert.SYSTEM_STATUS_CONV[cur_sys_status])
        if rc == '':
            return EnumConvert.SYSTEM_STATUS_CONV[System.STATUS_UNKNOWN]
        return rc


class DisplayData(object):

    def __init__(self):
        pass

    @staticmethod
    def _out(msg):
        try:
            sys.stdout.write(str(msg))
            sys.stdout.write("\n")
            sys.stdout.flush()
        except IOError:
            sys.exit(1)

    DISPLAY_WAY_COLUMN = 0
    DISPLAY_WAY_SCRIPT = 1

    DISPLAY_WAY_DEFAULT = DISPLAY_WAY_COLUMN

    DEFAULT_SPLITTER = ' | '

    SYSTEM_MAN_HEADER = OrderedDict()
    SYSTEM_MAN_HEADER['id'] = 'ID'
    SYSTEM_MAN_HEADER['name'] = 'Name'
    SYSTEM_MAN_HEADER['status'] = 'Status'
    SYSTEM_MAN_HEADER['status_info'] = 'Status Info'

    SYSTEM_OPT_HEADER = OrderedDict()

    SYSTEM_DSP_HEADER = SYSTEM_MAN_HEADER   # SYSTEM_DSP_HEADER should be
                                            # subset of SYSTEM_MAN_HEADER

    SYSTEM_VALUE_CONV_ENUM = {
        'status': EnumConvert.system_status_to_str,
    }

    SYSTEM_VALUE_CONV_HUMAN = []

    VALUE_CONVERT = {
        System: {
            'mandatory_headers': SYSTEM_MAN_HEADER,
            'display_headers': SYSTEM_DSP_HEADER,
            'optional_headers': SYSTEM_OPT_HEADER,
            'value_conv_enum': SYSTEM_VALUE_CONV_ENUM,
            'value_conv_human': SYSTEM_VALUE_CONV_HUMAN,
        }
    }

    @staticmethod
    def _get_man_pro_value(obj, key, value_conv_enum, value_conv_human,
                           flag_human, flag_enum):
        value = getattr(obj, key)
        if not flag_enum:
            if key in value_conv_enum.keys():
                value = value_conv_enum[key](value)
        if flag_human:
            if key in value_conv_human:
                value = size_bytes_2_size_human(value)
        return value

    @staticmethod
    def _get_opt_pro_value(obj, key, value_conv_enum, value_conv_human,
                           flag_human, flag_enum):
        value = obj.optional_data.get(key)
        if not flag_enum:
            if key in value_conv_enum.keys():
                value = value_conv_enum[key](value)
        if flag_human:
            if key in value_conv_human:
                value = size_bytes_2_size_human(value)
        return value

    @staticmethod
    def _find_max_width(two_d_list, column_index):
        max_width = 1
        for row_index in range(0, len(two_d_list)):
            row_data = two_d_list[row_index]
            if len(row_data[column_index]) > max_width:
                max_width = len(row_data[column_index])
        return max_width

    @staticmethod
    def _data_dict_gen(obj, flag_human, flag_enum, extra_properties=None,
                       flag_dsp_all_data=False):
        data_dict = OrderedDict()
        value_convert = DisplayData.VALUE_CONVERT[type(obj)]
        mandatory_headers = value_convert['mandatory_headers']
        display_headers = value_convert['display_headers']
        optional_headers = value_convert['optional_headers']
        value_conv_enum = value_convert['value_conv_enum']
        value_conv_human = value_convert['value_conv_human']

        for key in display_headers.keys():
            key_str = display_headers[key]
            value = DisplayData._get_man_pro_value(
                obj, key, value_conv_enum, value_conv_human, flag_human,
                flag_enum)
            data_dict[key_str] = value

        if flag_dsp_all_data:
            for key in mandatory_headers.keys():
                if key in display_headers.keys():
                    continue
                key_str = mandatory_headers[key]
                value = DisplayData._get_man_pro_value(
                    obj, key, value_conv_enum, value_conv_human, flag_human,
                    flag_enum)
                data_dict[key_str] = value

            for key in optional_headers.keys():
                key_str = optional_headers[key]
                value = DisplayData._get_opt_pro_value(
                    obj, key, value_conv_enum, value_conv_human, flag_human,
                    flag_enum)
                data_dict[key_str] = value

        elif extra_properties:
            for key in extra_properties:
                if key in mandatory_headers.keys():
                    if key in display_headers.keys():
                        continue
                    key_str = mandatory_headers[key]
                    value = DisplayData._get_man_pro_value(
                        obj, key, value_conv_enum, value_conv_human,
                        flag_human, flag_enum)
                    data_dict[key_str] = value
                elif key in optional_headers.keys():
                    key_str = optional_headers[key]
                    value = DisplayData._get_opt_pro_value(
                        obj, key, value_conv_enum, value_conv_human,
                        flag_human, flag_enum)
                    data_dict[key_str] = value

        return data_dict

    @staticmethod
    def display_data(objs, display_way=None,
                     flag_human=True, flag_enum=False,
                     extra_properties=None,
                     splitter=None,
                     flag_with_header=True,
                     flag_dsp_all_data=False):
        if len(objs) == 0:
            return None

        if display_way is None:
            display_way = DisplayData.DISPLAY_WAY_DEFAULT

        if splitter is None:
            splitter = DisplayData.DEFAULT_SPLITTER

        data_dict_list = []
        if type(objs[0]) in DisplayData.VALUE_CONVERT.keys():
            for obj in objs:
                data_dict = DisplayData._data_dict_gen(
                    obj, flag_human, flag_enum, extra_properties,
                    flag_dsp_all_data)
                data_dict_list.extend([data_dict])
        else:
            return None
        if display_way == DisplayData.DISPLAY_WAY_SCRIPT:
            DisplayData._display_data_script_way(data_dict_list, splitter)
        elif display_way == DisplayData.DISPLAY_WAY_COLUMN:
            DisplayData._display_data_column_way(
                data_dict_list, splitter, flag_with_header)
        return True

    @staticmethod
    def _display_data_script_way(data_dict_list, splitter):
        key_column_width = 1
        value_column_width = 1

        for data_dict in data_dict_list:
            for key_name in data_dict.keys():
                # find the max column width of key
                cur_key_width = len(key_name)
                if cur_key_width > key_column_width:
                    key_column_width = cur_key_width
                # find the max column width of value
                cur_value = data_dict[key_name]
                cur_value_width = 0
                if isinstance(cur_value, list):
                    if len(cur_value) == 0:
                        continue
                    cur_value_width = len(str(cur_value[0]))
                else:
                    cur_value_width = len(str(cur_value))
                if cur_value_width > value_column_width:
                    value_column_width = cur_value_width

        row_format = '%%-%ds%s%%-%ds' % (key_column_width,
                                         splitter,
                                         value_column_width)
        sub_row_format = '%s%s%%-%ds' % (' ' * key_column_width,
                                         splitter,
                                         value_column_width)
        obj_splitter = '%s%s%s' % ('-' * key_column_width,
                                  '-' * len(splitter),
                                  '-' * value_column_width)

        for data_dict in data_dict_list:
            DisplayData._out(obj_splitter)
            for key_name in data_dict:
                value = data_dict[key_name]
                if isinstance(value, list):
                    flag_first_data = True
                    for sub_value in value:
                        if flag_first_data:
                            DisplayData._out(row_format %
                                             (key_name, str(sub_value)))
                            flag_first_data = False
                        else:
                            DisplayData._out(sub_row_format % str(sub_value))
                else:
                    DisplayData._out(row_format % (key_name, str(value)))
        DisplayData._out(obj_splitter)

    @staticmethod
    def _display_data_column_way(data_dict_list, splitter, flag_with_header):
        if len(data_dict_list) == 0:
            return
        two_d_list = []

        item_count = len(data_dict_list[0].keys())

        # determine how many lines we will print
        row_width = 0
        for data_dict in data_dict_list:
            cur_max_wd = 0
            for key_name in data_dict.keys():
                if isinstance(data_dict[key_name], list):
                    cur_row_width = len(data_dict[key_name])
                    if cur_row_width > cur_max_wd:
                        cur_max_wd = cur_row_width
                else:
                    pass
            if cur_max_wd == 0:
                cur_max_wd = 1
            row_width += cur_max_wd

        if flag_with_header:
            # first line for header
            row_width += 1

        # init 2D list
        for raw in range(0, row_width):
            new = []
            for column in range(0, item_count):
                new.append([''])
            two_d_list.append(new)

        # header
        current_row_num = -1
        if flag_with_header:
            two_d_list[0] = data_dict_list[0].keys()
            current_row_num = 0

        # Fill the 2D list with data_dict_list
        for data_dict in data_dict_list:
            current_row_num += 1
            save_row_num = current_row_num
            values = data_dict.values()
            for index in range(0, len(values)):
                value = values[index]
                if isinstance(value, list):
                    for sub_index in range(0, len(value)):
                        tmp_row_num = save_row_num + sub_index
                        two_d_list[tmp_row_num][index] = str(value[sub_index])

                    if save_row_num + len(value) > current_row_num:
                        current_row_num = save_row_num + len(value) - 1
                else:
                    two_d_list[save_row_num][index] = str(value)

        # display two_list
        row_formats = []
        header_splitter = ''
        for column_index in range(0, len(two_d_list[0])):
            max_width = DisplayData._find_max_width(two_d_list, column_index)
            row_formats.extend(['%%-%ds' % max_width])
            header_splitter += '-' * max_width
            if column_index != (len(two_d_list[0]) - 1):
                header_splitter += '-' * len(splitter)

        row_format = splitter.join(row_formats)
        for row_index in range(0, len(two_d_list)):
            DisplayData._out(row_format % tuple(two_d_list[row_index]))
            if row_index == 0 and flag_with_header:
                DisplayData._out(header_splitter)
