#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
#
# Copyright (C) 2026 Red Hat, Inc.
#
# Checks the lsmcli bash completion against tools/lsmcli/cmdline.py, so that
# its tables cannot drift from the commands, aliases, options and choices the
# CLI has. cmdline.py is read as a syntax tree rather than imported: that
# needs neither a build nor the Python bindings, and nothing of it is run.
#
# What is compared, for every command and every alias:
#   - the command names the completion offers, against the commands and aliases
#   - the global options it offers, against _add_common_options
#   - the options offered after a command, against its required and optional
#     ones, with --force as the one extra, where it is listed below
#   - that an option is offered again after it is used only if it is one with
#     action='append'
#   - the values offered for an option with choices, against the choices

set -u

# Automake's exit code for a skipped test.
SKIP=77

srcdir=${srcdir:-$(dirname "$0")}
completion_script="$(readlink -f "${srcdir}/lsmcli")"
cmdline_py="$(readlink -f "${srcdir}/../lsmcli/cmdline.py")"
data_display_py="$(readlink -f "${srcdir}/../lsmcli/data_display.py")"

for required in "${completion_script}" "${cmdline_py}" "${data_display_py}" ; do
    if [[ ! -r ${required} ]] ; then
        echo "Skipping: cannot read ${required}"
        exit ${SKIP}
    fi
done

if ! command -v python3 > /dev/null ; then
    echo "Skipping: python3 not available"
    exit ${SKIP}
fi

# What the completion offers beyond what cmdline.py has: --force is a global
# option that is also accepted after a command, and is offered for the ones
# that ask before destroying data.
force_cmds=" volume-delete vd volume-resize vr volume-replicate-range \
fs-delete fs-resize fs-snap-restore "

