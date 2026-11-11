#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
#
# Copyright (C) 2026 Red Hat, Inc.
#
# Specification-based tests for the lsmcli bash completion.  Every case is a
# command line, where the cursor is, and what the environment is, in, and what
# the completion offered out, as it is seen by a real interactive bash on a pty:
# the line is typed, TAB is pressed, and a wrapper registered as the completion
# function records COMPREPLY of the real one.  Nothing here knows how the
# completion is done, so it holds for whatever does it.
#
# A stub 'lsmcli' is first in PATH so the value look-ups return known values
# and record how they were asked.  Every list type has an entry containing
# command substitution, which must be offered verbatim and never evaluated.
# For the cases about what must not be looked up, "ran" says if the stub was
# asked at all, and "no_argv" what it must not have been asked with.

import concurrent.futures
import fcntl
import os
import select
import shutil
import struct
import subprocess
import sys
import tempfile
import termios
import time

SKIP = 77
TIMEOUT = 20
WORKERS = 8

# A password that is put everywhere lsmcli can be given one: in LSMCLI_PASSWORD,
# in the URI of LSMCLI_URI, in ~/.lsmcli and in some URIs typed.  It must never
# be in what is offered, nor in what is written to stderr.
SECRET = "S3cr3tMarker"

# Marks where the cursor is in a line, else it is at the end.
CUR = "<CUR>"

# The completion's own reply for the stub's payload, escaped so the shell reads
# it back as the one word it was, and for the other awkward names.
PAYLOAD = "$(touch ${LSM_TEST_MARKER})"
PAYLOAD_ESC = r"\$\(touch\ \$\{LSM_TEST_MARKER\}\)"
BACKSLASH_ESC = r"back\\slash"
QUOTE_ESC = r"a\"q"

SYS_ALL = "|".join([
    PAYLOAD_ESC, "sys-one", "sys-two", r"sys\ three", BACKSLASH_ESC, QUOTE_ESC,
    "ax", "hash#tag"
])

# What the stub has for the initiators, none of it filtered.
INIT_ALL = PAYLOAD_ESC + "|init-two"

STUB = r'''#!/usr/bin/env bash
# Stub lsmcli returning terse output, separated by what -t asks for, for the
# requested --type.  For the completion, which lsmcli does itself, it is
# lsmcli's own code, asking this stub for the values.
if [[ ${1-} == __complete || ${3-} == __complete || ${5-} == __complete ]] ; then
    exec python3 "${LSM_TEST_DRIVER}" "$@"
fi
touch "${LSM_TEST_CALLED}"
# Noise on stderr, which completion must keep off the user's command line.
echo "lsmcli stub noise" >&2
{ printf '[%s]' "$@" ; echo ; } >> "${LSM_TEST_ARGV}"

list_type=''
sep=''
while [[ $# -gt 0 ]] ; do
    case "$1" in
        # A flag may come last, with no value, and shift 2 would then fail
        # and leave this looping for ever.
        --type) list_type="${2-}" ; shift ; [[ $# -gt 0 ]] && shift ;;
        -t?*) sep="${1#-t}" ; shift ;;
        *) shift ;;
    esac
done

# Without a separator there is no terse output to give.
[[ -n ${sep} ]] || exit 1

# Print one record, its fields joined by the separator.
function row() { local IFS="${sep}" ; echo "$*" ; }

payload='$(touch ${LSM_TEST_MARKER})'

case "${list_type}" in
    # 'sys three' proves a value containing a space survives as one entry,
    # the next two a backslash and a double quote, and 'hash#tag' that '#',
    # which once was the separator, is no longer taken as one.
    systems)
        row "${payload}" "sys desc"
        for name in sys-one sys-two "sys three" 'back\slash' 'a"q' ax \
                    $'t\tb' 'hash#tag' ; do
            row "${name}" desc
        done
        ;;
    pools) row "${payload}" "pool desc" ; row pool-one desc ;;
    volumes) row "${payload}" "vol desc" ; row vol-one desc ;;
    disks)
        row "${payload}" "disk desc"
        row disk-one desc
        if [[ -n ${LSM_TEST_AWKWARD-} ]] ; then
            # Names that need escaping, and one with a tab, which is not offered.
            for name in "zz it's" 'zz$x' 'zz`x`' 'zz\\b' $'zz\ttab' 'zz*' \
                        'zz&b' 'zz!x' 'zz a"q' 'zza!b!' ; do
                row "${name}" desc
            done
        fi
        ;;
    exports) row "${payload}" "export desc" ; row export-one desc ;;
    target_ports) row "${payload}" "tgt desc" ; row tgt-one desc ;;
    fs) row "${payload}" "fs desc" ; row fs-one desc ;;
    # The real lsmcli prints every type on the one line.
    nfs_client_auth) row "${payload}" standard krb5 ;;
    access_groups)
        # Third column is the initiator, which is what --init completes with.
        row ag-one "ag name" "${payload}" ISCSI
        row ag-two "ag name" init-two ISCSI
        if [[ -n ${LSM_TEST_COLON-} ]] ; then
            # An iSCSI name, which bash breaks the word of at every :.
            # Only some tests ask for it, as no other test wants it among
            # what is offered.
            row ag-iqn "ag name" 'iqn.1994-05.com.redhat:abc123' ISCSI
        fi
        ;;
esac
'''

# Run in the shell under test.  The completion function is wrapped to record
# what it offers and what it wrote to stderr, and to compare the state of the
# shell before and after, which must not change.
RC = r'''
PS1='P> '
PS2='> '
bind 'set enable-bracketed-paste off'
bind 'set bell-style none'
# A glob that matches nothing is an error with failglob, so a completion that
# lets a typed word through to the shell fails loudly.
shopt -s failglob
@PRE@
source '@SCRIPT@'

# What the shell looks like: its variables and functions by name, the options
# and the values of the ones the cases set to see if they are disturbed.
_cap_fp()
{
    compgen -v | grep -v '^_cap_\|^COMPREPLY$' | sort
    compgen -A function | sort
    printf '%s\n' "$-"
    printf '%q\n' "${IFS}"
    shopt -p
    printf '%q\n' "${i-} ${word-} ${count-} ${sep-} ${plain-} ${cur-} ${prev-}"
    declare -F join
}
_cap()
{
    local _cap_a _cap_b
    printf '[%s]' "${COMP_WORDS[@]}" > '@DIR@/words'
    printf ' %s %s\n' "${COMP_CWORD}" "${COMP_POINT}" >> '@DIR@/words'
    _cap_a=$(_cap_fp) || :
    _lsm "$@" 2> '@DIR@/err'
    _cap_b=$(_cap_fp) || :
    if (( ${#COMPREPLY[@]} )) ; then
        printf '%s\0' "${COMPREPLY[@]}" > '@DIR@/out.tmp'
    else
        : > '@DIR@/out.tmp'
    fi
    if [[ ${_cap_a} != "${_cap_b}" ]] ; then
        diff <(printf '%s\n' "${_cap_a}") <(printf '%s\n' "${_cap_b}") \
            > '@DIR@/state' || :
    fi
    COMPREPLY=()
    mv '@DIR@/out.tmp' '@DIR@/out'
}
complete -F _cap lsmcli
@POST@
'''


