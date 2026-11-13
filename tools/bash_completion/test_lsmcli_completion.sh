#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-only
#
# Copyright (C) 2026 Red Hat, Inc.,  Tony Asleson <tasleson@redhat.com>
#
# Tests for the lsmcli bash completion script.  Runs _lsm directly with a
# fake lsmcli on PATH, so no daemon or simulator is needed.  The fake
# returns hostile values (command substitutions, glob characters, words
# starting with '-') to check they are neither executed nor offered as
# option names.
#
# Runs with 'set -u': the completion file is sourced into the user's
# interactive shell, where 'set -u' may be in effect and every completion
# would fail with "$3: unbound variable" otherwise.

set -u

script_dir=$( cd "$(dirname "${BASH_SOURCE[0]}")" && pwd ) || exit 1
srcdir=$( cd "${srcdir:-${script_dir}}" && pwd ) || exit 1

failures=0
tmpdir=$(mktemp -d) || exit 1
trap 'rm -rf "${tmpdir}"' EXIT

args_log="${tmpdir}/args.log"
export LSM_TEST_ARGS_LOG="${args_log}"
stdin_log="${tmpdir}/stdin.log"
export LSM_TEST_STDIN_LOG="${stdin_log}"

# Fake lsmcli: logs its arguments and returns values for each --type.
# marker1/marker2 are touched if a payload is ever executed.  Values are one
# per line; the completion script splits on newline only.
mkdir "${tmpdir}/bin"
cat > "${tmpdir}/bin/lsmcli" <<'EOF'
#!/bin/bash
printf '%s\n' "$@" >> "$LSM_TEST_ARGS_LOG"
echo "LSM_TEST_STDERR_MARKER" >&2
# Record what stdin is: a tty, or whatever line it could read.
if [ -t 0 ] ; then echo tty >> "$LSM_TEST_STDIN_LOG" ; fi
IFS= read -r -t 1 line && echo "read:${line}" >> "$LSM_TEST_STDIN_LOG"
case "$*" in
    *'--type systems'*)
        printf '%s\n' 'sys-1#desc' 'sys-2*#desc' \
            '$(touch${IFS:0:1}marker1)' '`touch${IFS:0:1}marker2`' '--force'
        ;;
    *'--type volumes'*)
        # Column output pads each field with trailing spaces.
        printf '%s\n' 'my vol  #desc   ' 'my      #desc   ' 'other   #desc   '
        ;;
    *'--type nfs_client_auth'*)
        # Real lsmcli prints sep.join(types) on ONE line, sep being the
        # argument after -t (or attached to it).
        sep=' '
        prev=
        for a in "$@" ; do
            [ "$prev" = -t ] && sep=$a
            case $a in -t?*) sep=${a#-t} ;; esac
            prev=$a
        done
        printf '%s%s%s\n' standard "$sep" none
        ;;
    *'--type pools'*)
        # Value comes from the test, may hold quotes/control bytes.
        printf '%s\n' "$LSM_TEST_VALUE"
        ;;
    *'--type access_groups'*)
        if [[ "$*" == *'--ag=ag-1'* ]] ; then
            printf '%s\n' 'ag-1#ag-name#init-1'
        else
            printf '%s\n' 'ag-1#ag-name#init-1' 'ag-2#ag-name#init-2'
        fi
        ;;
    *'--type snapshots'*)
        printf '%s\n' 'snap-1#desc'
        ;;
    *)
        exit 1
        ;;
esac
EOF
chmod +x "${tmpdir}/bin/lsmcli"
PATH="${tmpdir}/bin:${PATH}"
export PATH

cd "${tmpdir}" || exit 1

# Value lookups are off by default; turn them on for these tests.
# (Non-zero runs the lookups, see NO_VALUE_LOOKUP in lsmcli.)
LSMCLI_AUTO_COMPLETE_VALUE=1
export LSMCLI_AUTO_COMPLETE_VALUE

# shellcheck disable=SC1090
source "${srcdir}/lsmcli"

