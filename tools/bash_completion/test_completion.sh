#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
#
# Copyright (C) 2026 Red Hat, Inc.
#
# Tests for the lsmcli bash completion script.  A stub 'lsmcli' is put first in
# PATH so the value look-ups return known values.  Every list type includes an
# entry containing command substitution, which the completion code must offer
# verbatim and must never evaluate.

set -u

# Nothing here may wait for input, so a failure reports rather than hangs.
exec < /dev/null

srcdir=${srcdir:-$(dirname "$0")}
completion_script="$(readlink -f "${srcdir}/lsmcli")"

if [[ ! -r ${completion_script} ]] ; then
    echo "Cannot read completion script ${completion_script}" >&2
    exit 1
fi

tmp_dir=$(mktemp -d) || exit 1
trap 'rm -rf "${tmp_dir}"' EXIT

# Created by the payload if it ever gets evaluated, and by the stub every time
# it is run. The stub also logs its arguments, one run per line, each in [].
export LSM_TEST_MARKER="${tmp_dir}/payload_ran"
export LSM_TEST_CALLED="${tmp_dir}/lsmcli_called"
export LSM_TEST_ARGV="${tmp_dir}/lsmcli_argv"

# The literal string the stub returns as a value.  Single quoted so it stays
# unevaluated here; if the completion code expands it the marker shows up.
PAYLOAD='$(touch ${LSM_TEST_MARKER})'
# What the completion offers for it: escaped, so the shell reads it back as the
# one word it was.
printf -v PAYLOAD_ESC '%q' "${PAYLOAD}"
# The same for the names with a backslash and a double quote.
BACKSLASH_ESC='back\\slash'
QUOTE_ESC='a\"q'
printf -v TAB_ESC '%q' $'t\tb'

# Some of the tests glob for files, so work in a directory with a few.
cd "${tmp_dir}" || exit 1
touch -- ./--name ./--size ./a1 ./a2 ./ac

cat > "${tmp_dir}/lsmcli" <<'STUB'
#!/usr/bin/env bash
# Stub lsmcli returning terse output, separated by what -t asks for, for the
# requested --type.
touch "${LSM_TEST_CALLED}"
# Noise on stderr, which completion must keep off the user's command line.
echo "lsmcli stub noise" >&2
{ printf '[%s]' "$@" ; echo ; } >> "${LSM_TEST_ARGV}"

list_type=''
ag=''
sep=''
while [[ $# -gt 0 ]] ; do
    case "$1" in
        # A flag may come last, with no value, and shift 2 would then fail
        # and leave this looping for ever.
        --type) list_type="${2-}" ; shift ; [[ $# -gt 0 ]] && shift ;;
        --ag) ag="${2-}" ; shift ; [[ $# -gt 0 ]] && shift ;;
        --fs) shift ; [[ $# -gt 0 ]] && shift ;;
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
    disks) row "${payload}" "disk desc" ; row disk-one desc ;;
    exports) row "${payload}" "export desc" ; row export-one desc ;;
    target_ports) row "${payload}" "tgt desc" ; row tgt-one desc ;;
    fs) row "${payload}" "fs desc" ; row fs-one desc ;;
    snapshots) row "${payload}" "snap desc" ; row snap-one desc ;;
    # The real lsmcli prints every type on the one line.
    nfs_client_auth) row "${payload}" standard krb5 ;;
    access_groups)
        # Third column is the initiator, which is what --init completes with.
        if [[ -n ${ag} ]] ; then
            row "${ag}" "ag name" init-filtered ISCSI
        else
            row ag-one "ag name" "${payload}" ISCSI
            row ag-two "ag name" init-two ISCSI
        fi
        ;;
esac
STUB
chmod +x "${tmp_dir}/lsmcli"

export PATH="${tmp_dir}:${PATH}"
export LSMCLI_AUTO_COMPLETE_VALUE=1

# A glob that matches nothing is an error with failglob, so a completion that
# lets a typed word through to the shell fails loudly.
shopt -s failglob

# shellcheck source=tools/bash_completion/lsmcli
source "${completion_script}"

failures=0

function fail()
{
    echo "FAIL: $*"
    failures=$(( failures + 1 ))
}