class Case:
    """One command line, its environment, and what is expected of it."""

    def __init__(self,
                 line,
                 expect="",
                 ran=None,
                 no_argv=(),
                 env=None,
                 pre=(),
                 post=(),
                 trap=None):
        self.line = line
        self.expect = expect.split("|") if expect else []
        self.ran = ran
        self.no_argv = list(no_argv)
        self.env = env or {}
        self.pre = list(pre)
        self.post = list(post)
        # Was the program that was typed run, for the case that has one that
        # leaves a mark when it is.
        self.trap = trap


cases = []


def T(line, expect="", **kw):
    """Add a case for a line, the cursor at the end unless <CUR> says."""
    cases.append(Case(line, expect, **kw))


VOL = PAYLOAD_ESC + "|vol-one"

# ---------------------------------------------------------------------------
# Values looked up, matched against what is typed.
# ---------------------------------------------------------------------------
T("lsmcli capabilities --sys ", SYS_ALL, ran=True, no_argv=["[-u]", "[-P]"])
T("lsmcli capabilities --sys sys-o", "sys-one")
T("lsmcli capabilities --sys nomatch", "")
T("lsmcli volume-create --pool ", PAYLOAD_ESC + "|pool-one")
T("lsmcli volume-delete --vol ", VOL)
T("lsmcli volume-replicate-range --src-vol ", VOL)
T("lsmcli volume-replicate-range --dst-vol ", VOL)
T("lsmcli volume-raid-create --disk ", PAYLOAD_ESC + "|disk-one")
T("lsmcli volume-mask --ag ", "ag-one|ag-two")
T("lsmcli access-group-create --init ", PAYLOAD_ESC + "|init-two")
T("lsmcli list --nfs-export ", PAYLOAD_ESC + "|export-one")
T("lsmcli list --tgt ", PAYLOAD_ESC + "|tgt-one")
T("lsmcli fs-delete --fs ", PAYLOAD_ESC + "|fs-one")
T("lsmcli fs-clone --src-fs ", PAYLOAD_ESC + "|fs-one")
T("lsmcli fs-unexport --export ", PAYLOAD_ESC + "|export-one")
T("lsmcli fs-export --auth-type ", PAYLOAD_ESC + "|standard|krb5")
T("lsmcli fs-export --auth-type k", "krb5")
# A value is matched literally, no globbing and no expansion.
T("lsmcli capabilities --sys $(", "", ran=False)
T("lsmcli capabilities --sys *", "")
# Nor does a word typed get to glob: ./a1 and ./a2 exist.
T("lsmcli capabilities --sys a*", "")
T("lsmcli volume-create --*", "")
T("lsmcli volume-create --n*", "")
T("lsmcli --type *", "")
# Each kind of completion, with words that match no file at all: under
# failglob an unquoted word is an error, and that is not allowed either, and
# with ones that match a file, ./ac, which must not be taken for the word.
for line in [
        "lsmcli zz*", "lsmcli --zz*", "lsmcli -zz*", "lsmcli list --type zz*",
        "lsmcli volume-create --size zz*",
        "lsmcli volume-replicate --rep-type zz*",
        "lsmcli list --type snapshots zz*", "lsmcli a*",
        "lsmcli list --type a*"
]:
    T(line, "")

# Names containing a space, completed from the forms the shell hands over for
# the word being typed: plain, and backslash escaped, which are read as the
# shell reads them.  What is offered is escaped, so the shell reads it back as
# the name.
for cur, expect in [("sys", r"sys-one|sys-two|sys\ three"),
                    ("sys\\ ", r"sys\ three"), ("sys\\ t", r"sys\ three"),
                    ("sys\\ x", ""), ("sys-o", "sys-one"),
                    (r"back\\sl", BACKSLASH_ESC), (r"back\sl", ""),
                    (r"back\\\\sl", ""), (r"b\a\c\k\\sl", BACKSLASH_ESC),
                    ("t", ""), ("t\\", ""), ("sys\\\\ ", "")]:
    T("lsmcli capabilities --sys " + cur, expect)

# Inside any quote, or with a $ or a backtick anywhere, no value is offered, nor
# is the stub asked: what is offered is for a word without them, and the quote
# that is open is not known to be what the shell will read.
for cur in [
        '"sys ', "'sys ", '"sys-', "'sys-", "$'sys-", '"sys three"',
        "'sys th'ree", "'a'x", '"a"x', "a'x'", 'a"x"', '"back\\\\sl',
        "'back\\sl", r"a\"q", '"a\\"q', '"a\\"q"', "'a\"q", "sys-\"o\"",
        r"sys\ 'th", r"sys\ $'th", r"'sys'\ th", "$'t\\t", "$'t\\x09", "$sys",
        r"sys\$", "sys$", "$(", "${O}", "`x`", r"sys\`", '"', "'", "$'",
        '$"sys', "sys\"", "sys'"
]:
    T("lsmcli capabilities --sys " + cur, "", ran=False)

# What is not a value is still completed in a quote, as that needs no escaping:
# the commands, options and choices have nothing in them that does.
for line, expect in [
    ('lsmcli "volume-d',
     "volume-delete|volume-dependants|volume-dependants-rm|"
     "volume-disable"), ("lsmcli 'volume-del", "volume-delete"),
    ("lsmcli list --type 'vol", "volumes"),
    ('lsmcli list --type "snap', "snapshots"),
    ('lsmcli vc --provisioning "T', "THIN"), ("lsmcli vd '--v", "--vol"),
    ('lsmcli vd "--vol" --f', ""), (r"lsmcli vd \-\-v", "--vol"),
    ('lsmcli vd "--vol" "', "")
]:
    T(line, expect)

