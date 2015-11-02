#!/usr/bin/env python2

# Simple tool to see if we have a SMI-S provider talking on the network and
# if it has any systems we can test.
#
# Can use for scripting as exit value will be:
# 0 if array is online and enumerate systems has some
# 1 if we can talk to provider, but no systems
# 2 Wrong credentials (Wrong username or password)
# 3 Unable to lookup RegisteredName in registered profile (interop support)
# 4 if we cannot talk to provider (network error, connection refused etc.)


from pywbem import Uint16, CIMError
import pywbem
import sys
DEFAULT_NAMESPACE = 'interop'
INTEROP_NAMESPACES = ['interop', 'root/interop', 'root/PG_Interop']


def get_cim_rps(c):
    cim_rps = []
    for n in INTEROP_NAMESPACES:
        try:
            cim_rps = c.EnumerateInstances('CIM_RegisteredProfile',
                                           namespace=n, localonly=False)
        except CIMError as e:
            if e[0] == pywbem.CIM_ERR_NOT_SUPPORTED or \
               e[0] == pywbem.CIM_ERR_INVALID_NAMESPACE or \
               e[0] == pywbem.CIM_ERR_INVALID_CLASS:
                pass
            else:
                raise

        if len(cim_rps):
            for cim_rp in cim_rps:
                if cim_rp['RegisteredOrganization'] == Uint16(11) and \
                        'Array' == cim_rp['RegisteredName']:
                    return cim_rp
    return None


def systems(url, username, password):
    # We will support interop so that we don't have to specify namespace
    rc = 4

    try:
        try:
            conn = pywbem.WBEMConnection(
                url, (username, password), DEFAULT_NAMESPACE,
                no_verification=True)
        except Exception as ei:
            # Some versions of pywbem don't support the parameter
            # 'no_verification'
            if 'no_verification' in str(ei):
                conn = pywbem.WBEMConnection(
                    url, (username, password), DEFAULT_NAMESPACE)
            else:
                raise

        if conn:
            rps = get_cim_rps(conn)

            if rps:
                cim_systems = conn.Associators(
                    rps.path, ResultClass='CIM_ComputerSystem',
                    AssocClass='CIM_ElementConformsToProfile')
                if len(cim_systems):
                    print 'Found %d system(s)' % (len(cim_systems))
                    rc = 0
                else:
                    print 'No systems found!'
                    rc = 1
            else:
                rc = 3
    except Exception as e:
        if 'Unauthorized' in str(e):
            rc = 2
        else:
            print 'Exception: ', str(e)
    return rc


if __name__ == '__main__':
    if len(sys.argv) != 4:
        print "syntax: ./smisping.py <URL> <username> <password>"
        print " eg. smisping.py https://127.0.0.1:5989 <username> <passwd>"
        sys.exit(1)

    sys.exit(systems(*(sys.argv[1:])))