# Run _lsm for the given words and compare the resulting COMPREPLY.
# $1 Expected COMPREPLY entries, '|' separated
# $2 Index of the word being completed
# $@ The words, the last of which is the word being completed unless the
#    index says otherwise
function check_words()
{
    local expected="$1"
    local rc actual err
    COMP_CWORD="$2"
    shift 2
    COMP_WORDS=("$@")
    COMPREPLY=()

    rm -f "${LSM_TEST_ARGV}"
    # Anything written to stderr lands on the user's command line, and it is
    # also where the shell reports a glob that matched nothing under failglob.
    _lsm 2> "${tmp_dir}/stderr"
    rc=$?
    err=$(cat "${tmp_dir}/stderr")
    [[ -n ${err} ]] && fail "'${COMP_WORDS[*]}' wrote to stderr: ${err}"

    actual=$(IFS='|' ; echo "${COMPREPLY[*]+${COMPREPLY[*]}}")
    if [[ ${actual} != "${expected}" || ${rc} -ne 0 ]] ; then
        fail "'${COMP_WORDS[*]}' (rc ${rc})" \
             "expected: [${expected}] actual: [${actual}]"
    fi
}

# Run _lsm for a command line and compare the resulting COMPREPLY.
# $1 The words already on the command line
# $2 The word being completed
# $3 Expected COMPREPLY entries, '|' separated
function check()
{
    local -a words
    read -r -a words <<< "$1"
    check_words "$3" "${#words[@]}" "${words[@]}" "$2"
}

# Did the last check run lsmcli with this exact argument list? Arguments are
# given as the stub logs them, eg. '[list][--type][systems]'.
function stub_ran_with()
{
    [[ -e ${LSM_TEST_ARGV} ]] && grep -qF -- "$1" "${LSM_TEST_ARGV}"
}

# Command line                                | word | expected completions
tests=(
    "lsmcli capabilities --sys|            |${PAYLOAD_ESC}|sys-one|sys-two|sys\\ three|${BACKSLASH_ESC}|${QUOTE_ESC}|ax|${TAB_ESC}|hash#tag"
    "lsmcli capabilities --sys|sys-o       |sys-one"
    "lsmcli capabilities --sys|nomatch     |"
    "lsmcli volume-create --pool|          |${PAYLOAD_ESC}|pool-one"
    "lsmcli volume-delete --vol|           |${PAYLOAD_ESC}|vol-one"
    "lsmcli volume-replicate-range --src-vol| |${PAYLOAD_ESC}|vol-one"
    "lsmcli volume-replicate-range --dst-vol| |${PAYLOAD_ESC}|vol-one"
    "lsmcli volume-raid-create --disk|     |${PAYLOAD_ESC}|disk-one"
    "lsmcli volume-mask --ag|              |ag-one|ag-two"
    "lsmcli access-group-create --init|    |${PAYLOAD_ESC}|init-two"
    "lsmcli list --nfs-export|             |${PAYLOAD_ESC}|export-one"
    "lsmcli list --tgt|                    |${PAYLOAD_ESC}|tgt-one"
    "lsmcli fs-delete --fs|                |${PAYLOAD_ESC}|fs-one"
    "lsmcli fs-clone --src-fs|             |${PAYLOAD_ESC}|fs-one"
    "lsmcli fs-unexport --export|          |${PAYLOAD_ESC}|export-one"
    "lsmcli fs-export --auth-type|         |${PAYLOAD_ESC}|standard|krb5"
    "lsmcli fs-export --auth-type|k        |krb5"
    # Filtered look-ups, where another argument narrows the result.
    "lsmcli access-group-add --ag ag-one --init| |init-filtered"
    "lsmcli fs-snap-delete --fs fs-one --snap|   |${PAYLOAD_ESC}|snap-one"
    # --snap without a --fs has nothing to offer.
    "lsmcli fs-snap-delete --snap|         |"
    # A value is matched literally, no globbing and no expansion.
    "lsmcli capabilities --sys|\$(          |${PAYLOAD_ESC}"
    "lsmcli capabilities --sys|*           |"
    # Nor does a word in the table's cur column get to glob: ./a1 and ./a2 exist.
    "lsmcli capabilities --sys|a*          |"
    "lsmcli volume-create|--*              |"
    "lsmcli volume-create|--n*             |"
    "lsmcli --type|*                       |"
    # Each kind of compgen call, with words that match no file at all: under
    # failglob an unquoted word is an error, and that is not allowed either.
    "lsmcli|zz*                            |"
    "lsmcli|--zz*                          |"
    "lsmcli|-zz*                           |"
    "lsmcli list --type|zz*                |"
    "lsmcli volume-create --size|zz*       |"
    "lsmcli volume-replicate --rep-type|zz*|"
    "lsmcli list --type snapshots|zz*      |"
    # And ones that match a file, ./ac, which must not be taken for the word.
    "lsmcli|a*                             |"
    "lsmcli list --type|a*                 |"
)