# What is offered for a word that is unquoted, with each awkward name escaped
# so the shell reads it back as the name: the expected text is as the
# completion writes it, and the check that it reads back as the name is the
# shell's, see readback_problems().  Every name starts with zz, so that is
# what is typed.  One with a control character in it, the stub has one, could
# not be put on the line as it is, so is not offered.
AWKWARD = [
    "zz it's", "zz$x", "zz`x`", "zz\\\\b", "zz*", "zz&b", "zz!x", 'zz a"q',
    "zza!b!"
]
EXPECT_AWKWARD = {
    "zz it's": r"zz\ it\'s",
    "zz$x": r"zz\$x",
    "zz`x`": r"zz\`x\`",
    "zz\\\\b": r"zz\\\\b",
    "zz*": r"zz\*",
    "zz&b": r"zz\&b",
    "zz!x": r"zz\!x",
    'zz a"q': r"zz\ a\"q",
    "zza!b!": r"zza\!b\!"
}
T("lsmcli volume-raid-create --disk zz",
  "|".join(EXPECT_AWKWARD[n] for n in AWKWARD),
  env={"LSM_TEST_AWKWARD": "1"})
for opener in ['"', "'", "$'"]:
    T("lsmcli volume-raid-create --disk %szz" % opener,
      "",
      ran=False,
      env={"LSM_TEST_AWKWARD": "1"})

# ---------------------------------------------------------------------------
# An iSCSI name or a WWPN has a : in it, which bash breaks the word of, so what
# is being completed is more than one word.  The whole of it is what values are
# matched with, and what is offered is what follows the last :, which is all
# readline replaces.  Their values are in the stub only with LSM_TEST_COLON.
# ---------------------------------------------------------------------------
COLON = {"LSM_TEST_COLON": "1"}
IQN = "iqn.1994-05.com.redhat"
T("lsmcli access-group-add --init " + IQN,
  "iqn.1994-05.com.redhat:abc123",
  env=COLON)
T("lsmcli access-group-add --init %s:" % IQN, "abc123", env=COLON)
T("lsmcli access-group-add --init %s:abc1" % IQN, "abc123", env=COLON)
T("lsmcli access-group-add --init %s:zz" % IQN, "", env=COLON)
T(r"lsmcli access-group-add --init %s\:ab" % IQN,
  "iqn.1994-05.com.redhat:abc123",
  env=COLON)
# Quoted, the word is not one that values are offered for.
T("lsmcli access-group-add --init '%s'" % IQN, "", ran=False, env=COLON)
T('lsmcli access-group-add --init "%s:' % IQN, "", ran=False, env=COLON)

# ---------------------------------------------------------------------------
# The sub command is the first word that is not a global option or the value
# of one, wherever that is, and the first word, lsmcli, is never looked at.
# ---------------------------------------------------------------------------
VD = "--vol"
for line, expect in [
    ("lsmcli -b volume-create --", "--name|--size|--pool|--provisioning"),
    ("lsmcli -H -b -s volume-create --n", "--name"),
    ("lsmcli -u sim:// volume-delete ", VD),
    ("lsmcli -u sim://x:80/y volume-delete ", VD),
    ("lsmcli --uri=sim://h volume-delete ", VD),
    ("lsmcli -usim:// volume-delete ", VD),
    ("lsmcli -t , volume-delete ", VD),
    ("lsmcli -t: volume-delete ", VD),
    ("lsmcli --terse=, volume-delete ", VD),
    ("lsmcli -w 100 -H volume-delete ", VD),
    ("lsmcli --wait=100 volume-delete ", VD),
    ("lsmcli --fork_plugin /x/y -u sim:// volume-delete ", VD),
    ("lsmcli volume-delete -u sim:// ", VD),
    ("lsmcli -v access-group-c", "access-group-create"),
    ("lsmcli -u sim:// access-group-c", "access-group-create"),
    ("lsmcli -H -b zz", ""),
    ("/usr/bin/lsmcli volume-d",
     "volume-delete|volume-dependants|volume-dependants-rm|volume-disable"),
    ("./lsmcli volume-delete ", VD),
    ("/usr/bin/lsmcli -u sim:// vd ", VD),
    ("lsmcli -H --ver", "--version"),
        # Nothing to offer where a value is being typed.
    ("lsmcli -u ", ""),
    ("lsmcli -u sim", ""),
    ("lsmcli -u sim:", ""),
    ("lsmcli -u sim://", ""),
    ("lsmcli -u sim://x:", ""),
    ("lsmcli --uri ", ""),
    ("lsmcli --uri=", ""),
    ("lsmcli --uri=sim://", ""),
    ("lsmcli -t ", ""),
    ("lsmcli --terse ", ""),
    ("lsmcli -w ", ""),
    ("lsmcli --wait ", ""),
    ("lsmcli --fork_plugin ", ""),
    ("lsmcli -H -u ", ""),
    ("lsmcli volume-delete -u ", ""),
    ("lsmcli volume-delete --vol x -t ", ""),
        # The list aliases stand for 'list --type', so --type is not offered.
    ("lsmcli ls ", "--sys|--pool|--vol|--disk|--ag|--fs|--nfs-export|--tgt"),
    ("lsmcli lp --vol x ", "--sys|--pool|--disk|--ag|--fs|--nfs-export|--tgt"),
    ("lsmcli -H lv ",
     "--sys|--pool|--vol|--disk|--ag|--fs|--nfs-export|--tgt"),
    ("lsmcli list ",
     "--type|--sys|--pool|--vol|--disk|--ag|--fs|--nfs-export|--tgt"),
    ("lsmcli list --type volumes ",
     "--sys|--pool|--vol|--disk|--ag|--fs|--nfs-export|--tgt"),
        # SNAPSHOTS needs a file system, whatever the case and wherever the list.
    ("lsmcli list --type snapshots ", "--fs"),
    ("lsmcli list --type SNAPSHOTS ", "--fs"),
    ("lsmcli list --type Snapshots --f", "--fs"),
    ("lsmcli -H list --type snapshots ", "--fs"),
    ("lsmcli -u sim:// -b list --type SNAPSHOTS ", "--fs"),
    ("lsmcli x --type snapshots ", ""),
    ("lsmcli list snapshots --ty", "--type"),
        # A word of the sub command being typed in full is still wanted.
    ("lsmcli access-group-add --ag", "--ag"),
        # Case matters, whatever the shell's nocasematch is.
    ("lsmcli VOLUME-DELETE ", ""),
    ("lsmcli x --SYS ", "")
]:
    T(line, expect)