# One line for each fact, words separated by a space:
#   command <name> <option>...
#   alias <name> <the words it stands for>
#   repeat <command> <option>
#   choices <command> <option> <choice>...
#   global <option>...
#   error <what could not be read>
# Read the syntax trees of cmdline.py and of data_display.py, for the RAID
# types in it, evaluating no more than names, constants, lists, tuples and
# dict() calls, and what is not understood is an 'error' line, which fails
# the test naming the command and option, rather than being compared as if
# it was not there.
facts=$(python3 -I - "${cmdline_py}" "${data_display_py}" <<'PYEOF'
import ast
import sys

UNKNOWN = object()


def parse(path):
    with open(path) as handle:
        return ast.parse(handle.read(), path)


def class_constants(tree, class_name):
    found = {}
    for node in tree.body:
        if isinstance(node, ast.ClassDef) and node.name == class_name:
            for stmt in node.body:
                if (isinstance(stmt, ast.Assign) and len(stmt.targets) == 1
                        and isinstance(stmt.targets[0], ast.Name)):
                    found[stmt.targets[0].id] = ev(stmt.value, {}, {})
    return found


def ev(node, env, classes):
    if isinstance(node, ast.Constant):
        return node.value
    if isinstance(node, ast.Name):
        return env.get(node.id, UNKNOWN)
    if isinstance(node, (ast.List, ast.Tuple)):
        return [ev(item, env, classes) for item in node.elts]
    if isinstance(node, ast.Attribute) and isinstance(node.value, ast.Name):
        return classes.get(node.value.id, {}).get(node.attr, UNKNOWN)
    if isinstance(node, ast.Call):
        if isinstance(node.func, ast.Name) and node.func.id == 'dict':
            result = {}
            for arg in node.args:
                value = ev(arg, env, classes)
                if isinstance(value, dict):
                    result.update(value)
            for keyword in node.keywords:
                if keyword.arg is not None:
                    result[keyword.arg] = ev(keyword.value, env, classes)
            return result
        if (isinstance(node.func, ast.Attribute) and node.func.attr == 'copy'
                and not node.args):
            value = ev(node.func.value, env, classes)
            if isinstance(value, dict):
                return dict(value)
    return UNKNOWN


def led_states(tree):
    for node in ast.walk(tree):
        if isinstance(node, ast.FunctionDef) and node.name == '_check_led_state':
            for call in ast.walk(node):
                if (isinstance(call, ast.Call)
                        and isinstance(call.func, ast.Name)
                        and call.func.id == 'dict' and call.keywords):
                    return [kw.arg for kw in call.keywords]
    return None


def common_options(tree):
    found = []
    for node in tree.body:
        if isinstance(node, ast.FunctionDef) and node.name == '_add_common_options':
            for call in ast.walk(node):
                if (isinstance(call, ast.Call)
                        and isinstance(call.func, ast.Attribute)
                        and call.func.attr == 'add_argument'):
                    hidden = any(
                        kw.arg == 'help' and isinstance(kw.value, ast.Attribute)
                        and kw.value.attr == 'SUPPRESS' for kw in call.keywords)
                    if hidden:
                        continue
                    for arg in call.args:
                        if (isinstance(arg, ast.Constant)
                                and str(arg.value).startswith('-')):
                            found.append(arg.value)
    return found


def main():
    cmdline = parse(sys.argv[1])
    classes = {'VolumeRAIDInfo': class_constants(parse(sys.argv[2]),
                                                 'VolumeRAIDInfo')}
    env = {}
    for node in cmdline.body:
        if (isinstance(node, ast.Assign) and len(node.targets) == 1
                and isinstance(node.targets[0], ast.Name)):
            env[node.targets[0].id] = ev(node.value, env, classes)

    # argparse adds -h and --help itself.
    print('global', '-h', '--help', *common_options(cmdline))
    cmds = env.get('cmds', UNKNOWN)
    if not isinstance(cmds, list):
        print('error cmds cannot be read')
        cmds = []
    for cmd in cmds:
        if not isinstance(cmd, dict) or not isinstance(cmd.get('name'), str):
            print('error a command in cmds cannot be read')
            continue
        name = cmd['name']
        options = []
        for kind in ('args', 'optional'):
            found = cmd.get(kind, [])
            if not isinstance(found, list):
                print('error %s of %s cannot be read' % (kind, name))
                continue
            options.extend(found)
        names = []
        for opt in options:
            if not isinstance(opt, dict) or not isinstance(opt.get('name'), str):
                print('error an option of %s cannot be read' % name)
                continue
            names.append(opt['name'])
        print('command', name, *names)
        for opt in options:
            if not isinstance(opt, dict) or not isinstance(opt.get('name'), str):
                continue
            where = '%s %s' % (name, opt['name'])
            if 'action' in opt:
                if not isinstance(opt['action'], str):
                    print('error action of %s cannot be read' % where)
                elif opt['action'] == 'append':
                    print('repeat', name, opt['name'])
            choices = opt.get('choices')
            if opt['name'] == '--state':
                # Its choices are the keys of a dict in the type function.
                choices = led_states(cmdline)
                if choices is None:
                    print('error choices of %s cannot be read' % where)
                    continue
            elif 'choices' not in opt:
                continue
            if (not isinstance(choices, list)
                    or not all(isinstance(c, str) for c in choices)):
                print('error choices of %s cannot be read' % where)
                continue
            print('choices', name, opt['name'], *choices)
    aliases = env.get('aliases', UNKNOWN)
    if not isinstance(aliases, dict):
        print('error aliases cannot be read')
        aliases = {}
    for name, target in aliases.items():
        if not isinstance(target, str):
            print('error alias %s cannot be read' % name)
            continue
        print('alias', name, target)


main()
PYEOF
)
if [[ $? -ne 0 || -z ${facts} ]] ; then
    echo "Could not read ${cmdline_py}"
    exit 1
fi

# The completion offers nothing that needs a look-up.
unset LSMCLI_AUTO_COMPLETE_VALUE
# shellcheck source=tools/bash_completion/lsmcli
source "${completion_script}"

failures=0

function fail()
{
    echo "FAIL: $*"
    failures=$(( failures + 1 ))
}