# Run completion for the given command line; the last word is the word
# being completed.  Results in COMPREPLY.
run_complete()
{
    COMP_WORDS=("$@")
    COMP_CWORD=$(( ${#COMP_WORDS[@]} - 1 ))
    COMPREPLY=()
    _lsm
}

reply()
{
    local IFS=' '
    echo "${COMPREPLY[*]}"
}

assert_eq()
{
    if [[ "${2}" == "${3}" ]] ; then
        echo "ok: ${1}"
    else
        echo "FAIL: ${1}: expected [${2}] got [${3}]" >&2
        failures=$(( failures + 1 ))
    fi
}

assert_file_absent()
{
    if [[ ! -e "${2}" ]] ; then
        echo "ok: ${1}"
    else
        echo "FAIL: ${1}: ${2} exists" >&2
        failures=$(( failures + 1 ))
    fi
}

# --- Option completion (no lsmcli values involved) ---

run_complete lsmcli --he
assert_eq "long option completion" "--help --header" "$(reply)"

run_complete lsmcli list --ty
assert_eq "list argument completion" "--type" "$(reply)"

run_complete lsmcli list --type sys
assert_eq "list type completion" "systems" "$(reply)"

# A filename in the current directory must not leak into the completion.
touch leaked-filename
run_complete lsmcli list --type leak
assert_eq "no filename completion" "" "$(reply)"

# The current word is not pathname expanded: a literal '*' only matches
# words that start with '*', never files in the current directory.
run_complete lsmcli list --type '*'
assert_eq "current word not pathname expanded" "" "$(reply)"

# --- Value completion (fake lsmcli output) ---

run_complete lsmcli list --type systems --sys ''
assert_eq "hostile and option-like values filtered" "sys-1" "$(reply)"

assert_file_absent "command substitution not executed (TAB)" "${tmpdir}/marker1"
assert_file_absent "backticks not executed (TAB)" "${tmpdir}/marker2"

# A value that lsmcli would parse as an option is not offered.
run_complete lsmcli list --type systems --sys --f
assert_eq "option-like value not offered" "" "$(reply)"

# --- Secondary lookups splice words with the --opt=value form ---

rm -f "${args_log}"
run_complete lsmcli list --type access_groups --ag ag-1 --init ''
assert_eq "initiator completion, filtered by access group" "init-1" "$(reply)"
assert_eq "access group spliced with = form" "--ag=ag-1" \
    "$(grep -x -- '--ag=ag-1' "${args_log}" || true)"

rm -f "${args_log}"
# A word starting with '-' must reach lsmcli attached to --fs, not as its
# own argument.
run_complete lsmcli list --type snapshots --fs -force --snap ''
assert_eq "snapshot completion" "snap-1" "$(reply)"
assert_eq "hostile fs value spliced with = form" "--fs=-force" \
    "$(grep -x -- '--fs=-force' "${args_log}" || true)"
assert_eq "hostile fs value not a separate argument" "" \
    "$(grep -x -- '-force' "${args_log}" || true)"

# The option whose value filters the lookup may itself be the word being
# completed: its value is not on the command line yet, and reading past the
# end of COMP_WORDS would fail under 'set -u'.  The unfiltered lookup runs
# (logged below) and nothing is offered: the option-like word being completed
# matches no value.
rm -f "${args_log}"
run_complete lsmcli access-group-add --init --ag
assert_eq "initiator completion, --ag being completed" "" "$(reply)"
assert_eq "unfiltered initiator lookup ran" "1" \
    "$([[ -s ${args_log} ]] && echo 1 || echo 0)"

run_complete lsmcli fs-snap-delete --snap --fs
assert_eq "snapshot completion, --fs being completed" "" "$(reply)"

# --- Allowlist: value mode only offers letters, digits, space and _.:/@+=,- ---

# Control byte and disallowed characters, each embedded in an otherwise fine
# word.  None may be offered.
disallowed=(
    $'ab\x01cd' 'a$b' 'a`b' 'a*b' 'a"b' "a'b" 'a;b' 'a~b' 'a(b'
    $'a\xc3\xa9b' 'a?b' 'a[b' 'a\b' 'a&b' 'a|b' 'a<b' 'a>b' 'a#b' 'a!b'
)
for v in "${disallowed[@]}" ; do
    desc="$(printf '%q' "${v}")"
    set_compreply "${v}" "" value
    assert_eq "disallowed value not offered: ${desc}" "" "$(reply)"
    set_compreply "${v}" "a" value
    assert_eq "disallowed value not offered (prefix): ${desc}" "" "$(reply)"
done

# Allowed punctuation passes (output is still %q quoted, which escapes ',').
for v in 'iqn.1994-05.com.example:tgt_1' '/dev/sda' 'a@b+c=d,e' 'naa.6001/x'; do
    set_compreply "${v}" "" value
    assert_eq "allowed value offered: ${v}" "$(printf '%q' "${v}")" "$(reply)"
done

# Option mode (no $3) is not subject to the value allowlist.
set_compreply '--he$ad' "--h"
assert_eq "option mode not filtered by allowlist" '--he\$ad' "$(reply)"

# Same through the real lookup path with the fake lsmcli.
run_value_lookup()
{
    LSM_TEST_VALUE="${1}" run_complete lsmcli list --type pools --pool "${2}"
}
export LSM_TEST_VALUE
run_value_lookup 'pool-1' ''
assert_eq "pool value offered through lsmcli lookup" "pool-1" "$(reply)"
run_value_lookup $'\x01$(touch${IFS}marker4)' ''
assert_eq "control byte payload not offered" "" "$(reply)"
assert_file_absent "control byte payload not executed" "${tmpdir}/marker4"

# --- A quote in the prefix means offer nothing ---

quote_prefixes=('"' "'" '"p' "'p" 'p"q' "p'q" '"--he' "'--he")
for q in "${quote_prefixes[@]}" ; do
    desc="$(printf '%q' "${q}")"
    # Value mode, lsmcli returns a value starting with the same quote.
    run_value_lookup "${q}pool" "${q}"
    assert_eq "quoted prefix, value mode: ${desc}" "" "$(reply)"
    run_value_lookup $'\x01$(touch${IFS}marker5)' "${q}"
    assert_eq "quoted prefix, hostile value: ${desc}" "" "$(reply)"
    # Option mode.
    run_complete lsmcli "${q}"
    assert_eq "quoted prefix, option mode: ${desc}" "" "$(reply)"
    set_compreply "--help --header" "${q}"
    assert_eq "quoted prefix, direct option mode: ${desc}" "" "$(reply)"
done

# Words that really do match a quoted prefix in option mode (no $3): only the
# quote bail-out keeps these from being offered.
set_compreply $'"x$(touch${IFS}m6)' '"'
assert_eq "option mode, dquote prefix matches word: nothing" "" "$(reply)"
set_compreply "'ab" "'"
assert_eq "option mode, squote prefix matches word: nothing" "" "$(reply)"
set_compreply 'ab"cd' 'ab"'
assert_eq "option mode, embedded dquote prefix: nothing" "" "$(reply)"
set_compreply "ab'cd" "ab'"
assert_eq "option mode, embedded squote prefix: nothing" "" "$(reply)"
assert_file_absent "option mode quoted prefix payload not executed" "${tmpdir}/m6"
assert_file_absent "quoted prefix payload not executed" "${tmpdir}/marker5"

# --- Locale and shopt independence ---

check_allowlist()
{
    local label="${1}" v
    # U+0130 and the Kelvin sign case-fold to ASCII letters.
    for v in $'a\xc3\xa9b' $'ab\x01cd' $'a\x7fb' 'a$b' $'a\xc4\xb0b' $'a\xe2\x84\xaab'; do
        set_compreply "${v}" "" value
        assert_eq "${label}: rejects $(printf '%q' "${v}")" "" "$(reply)"
    done
    set_compreply $'iqn.1994-05.com.example:tgt_1\n/dev/sda\nZz09' "" value
    assert_eq "${label}: accepts allowed words" \
        "$(printf '%q ' iqn.1994-05.com.example:tgt_1 /dev/sda Zz09 | sed 's/ $//')" \
        "$(reply)"
}

shopt -u globasciiranges
check_allowlist "globasciiranges off"
shopt -s globasciiranges

# Prefer C.UTF-8, then en_US.UTF-8, then any UTF-8 locale.
utf8_locale=
available_locales=$(locale -a 2>/dev/null)
for pattern in '^c\.utf-?8$' '^en_us\.utf-?8$' '^[a-z_]+\.utf-?8$' ; do
    utf8_locale=$(grep -i -m1 -E "${pattern}" <<< "${available_locales}")
    [[ -n "${utf8_locale}" ]] && break
done
if [[ -n "${utf8_locale}" ]] ; then
    saved_lc_all="${LC_ALL-}"
    LC_ALL="${utf8_locale}"
    shopt -u globasciiranges
    check_allowlist "${utf8_locale}, globasciiranges off"
    shopt -s globasciiranges
    check_allowlist "${utf8_locale}"
    assert_eq "LC_ALL not leaked by set_compreply" "${utf8_locale}" "${LC_ALL}"
    LC_ALL="${saved_lc_all}"
    [[ -n "${saved_lc_all}" ]] || unset LC_ALL
else
    echo "SKIP: no UTF-8 locale installed, locale checks not run"
fi

# Case-insensitive matching must not widen the allowlist.
if [[ -n "${utf8_locale}" ]] ; then
    LC_ALL="${utf8_locale}"
    shopt -s nocasematch
    check_allowlist "${utf8_locale}, nocasematch"
    shopt -u nocasematch
    LC_ALL="${saved_lc_all}"
    [[ -n "${saved_lc_all}" ]] || unset LC_ALL
fi

# nocasematch state is preserved on the normal and the quote bail-out path.
for start in on off ; do
    for pre in '' '"' ; do
        [[ ${start} == on ]] && shopt -s nocasematch || shopt -u nocasematch
        set_compreply 'ab' "${pre}" value
        if shopt -q nocasematch ; then now=on ; else now=off ; fi
        assert_eq "nocasematch ${start} preserved (prefix [${pre}])" \
            "${start}" "${now}"
    done
done
shopt -u nocasematch

# A readonly LC_ALL must neither produce errors nor change the result.
if [[ -n "${utf8_locale}" ]] ; then
    out=$( ( LC_ALL="${utf8_locale}"; readonly LC_ALL
             set_compreply $'a\xc3\xa9b\nok-1' "" value; reply ) 2>&1 )
    assert_eq "readonly LC_ALL: no errors, non-ASCII rejected" "ok-1" "${out}"
fi

# --- noglob state is preserved on every return path ---

for start in off on ; do
    for args in "a b|a|value" "a b|'|value"; do
        IFS='|' read -r vals pre mode <<< "${args}"
        [[ ${start} == on ]] && set -f || set +f
        set_compreply "${vals}" "${pre}" "${mode}"
        case $- in *f*) now=on ;; *) now=off ;; esac
        assert_eq "noglob ${start} preserved (prefix [${pre}])" "${start}" "${now}"
    done
done
set +f

# --- LSMCLI_AUTO_COMPLETE_VALUE: only a non-zero integer enables lookups ---

# The variable is read when the completion file is sourced, so source it
# again for each value.
for v in '1' '2' '01' '10' '0' '' 'false' 'no' 'off' '00' '-1' '1x' ' 1' '1 ' \
         'a[$(touch marker7)]' '$(touch marker7)' ; do
    rm -f "${args_log}"
    (
        LSMCLI_AUTO_COMPLETE_VALUE="${v}"
        unset NO_VALUE_LOOKUP
        # shellcheck disable=SC1090
        source "${srcdir}/lsmcli"
        run_complete lsmcli list --type systems --sys ''
    )
    case "${v}" in
        1|2|01|10) want=1 ;;
        *) want=0 ;;
    esac
    if [[ -s "${args_log}" ]] ; then got=1 ; else got=0 ; fi
    assert_eq "lookup enabled=${want} for LSMCLI_AUTO_COMPLETE_VALUE=[${v}]" \
        "${want}" "${got}"