# Every sub command, and alias, with what it offers when nothing is on the line
# but the command.  --force is never offered, though it is accepted.
OPTION_TABLE = [
    ("list", "--type --sys --pool --vol --disk --ag --fs --nfs-export --tgt"),
    ("ls lp lv ld la lf lt lb",
     "--sys --pool --vol --disk --ag --fs --nfs-export --tgt"),
    ("job-status", "--job"),
    ("capabilities c volume-raid-create-cap vrcc volume-replicate-range-block-size",
     "--sys"),
    ("plugin-info p local-disk-list ldl local-led-slot-list", ""),
    ("volume-create vc", "--name --size --pool --provisioning"),
    ("volume-raid-create vrc", "--name --disk --raid-type --strip-size"),
    ("volume-delete vd", "--vol"),
    ("volume-resize vr", "--vol --size"),
    ("volume-replicate", "--vol --name --rep-type --pool"),
    ("volume-replicate-range",
     "--src-vol --dst-vol --rep-type --src-start --dst-start --count"),
    ("volume-dependants volume-dependants-rm volume-access-group volume-enable volume-disable ve vi volume-raid-info vri volume-cache-info vci volume-ident-led-on vilon volume-ident-led-off viloff",
     "--vol"),
    ("volume-mask volume-unmask vm vu", "--vol --ag"),
    ("volume-phy-disk-cache-update vpdcu volume-read-cache-policy-update vrcpu volume-write-cache-policy-update vwcpu",
     "--vol --policy"),
    ("system-read-cache-pct-update srcpu", "--sys --read-pct"),
    ("pool-member-info pmi", "--pool"),
    ("access-group-create ac", "--name --init --sys"),
    ("access-group-add access-group-remove aa ar", "--ag --init"),
    ("access-group-delete ad access-group-volumes", "--ag"),
    ("iscsi-chap", "--init --in-user --in-pass --out-user --out-pass"),
    ("fs-create", "--name --size --pool"),
    ("fs-delete", "--fs"),
    ("fs-resize", "--fs --size"),
    ("fs-export",
     "--fs --exportpath --anonuid --anongid --auth-type --root-host --ro-host --rw-host"
     ),
    ("fs-unexport", "--export"),
    ("fs-clone", "--src-fs --dst-name --backing-snapshot"),
    ("fs-snap-create", "--name --fs"),
    ("fs-snap-delete", "--snap --fs"),
    ("fs-snap-restore", "--snap --fs --file --fileas"),
    ("fs-dependants fs-dependants-rm", "--fs --file"),
    ("file-clone", "--fs --src --dst --backing-snapshot"),
    ("local-disk-ident-led-on ldilon local-disk-ident-led-off ldiloff local-disk-fault-led-on ldflon local-disk-fault-led-off ldfloff",
     "--path"),
    ("local-led-slot-set", "--slot --state"),
]
ALL_CMDS = []
for cmds, options in OPTION_TABLE:
    for cmd in cmds.split():
        ALL_CMDS.append(cmd)
        T("lsmcli %s " % cmd, options.replace(" ", "|"))
# Every sub command, each with a word that globs: under failglob a word that
# reaches the shell unquoted is an error, and with a file it matches, here
# ./--name and friends, it completes the wrong thing.
for cmd in ALL_CMDS:
    for cur in ["zz*", "--zz*", "--*", "--n*", "a*"]:
        T("lsmcli %s %s" % (cmd, cur), "")
# A word on the command line that globs does not hide an option, and an option
# that globs does not match a word.
T("lsmcli volume-delete --v* ", VD)
T("lsmcli volume-delete --* ", VD)

# Options that can be repeated stay on offer after they are used, the others,
# even ones that are repeatable for some other command, do not.
for line, expect in [
    ("lsmcli volume-raid-create --name x --disk d1 --disk d2 ",
     "--disk|--raid-type|--strip-size"),
    ("lsmcli vrc --disk d1 --raid-type RAID5 --strip-size 8 ",
     "--name|--disk"),
    ("lsmcli volume-replicate-range --src-vol a --dst-vol b --rep-type COPY --src-start 1 --dst-start 2 --count 3 ",
     "--src-start|--dst-start|--count"),
    ("lsmcli fs-export --fs f --root-host a --ro-host b --rw-host c --exportpath /x ",
     "--anonuid|--anongid|--auth-type|--root-host|--ro-host|--rw-host"),
    ("lsmcli fs-snap-restore --snap s --fs f --file a --fileas b ",
     "--file|--fileas"), ("lsmcli fs-dependants --fs f --file a ", "--file"),
    ("lsmcli fs-dependants-rm --fs f --file a --file b ", "--file"),
    ("lsmcli list --disk d --sys s ",
     "--type|--pool|--vol|--ag|--fs|--nfs-export|--tgt"),
    ("lsmcli lv --disk d ", "--sys|--pool|--vol|--ag|--fs|--nfs-export|--tgt"),
    ("lsmcli volume-create --name a --name b ",
     "--size|--pool|--provisioning"),
    ("lsmcli fs-snap-restore --snap s --fs f --file a --force ",
     "--file|--fileas"),
    ("lsmcli fs-export --fs f --root-host a --r",
     "--root-host|--ro-host|--rw-host")
]:
    T(line, expect)

