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

# Run once more with LSM_TEST_STRICT set, see the end of this file: _lsm is
# then run with set -e and with the shell options that change how patterns
# match, as a user's shell might have them.
strict=${LSM_TEST_STRICT-}

# Nothing here may wait for input, so a failure reports rather than hangs.
exec < /dev/null

self="$(readlink -f "$0")"
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
export LSM_TEST_PROG="${tmp_dir}/lsmcli_prog"

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
# How it was run, as ./lsmcli or by what a search of PATH gave.
echo "$0" >> "${LSM_TEST_PROG}"

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
            if [[ -n ${LSM_TEST_COLON-} ]] ; then
                # An iSCSI name, which bash breaks the word of at every :.
                # Only some tests ask for it, as no other test wants it among
                # what is offered.
                row ag-iqn "ag name" 'iqn.1994-05.com.redhat:abc123' ISCSI
            fi
        fi
        ;;
esac
STUB
chmod +x "${tmp_dir}/lsmcli"
# The command itself can have a word break character in its path, which bash
# makes more than one of COMP_WORDS of.
ln "${tmp_dir}/lsmcli" "${tmp_dir}/lsm:cli"
ln "${tmp_dir}/lsmcli" "${tmp_dir}/lsm=cli"

export PATH="${tmp_dir}:${PATH}"
export LSMCLI_AUTO_COMPLETE_VALUE=1

# A glob that matches nothing is an error with failglob, so a completion that
# lets a typed word through to the shell fails loudly.
shopt -s failglob

# shellcheck source=tools/bash_completion/lsmcli
source "${completion_script}"

failures=0

# Every function the completion script defines, sorted as the C locale does.
lsm_funcs='_lsm _lsm_ansi_c _lsm_complete _lsm_escape _lsm_expands _lsm_flag_value _lsm_join _lsm_known_option _lsm_layout _lsm_listcontains _lsm_option_like _lsm_options _lsm_ordinal _lsm_possible_args _lsm_run _lsm_scan _lsm_unescape _lsm_values _lsm_whole_word'

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
    # The line as typed, and where the cursor is in it. Unless a test says,
    # it is the words with white space between them as bash would have it:
    # none before or after the : and = that bash makes words of, and the cursor
    # at the end.
    COMP_LINE="${test_line-}"
    if [[ -z ${test_line+set} ]] ; then
        local i cursor_at
        COMP_LINE=''
        for (( i = 0; i < $#; i++ )) ; do
            if (( i > 0 )) && [[ ${COMP_WORDS[i]} != [:=\<\>] && ${COMP_WORDS[i-1]} != [:=\<\>] ]] ; then
                COMP_LINE+=' '
            fi
            COMP_LINE+="${COMP_WORDS[i]}"
            # The cursor is at the end of the word being completed.
            (( i == COMP_CWORD )) && cursor_at=${#COMP_LINE}
        done
    fi
    COMP_POINT="${test_point-${cursor_at-${#COMP_LINE}}}"

    rm -f "${LSM_TEST_ARGV}" "${LSM_TEST_PROG}"
    # Anything written to stderr lands on the user's command line, and it is
    # also where the shell reports a glob that matched nothing under failglob.
    if [[ -n ${strict} ]] ; then
        set -e
        shopt -s nocasematch extglob nocaseglob
    fi
    _lsm 2> "${tmp_dir}/stderr"
    rc=$?
    if [[ -n ${strict} ]] ; then
        set +e
        shopt -u nocasematch extglob nocaseglob
    fi
    err=$(cat "${tmp_dir}/stderr")
    [[ -n ${err} ]] && fail "'${COMP_WORDS[*]}' wrote to stderr: ${err}"

    actual=$(IFS='|' ; echo "${COMPREPLY[*]+${COMPREPLY[*]}}")
    if [[ ${actual} != "${expected}" || ${rc} -ne 0 ]] ; then
        fail "'${COMP_WORDS[*]}' (rc ${rc})" \
             "expected: [${expected}] actual: [${actual}]"
    fi
}

# As check_words, for the last of the words being the one completed.
# $1 Expected COMPREPLY entries, '|' separated
# $@ The words
function check_end()
{
    local expected="$1"
    shift
    check_words "${expected}" "$(( $# - 1 ))" "$@"
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
# quote. All have to find the name and offer it so that the shell reads the
# word back as the name, space included. That is escaped with a backslash for
# a word not in quotes, and as is inside quotes, where readline replaces what
# follows the quote and puts back the closing one. The cur column here is
# exact, with no trimming.
#
# Command line      | word      | expected completions
space_tests=(
    "lsmcli capabilities --sys|sys|sys-one|sys-two|sys\\ three"
    "lsmcli capabilities --sys|sys\\ |sys\\ three"
    "lsmcli capabilities --sys|sys\\ t|sys\\ three"
    "lsmcli capabilities --sys|\"sys |sys three"
    "lsmcli capabilities --sys|'sys |sys three"
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
# in it is processed twice. Names the stub has: back\slash and a"q. Inside
# quotes what is offered goes in the place of what follows the quote.
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
lsmcli capabilities --sys|'back\sl|back\slash
lsmcli capabilities --sys|'back\\sl|
lsmcli capabilities --sys|a\"q|a\"q
lsmcli capabilities --sys|"a\"q|a\"q
lsmcli capabilities --sys|"a\"q"|a\"q
lsmcli capabilities --sys|'a"q|a"q
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
lsmcli capabilities --sys|sys\ "th|three
lsmcli capabilities --sys|sys\ 'th|three
lsmcli capabilities --sys|'sys'\ th|sys\ three
lsmcli capabilities --sys|"sys"' th| three
lsmcli capabilities --sys|sys\ $'th|three
lsmcli capabilities --sys|$'t\t|t\tb
lsmcli capabilities --sys|$'t\T|
lsmcli capabilities --sys|$'t\X09|
lsmcli capabilities --sys|$'t\x09|t\tb
lsmcli capabilities --sys|$'t\011b'|$'t\tb'
lsmcli capabilities --sys|$'t\n|
lsmcli capabilities --sys|t\|$'t\tb'
lsmcli capabilities --sys|'sys\ |
lsmcli capabilities --sys|sys\\ |
TABLE

# What is offered for a word, written for the quote the word has open, reads
# back as the name. For each quote: the name, and how to read the result back
# the way the shell does it, and compare, eval'ing only this fixed data.
# State | before the offered text | after
for state in none double single ansi ; do
    case "${state}" in
        none) before='' ; after='' ;;
        double) before='"' ; after='"' ;;
        single) before="'" ; after="'" ;;
        ansi) before="\$'" ; after="'" ;;
    esac
    for name in 'sys three' "it's" 'a"q' '$x' '`x`' 'back\slash' 'a\\b' $'t\tb' \
                $'n\nl' '*' 'a&b' '!x' 'a!b!' '' ; do
        _lsm_escape "${name}" "${state}" offered
        eval "read_back=${before}${offered}${after}" 2> /dev/null
        [[ ${read_back} == "${name}" ]] || \
            fail "${state} quote: $(printf '%q' "${name}") offered as ${offered}," \
                 "which reads back as $(printf '%q' "${read_back}")"
    done
done

# History expansion is on for a word in double quotes, so a ! is not left in
# one: it is closed, quoted in single quotes, and opened again.
dq='"' sq="'"
want="a${dq}${sq}!${sq}${dq}b${dq}${sq}!${sq}${dq}"
_lsm_escape 'a!b!' double offered
[[ ${offered} == "${want}" ]] || fail "! in double quotes offered as ${offered}"
_lsm_escape 'a!b' none offered
[[ ${offered} == 'a\!b' ]] || fail "! without quotes offered as ${offered}"

# The quote a word leaves open, and how much of the value came before it.
# Word | state | characters before
while IFS='|' read -r word state before ; do
    _lsm_unescape "${word}" value got_state got_before
    if [[ ${got_state}/${got_before} != "${state}/${before}" ]] ; then
        fail "'${word}' ends ${got_state}, ${got_before} before, not ${state}, ${before}"
    fi