done
assert_file_absent "env value not evaluated" "${tmpdir}/marker7"
rm -f "${args_log}"

# --- Lookups must not inherit the terminal ---

# The completion run is fed a known line on stdin and its stderr is captured.
# The fake lsmcli writes a marker to stderr and logs anything it can read from
# stdin or whether it is a tty.
: > "${stdin_log}"
rm -f "${args_log}"
captured=$( run_complete lsmcli list --type systems --sys '' \
    <<< "secret-line" 2>&1 >/dev/null )
assert_eq "lookup ran" "1" "$([[ -s ${args_log} ]] && echo 1 || echo 0)"
assert_eq "lookup stderr not passed through" "" "${captured}"
assert_eq "lookup did not read or inherit stdin" "" "$(cat "${stdin_log}")"

# --- Values are split on newline: a name with a space is one candidate ---

check_values()
{
    local label="${1}" values="${2}" want="${3}"
    set_compreply "${values}" "" value
    assert_eq "${label}" "${want}" "$(reply)"
}

check_values "space in name stays one candidate" $'my vol\nmy\nother' 'my\ vol my other'
check_values "tab in value rejected" $'my\tvol\nok' 'ok'
check_values "leading space then option-like rejected" $' -x\nok' 'ok'
check_values "leading space rejected" $' vol\nok' 'ok'
check_values "trailing space rejected" $'vol \nok' 'ok'
check_values "all-space value rejected" $'   \nok' 'ok'
check_values "empty lines skipped" $'\n\nok\n\n' 'ok'
check_values "option-like value rejected" $'-x\nok' 'ok'