# Values with a fixed set of choices.  --rep-type, --policy and the rest depend
# on the sub command, as the choices of cmdline.py do.  None is looked up.
for line, expect in [
    ("lsmcli volume-replicate --rep-type ",
     "CLONE|COPY|MIRROR_ASYNC|MIRROR_SYNC"),
    ("lsmcli volume-replicate --rep-type M", "MIRROR_ASYNC|MIRROR_SYNC"),
    ("lsmcli volume-replicate-range --rep-type ", "CLONE|COPY"),
    ("lsmcli volume-replicate-range --rep-type M", ""),
    ("lsmcli -u sim:// volume-replicate-range --rep-type ", "CLONE|COPY"),
    ("lsmcli volume-phy-disk-cache-update --policy ", "ENABLE|DISABLE"),
    ("lsmcli vpdcu --vol v --policy E", "ENABLE"),
    ("lsmcli volume-read-cache-policy-update --policy ", "ENABLE|DISABLE"),
    ("lsmcli vrcpu --policy D", "DISABLE"),
    ("lsmcli volume-write-cache-policy-update --policy ", "WB|AUTO|WT"),
    ("lsmcli vwcpu --policy W", "WB|WT"), ("lsmcli vwcpu --policy E", ""),
    ("lsmcli vrcpu --policy W", ""), ("lsmcli x --policy ", ""),
    ("lsmcli volume-raid-create --raid-type ",
     "RAID0|RAID1|RAID5|RAID6|RAID10|RAID50|RAID60"),
    ("lsmcli vrc --raid-type RAID1", "RAID1|RAID10"),
    ("lsmcli volume-create --provisioning ", "DEFAULT|THIN|FULL"),
    ("lsmcli vc --provisioning T", "THIN"),
    ("lsmcli local-led-slot-set --state ",
     "IDENT_ON|IDENT_OFF|FAULT_ON|FAULT_OFF"),
    ("lsmcli local-led-slot-set --slot s --state F", "FAULT_ON|FAULT_OFF"),
    ("lsmcli list --type ",
     "volumes|pools|fs|snapshots|exports|nfs_client_auth|access_groups|systems|disks|plugins|target_ports|batteries"
     ), ("lsmcli list --type s", "snapshots|systems")
]:
    T(line, expect, ran=False)

# Values that are free form, which nothing can be offered for, with a word that
# matches a file as well as with none.
for flag in [
        "--size", "--count", "--src-start", "--dst-start", "--name",
        "--in-user", "--in-pass", "--out-user", "--out-pass", "--exportpath",
        "--anonuid", "--anongid", "--root-host", "--ro-host", "--rw-host",
        "--dst-name", "--file", "--fileas", "--src", "--dst", "--job",
        "--strip-size", "--read-pct", "--path", "--slot", "--snap",
        "--backing-snapshot"
]:
    for cur in ["", "a", "a*", "--n*"]:
        T("lsmcli x %s %s" % (flag, cur), "", ran=False)

# ---------------------------------------------------------------------------
# The line never chooses the target of a look-up.  It is the URI lsmcli is set
# up with, LSMCLI_URI or ~/.lsmcli, or that of a wrapper in front of lsmcli,
# and nothing of the line is passed on: -P would prompt for a password.  A -u
# or --uri anywhere on the line, in any form, is for an array that is not
# known, so then there is no look-up at all.
# ---------------------------------------------------------------------------
T("lsmcli volume-delete --vol ", VOL, ran=True, no_argv=["[-u]", "[-P]"])
T("lsmcli vd --vol ", VOL, ran=True, env={"LSMCLI_URI": "sim://fromenv"})
# Only ~/.lsmcli, and not even that, which is nothing to look up.
T("lsmcli vd --vol ", VOL, ran=True, env={"LSMCLI_URI": None})
T("lsmcli vd --vol ",
  "",
  ran=False,
  env={"LSMCLI_URI": None},
  pre=['rm -f "${HOME}/.lsmcli"'])
TYPED_URI = [
    "lsmcli -u sim:// vd --vol ", "lsmcli -usim:// vd --vol ",
    "lsmcli --uri sim:// vd --vol ", "lsmcli --uri=sim://x:80 vd --vol ",
    "lsmcli vd -u sim:// --vol ", "lsmcli vd --uri=sim:// --vol ",
    "lsmcli vd --vol %s -u sim://x" % CUR,
    "lsmcli vd --vol %s --uri=sim://y" % CUR,
    "lsmcli vd --vol %s -u" % CUR, "lsmcli -u=sim://x vd --vol ",
    "lsmcli -u =sim://x vd --vol ", "lsmcli -t , -u sim://a vd --vol ",
    "lsmcli -t: -u a vd -u b --vol ", 'lsmcli "-u" sim://a vd --vol ',
    r"lsmcli \-u sim://a vd --vol ", "lsmcli '--uri=sim://a' vd --vol ",
    "lsmcli $'-u' sim://a vd --vol ", "lsmcli -u '' vd --vol ",
    "lsmcli -u $X vd --vol ", "lsmcli -u ls vd --vol ",
    "lsmcli vd # -u sim://b --vol ",
    "lsmcli -u sim://u:%s@h vd --vol " % SECRET, "lsmcli vd --vol -u sim://a",
    "lsmcli vd --uri sim://a --vol ", "lsmcli -u sim://a -u sim://b vd --vol ",
    "lsmcli vd --name a --uri=x --vol ", "lsmcli -Hbu sim://a vd --vol "
]
# argparse takes an abbreviation of --uri for it.
for abbreviation in ["--ur", "--u", "--ur=sim://x"]:
    TYPED_URI.append("lsmcli %s sim://x vd --vol " % abbreviation)
for line in TYPED_URI:
    T(line, "", ran=False)
# A word of the line that is not one of those is not taken for one.
for line in [
        "lsmcli vd --name uri --vol ", "lsmcli vd --name a-u --vol ",
        "lsmcli vd --name u --vol ", "lsmcli vd --name --x --vol ",
        "lsmcli vd --name 'u' --vol ", "lsmcli vd --vol=u --vol "
]:
    T(line, VOL, ran=True, no_argv=["[-u]"])
# The cursor in the word of the -u, wherever.
for point in [16, 17, 18]:
    line = "lsmcli vd --vol -u sim://a"
    T(line[:point] + CUR + line[point:], "", ran=False)

# The line is not what the look-up is filtered by either: whatever the
# filters on the line say, or do not, the whole list is looked up.
NO_FILTER = ["[--ag]", "[--fs]", "[--sys]", "[--pool]", "[-u]"]
for line, expect in [("lsmcli aa --ag ag-one --init ", INIT_ALL),
                     ("lsmcli aa --ag=ag-one --init ", INIT_ALL),
                     ("lsmcli aa --ag a --ag b --init ", INIT_ALL),
                     ("lsmcli x --ag --init ", INIT_ALL),
                     ("lsmcli x --ag '' --init ", INIT_ALL),
                     ("lsmcli x --init --ag ", "ag-one|ag-two"),
                     ("lsmcli list --sys s-one --pool p --vol ", VOL),
                     ("lsmcli lv --sys s-one --pool p --vol ", VOL),
                     ("lsmcli lp --sys s-one --pool ",
                      PAYLOAD_ESC + "|pool-one"),
                     ("lsmcli vm --vol v1 --ag ", "ag-one|ag-two")]:
    T(line, expect, ran=True, no_argv=NO_FILTER)