# What _lsm offers for the words, the last the one being completed, sorted and
# on one line.
# $@ The words
function offered()
{
    COMP_WORDS=("$@")
    COMP_CWORD=$(( $# - 1 ))
    COMPREPLY=()
    _lsm
    printf '%s\n' ${COMPREPLY[@]+"${COMPREPLY[@]}"} | LC_ALL=C sort | grep . | tr '\n' ' '
}

# The words, sorted and on one line.
function sorted()
{
    printf '%s\n' "$@" | LC_ALL=C sort -u | grep . | tr '\n' ' '
}

# Report a difference between two lists of words.
# $1 What is compared
# $2 What cmdline.py has
# $3 What the completion offers
function same()
{
    [[ $2 == "$3" ]] || fail "$1: cmdline.py has [$2] the completion offers [$3]"
}

declare -A cmd_options=() cmd_repeat=() cmd_choices=() alias_target=()
global_options=()
while read -r kind name rest ; do
    case "${kind}" in
        global) global_options=("${name}" ${rest}) ;;
        command) cmd_options[${name}]="${rest}" ;;
        repeat) cmd_repeat[${name}]+=" ${rest}" ;;
        choices) cmd_choices["${name} ${rest%% *}"]="${rest#* }" ;;
        alias) alias_target[${name}]="${rest}" ;;
        error) fail "cmdline.py: ${name} ${rest}" ;;
    esac
done <<< "${facts}"

if [[ ${#cmd_options[@]} -lt 50 || ${#alias_target[@]} -lt 30 ]] ; then
    fail "read only ${#cmd_options[@]} commands and ${#alias_target[@]} aliases from cmdline.py"
fi

# The commands and aliases, and the global options.
everything=$(offered lsmcli "")
global_offered=$(printf '%s\n' ${everything} | grep '^-')
cmds_offered=$(printf '%s\n' ${everything} | grep -v '^-')
same "global options" "$(sorted "${global_options[@]}")" "$(sorted ${global_offered})"
same "commands and aliases" \
     "$(sorted "${!cmd_options[@]}" "${!alias_target[@]}")" \
     "$(sorted ${cmds_offered})"

# The options of each command, and of each alias, which stands for a command
# with some of its options given, eg. 'list --type systems'.
declare -A names_to_check=()
for name in "${!cmd_options[@]}" ; do
    names_to_check[${name}]="${cmd_options[${name}]}"
done
for name in "${!alias_target[@]}" ; do
    read -r -a target_words <<< "${alias_target[${name}]}"
    target=${target_words[0]}
    if [[ -z ${cmd_options[${target}]+set} ]] ; then
        fail "alias ${name} is for ${target}, which is not a command"
        continue
    fi
    left=" ${cmd_options[${target}]} "
    for word in "${target_words[@]:1}" ; do
        left=${left/ ${word} / }
    done
    names_to_check[${name}]="${left}"
done

for name in "${!names_to_check[@]}" ; do
    # An alias of a command shares its repeatable options and choices.
    command=${name}
    [[ -n ${alias_target[${name}]+set} ]] && command=${alias_target[${name}]%% *}
    options=${names_to_check[${name}]}

    expected=$(sorted ${options})
    if [[ ${force_cmds} == *" ${name} "* ]] ; then
        expected=$(sorted ${options} --force)
    fi
    same "options of ${name}" "${expected}" "$(offered lsmcli "${name}" "")"

    # Once used an option is not offered again, unless it is one that can be.
    for option in ${options} ; do
        again=no
        [[ " ${cmd_repeat[${command}]-} " == *" ${option} "* ]] && again=yes
        # A whole word, --vol is not in --src-vol.
        if [[ " $(offered lsmcli "${name}" "${option}" x "")" == *" ${option} "* ]] ; then
            [[ ${again} == yes ]] || fail "${name} offers ${option} again, it cannot be repeated"
        else
            [[ ${again} == no ]] || fail "${name} does not offer ${option} again, it can be repeated"
        fi
    done

    # The values of an option with choices. --type takes any case and the
    # completion gives it in lower case.
    for option in ${options} ; do
        choices=${cmd_choices["${command} ${option}"]-}
        [[ -n ${choices} ]] || continue
        wanted=$(sorted ${choices})
        got=$(offered lsmcli "${name}" "${option}" "")
        if [[ ${option} == --type ]] ; then
            wanted=${wanted,,}
            wanted=$(sorted ${wanted})
            got=${got,,}
        fi
        same "choices of ${name} ${option}" "${wanted}" "${got}"
    done
done

if [[ ${failures} -ne 0 ]] ; then
    echo "${failures} completion drift test(s) failed"
    exit 1
fi

echo "All completion drift tests passed"
exit 0