done <<'TABLE'
abc|none|0
a\ b|none|0
'a b'|none|0
"a b"|none|0
$'a b'|none|0
'a b|single|0
"a b|double|0
$'a b|ansi|0
x'a b|single|1
sys\ "th|double|4
sys\ 'th|single|4
"sys"' th|single|3
'sys'"th|double|3
x$'a|ansi|1
$'a'$'b|ansi|1
a\"b|none|0
TABLE

# A filter flag without a usable value is treated as if it was not there:
# lsmcli is never handed the flag name, which would be taken as its value.
# A missing or empty value counts, and a value that is an option itself is
# one lsmcli refuses the whole line for, so nothing is looked up at all.
# Last word, the one completed, is always empty here.
check_words "" 4 lsmcli x --ag --init ""
check_stub_args no "[--ag]" "--init after a --ag with no value"
check_words "${PAYLOAD_ESC}|init-two" 5 lsmcli x --ag '""' --init ""
check_stub_args no "[--ag]" "--init after an empty --ag value"
# The flag is the last word, being completed itself, so there is nothing after.
check_words "" 3 lsmcli x --init --ag
check_stub_args no "[--ag]" "--ag is the last word"

# --snap with no --fs to filter on offers nothing, and does not ask lsmcli.
check_words "" 4 lsmcli x --fs --snap ""
check_stub_args no "[--fs]" "--snap after a --fs with no value"
check_words "" 5 lsmcli x --fs '""' --snap ""
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

# A quoted or escaped option is still the option, and a quoted unknown one
# is still unknown: every word is read again as the shell would read it
# before it is taken for one or the other.
check_words "${PAYLOAD_ESC}|vol-one" 7 lsmcli '"-u"' sim : //a vd --vol '""'
check_stub_args yes "[-u][sim://a]" "quoted -u"
check_words "${PAYLOAD_ESC}|vol-one" 7 lsmcli '\-u' sim : //a vd --vol '""'
check_stub_args yes "[-u][sim://a]" "escaped -u"
check_words "" 5 lsmcli '"--ur"' x vd --vol '""'
[[ -e ${LSM_TEST_ARGV} ]] && fail "a quoted unknown option ran lsmcli"

# A global option that takes a value does not take the next option for its
# value, which lsmcli refuses the line for, so nothing is looked up. Nor is
# its value a command or alias name, which the shell would expand into one.
check_words "" 8 lsmcli -t -u sim : //a vd --vol '""'
[[ -e ${LSM_TEST_ARGV} ]] && fail "-t taking -u for its value ran lsmcli"
check_words "" 5 lsmcli -t ls vd --vol '""'
[[ -e ${LSM_TEST_ARGV} ]] && fail "-t with a command name for its value ran lsmcli"

# The value of an option of the sub command is not read as a global option:
# --uri there is a name, which lsmcli refuses the line for, so nothing is
# looked up.
check_words "" 9 lsmcli volume-create --name --uri = sim : //other --pool '""'
[[ -e ${LSM_TEST_ARGV} ]] && fail "an option-like value of a sub command option ran lsmcli"
# Nor is such a value a command or alias name, which the shell would expand
# into one.
check_words "" 5 lsmcli volume-create --name ls --pool '""'
[[ -e ${LSM_TEST_ARGV} ]] && fail "a command name for an option value ran lsmcli"

# A NUL in an ANSI-C quoted word ends it, as bash cannot go on past one.
for nul in '\0' '\x00' '\u0000' '\c@' '\400' ; do
    check_words "init-filtered" 5 lsmcli x --ag "\$'a${nul}b'" --init ""
    check_stub_args yes "[--ag][a]" "a NUL written ${nul} in an ANSI-C quoted word"
done

# A filter flag that is quoted is still the filter flag, with its value
# glued on or as the next word, and one given more than once where the last
# cannot be read is not masked by an earlier one that can.
check_words "init-filtered" 5 lsmcli x "'--ag'" ag-one --init ""
check_stub_args yes "[--ag][ag-one]" "quoted --ag"
check_words "init-filtered" 4 lsmcli x "'--ag=ag-one'" --init ""
check_stub_args yes "[--ag][ag-one]" "quoted --ag with its value glued on"
check_end "" lsmcli aa --ag ag-one --ag --init ""
[[ -e ${LSM_TEST_ARGV} ]] && fail "a --ag with an option for its value ran lsmcli"
check_end "" lsmcli aa --ag ag-one --ag=ag-two --init ""
[[ -e ${LSM_TEST_ARGV} ]] && fail "an earlier --ag masked a later unreadable one"

# A word before the cursor that the shell would not read as that word alone,
# an open quote or a hanging backslash, means the line is not what the words
# seem to say.
check_words "" 5 lsmcli x --ag "'ag-one" --init '""'
[[ -e ${LSM_TEST_ARGV} ]] && fail "an open quote before the cursor ran lsmcli"
check_words "" 5 lsmcli x --ag 'ag-one\' --init '""'
[[ -e ${LSM_TEST_ARGV} ]] && fail "a hanging backslash before the cursor ran lsmcli"

# The command itself may be a path with a word break character in it, which
# bash makes more than one of COMP_WORDS of, both for reading the line and
# for running the program.
check_words "${PAYLOAD_ESC}|vol-one" 9 './lsm' ':' cli -u sim : //a vd --vol '""'
check_stub_args yes "[-u][sim://a]" "the command as ./lsm:cli"
check_words "${PAYLOAD_ESC}|vol-one" 9 './lsm' '=' cli -u sim : //a vd --vol '""'
check_stub_args yes "[-u][sim://a]" "the command as ./lsm=cli"

# An iSCSI name or a WWPN has a : in it, which bash breaks the word of, so
# what is being completed is more than one of COMP_WORDS. The whole of it is
# what values are matched with, and what is offered is what follows the last
# :, which is all readline replaces. Their values are in the stub only with
# LSM_TEST_COLON set, as no other test wants them among what is offered.
export LSM_TEST_COLON=1
check_words "iqn.1994-05.com.redhat:abc123" 3 \
    lsmcli access-group-add --init iqn.1994-05.com.redhat
check_words "abc123" 4 lsmcli access-group-add --init iqn.1994-05.com.redhat ':'
check_words "abc123" 5 lsmcli access-group-add --init iqn.1994-05.com.redhat ':' abc1
check_words "" 5 lsmcli access-group-add --init iqn.1994-05.com.redhat ':' zz
check_words "iqn.1994-05.com.redhat:abc123" 3 \
    lsmcli access-group-add --init "'iqn.1994-05.com.redhat'"
unset LSM_TEST_COLON