# No --fs is taken from the line for the snapshots, so nothing is offered, and
# nothing is looked up.
for line in [
        "lsmcli fs-snap-delete --fs fs-one --snap ",
        "lsmcli fs-snap-delete --snap ",
        "lsmcli fs-clone --src-fs fs-one --backing-snapshot ",
        "lsmcli file-clone --fs=fs-one --backing-snapshot "
]:
    T(line, "", ran=False)

# Nothing in the line stops the look-up but a -u, however the shell would read
# it: nothing is expanded or run, and a word that is not the one that is
# completed is only a word.
for line in [
        "lsmcli vd $O --vol ", "lsmcli $O vd --vol ", "lsmcli ${O} vd --vol ",
        "lsmcli `echo vd --vol ", "lsmcli vd a* --vol ", "lsmcli vd ~ --vol ",
        "lsmcli vd !x --vol ", "lsmcli vd 'a b' --vol ",
        "lsmcli vd # something --vol ", "lsmcli vd >out --vol ",
        "lsmcli vd 2>/dev/null --vol ", "lsmcli vd <(echo a) --vol ",
        "lsmcli >out vd --vol ", "lsmcli FOO=1 vd --vol "
]:
    T(line, VOL, ran=True, no_argv=["[-u]"])
T("lsmcli aa --ag a>out --init ", INIT_ALL, ran=True)
# An open quote earlier on the line takes the rest of it into the word.
T("lsmcli vd 'unclosed --vol ", "", ran=False)
# The word being completed is, with the rest of it that is after the cursor.
T("lsmcli vd --vol %sx" % CUR, "", ran=True)
T("lsmcli vd --vol %s#x" % CUR, "", ran=True)

# The program that is run is the one that was typed, and when that is an alias
# or a function nothing is looked up, as what it adds to the line is not
# known, eg. a -u of its own, and with it the array the line is for.
T("./lsmcli vd --vol ", VOL)
T("lsmcli vd --vol ", VOL)
T("/nonexistent/lsmcli vd --vol ", "", ran=False)
T("lsmcli vd --vol ",
  "",
  ran=False,
  post=["alias lsmcli='lsmcli -u sim://ALIASED'"])
T("./lsmcli vd --vol ", VOL, post=["alias lsmcli='lsmcli -u sim://ALIASED'"])
T("lsmcli vd --vol ",
  "",
  ran=False,
  post=["lsmcli() { echo 'a function of the user'; }"])
# Options, and values that are not looked up, still complete for those.
T("lsmcli vd --", VD, post=["alias lsmcli='lsmcli -u sim://ALIASED'"])
T("lsmcli vd --", VD, post=["lsmcli() { echo 'a function of the user'; }"])
T("lsmcli vrc --raid-type RAID1",
  "RAID1|RAID10",
  post=["lsmcli() { echo 'a function of the user'; }"])

# Nothing that could be a password or carry one is ever offered, however much
# is typed of it and whether look-ups are on or not.  The password of the
# environment, of LSMCLI_URI and of ~/.lsmcli are in every case, see SECRET.
for setting in ("1", "0"):
    for line in [
            "lsmcli x --in-pass ", "lsmcli x --in-pass p",
            "lsmcli x --out-pass ", "lsmcli x --out-pass p", "lsmcli -u ",
            "lsmcli -u sim", "lsmcli -u sim://u:",
            "lsmcli -u sim://u:%s" % SECRET[:3], "lsmcli --uri ",
            "lsmcli --uri s", "lsmcli --uri=", "lsmcli --uri=sim",
            "lsmcli vd -u ",
            "lsmcli vd --uri=sim://u:%s@" % SECRET,
            "lsmcli iscsi-chap --in-pass ", "lsmcli iscsi-chap --out-pass "
    ]:
        T(line, "", ran=False, env={"LSMCLI_AUTO_COMPLETE_VALUE": setting})
T("lsmcli -u sim://u:%s@h vd --vol " % SECRET, "", ran=False)
T("lsmcli vd --vol ", VOL)

# --force is accepted on a line, but never offered, in a list of options or
# of the global ones.
T("lsmcli vd --force --vol ", VOL)
T("lsmcli -f vd --vol ", VOL)
T("lsmcli --f", "")
T(
    "lsmcli -",
    "-v|-u|-P|-H|-t|-e|-w|-b|-s|-h|--help|--version|--uri|--prompt|"
    "--human|--terse|--enum|--wait|--header|--script")
T("lsmcli vd --f", "")
T("lsmcli fs-snap-restore --", "--snap|--fs|--file|--fileas")

# The program that was typed with a path is run only when look-ups are on, else
# the one in PATH is.
T("./trap/lsmcli vd --",
  VD,
  trap=False,
  env={"LSMCLI_AUTO_COMPLETE_VALUE": "0"})
T("./trap/lsmcli vd --",
  VD,
  trap=False,
  env={"LSMCLI_AUTO_COMPLETE_VALUE": None})
T("./trap/lsmcli vd --vol ", "", trap=True)

# What is not a value still completes on a line like that, as well as it can:
# the sub command is the first word that is not an option, so after a
# redirection that is the file, but one after the sub command is fine.
for line, expect in [
    ("lsmcli -u sim://a>out vd --v", "--vol"),
    ("lsmcli -u a>o fs-export --anon", "--anonuid|--anongid"),
    ("lsmcli volume-replicate-range -u sim://a>out --rep-type ", "CLONE|COPY"),
    ("lsmcli vpdcu -u a>o --policy ", "ENABLE|DISABLE"),
    ("lsmcli vwcpu 2>/dev/null --policy ", "WB|AUTO|WT")
]:
    T(line, expect)