# Padded column output from the (fake) lsmcli, through the real lookup path.
run_complete lsmcli list --type volumes --vol ''
assert_eq "lookup: padding stripped, space name one candidate" \
    'my\ vol my other' "$(reply)"
run_complete lsmcli list --type volumes --vol 'my'
assert_eq "lookup: prefix match" 'my\ vol my' "$(reply)"

# A caller's IFS changes nothing, in either mode, and is not modified.
for odd_ifs in ':' $'\n' 'o' ''; do
    IFS="${odd_ifs}"
    set_compreply $'my vol\nmy\nother' "" value
    value_out="$(IFS=' '; echo "${COMPREPLY[*]}")"
    set_compreply "--help --header --version" "--h"
    opt_out="$(IFS=' '; echo "${COMPREPLY[*]}")"
    after="${IFS-unset}"
    IFS=$' \t\n'
    odd_ifs_label=$(printf '%q' "${odd_ifs}")
    assert_eq "IFS=${odd_ifs_label} value mode" 'my\ vol my other' "${value_out}"
    assert_eq "IFS=${odd_ifs_label} option mode" '--help --header' "${opt_out}"
    assert_eq "IFS=${odd_ifs_label} unchanged" "${odd_ifs}" "${after}"
done

run_complete lsmcli fs-export --auth-type ''
assert_eq "auth-type: one candidate per type" 'standard none' "$(reply)"

# Hostile value with a space is not offered or executed.
check_values "hostile value with space rejected" \
    $'x $(touch${IFS}marker8)\nok' 'ok'
assert_file_absent "hostile value with space not executed" "${tmpdir}/marker8"

# --- A quoted value is inert once the command line runs ---

word="$(printf '%q' '$(touch${IFS:0:1}marker3)')"
eval "set -- ${word}"
assert_eq "quoted value survives evaluation" '$(touch${IFS:0:1}marker3)' "${1}"
assert_file_absent "quoted value inert when command line runs" "${tmpdir}/marker3"

if [[ ${failures} -eq 0 ]] ; then
    echo "All completion tests passed"
    exit 0
fi
echo "${failures} completion test(s) failed" >&2
exit 1