# What _lsm_complete leaves in COMPREPLY, _lsm hands back as it was, down to
# an entry at the end that is empty, which the command substitution it comes
# through would otherwise strip off with the newlines. _lsm_complete is stood
# in for, as no look-up of values returns an empty one.
_lsm_complete_saved=$(declare -f _lsm_complete)
function _lsm_complete() { COMPREPLY=(one '') ; }
COMP_WORDS=(lsmcli x "") COMP_CWORD=2
COMP_LINE="lsmcli x " COMP_POINT=${#COMP_LINE}
COMPREPLY=()
_lsm
eval "${_lsm_complete_saved}"
unset _lsm_complete_saved
if [[ ${#COMPREPLY[@]} -ne 2 || -n ${COMPREPLY[1]-x} ]] ; then
    fail "an empty entry at the end did not come back: ${#COMPREPLY[@]} entries, last [${COMPREPLY[1]-}]"
fi

# The sub command is the first word that is not a global option or the value of
# one, wherever that is, and the first word, lsmcli, is never looked at. bash
# splits --uri=sim://x into --uri = sim : //x, so the words below are as bash
# gives them, one per column.
#
# Command line                      | word     | expected completions
cmd_tests=(
    "lsmcli -b volume-create|--|--name|--size|--pool|--provisioning"
    "lsmcli -H -b -s volume-create|--n|--name"
    "lsmcli -u sim : // volume-delete||--vol|--force"
    "lsmcli -u sim : //x : 80/y volume-delete||--vol|--force"
    "lsmcli --uri = sim : //h volume-delete||--vol|--force"
    "lsmcli -usim : // volume-delete||--vol|--force"
    "lsmcli -t , volume-delete||--vol|--force"
    "lsmcli -t: volume-delete||--vol|--force"
    "lsmcli --terse = , volume-delete||--vol|--force"
    "lsmcli -w 100 -H volume-delete||--vol|--force"
    "lsmcli --wait = 100 volume-delete||--vol|--force"
    "lsmcli --fork_plugin /x/y -u sim : // volume-delete||--vol|--force"
    "lsmcli volume-delete -u sim : //||--vol|--force"
    "lsmcli -v|access-group-c|access-group-create"
    "lsmcli -u sim : //|access-group-c|access-group-create"
    "lsmcli -H -b|zz|"
    "/usr/bin/lsmcli|volume-d|volume-delete|volume-dependants|volume-dependants-rm|volume-disable"
    "./lsmcli volume-delete||--vol|--force"
    "/usr/bin/lsmcli -u sim : // vd||--vol|--force"
    "lsmcli -H|--ver|--version"
    # Nothing to offer where a value is being typed.
    "lsmcli -u||"
    "lsmcli -u|sim|"
    "lsmcli -u sim :||"
    "lsmcli -u sim :|//|"
    "lsmcli -u sim : //x :||"
    "lsmcli --uri||"
    "lsmcli --uri =||"
    "lsmcli --uri = sim :|//|"
    "lsmcli -t||"
    "lsmcli --terse||"
    "lsmcli -w||"
    "lsmcli --wait||"
    "lsmcli --fork_plugin||"
    "lsmcli -H -u||"
    "lsmcli volume-delete -u||"
    "lsmcli volume-delete --vol x -t||"
    # The list aliases stand for 'list --type', so --type is not offered.
    "lsmcli ls||--sys|--pool|--vol|--disk|--ag|--fs|--nfs-export|--tgt"
    "lsmcli lp --vol x||--sys|--pool|--disk|--ag|--fs|--nfs-export|--tgt"
    "lsmcli -H lv||--sys|--pool|--vol|--disk|--ag|--fs|--nfs-export|--tgt"
    "lsmcli list||--type|--sys|--pool|--vol|--disk|--ag|--fs|--nfs-export|--tgt"
    "lsmcli list --type volumes||--sys|--pool|--vol|--disk|--ag|--fs|--nfs-export|--tgt"
    # SNAPSHOTS needs a file system, whatever the case and wherever the list.
    "lsmcli list --type snapshots||--fs"
    "lsmcli list --type SNAPSHOTS||--fs"
    "lsmcli list --type Snapshots|--f|--fs"
    "lsmcli -H list --type snapshots||--fs"
    "lsmcli -u sim : // -b list --type SNAPSHOTS||--fs"
    "lsmcli x --type snapshots||"
    "lsmcli list snapshots|--ty|--type"
    # A word of the sub command being typed in full is still wanted.
    "lsmcli access-group-add|--ag|--ag"
    # Case matters, whatever the shell's nocasematch is.
    "lsmcli VOLUME-DELETE||"
    "lsmcli x --SYS||"
)
for t in "${cmd_tests[@]}" ; do
    IFS='|' read -r comp_line cur expected_head rest <<< "${t}"
    expected="${expected_head}"
    [[ -n ${rest} ]] && expected="${expected_head}|${rest}"
    check "${comp_line}" "${cur}" "${expected}"
done
# Every sub command, and alias, with what it offers when nothing is on the line
# but the command. These are what cmdline.py has, and test_cmdline_drift.sh
# checks they still are. The commands taking --force have it offered too.
#
# Commands (the first of them the one tested, the rest aliases) | options
option_tests=(
    "list|--type --sys --pool --vol --disk --ag --fs --nfs-export --tgt"
    "ls lp lv ld la lf lt lb|--sys --pool --vol --disk --ag --fs --nfs-export --tgt"
    "job-status|--job"
    "capabilities c volume-raid-create-cap vrcc volume-replicate-range-block-size|--sys"
    "plugin-info p local-disk-list ldl local-led-slot-list|"
    "volume-create vc|--name --size --pool --provisioning"
    "volume-raid-create vrc|--name --disk --raid-type --strip-size"
    "volume-delete vd|--vol --force"
    "volume-resize vr|--vol --size --force"
    "volume-replicate|--vol --name --rep-type --pool"
    "volume-replicate-range|--src-vol --dst-vol --rep-type --src-start --dst-start --count --force"
    "volume-dependants volume-dependants-rm volume-access-group volume-enable volume-disable ve vi volume-raid-info vri volume-cache-info vci volume-ident-led-on vilon volume-ident-led-off viloff|--vol"
    "volume-mask volume-unmask vm vu|--vol --ag"
    "volume-phy-disk-cache-update vpdcu volume-read-cache-policy-update vrcpu volume-write-cache-policy-update vwcpu|--vol --policy"
    "system-read-cache-pct-update srcpu|--sys --read-pct"
    "pool-member-info pmi|--pool"
    "access-group-create ac|--name --init --sys"
    "access-group-add access-group-remove aa ar|--ag --init"
    "access-group-delete ad access-group-volumes|--ag"
    "iscsi-chap|--init --in-user --in-pass --out-user --out-pass"
    "fs-create|--name --size --pool"
    "fs-delete|--fs --force"
    "fs-resize|--fs --size --force"
    "fs-export|--fs --exportpath --anonuid --anongid --auth-type --root-host --ro-host --rw-host"
    "fs-unexport|--export"
    "fs-clone|--src-fs --dst-name --backing-snapshot"
    "fs-snap-create|--name --fs"
    "fs-snap-delete|--snap --fs"
    "fs-snap-restore|--snap --fs --file --fileas --force"
    "fs-dependants fs-dependants-rm|--fs --file"
    "file-clone|--fs --src --dst --backing-snapshot"
    "local-disk-ident-led-on ldilon local-disk-ident-led-off ldiloff local-disk-fault-led-on ldflon local-disk-fault-led-off ldfloff|--path"
    "local-led-slot-set|--slot --state"
)
for t in "${option_tests[@]}" ; do
    IFS='|' read -r option_cmds option_list <<< "${t}"
    expected="${option_list// /|}"
    for option_cmd in ${option_cmds} ; do
        check_end "${expected}" lsmcli "${option_cmd}" ""
    done
done

# Options that can be repeated stay on offer after they are used, the others,
# even ones that are repeatable for some other command, do not.
#
# Command line | word | expected completions
repeat_tests=(
    "lsmcli volume-raid-create --name x --disk d1 --disk d2||--disk|--raid-type|--strip-size"
    "lsmcli vrc --disk d1 --raid-type RAID5 --strip-size 8||--name|--disk"
    "lsmcli volume-replicate-range --src-vol a --dst-vol b --rep-type COPY --src-start 1 --dst-start 2 --count 3||--src-start|--dst-start|--count|--force"
    "lsmcli fs-export --fs f --root-host a --ro-host b --rw-host c --exportpath /x||--anonuid|--anongid|--auth-type|--root-host|--ro-host|--rw-host"
    "lsmcli fs-snap-restore --snap s --fs f --file a --fileas b||--file|--fileas|--force"
    "lsmcli fs-dependants --fs f --file a||--file"
    "lsmcli fs-dependants-rm --fs f --file a --file b||--file"
    "lsmcli list --disk d --sys s||--type|--pool|--vol|--ag|--fs|--nfs-export|--tgt"
    "lsmcli lv --disk d||--sys|--pool|--vol|--ag|--fs|--nfs-export|--tgt"
    "lsmcli volume-create --name a --name b||--size|--pool|--provisioning"
    "lsmcli fs-snap-restore --snap s --fs f --file a --force||--file|--fileas"
    "lsmcli fs-export --fs f --root-host a|--r|--root-host|--ro-host|--rw-host"
)
for t in "${repeat_tests[@]}" ; do
    IFS='|' read -r comp_line cur expected_head rest <<< "${t}"
    expected="${expected_head}"
    [[ -n ${rest} ]] && expected="${expected_head}|${rest}"
    check "${comp_line}" "${cur}" "${expected}"
done

# Values with a fixed set of choices. --rep-type, --policy and the rest depend
# on the sub command, as the choices of cmdline.py do.
#
# Command line | word | expected completions
enum_tests=(
    "lsmcli volume-replicate --rep-type||CLONE|COPY|MIRROR_ASYNC|MIRROR_SYNC"
    "lsmcli volume-replicate --rep-type|M|MIRROR_ASYNC|MIRROR_SYNC"
    "lsmcli volume-replicate-range --rep-type||CLONE|COPY"
    "lsmcli volume-replicate-range --rep-type|M|"
    "lsmcli -u sim : // volume-replicate-range --rep-type||CLONE|COPY"
    "lsmcli volume-phy-disk-cache-update --policy||ENABLE|DISABLE"
    "lsmcli vpdcu --vol v --policy|E|ENABLE"
    "lsmcli volume-read-cache-policy-update --policy||ENABLE|DISABLE"
    "lsmcli vrcpu --policy|D|DISABLE"
    "lsmcli volume-write-cache-policy-update --policy||WB|AUTO|WT"
    "lsmcli vwcpu --policy|W|WB|WT"
    "lsmcli vwcpu --policy|E|"
    "lsmcli vrcpu --policy|W|"
    "lsmcli x --policy||"
    "lsmcli volume-raid-create --raid-type||RAID0|RAID1|RAID5|RAID6|RAID10|RAID50|RAID60"
    "lsmcli vrc --raid-type|RAID1|RAID1|RAID10"
    "lsmcli volume-create --provisioning||DEFAULT|THIN|FULL"
    "lsmcli vc --provisioning|T|THIN"
    "lsmcli local-led-slot-set --state||IDENT_ON|IDENT_OFF|FAULT_ON|FAULT_OFF"
    "lsmcli local-led-slot-set --slot s --state|F|FAULT_ON|FAULT_OFF"
)
for t in "${enum_tests[@]}" ; do
    IFS='|' read -r comp_line cur expected_head rest <<< "${t}"
    expected="${expected_head}"
    [[ -n ${rest} ]] && expected="${expected_head}|${rest}"
    check "${comp_line}" "${cur}" "${expected}"
    # None of these is looked up.
    [[ -e ${LSM_TEST_ARGV} ]] && fail "'${comp_line}' ran lsmcli"
done

# Values that are free form, which nothing can be offered for, with a word
# that matches a file as well as with none.
for flag in --size --count --src-start --dst-start --name --in-user --in-pass \
            --out-user --out-pass --exportpath --anonuid --anongid \
            --root-host --ro-host --rw-host --dst-name --file --fileas --src \
            --dst --job --strip-size --read-pct --path --slot ; do
    for cur in '' 'a' 'a*' '--n*' ; do
        check_end "" lsmcli x "${flag}" "${cur}"
        [[ -e ${LSM_TEST_ARGV} ]] && fail "${flag} ran lsmcli"
    done
done

# --backing-snapshot completes the snapshots of the file system given with
# --fs, or --src-fs for fs-clone, as --snap does, and with no file system there
# is nothing to offer and lsmcli is not run. With look-ups off it is not run
# either.
check_end "${PAYLOAD_ESC}|snap-one" lsmcli file-clone --fs fs-one --backing-snapshot ""
check_stub_args yes "[--fs][fs-one]" "file-clone --backing-snapshot"
check_end "snap-one" lsmcli file-clone --fs 'fs\ one' --backing-snapshot "s"
check_stub_args yes "[--fs][fs one]" "file-clone --backing-snapshot, escaped --fs"
check_end "${PAYLOAD_ESC}|snap-one" lsmcli fs-clone --src-fs fs-one --dst-name n --backing-snapshot ""
check_stub_args yes "[--fs][fs-one]" "fs-clone --backing-snapshot"
check_end "snap-one" lsmcli -u sim : // fs-clone --src-fs fs-one --backing-snapshot "s"
check_stub_args yes "[-u][sim://]" "fs-clone --backing-snapshot"
check_end "" lsmcli file-clone --src a --backing-snapshot ""
[[ -e ${LSM_TEST_ARGV} ]] && fail "--backing-snapshot with no file system ran lsmcli"
check_end "" lsmcli file-clone --fs "" --backing-snapshot ""
[[ -e ${LSM_TEST_ARGV} ]] && fail "--backing-snapshot with an empty --fs ran lsmcli"
check_end "" lsmcli fs-clone --src-fs --backing-snapshot ""
[[ -e ${LSM_TEST_ARGV} ]] && fail "--backing-snapshot with a --src-fs of no value ran lsmcli"
LSMCLI_AUTO_COMPLETE_VALUE=0
check_end "" lsmcli file-clone --fs fs-one --backing-snapshot ""
[[ -e ${LSM_TEST_ARGV} ]] && fail "--backing-snapshot with look-ups off ran lsmcli"
LSMCLI_AUTO_COMPLETE_VALUE=1

# The first word, and the last of a line that is all there is.
check_words "" 0 lsmcli
check_words "" 0 ""

# The URI on the command line is passed on to every look-up, and nothing else
# is: -P would prompt for a password, and the value could be for another array.
check_end "${PAYLOAD_ESC}|vol-one" lsmcli volume-delete --vol ""
check_stub_args no "[-u]" "no URI on the command line"
check_end "${PAYLOAD_ESC}|vol-one" lsmcli -u sim : // volume-delete --vol ""
check_stub_args yes "[-u][sim://]" "-u sim://"
check_stub_args no "[-P]" "-u sim://"
check_end "${PAYLOAD_ESC}|vol-one" lsmcli --uri = sim : //x : 80 volume-delete --vol ""
check_stub_args yes "[-u][sim://x:80]" "--uri=sim://x:80"
check_end "${PAYLOAD_ESC}|vol-one" lsmcli -usim : // volume-delete --vol ""
check_stub_args yes "[-u][sim://]" "-usim://"
check_end "${PAYLOAD_ESC}|vol-one" lsmcli -u '"sim://a b"' volume-delete --vol ""
check_stub_args yes "[-u][sim://a b]" "a quoted URI"
check_end "${PAYLOAD_ESC}|vol-one" lsmcli -u 'sim://a\ b' volume-delete --vol ""
check_stub_args yes "[-u][sim://a b]" "an escaped URI"
check_end "${PAYLOAD_ESC}|vol-one" lsmcli volume-delete -u sim : // --vol ""
check_stub_args yes "[-u][sim://]" "-u after the sub command"
check_end "${PAYLOAD_ESC}|vol-one" lsmcli -u a : // -u b : // volume-delete --vol ""
check_stub_args yes "[-u][b://]" "the last -u"
check_end "${PAYLOAD_ESC}|vol-one" lsmcli -t , volume-delete --vol ""
check_stub_args no "[-u]" "-t on the command line"
check_end "init-filtered" lsmcli -u sim : // access-group-add --ag ag-one --init ""
check_stub_args yes "[-u][sim://]" "--init filtered by --ag"
check_stub_args yes "[--ag][ag-one]" "--init filtered by --ag"
# The line says where white space was, which the words do not. Here that is
# -u sim: and volume-delete is a word of its own, so the URI is sim:
test_line='lsmcli -u sim: volume-delete --vol '
check_words "${PAYLOAD_ESC}|vol-one" 6 lsmcli -u sim : volume-delete --vol ""
check_stub_args yes "[-u][sim:]" "a URI that ends in a :"
# The same words as sim:volume-delete, which is a URI of its own, and what is
# on the line does not say sim:
test_line='lsmcli -u sim:volume-delete vd --vol '
check_words "${PAYLOAD_ESC}|vol-one" 7 lsmcli -u sim : volume-delete vd --vol ""
check_stub_args yes "[-u][sim:volume-delete]" "a URI with a : in it"
test_line='lsmcli -u sim:// volume-delete --vol '
check_end "${PAYLOAD_ESC}|vol-one" lsmcli -u sim : // volume-delete --vol ""
check_stub_args yes "[-u][sim://]" "a URI that is in the line as typed"
test_line='lsmcli -u "" volume-delete --vol '
check_end "" lsmcli -u '""' volume-delete --vol ""
[[ -e ${LSM_TEST_ARGV} ]] && fail "lsmcli run with an empty URI"
unset test_line

# The URI is the one on the line however it is given, and whatever the line
# has after the cursor, as global options are accepted there too. Where it
# cannot be said for certain there is no look-up. Word by word as bash gives
# them, where = and : are not in COMP_WORDBREAKS the whole is one word.
check_end "${PAYLOAD_ESC}|vol-one" lsmcli --uri=sim://x vd --vol ""
check_stub_args yes "[-u][sim://x]" "--uri=sim://x as one word"
check_end "${PAYLOAD_ESC}|vol-one" lsmcli -t, --uri=sim://x vd --vol ""
check_stub_args yes "[-u][sim://x]" "--terse=, before --uri=sim://x"
check_end "${PAYLOAD_ESC}|vol-one" lsmcli vd --terse=, --vol x --force --vol ""
check_stub_args no "[-u]" "the options of the sub command"
# An option that is not known could be --uri, abbreviated or not.
for unknown in --ur=sim://x --x=y --ur --u --zz ; do
    check_end "" lsmcli "${unknown}" vd --vol ""
    [[ -e ${LSM_TEST_ARGV} ]] && fail "${unknown} before the sub command, but lsmcli was run"
    check_end "" lsmcli vd "${unknown}" sim://x --vol ""
    [[ -e ${LSM_TEST_ARGV} ]] && fail "${unknown} after the sub command, but lsmcli was run"
done
# A later -u makes up for what is before it, and not for what is after it.
check_end "${PAYLOAD_ESC}|vol-one" lsmcli --zz -u sim : // vd --vol ""
check_stub_args yes "[-u][sim://]" "-u after an option that is not known"
# --ur could take the next word as its value, and be the URI.
check_end "" lsmcli --ur x -u sim : // vd --vol ""
[[ -e ${LSM_TEST_ARGV} ]] && fail "--ur x before a -u, but lsmcli was run"
check_end "" lsmcli -u sim : // --ur x vd --vol ""
[[ -e ${LSM_TEST_ARGV} ]] && fail "--ur after -u, but lsmcli was run"
# The cursor is in the middle of the line, with -u after it.
test_line='lsmcli vd --vol  -u sim://x'
test_point=16
check_words "${PAYLOAD_ESC}|vol-one" 3 lsmcli vd --vol "" -u sim : //x
check_stub_args yes "[-u][sim://x]" "-u after the cursor"
check_words "${PAYLOAD_ESC}|vol-one" 3 lsmcli vd --vol "" -u sim : //x
test_line='lsmcli -u a:b vd --vol  -u c'
test_point=23
check_words "${PAYLOAD_ESC}|vol-one" 7 lsmcli -u a : b vd --vol "" -u c
check_stub_args yes "[-u][a:b]" "the -u before the sub command, not the one after"
test_line='lsmcli vd --vol  --uri=sim://y'
test_point=16
check_words "${PAYLOAD_ESC}|vol-one" 3 lsmcli vd --vol "" --uri = sim : //y
check_stub_args yes "[-u][sim://y]" "--uri= after the cursor"
test_line='lsmcli vd --vol  -u'
test_point=16
check_words "" 3 lsmcli vd --vol "" -u
[[ -e ${LSM_TEST_ARGV} ]] && fail "-u with no value after the cursor, but lsmcli was run"
unset test_line test_point

# Which -u lsmcli uses: cmdline.py copies the one after the sub command over
# only if there was none before it, so the last one before the sub command
# wins, and failing that the last one after it. Checked with argparse:
#   -u a vd --vol x -u b   uses a        vd -u a -u b   uses b
# Where the one that wins cannot be told there is no look-up, and the one that
# loses does not matter.
# Words, @ being where the cursor is, with an empty word | URI used, or - for
# no look-up.
uri_precedence_tests=(
    "-u sim://a vd --vol @ -u sim://b|sim://a"
    "vd -u sim://a -u sim://b --vol @|sim://b"
    "-u sim://a -u sim://b vd --vol @|sim://b"
    "vd -u sim://a --vol @|sim://a"
    "-u sim://a vd --vol @|sim://a"
    "-u sim://a vd -u sim://b --vol @ -u sim://c|sim://a"
    "vd --vol @ -u sim://b -u sim://c|sim://c"
    "vd --vol @ -u sim://b --uri sim://c|sim://c"
    "-u a: vd --vol @|a:"
    "-u '' vd -u sim://b --vol @|-"
    "-u sim://\$X vd -u sim://b --vol @|-"
    "-u sim://a vd -u '' --vol @|sim://a"
    "vd -u '' -u sim://b --vol @|sim://b"
    "vd -u sim://b -u '' --vol @|-"
    "-u sim://a --ur x vd --vol @|-"
    "--ur x vd -u sim://b --vol @|-"
    "vd --ur x -u sim://b --vol @|sim://b"
    "vd -u sim://b --ur x --vol @|-"
    "-u sim://a vd --ur x --vol @|sim://a"
)
for t in "${uri_precedence_tests[@]}" ; do
    IFS='|' read -r words used <<< "${t}"
    read -r -a line_words <<< "lsmcli ${words}"
    cword=0
    for (( n = 0; n < ${#line_words[@]}; n++ )) ; do
        if [[ ${line_words[n]} == @ ]] ; then
            line_words[n]=''
            cword=${n}
        fi
    done
    if [[ ${used} == - ]] ; then
        check_words "" "${cword}" "${line_words[@]}"
        [[ -e ${LSM_TEST_ARGV} ]] && fail "'${words}' ran lsmcli, the URI is not certain"
    else
        check_words "${PAYLOAD_ESC}|vol-one" "${cword}" "${line_words[@]}"
        check_stub_args yes "[-u][${used}]" "'${words}'"
        if grep -q -- '\[-u\].*\[-u\]' "${LSM_TEST_ARGV}" ; then
            fail "'${words}' passed -u more than once"
        fi
    fi
done

# The word under the cursor is on the line, unless it is empty, and it can be
# a -u: bash gives the whole word as the cursor word whatever the cursor is
# in. These are as recorded from an interactive bash for the cursor at the
# start, inside and at the end of the word. What lsmcli would make of such
# a line is not known, so there is no look-up.
# COMP_WORDS (a space between words) | COMP_CWORD | COMP_POINT | COMP_LINE
recorded_tests=(
    "lsmcli vd --vol -u sim : //a|3|16|lsmcli vd --vol -u sim://a"
    "lsmcli vd --vol -u sim : //a|3|17|lsmcli vd --vol -u sim://a"
    "lsmcli vd --vol -u sim : //a|3|18|lsmcli vd --vol -u sim://a"
    "lsmcli vd --vol --uri sim : //a|3|17|lsmcli vd --vol --uri sim://a"
    "lsmcli vd --vol -u|3|16|lsmcli vd --vol -u"
    "lsmcli vd --vol -u|3|17|lsmcli vd --vol -u"
    "lsmcli vd --vol -u|3|18|lsmcli vd --vol -u"
)
for t in "${recorded_tests[@]}" ; do
    IFS='|' read -r words cword point line <<< "${t}"
    read -r -a line_words <<< "${words}"
    test_line="${line}"
    test_point="${point}"
    check_words "" "${cword}" "${line_words[@]}"
    [[ -e ${LSM_TEST_ARGV} ]] && fail "'${line}' with the cursor at ${point} ran lsmcli"
done
unset test_line test_point

# -u=x is x or =x depending on the argparse of the Python, so it is not
# certain, while -u =x as two words is =x everywhere.
check_end "" lsmcli -u = sim : //x vd --vol ""
[[ -e ${LSM_TEST_ARGV} ]] && fail "-u=sim://x ran lsmcli"
test_line='lsmcli -u =sim://x vd --vol '
check_end "${PAYLOAD_ESC}|vol-one" lsmcli -u =sim : //x vd --vol ""
check_stub_args yes "[-u][=sim://x]" "-u =sim://x"
unset test_line
check_end "${PAYLOAD_ESC}|vol-one" lsmcli --uri = sim : //x vd --vol ""
check_stub_args yes "[-u][sim://x]" "--uri=sim://x"

# What the shell would expand is not what is written, so the URI, or a filter
# value, that has any of it is not known. Nothing is expanded, evaluated or
# run to find that out. Typed word | what it is about
expansion_tests=(
    'sim://$S'
    '"$URI"'
    '$URI'
    '~/x'
    'sim://{a,b}'
    'sim://a*'
    'sim://a?'
    'sim://[ab]'
    '"$(touch ${LSM_TEST_MARKER})"'
    '$(touch ${LSM_TEST_MARKER})'
    '`touch ${LSM_TEST_MARKER}`'
    '"`touch ${LSM_TEST_MARKER}`"'
    'sim://a!b'
    '"sim://a!b"'
    '"a$"'
)
for typed in "${expansion_tests[@]}" ; do
    check_end "" lsmcli -u "${typed}" vd --vol ""
    [[ -e ${LSM_TEST_ARGV} ]] && fail "-u ${typed} ran lsmcli"
    check_end "" lsmcli aa --ag "${typed}" --init ""
    [[ -e ${LSM_TEST_ARGV} ]] && fail "--ag ${typed} ran lsmcli"
    check_end "" lsmcli fs-snap-delete --fs "${typed}" --snap ""
    [[ -e ${LSM_TEST_ARGV} ]] && fail "--fs ${typed} ran lsmcli"
done
# Protected from the shell, they are what they say.
quoted_tests=(
    "'sim://\$S'|sim://\$S"
    "sim://\\\$S|sim://\$S"
    "\$'sim://a*'|sim://a*"
    "'~/x'|~/x"
    "\\~/x|~/x"
    "sim://a~|sim://a~"
    "'sim://{a,b}'|sim://{a,b}"
    "\"sim://a*\"|sim://a*"
    "\"sim://\\\$S\"|sim://\$S"
    "sim://a\\!b|sim://a!b"
    "'sim://a!b'|sim://a!b"
)
for t in "${quoted_tests[@]}" ; do
    IFS='|' read -r typed meant <<< "${t}"
    check_end "${PAYLOAD_ESC}|vol-one" lsmcli -u "${typed}" vd --vol ""
    check_stub_args yes "[-u][${meant}]" "-u ${typed}"
    check_end "init-filtered" lsmcli aa --ag "${typed}" --init ""
    check_stub_args yes "[--ag][${meant}]" "--ag ${typed}"
done
# A URI that is a command or an alias is replaced by what it stands for, by
# lsmcli, before it reads the options.
check_end "" lsmcli -u ls vd --vol ""
[[ -e ${LSM_TEST_ARGV} ]] && fail "-u ls ran lsmcli"
# What is typed so far is matched as it was, with a $ in it.
check_end "${PAYLOAD_ESC}" lsmcli capabilities --sys '\$('
# What is typed, not just the words, says where there was white space. This
# line has -u a, and the b is another word, which is what a substring of the
# line would not tell.
test_line='lsmcli -u a : b vd --name a:b --vol '
check_end "${PAYLOAD_ESC}|vol-one" lsmcli -u a : b vd --name a : b --vol ""
check_stub_args yes "[-u][a]" "-u a : b"
unset test_line
# Words that are not in the line say nothing is known, and then there is no
# look-up: not for the URI that may be on it, and not for the default one,
# which an assignment before the command, if bash shows it, may be for.
test_line='something else'
check_end "" lsmcli vd --vol ""
[[ -e ${LSM_TEST_ARGV} ]] && fail "layout not known, but lsmcli was run"
check_end "" lsmcli -u sim vd --vol ""
[[ -e ${LSM_TEST_ARGV} ]] && fail "-u with the layout not known, but lsmcli was run"
test_line='LSMCLI_URI=sim://other lsmcli vd --vol '
check_end "" lsmcli vd --vol ""
[[ -e ${LSM_TEST_ARGV} ]] && fail "text before the command, but lsmcli was run"
unset test_line

# Words that the shell expands, or that start a comment, can be an option, or
# take one off the line, so a line with one is not known and there is no
# look-up, whatever the cursor is after. The same quoted, or with the sign
# inside a word, is fine. O is '-u sim://z' in the shell that was asked.
# Words, @ for the cursor | whether there is a look-up
expansion_line_tests=(
    'vd $O --vol @|-'
    '$O vd --vol @|-'
    '${O} vd --vol @|-'
    '$P$Q vd --vol @|-'
    '`echo vd --vol @|-'
    '{-u,sim : //z} vd --vol @|-'
    '$O aa --ag a --init @|-'
    'vd a* --vol @|-'
    'vd a?b --vol @|-'
    'vd [ab] --vol @|-'
    'vd ~ --vol @|-'
    'vd !x --vol @|-'
    'vd @(a|b) --vol @|-'
    "vd '\$O' --vol @|yes"
    'vd "$O" --vol @|-'
    "vd 'a*' --vol @|yes"
    'vd a\*b --vol @|yes'
    'vd a~ --vol @|yes'
    'vd a\$O --vol @|yes'
    'vd # -u sim : //b --vol @|-'
    '# -u sim : //b vd --vol @|-'
    'vd -u sim : //a#b --vol @|yes'
    'vd a#b --vol @|yes'
)
for t in "${expansion_line_tests[@]}" ; do
    IFS='|' read -r words used <<< "${t}"
    read -r -a line_words <<< "lsmcli ${words}"
    cword=0
    for (( n = 0; n < ${#line_words[@]}; n++ )) ; do
        if [[ ${line_words[n]} == @ ]] ; then
            line_words[n]=''
            cword=${n}
        fi
    done
    if [[ ${used} == yes ]] ; then
        check_words "${PAYLOAD_ESC}|vol-one" "${cword}" "${line_words[@]}"
    else
        check_words "" "${cword}" "${line_words[@]}"
        [[ -e ${LSM_TEST_ARGV} ]] && fail "'${words}' ran lsmcli"
    fi
done

# A comment, as recorded from bash: # stays a word, and the cursor word is the
# # itself when the cursor is at it. A word with the # inside is one word.
# COMP_WORDS | COMP_CWORD | COMP_POINT | COMP_LINE | look-up
comment_tests=(
    "lsmcli vd # -u sim : //b --vol @|8|29|lsmcli vd # -u sim://b --vol |no"
    "lsmcli vd --vol # -u sim : //b|3|17|lsmcli vd --vol  # -u sim://b|no"
    "lsmcli # -u sim : //b vd --vol @|8|29|lsmcli # -u sim://b vd --vol |no"
    "lsmcli vd --vol #x|3|16|lsmcli vd --vol #x|no"
    "lsmcli vd --vol \${O}|3|16|lsmcli vd --vol \${O}|no"
    "lsmcli vd --vol \${O}|3|18|lsmcli vd --vol \${O}|no"
    "lsmcli vd --vol \${O}|3|20|lsmcli vd --vol \${O}|yes-end"
    "lsmcli vd --vol \$P\$Q|3|16|lsmcli vd --vol \$P\$Q|no"
    "lsmcli vd -u sim : //a#b --vol @|7|29|lsmcli vd -u sim://a#b --vol |yes"
)
for t in "${comment_tests[@]}" ; do
    IFS='|' read -r words cword point line used <<< "${t}"
    read -r -a line_words <<< "${words}"
    for (( n = 0; n < ${#line_words[@]}; n++ )) ; do
        [[ ${line_words[n]} == @ ]] && line_words[n]=''
    done
    test_line="${line}"
    test_point="${point}"
    if [[ ${used} == yes-end ]] ; then
        # At the end of the word it is what is typed, and is replaced.
        check_words "" "${cword}" "${line_words[@]}"
    elif [[ ${used} == yes ]] ; then
        check_words "${PAYLOAD_ESC}|vol-one" "${cword}" "${line_words[@]}"
        check_stub_args yes "[-u][sim://a#b]" "${line}"
    else
        check_words "" "${cword}" "${line_words[@]}"
        [[ -e ${LSM_TEST_ARGV} ]] && fail "'${line}' ran lsmcli"
    fi
done
unset test_line test_point

# The program that is run is the one that was typed, and when that is an alias
# or a function nothing is run, as what it adds to the line is not known, eg.
# a -u of its own, and with it the array the line is for.
check_words "${PAYLOAD_ESC}|vol-one" 3 ./lsmcli vd --vol ""
[[ $(cat "${LSM_TEST_PROG}") == ./lsmcli ]] || fail "./lsmcli ran $(cat "${LSM_TEST_PROG}")"
check_words "${PAYLOAD_ESC}|vol-one" 3 lsmcli vd --vol ""
[[ $(cat "${LSM_TEST_PROG}") == "${tmp_dir}/lsmcli" ]] || fail "lsmcli ran $(cat "${LSM_TEST_PROG}")"
check_words "" 3 /nonexistent/lsmcli vd --vol ""
alias lsmcli='lsmcli -u sim://ALIASED'
check_words "" 3 lsmcli vd --vol ""
[[ -e ${LSM_TEST_ARGV} ]] && fail "lsmcli is an alias, but lsmcli was run"
check_words "${PAYLOAD_ESC}|vol-one" 3 ./lsmcli vd --vol ""
unalias lsmcli
lsmcli() { echo "a function of the user"; }
check_words "" 3 lsmcli vd --vol ""
[[ -e ${LSM_TEST_ARGV} ]] && fail "lsmcli is a function, but lsmcli was run"
unset -f lsmcli

# What is not a value still completes on a line like that, as well as it can:
# the sub command is the first word that is not an option, so after a
# redirection that is the file, but one after the sub command is fine.
# Words | word | expected, with the line as it was typed
operator_option_tests=(
    "lsmcli -u sim : //a > out vd|--v|--vol"
    "lsmcli -u a > o fs-export|--anon|--anonuid|--anongid"
    "lsmcli volume-replicate-range -u sim : //a > out --rep-type||CLONE|COPY"
    "lsmcli vpdcu -u a > o --policy||ENABLE|DISABLE"
    "lsmcli vwcpu 2 > /dev/null --policy||WB|AUTO|WT"
)
for t in "${operator_option_tests[@]}" ; do
    IFS='|' read -r comp_line cur expected_head rest <<< "${t}"
    expected="${expected_head}"
    [[ -n ${rest} ]] && expected="${expected_head}|${rest}"
    check "${comp_line}" "${cur}" "${expected}"
done

# What the line looks like to bash for the commands that come before, which
# is nothing: bash takes those and the variable assignments off. Recorded.
test_line='lsmcli -u sim://a vd --vol '
check_words "${PAYLOAD_ESC}|vol-one" 7 lsmcli -u sim : //a vd --vol ""
check_stub_args yes "[-u][sim://a]" "after cd /x &&"
unset test_line

# Short options in one word, as argparse reads them: the ones without a value
# and then one with, whose value is the rest of the word or the next. Lines as
# recorded from bash, and the URI that lsmcli would use.
# Words | URI used, or - for no look-up
cluster_tests=(
    "-Hbu sim://a vd --vol @|sim://a"
    "-Hbusim : //a vd --vol @|sim://a"
    "-Hbusim://a vd --vol @|sim://a"
    "vd -Hbu sim://a --vol @|sim://a"
    "-Hbt , -u sim://a vd -u sim://b --vol @|sim://a"
    "-Hbw 5 -u sim://a vd -u sim://b --vol @|sim://a"
    "-Hbt, -u sim://a vd -u sim://b --vol @|sim://a"
    "-Hb -u sim://a vd --vol @|sim://a"
    "-uH vd --vol @|H"
    "-Hbu vd --vol @|-"
    "-Hbx -u sim://a vd --vol @|sim://a"
    "-u sim://a -Hbx vd --vol @|-"
    "-u sim://a vd --vol @ -vH|sim://a"
    "vd --vol @ -u sim://a -vH|-"
    "vd --vol @ -vH|-"
    "-Hb vd --vol @|none"
    "-H -b -s vd --vol @|none"
)
for t in "${cluster_tests[@]}" ; do
    IFS='|' read -r words used <<< "${t}"
    read -r -a line_words <<< "lsmcli ${words}"
    # The words as bash gives them, split at : and =, where they were given
    # as one in the table with the spaces.
    cword=0
    for (( n = 0; n < ${#line_words[@]}; n++ )) ; do
        if [[ ${line_words[n]} == @ ]] ; then
            line_words[n]=''
            cword=${n}
        fi
    done
    if [[ ${used} == - ]] ; then
        check_words "" "${cword}" "${line_words[@]}"
        [[ -e ${LSM_TEST_ARGV} ]] && fail "'${words}' ran lsmcli, the URI is not certain"
    else
        check_words "${PAYLOAD_ESC}|vol-one" "${cword}" "${line_words[@]}"
        if [[ ${used} == none ]] ; then
            check_stub_args no "[-u]" "'${words}'"
        else
            check_stub_args yes "[-u][${used}]" "'${words}'"
        fi
    fi
done

# Operators of the shell. bash splits the words there, so what follows is a
# word on its own, and a redirection takes the next word, which is not an
# argument. As recorded, all of them have no look-up, of values; the options
# still complete.
# COMP_WORDS (a space between words) | COMP_CWORD | COMP_LINE
operator_tests=(
    "lsmcli vd -u sim : //a > out --vol @|8|lsmcli vd -u sim://a>out --vol "
    "lsmcli vd -u sim : //a < in --vol @|8|lsmcli vd -u sim://a<in --vol "
    "lsmcli vd -u sim : //a) --vol @|6|lsmcli vd -u sim://a) --vol "
    "lsmcli vd -u <( echo a) --vol @|6|lsmcli vd -u <(echo a) --vol "
    "lsmcli > out -u sim : //a vd -u sim://b --vol @|9|lsmcli >out -u sim://a vd -u sim://b --vol "
    "lsmcli 2 > /dev/null -u a vd -u b --vol @|9|lsmcli 2>/dev/null -u a vd -u b --vol "
    "lsmcli aa --ag a > out --init @|6|lsmcli aa --ag a>out --init "
    "lsmcli vd --vol x | cat --vol @|6|lsmcli vd --vol x | cat --vol "
    "lsmcli vd --vol x & --vol @|5|lsmcli vd --vol x & --vol "
    "lsmcli vd --vol x ; --vol @|5|lsmcli vd --vol x ; --vol "
    "lsmcli -u @( sim://a) vd --vol @|6|lsmcli -u @(sim://a) vd --vol "
    "lsmcli -u +( sim://a) vd --vol @|6|lsmcli -u +(sim://a) vd --vol "
    "lsmcli -u !( sim://a) vd --vol @|6|lsmcli -u !(sim://a) vd --vol "
)
for t in "${operator_tests[@]}" ; do
    IFS='|' read -r words cword line <<< "${t}"
    read -r -a line_words <<< "${words}"
    for (( n = 0; n < ${#line_words[@]}; n++ )) ; do
        [[ ${line_words[n]} == @ ]] && line_words[n]=''
    done
    # What bash gives for ( glued to a word is the same words, see the
    # recorded: <( echo is [<(][echo]. Only the shape matters here.
    test_line="${line}"
    check_words "" "$(( ${#line_words[@]} - 1 ))" "${line_words[@]}"
    [[ -e ${LSM_TEST_ARGV} ]] && fail "'${line}' ran lsmcli"
done
unset test_line
# The options and the enums are still completed on such a line.
test_line='lsmcli vd -u sim://a>out '
check_words "--vol|--force" 8 lsmcli vd -u sim : //a '>' out ""
unset test_line

# Filters read from the line are put back together too: bash splits foo:bar
# into three words and --ag=foo into three. A value that cannot be told is not
# left out, as that would be the wrong list. Given more than once the last
# counts.
check_end "init-filtered" lsmcli aa --ag foo : bar --init ""
check_stub_args yes "[--ag][foo:bar]" "--ag foo:bar"
check_end "init-filtered" lsmcli aa --ag = foo --init ""
check_stub_args yes "[--ag][foo]" "--ag=foo"
check_end "init-filtered" lsmcli aa --ag a --ag b --init ""
check_stub_args yes "[--ag][b]" "--ag twice"
check_end "${PAYLOAD_ESC}|snap-one" lsmcli fs-snap-delete --fs a = b --snap ""
check_stub_args yes "[--fs][a=b]" "--fs a=b"
check_end "${PAYLOAD_ESC}|snap-one" lsmcli fs-clone --src-fs 'a\:b' --dst-name n --backing-snapshot ""
check_stub_args yes "[--fs][a:b]" "--src-fs a\\:b"
check_end "" lsmcli aa --ag=foo --init ""
[[ -e ${LSM_TEST_ARGV} ]] && fail "--init with --ag=foo as one word ran lsmcli"
check_end "" lsmcli fs-snap-delete --fs=foo --snap ""
[[ -e ${LSM_TEST_ARGV} ]] && fail "--snap with --fs=foo as one word ran lsmcli"
test_line='lsmcli aa --ag foo --init '
check_end "init-filtered" lsmcli aa --ag foo --init ""
unset test_line
test_line='nothing'
check_end "" lsmcli aa --ag foo --init ""
[[ -e ${LSM_TEST_ARGV} ]] && fail "--init with the layout not known ran lsmcli"
unset test_line
# Text on the line after the last of the words, which no word accounts for,
# means the same.
test_line='lsmcli aa --ag foo --init x'
check_end "" lsmcli aa --ag foo --init ""
[[ -e ${LSM_TEST_ARGV} ]] && fail "text after the last word ran lsmcli"
unset test_line

# More words than an exit status can count, with --ag at a position that a
# status truncated modulo 256 would get wrong, or confuse with 'not found'.
for ag_at in 100 254 255 256 257 260 ; do
    words=()
    for (( n = 0; n < 300; n++ )) ; do words+=("w${n}") ; done
    words[0]=lsmcli
    words[1]=x
    words[ag_at]="--ag"
    words[ag_at+1]="ag-at-${ag_at}"
    words[298]="--init"
    words[299]=""
    check_words "init-filtered" 299 "${words[@]}"
    check_stub_args yes "[--ag][ag-at-${ag_at}]" "--ag at ${ag_at} of 300 words"
done

# _lsm_join keeps a leading -n or -e, which echo would take for an option.
# Data: separator|items, '/' separated
for t in ' |-n/x' ' |-e/-E/y' ',|a/b/c' ' |one' ; do
    IFS='|' read -r sep items <<< "${t}"
    IFS=/ read -r -a join_items <<< "${items}"
    expected_join="${join_items[*]}"
    [[ ${sep} == ' ' ]] || expected_join="${items//\//${sep}}"
    actual_join=$(_lsm_join "${sep}" "${join_items[@]}")
    [[ ${actual_join} == "${expected_join}" ]] || \
        fail "_lsm_join '${sep}' '${items}' gave [${actual_join}] not [${expected_join}]"
done

# An entry with a glob character in the possible arguments stays as it is, even
# with files that match it. Data: possible|words on the line|left
for t in '--a* --b?||--a* --b?' '--a* --b?|--b?|--a*' '--n* --size|--size|--n*' ; do
    IFS='|' read -r possible words left <<< "${t}"
    read -r -a line_words <<< "${words}"
    COMP_WORDS=(lsmcli x ${line_words[@]+"${line_words[@]}"})
    _lsm_possible_args "${possible}"
    [[ ${_lsm_potential_args} == "${left}" ]] || \
        fail "_lsm_possible_args '${possible}' with '${words}' gave [${_lsm_potential_args}] not [${left}]"
done
unset _lsm_potential_args

# What the word \$'...' stands for has to be what bash itself makes of it, so
# compare against bash for the bodies below. Only fixed test data is eval'd.
ansi_bodies=('\cA' '\ca' '\cz' '\c?' '\c@' '\c[' '\c\\' '\c\\\\' '\c1' '\c ' 'a\cAb' '\c'
             'x\c' '\u00e9' '\u20ac' '\U0001F600' '\u' '\uZ' '\u12345' '\U1' '\ux' '\u41b'
             '\U000041' '\u0' '\x41' '\x4' '\x' '\101' '\0' '\18' '\n\t' "\\'" '\q' '\')
for body in "${ansi_bodies[@]}" ; do
    _lsm_unescape "\$'${body}'" mine
    eval "real=\$'${body}'" 2> /dev/null || continue
    [[ ${mine} == "${real}" ]] || \
        fail "\$'${body}' read as $(printf '%q' "${mine}"), bash reads $(printf '%q' "${real}")"
done

# The completion is done in a subshell, so how the user has set up the shell
# neither changes it nor is changed by it: a word splitting IFS, and
# nocasematch, which is still on afterwards, as it would be if the completion
# was interrupted. And no match is no entries, not one empty entry.
COMP_WORDS=(lsmcli volume-create "")
COMP_CWORD=2
COMP_LINE="lsmcli volume-create "
COMP_POINT=${#COMP_LINE}
IFS=a
shopt -s nocasematch
_lsm
shopt -q nocasematch || fail "nocasematch was turned off by the completion"
shopt -u nocasematch
IFS=$' \t\n'
[[ ${COMPREPLY[*]} == '--name --size --pool --provisioning' && ${#COMPREPLY[@]} -eq 4 ]] || \
    fail "with IFS=a the completion was [${COMPREPLY[*]}]"
COMP_WORDS=(lsmcli volume-create "zz")
COMP_CWORD=2
COMP_LINE="lsmcli volume-create zz"
COMP_POINT=${#COMP_LINE}
COMPREPLY=(stale)
_lsm
[[ ${#COMPREPLY[@]} -eq 0 ]] || fail "no match left ${#COMPREPLY[@]} entries"

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
    COMP_WORDS=() COMP_CWORD=0 COMPREPLY=() COMP_LINE= COMP_POINT= words=() line= before_vars= before_funcs=
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
        COMP_LINE="${line} " COMP_POINT=$(( ${#line} + 1 ))
        COMP_CWORD=$(( ${#COMP_WORDS[@]} - 1 ))
        _lsm
    done
    echo "vars: $(comm -13 <(echo "${before_vars}") <(compgen -v | sort) \
                  | tr "\n" " ")"
    echo "funcs: $(comm -13 <(echo "${before_funcs}") \
                   <(compgen -A function | sort) | tr "\n" " ")"' \
    _ "${completion_script}" 2>&1)
printf -v expected_leaked '%s\n%s' 'vars: ' "funcs: ${lsm_funcs} "
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
        COMP_CWORD=3 COMP_LINE="lsmcli capabilities --sys " COMP_POINT=26
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
    COMP_CWORD=3 COMP_LINE="lsmcli capabilities --sys " COMP_POINT=26
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
        COMP_LINE="${words} " COMP_POINT=$(( ${#words} + 1 ))
        COMP_CWORD=$(( ${#COMP_WORDS[@]} - 1 ))
        _lsm
    done
    echo "join=$(join)"
    echo "vars=${i} ${word} ${count} ${sep} ${index} ${value} ${item} ${plain} ${potential_args} ${cur} ${prev}"
    declare -F | sed "s/^declare -f //" | LC_ALL=C sort | tr "\n" " "' _ "${completion_script}")
expected_leaks="join=user-join
vars=I W C S X V T P PA C2 P2
${lsm_funcs} join "
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
expected_readonly=$'sys-one\ninit-filtered\n--pool --provisioning'
for name in "${read_only_names[@]}" ; do
    actual=$(env LSMCLI_AUTO_COMPLETE_VALUE=1 bash --noprofile --norc -c '
        readonly "$2"
        source "$1"
        COMP_WORDS=(lsmcli capabilities --sys "sys-o") COMP_CWORD=3
        COMP_LINE="lsmcli capabilities --sys sys-o" COMP_POINT=31
        _lsm
        echo "${COMPREPLY[*]}"
        COMP_WORDS=(lsmcli x --ag ag-one --init "") COMP_CWORD=5
        COMP_LINE="lsmcli x --ag ag-one --init " COMP_POINT=28
        _lsm
        echo "${COMPREPLY[*]}"
        COMP_WORDS=(lsmcli volume-create "--p") COMP_CWORD=2
        COMP_LINE="lsmcli volume-create --p" COMP_POINT=24
        _lsm
        echo "${COMPREPLY[*]}"' _ "${completion_script}" "${name}" 2>&1)
    if [[ ${actual} != "${expected_readonly}" ]] ; then
        fail "completion with readonly ${name}: ${actual}"
    fi
done

# The whole of it again with set -e on for the completion, and the options that
# change how patterns match: a completion that stops under set -e, or reads
# $'\Ay' as a bell, is a bug in the user's shell.
if [[ -z ${strict} ]] ; then
    strict_out=$(LSM_TEST_STRICT=1 srcdir="$(dirname "${completion_script}")" bash "${self}" 2>&1)
    strict_rc=$?
    if [[ ${strict_rc} -ne 0 ]] ; then
        fail "under set -e and nocasematch, extglob and nocaseglob (rc ${strict_rc}):"
        printf '%s\n' "${strict_out}" | tail -n 20
    fi
fi

if [[ ${failures} -ne 0 ]] ; then
    echo "${failures} completion test(s) failed"
    exit 1
fi

echo "All completion tests passed"
exit 0