# Short options in one word, as argparse reads them: the ones without a value
# and then one with, whose value is the rest of the word or the next.  A -u
# among them is a typed URI, wherever it is in the word, and no other is.
for words, look in [("-Hbu sim://a vd --vol ", False),
                    ("-Hbusim://a vd --vol ", False),
                    ("vd -Hbu sim://a --vol ", False),
                    ("-Hbt , -u sim://a vd -u sim://b --vol ", False),
                    ("-Hb -u sim://a vd --vol ", False),
                    ("-uH vd --vol ", False), ("-Hbu vd --vol ", False),
                    ("-u sim://a -Hbx vd --vol ", False),
                    ("vd --vol <CUR> -u sim://a -vH", False),
                    ("-Hbt , vd --vol ", True), ("-Hbw 5 vd --vol ", True),
                    ("-Hbt, vd --vol ", True), ("-Hb vd --vol ", True),
                    ("-Hbx vd --vol ", True), ("-H -b -s vd --vol ", True),
                    ("vd --vol <CUR> -vH", True)]:
    T("lsmcli " + words.replace("<CUR>", CUR),
      VOL if look else "",
      ran=look,
      no_argv=["[-u]"])

# Many words, as a line from a script can have: where the options are is not
# what the look-up depends on.
many = ["w%d" % n for n in range(300)]
many[0] = "lsmcli"
many[1] = "x"
many[100] = "--ag"
many[101] = "ag-one"
many[298] = "--init"
many[299] = ""
T(" ".join(many), INIT_ALL, no_argv=["[--ag]"])

# The setting that turns look-ups on is exactly 1, and is read at completion
# time so it can be exported, changed or removed after the script was loaded.
for setting in [None, "", "0", "2", "yes"]:
    T("lsmcli capabilities --sys ",
      "",
      ran=False,
      env={"LSMCLI_AUTO_COMPLETE_VALUE": setting})
T("lsmcli capabilities --sys ",
  SYS_ALL,
  ran=True,
  env={"LSMCLI_AUTO_COMPLETE_VALUE": None},
  post=["export LSMCLI_AUTO_COMPLETE_VALUE=1"])
T("lsmcli capabilities --sys ",
  SYS_ALL,
  ran=True,
  env={"LSMCLI_AUTO_COMPLETE_VALUE": None},
  post=["LSMCLI_AUTO_COMPLETE_VALUE=1"])
T("lsmcli capabilities --sys ",
  "",
  ran=False,
  post=["LSMCLI_AUTO_COMPLETE_VALUE=0"])

# The completion does not depend on how the user has set up the shell, nor
# does it change it: a word splitting IFS, nocasematch, a variable that is
# readonly, and no match is no entries.
SHELL_SETUPS = [
    ["IFS=a"], ["shopt -s nocasematch"], ["shopt -s extglob nocaseglob"],
    ["set -f"], ["set -u"],
    [
        "readonly i sep word count cur prev out entry line words item "
        "plain uri cmd args"
    ]
]
for setup in SHELL_SETUPS:
    T("lsmcli volume-create ",
      "--name|--size|--pool|--provisioning",
      post=setup)
    T("lsmcli volume-create zz", "", post=setup)
    T("lsmcli capabilities --sys sys-o", "sys-one", post=setup)
    T("lsmcli x --ag ag-one --init ", INIT_ALL, post=setup)
    T("lsmcli volume-create --p", "--pool|--provisioning", post=setup)
# A function and variables of the user's are left alone, and the script defines
# no 'join' of its own.
T("lsmcli volume-create ",
  "--name|--size|--pool|--provisioning",
  pre=[
      "join() { echo user-join; }",
      "i=I word=W count=C sep=S plain=P cur=C2 prev=P2"
  ])

# ---------------------------------------------------------------------------
# The runner.
# ---------------------------------------------------------------------------