for t in "${tests[@]}" ; do
    IFS='|' read -r comp_line cur expected_head rest <<< "${t}"
    expected="${expected_head}"
    [[ -n ${rest} ]] && expected="${expected_head}|${rest}"
    # Trailing blanks in the table keep the columns readable.
    check "${comp_line}" "${cur%% *}" "${expected}"
done

# Names containing a space, completed from the forms the shell hands over for
# the word being typed: plain, backslash escaped, and inside either kind of
# quote. All have to find the name and offer it escaped, space included.
# The cur column here is exact, with no trimming.
#
# Command line      | word      | expected completions
space_tests=(
    "lsmcli capabilities --sys|sys|sys-one|sys-two|sys\\ three"
    "lsmcli capabilities --sys|sys\\ |sys\\ three"
    "lsmcli capabilities --sys|sys\\ t|sys\\ three"
    "lsmcli capabilities --sys|\"sys |sys\\ three"
    "lsmcli capabilities --sys|'sys |sys\\ three"
    "lsmcli capabilities --sys|\"sys-|sys-one|sys-two"
    "lsmcli capabilities --sys|sys\\ x|"
)

for t in "${space_tests[@]}" ; do
    IFS='|' read -r comp_line cur expected_head rest <<< "${t}"
    expected="${expected_head}"
    [[ -n ${rest} ]] && expected="${expected_head}|${rest}"
    check "${comp_line}" "${cur}" "${expected}"
done

# Check the stub was, or was not, run with some arguments, eg. '[--ag]'.
# $1 'yes' or 'no'
# $2 The text of the argument log to look for
# $3 What the check is about
function check_stub_args()
{
    local want="$1"
    local needle="$2"
    local what="$3"

    if stub_ran_with "${needle}" ; then
        [[ ${want} == yes ]] || fail "${what}: lsmcli was run with ${needle}"
    else
        [[ ${want} == no ]] || fail "${what}: lsmcli not run with ${needle}"
    fi
}

# Backslashes and quotes in the word being completed are read the way the shell
# reads them. Unquoted, a backslash escapes any character; in double quotes
# only \ " $ and ` and any other backslash stays as typed; in single quotes
# nothing is escaped. The table is read from a here-document so that nothing
# in it is processed twice. Names the stub has: back\slash and a"q.
#
# Command line|word|expected completions
while IFS='|' read -r comp_line cur expected_head rest ; do
    [[ -z ${comp_line} ]] && continue
    expected="${expected_head}"
    [[ -n ${rest} ]] && expected="${expected_head}|${rest}"
    check "${comp_line}" "${cur}" "${expected}"
done <<'TABLE'
lsmcli capabilities --sys|back\\sl|back\\slash
lsmcli capabilities --sys|back\sl|
lsmcli capabilities --sys|back\\\\sl|
lsmcli capabilities --sys|"back\\sl|back\\slash
lsmcli capabilities --sys|"back\sl|back\\slash
lsmcli capabilities --sys|"back\\\\sl|
lsmcli capabilities --sys|'back\sl|back\\slash
lsmcli capabilities --sys|'back\\sl|
lsmcli capabilities --sys|a\"q|a\"q
lsmcli capabilities --sys|"a\"q|a\"q
lsmcli capabilities --sys|"a\"q"|a\"q
lsmcli capabilities --sys|'a"q|a\"q
lsmcli capabilities --sys|"sys\ |
lsmcli capabilities --sys|'a'x|ax
lsmcli capabilities --sys|"a"x|ax
lsmcli capabilities --sys|a'x'|ax
lsmcli capabilities --sys|a"x"|ax
lsmcli capabilities --sys|'a'\"q|a\"q
lsmcli capabilities --sys|"a"\"q|a\"q
lsmcli capabilities --sys|sys-"o"|sys-one
lsmcli capabilities --sys|"sys three"|sys\ three
lsmcli capabilities --sys|'sys th'ree|sys\ three
lsmcli capabilities --sys|sys\ "th|sys\ three
lsmcli capabilities --sys|$'t\t|$'t\tb'
lsmcli capabilities --sys|$'t\x09|$'t\tb'
lsmcli capabilities --sys|$'t\011b'|$'t\tb'
lsmcli capabilities --sys|$'t\n|
lsmcli capabilities --sys|t\|$'t\tb'
lsmcli capabilities --sys|'sys\ |
lsmcli capabilities --sys|sys\\ |
TABLE

