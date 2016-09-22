# Copyright (C) 2016 Red Hat, Inc.
# This library is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public
# License as published by the Free Software Foundation; either
# version 2.1 of the License, or (at your option) any later version.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public
# License along with this library; If not, see <http://www.gnu.org/licenses/>.

# Try to make lmiwbem look like and toss errors like pywbem to prevent changing
# all kinds of code that depends on pywbem behavior


using_pywbem = False
try:
    import pywbem as wbem
    using_pywbem = True
except ImportError:
    from lsm.plugin.smispy.lmiwbem_wrap import wbem


# Handle differences in pywbem
# older versions don't have pywbem.Error & pywbem.AuthError
if using_pywbem:
    try:
        from pywbem import Error
    except ImportError:
        from pywbem.cim_http import Error
    try:
        from pywbem import AuthError
    except ImportError:
        from pywbem.cim_http import AuthError
else:
    from lsm.plugin.smispy.lmiwbem_wrap import Error
    from lsm.plugin.smispy.lmiwbem_wrap import AuthError