def run_case(index, case, root, script, strict):
    """Return a list of what was wrong with a case, empty if nothing."""
    case_dir = os.path.join(root, "case%d" % index)
    os.mkdir(case_dir)
    marker = os.path.join(case_dir, "payload_ran")
    called = os.path.join(case_dir, "lsmcli_called")
    argv_log = os.path.join(case_dir, "lsmcli_argv")

    rc = RC.replace("@DIR@", case_dir).replace("@SCRIPT@", script)
    pre = list(case.pre)
    post = list(case.post)
    if strict:
        post.append("set -eu; shopt -s nocasematch extglob nocaseglob")
    rc = rc.replace("@PRE@", "\n".join(pre)).replace("@POST@", "\n".join(post))
    rc_path = os.path.join(case_dir, "rc")
    with open(rc_path, "w") as handle:
        handle.write(rc)
    inputrc = os.path.join(case_dir, "inputrc")
    open(inputrc, "w").close()

    env = dict(os.environ,
               TERM="dumb",
               INPUTRC=inputrc,
               LC_ALL="C.UTF-8",
               PYTHONPATH=os.path.join(root, "stage"),
               LD_LIBRARY_PATH=os.environ["LSM_TEST_LIBDIR"],
               LSM_TEST_DRIVER=os.path.join(root, "stub_complete.py"),
               LSM_TEST_STUB=os.path.join(root, "bin", "lsmcli"),
               LSM_TEST_MARKER=marker,
               LSM_TEST_CALLED=called,
               LSM_TEST_ARGV=argv_log,
               LSMCLI_AUTO_COMPLETE_VALUE="1",
               PATH=os.path.join(root, "bin") + ":" + os.environ["PATH"])
    env.update(LSMCLI_URI="sim://user:%s@host" % SECRET,
               LSMCLI_PASSWORD=SECRET,
               HOME=case_dir,
               LSM_TEST_TRAP=os.path.join(case_dir, "trap_ran"))
    with open(os.path.join(case_dir, ".lsmcli"), "w") as handle:
        handle.write("uri = sim://cfg:%s@host\n" % SECRET)
    for key, value in case.env.items():
        if value is None:
            env.pop(key, None)
        else:
            env[key] = value

    line = case.line
    point = len(line)
    if CUR in line:
        point = line.index(CUR)
        line = line.replace(CUR, "")
    typed = line.encode()

    master, slave = os.openpty()
    fcntl.ioctl(master, termios.TIOCSWINSZ,
                struct.pack("HHHH", 50, 2000, 0, 0))
    proc = subprocess.Popen(["bash", "--rcfile", rc_path, "-i"],
                            stdin=slave,
                            stdout=slave,
                            stderr=slave,
                            env=env,
                            cwd=os.path.join(root, "files"),
                            start_new_session=True)
    os.close(slave)
    seen = b""

    def pump(until):
        nonlocal seen
        end = time.time() + TIMEOUT
        while time.time() < end:
            if until():
                return True
            ready, _, _ = select.select([master], [], [], 0.05)
            if ready:
                try:
                    chunk = os.read(master, 65536)
                except OSError:
                    return until()
                if not chunk:
                    return until()
                seen += chunk
        return until()

    problems = []
    out_path = os.path.join(case_dir, "out")
    try:
        if not pump(lambda: seen.endswith(b"P> ")):
            return ["no prompt: %r" % seen[-200:]]
        os.write(master, typed + b"\x02" * (len(line) - point) + b"\t")
        if not pump(lambda: os.path.exists(out_path)):
            return ["no answer, the shell said: %r" % seen[-300:]]
    finally:
        try:
            os.killpg(proc.pid, 9)
        except OSError:
            pass
        proc.wait()
        os.close(master)

    with open(out_path, "rb") as handle:
        raw = handle.read()
    got = [e.decode(errors="surrogateescape") for e in raw.split(b"\0")[:-1]]
    expect = case.expect
    # The order is not seen by anyone, readline sorts what it is offered.
    if sorted(got) != sorted(expect):
        problems.append("offered %r, expected %r" % (got, expect))
    for path, what in [(os.path.join(case_dir, "err"), "stderr"),
                       (os.path.join(case_dir, "state"), "shell state")]:
        if os.path.exists(path) and os.path.getsize(path):
            with open(path, errors="replace") as handle:
                problems.append("%s: %s" % (what, handle.read()[:600]))
    if os.path.exists(marker):
        problems.append("the payload was evaluated")
    if any(SECRET in e for e in got):
        problems.append("the password was offered: %r" % got)
    if os.path.exists(os.path.join(case_dir, "err")):
        with open(os.path.join(case_dir, "err"), errors="replace") as handle:
            if SECRET in handle.read():
                problems.append("the password was written to stderr")
    if case.trap is not None and \
            os.path.exists(os.path.join(case_dir, "trap_ran")) != case.trap:
        problems.append("the program typed was %s run" %
                        ("not" if case.trap else "wrongly"))
    was_called = os.path.exists(called)
    if case.ran is True and not was_called:
        problems.append("lsmcli was not asked, it should have been")
    if case.ran is False and was_called:
        problems.append("lsmcli was asked, it should not have been")
    log = b""
    if os.path.exists(argv_log):
        with open(argv_log, "rb") as handle:
            log = handle.read()
    for needle in case.no_argv:
        if was_called and needle.encode() in log:
            problems.append("lsmcli asked with %s" % needle)
    if problems and os.environ.get("LSM_TEST_DEBUG"):
        with open(os.path.join(case_dir, "words")) as handle:
            problems.append("bash words: " + handle.read().strip())
    return problems


def readback_problems():
    """
    The names the stub has, offered for a word with no quote open, read back as
    the names.
    """
    problems = []
    for name in AWKWARD:
        offered = EXPECT_AWKWARD[name]
        result = subprocess.run(["bash", "-c", "printf %s " + offered],
                                capture_output=True,
                                check=False)
        if result.stdout.decode() != name:
            problems.append("%r offered as %s, which reads back as %r" %
                            (name, offered, result.stdout.decode()))
    return problems


def main():
    script = os.path.abspath(
        os.path.join(
            os.environ.get("srcdir",
                           os.path.dirname(os.path.abspath(__file__))),
            "lsmcli"))
    if not os.access(script, os.R_OK):
        print("Cannot read completion script %s" % script)
        return 1
    if shutil.which("bash") is None:
        print("Skipping: no bash")
        return SKIP

    srcdir = os.path.dirname(script)
    os.environ["LSM_TEST_LIBDIR"] = os.path.abspath(
        os.path.join(os.getcwd(), "..", "..", "c_binding", ".libs"))
    root = tempfile.mkdtemp()
    failures = 0
    try:
        staged = subprocess.run([
            os.path.join(srcdir, "stage_lsm.sh"), srcdir,
            os.path.join(root, "stage")
        ],
                                check=False)
        if staged.returncode != 0:
            return SKIP if staged.returncode == SKIP else 1
        shutil.copy(os.path.join(srcdir, "stub_complete.py"), root)
        os.mkdir(os.path.join(root, "bin"))
        os.mkdir(os.path.join(root, "files"))
        for name in ["--name", "--size", "a1", "a2", "ac"]:
            open(os.path.join(root, "files", name), "w").close()
        stub = os.path.join(root, "bin", "lsmcli")
        with open(stub, "w") as handle:
            handle.write(STUB)
        os.chmod(stub, 0o755)
        # ./lsmcli is run from the directory the cases are run in.
        os.symlink(stub, os.path.join(root, "files", "lsmcli"))
        # A program that leaves a mark when it is run, to see what is run.
        os.mkdir(os.path.join(root, "files", "trap"))
        trap = os.path.join(root, "files", "trap", "lsmcli")
        with open(trap, "w") as handle:
            handle.write('#!/usr/bin/env bash\ntouch "${LSM_TEST_TRAP}"\n')
        os.chmod(trap, 0o755)

        for problem in readback_problems():
            failures += 1
            print("FAIL: %s" % problem)

        for strict in (False, True):
            jobs = []
            with concurrent.futures.ThreadPoolExecutor(WORKERS) as pool:
                for index, case in enumerate(cases):
                    num = index + (len(cases) if strict else 0)
                    jobs.append((case,
                                 pool.submit(run_case, num, case, root, script,
                                             strict)))
            for case, job in jobs:
                try:
                    problems = job.result()
                except OSError as err:
                    print("Skipping: no pty to run bash on: %s" % err)
                    return SKIP
                if problems:
                    failures += 1
                    print("FAIL%s: %r" %
                          (" (set -eu, nocasematch, extglob, "
                           "nocaseglob)" if strict else "", case.line))
                    for problem in problems:
                        print("    " + problem)
    finally:
        shutil.rmtree(root, ignore_errors=True)

    if failures:
        print("%d completion test(s) failed" % failures)
        return 1
    print("All %d completion cases passed, twice" % len(cases))
    return 0


sys.exit(main())