# A filter flag without a usable value is treated as if it was not there:
# lsmcli is never handed the flag name, which would be taken as its value.
# A missing, empty or option-like value all count.
# Last word, the one completed, is always empty here.
check_words "${PAYLOAD_ESC}|init-two" 4 lsmcli x --ag --init ""
check_stub_args no "[--ag]" "--init after a --ag with no value"
check_words "${PAYLOAD_ESC}|init-two" 5 lsmcli x --ag "" --init ""
check_stub_args no "[--ag]" "--init after an empty --ag value"
# The flag is the last word, being completed itself, so there is nothing after.
check_words "" 3 lsmcli x --init --ag
check_stub_args no "[--ag]" "--ag is the last word"

# --snap with no --fs to filter on offers nothing, and does not ask lsmcli.
check_words "" 4 lsmcli x --fs --snap ""
check_stub_args no "[--fs]" "--snap after a --fs with no value"
check_words "" 5 lsmcli x --fs "" --snap ""
[[ -e ${LSM_TEST_ARGV} ]] && fail "--snap with an empty --fs ran lsmcli"
check_words "" 3 lsmcli x --snap ""

# A value as typed, escaped, is unescaped before lsmcli sees it.
check_words "init-filtered" 5 lsmcli x --ag 'ag\ one' --init ""
check_stub_args yes "[--ag][ag one]" "escaped --ag"
check_words "init-filtered" 5 lsmcli x --ag "'ag one'" --init ""
check_stub_args yes "[--ag][ag one]" "single quoted --ag"
check_words "init-filtered" 5 lsmcli x --ag '"a\"q"' --init ""
check_stub_args yes '[--ag][a"q]' "double quoted --ag with an escaped quote"
check_words "init-filtered" 5 lsmcli x --ag 'back\\slash' --init ""
check_stub_args yes '[--ag][back\slash]' "--ag with an escaped backslash"
check_words "${PAYLOAD_ESC}|snap-one" 5 lsmcli x --fs 'fs\ one' --snap ""
check_stub_args yes "[--fs][fs one]" "escaped --fs"

# More words than an exit status can count, with --ag at a position that a
# status truncated modulo 256 would get wrong, or confuse with 'not found'.
for ag_at in 100 254 255 256 257 260 ; do
    words=()
    for (( n = 0; n < 300; n++ )) ; do words+=("w${n}") ; done
    words[1]=x
    words[ag_at]="--ag"
    words[ag_at+1]="ag-at-${ag_at}"
    words[298]="--init"
    words[299]=""
    check_words "init-filtered" 299 "${words[@]}"
    check_stub_args yes "[--ag][ag-at-${ag_at}]" "--ag at ${ag_at} of 300 words"
done

# Nothing above may have run the payload.
if [[ -e ${LSM_TEST_MARKER} ]] ; then
    fail "completion evaluated a returned value"
fi

# Every sub command, each with a word that globs. Under failglob a word that
# reaches the shell unquoted is an error, and with a file it matches, here
# ./--name and friends, it completes the wrong thing.
mapfile -t all_cmds < <(sed -n '/_lsm_opts_cmds="/,/"$/p' "${completion_script}" \
                        | sed -e 's/.*_lsm_opts_cmds="//' -e 's/"$//' \
                        | tr -d '\\' | tr -s ' ' '\n' | grep .)
if [[ ${#all_cmds[@]} -lt 50 ]] ; then
    fail "found only ${#all_cmds[@]} sub commands in ${completion_script}"
fi
for cmd in "${all_cmds[@]}" ; do
    for cur in 'zz*' '--zz*' '--*' '--n*' 'a*' ; do
        check_words "" 2 lsmcli "${cmd}" "${cur}"
    done
done

# A word on the command line that globs does not hide an option, and an
# option that globs does not match a word.
check_words "--vol|--force" 3 lsmcli volume-delete '--v*' ""
check_words "--vol|--force" 3 lsmcli volume-delete '--*' ""
# Data: needle|expected exit status, for a list of --vol, x and 'a b'.
listcontains_words=(--vol x 'a b')
for t in '--vol|0' 'x|0' 'a b|0' '--v*|1' '--vo?|1' '*|1' 'a*|1' '[x]|1' '|1' ; do
    IFS='|' read -r needle expected_rc <<< "${t}"
    _lsm_listcontains "listcontains_words[@]" "${needle}"
    if [[ $? -ne ${expected_rc} ]] ; then
        fail "_lsm_listcontains '${needle}' did not give ${expected_rc}"
    fi
done

# After completing nothing is left behind in the shell, not even a variable or
# function of the script's own, other than the functions it is made of.
leaked=$(env LC_ALL=C LSMCLI_AUTO_COMPLETE_VALUE=1 bash --noprofile --norc -c '
    COMP_WORDS=() COMP_CWORD=0 COMPREPLY=() words=() line= before_vars= before_funcs=
    before_vars=$(compgen -v | sort)
    before_funcs=$(compgen -A function | sort)
    source "$1"
    for line in "lsmcli capabilities --sys sys" "lsmcli volume-create --p" \
                "lsmcli x --ag a --init" "lsmcli x --ag --init" \
                "lsmcli x --fs a --snap" "lsmcli list --type" "lsmcli" \
                "lsmcli list --type snapshots" "lsmcli volume-delete --vol v" \
                "lsmcli foo" ; do
        read -r -a COMP_WORDS <<< "${line}"
        COMP_WORDS+=("")
        COMP_CWORD=$(( ${#COMP_WORDS[@]} - 1 ))
        _lsm
    done
    echo "vars: $(comm -13 <(echo "${before_vars}") <(compgen -v | sort) \
                  | tr "\n" " ")"
    echo "funcs: $(comm -13 <(echo "${before_funcs}") \
                   <(compgen -A function | sort) | tr "\n" " ")"' \
    _ "${completion_script}" 2>&1)
printf -v expected_leaked '%s\n%s' 'vars: ' \
    'funcs: _lsm _lsm_arg_index _lsm_flag_value _lsm_join _lsm_listcontains _lsm_possible_args _lsm_unescape _lsm_values '
if [[ ${leaked} != "${expected_leaked}" ]] ; then
    fail "completion left things behind:"
    echo "expected: ${expected_leaked}"
    echo "actual:   ${leaked}"
fi

# lsmcli is run only when the setting is exactly 1, and the setting is read at
# completion time so it can be exported, changed or removed after sourcing.
# Setting | what it does  ('-' is unset)
look_up_tests=(
    "-|no"
    "|no"
    "0|no"
    "2|no"
    "yes|no"
    "1|yes"
)
for t in "${look_up_tests[@]}" ; do
    IFS='|' read -r setting expect <<< "${t}"
    rm -f "${LSM_TEST_CALLED}"
    if [[ ${setting} == - ]] ; then
        export_cmd=':'
    else
        export_cmd="export LSMCLI_AUTO_COMPLETE_VALUE='${setting}'"
    fi
    env -u LSMCLI_AUTO_COMPLETE_VALUE bash --noprofile --norc -c '
        source "$1"
        eval "$2"
        COMP_WORDS=(lsmcli capabilities --sys "")
        COMP_CWORD=3
        _lsm' _ "${completion_script}" "${export_cmd}"
    if [[ ${expect} == yes && ! -e ${LSM_TEST_CALLED} ]] ; then
        fail "lsmcli not run with look-up setting '${setting}' exported after sourcing"
    elif [[ ${expect} == no && -e ${LSM_TEST_CALLED} ]] ; then
        fail "lsmcli run with look-up setting '${setting}'"
    fi
done

# Turning it off again after use takes effect too.
rm -f "${LSM_TEST_CALLED}"
env LSMCLI_AUTO_COMPLETE_VALUE=1 bash --noprofile --norc -c '
    source "$1"
    LSMCLI_AUTO_COMPLETE_VALUE=0
    COMP_WORDS=(lsmcli capabilities --sys "")
    COMP_CWORD=3
    _lsm' _ "${completion_script}"
[[ -e ${LSM_TEST_CALLED} ]] && fail "lsmcli run after the setting was changed to 0"

# Sourcing and completing leave the user's shell alone: no functions of ours
# beyond _lsm and the _lsm_ helpers, and none of the variables they use.
leaks=$(env LSMCLI_AUTO_COMPLETE_VALUE=1 bash --noprofile --norc -c '
    join() { echo user-join ; }
    i=I word=W count=C sep=S index=X value=V item=T plain=P potential_args=PA
    cur=C2 prev=P2
    source "$1"
    for words in "lsmcli capabilities --sys" "lsmcli volume-create" \
                 "lsmcli x --ag a --init" ; do
        read -r -a COMP_WORDS <<< "${words}"
        COMP_WORDS+=("")
        COMP_CWORD=$(( ${#COMP_WORDS[@]} - 1 ))
        _lsm
    done
    echo "join=$(join)"
    echo "vars=${i} ${word} ${count} ${sep} ${index} ${value} ${item} ${plain} ${potential_args} ${cur} ${prev}"
    declare -F | sed "s/^declare -f //" | LC_ALL=C sort | tr "\n" " "' _ "${completion_script}")
expected_leaks='join=user-join
vars=I W C S X V T P PA C2 P2
_lsm _lsm_arg_index _lsm_flag_value _lsm_join _lsm_listcontains _lsm_possible_args _lsm_unescape _lsm_values join '
if [[ ${leaks} != "${expected_leaks}" ]] ; then
    fail "user's functions or variables disturbed:"
    echo "expected: ${expected_leaks}"
    echo "actual:   ${leaks}"
fi

# Without a 'join' of their own the user ends up with none.
if env bash --noprofile --norc -c 'source "$1"; declare -F join' _ \
        "${completion_script}" > /dev/null ; then
    fail "sourcing the completion script defined 'join'"
fi

# Every variable the script uses has to carry the _lsm_ prefix: they are all
# 'local', and a name the user made readonly breaks 'local' for it. Check the
# declarations, then check that completion works with the unprefixed name of
# each of them, and a few common ones besides, made readonly.
mapfile -t declared < <(sed -n 's/^[[:space:]]*local[[:space:]]\{1,\}//p' \
                        "${completion_script}" | tr ' ' '\n' | \
                        grep -v '^-' | sed 's/=.*//' | grep -v '^$')
if [[ ${#declared[@]} -eq 0 ]] ; then
    fail "found no local declarations in ${completion_script}"
fi
read_only_names=(i sep word count)
for name in "${declared[@]}" ; do
    if [[ ${name} != _lsm_* ]] ; then
        fail "local variable '${name}' of the completion script has no _lsm_ prefix"
    else
        read_only_names+=("${name#_lsm_}")
    fi
done

# Each completes a value look-up, the --ag filter and the plain argument list.
expected_readonly=$'sys-one\ninit-filtered\n--pool'
for name in "${read_only_names[@]}" ; do
    actual=$(env LSMCLI_AUTO_COMPLETE_VALUE=1 bash --noprofile --norc -c '
        readonly "$2"
        source "$1"
        COMP_WORDS=(lsmcli capabilities --sys "sys-o") COMP_CWORD=3
        _lsm
        echo "${COMPREPLY[*]}"
        COMP_WORDS=(lsmcli x --ag ag-one --init "") COMP_CWORD=5
        _lsm
        echo "${COMPREPLY[*]}"
        COMP_WORDS=(lsmcli volume-create "--p") COMP_CWORD=2
        _lsm
        echo "${COMPREPLY[*]}"' _ "${completion_script}" "${name}" 2>&1)
    if [[ ${actual} != "${expected_readonly}" ]] ; then
        fail "completion with readonly ${name}: ${actual}"
    fi
done

if [[ ${failures} -ne 0 ]] ; then
    echo "${failures} completion test(s) failed"
    exit 1
fi

echo "All completion tests passed"
exit 0
