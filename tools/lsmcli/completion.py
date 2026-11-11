# SPDX-License-Identifier: LGPL-2.1-or-later
#
# Copyright (C) 2015-2026 Red Hat, Inc.
#
# Author: Tony Asleson <tasleson@redhat.com>
"""
The completion for lsmcli, run as the hidden

    lsmcli __complete --point <N> --breaks <COMP_WORDBREAKS> [--no-lookup] \
        -- <COMP_LINE>

by the bash completion, which only hands over what bash gives it. The
candidates are written to stdout, one to a line, trimmed to what follows the
last word break character of the word being completed, as that is all readline
replaces. Nothing else is written to stdout, and it exits with 0 whatever
happens, as a completion that fails is no completion.

The commands, aliases, options and choices are those lsmcli itself parses
with, so they cannot differ from it.

Completion only fills things in. The typed line is used to find where the
cursor is, the sub command, the option before the cursor and the word being
completed, and for nothing else: it never chooses a target or a filter.

Values, ie. the ids of the systems, pools, volumes and so on, are looked up
only if the environment has LSMCLI_AUTO_COMPLETE_VALUE=1, as they can be slow
or hang depending on the plugin and configuration. The look-up is for the URI
lsmcli itself would use from its set up, see default_uri(), or the one a
wrapper put in front of lsmcli, see main(), and is not filtered by anything
typed, so --vol offers every volume of that array. A line that has a -u or
--uri anywhere in it is for some other array, which is not known, so there is
no look-up for it. Nor is there one when the word being completed has a quote
or a $ in it, as what is offered is escaped for a word that has none and it is
not known what the shell would make of the rest. Nothing is expanded or
evaluated, and only lists are asked for, nothing is changed.
"""
import argparse
import os
import re
import signal
import sys
import threading

from lsm import Client
from lsm.lsmcli.cmdline import (cmds, aliases, list_choices, LED_STATES,
                                default_uri, _add_common_options,
                                _fork_exec_plugin)

# Seconds the look-up of values is given, in all. The library is given a
# little less, and a watchdog ends the process at the end of them whatever it
# is blocked in, see main().
LOOKUP_TIMEOUT = 5

# What bash (readline) breaks words at when COMP_WORDBREAKS says nothing.
DEFAULT_BREAKS = " \t\n\"'><=;|&(:"

# What in the word being completed stops values being offered for it, see
# Completer.values().
_QUOTING = "'\"$`"


def _common_actions():
    """The global options as argparse has them from cmdline.py."""
    parser = argparse.ArgumentParser(add_help=True)
    _add_common_options(parser)
    return [a for a in parser._actions if a.option_strings]


# The global options that take a value, and the ones that do not, as short
# ones can be given together in one word, eg. -Hbu <URI>. -v and -h end
# lsmcli, so are not ones to be given together with others.
_ACTIONS = _common_actions()
_SHORT_NO_VALUE = ''.join(f[1] for a in _ACTIONS
                          if a.nargs == 0 and a.dest not in ('help', 'version')
                          for f in a.option_strings if not f.startswith('--'))
_SHORT_WITH_VALUE = ''.join(f[1] for a in _ACTIONS if a.nargs != 0
                            for f in a.option_strings
                            if not f.startswith('--'))
_LONG_WITH_VALUE = tuple(f[2:] for a in _ACTIONS if a.nargs != 0
                         for f in a.option_strings if f.startswith('--'))

_SHORT_RE = re.compile(
    r'^-([%s]*)([%s])(.*)$' % (_SHORT_NO_VALUE, _SHORT_WITH_VALUE), re.S)
_LONG_RE = re.compile(r'^--(%s)(=(.*))?$' % '|'.join(_LONG_WITH_VALUE), re.S)

# Options that are never completed, whatever the line and the settings are.
# Completing a value of one of these would write a secret onto the command
# line, where it ends up in the history and on the screen: a password, or a
# URI, which can carry one as user:password@host. Nothing of the environment
# (LSMCLI_PASSWORD, LSMCLI_URI) or of a URI is ever part of a candidate.
_SECRET_OPTIONS = frozenset(['--in-pass', '--out-pass', '-u', '--uri'])

# Options that are not offered, only recognised: --force and -f skip the
# question lsmcli asks before it destroys data, which is not for a TAB to
# put on a line.
_NEVER_OFFERED = frozenset(['--force', '-f'])

# What to look up for the value of an option, as (list type, column), the
# column being 'all' for every field of every record.
_LOOKUPS = {
    '--sys': ('systems', 1),
    '--pool': ('pools', 1),
    '--vol': ('volumes', 1),
    '--src-vol': ('volumes', 1),
    '--dst-vol': ('volumes', 1),
    '--disk': ('disks', 1),
    '--ag': ('access_groups', 1),
    '--init': ('access_groups', 3),
    '--nfs-export': ('exports', 1),
    '--export': ('exports', 1),
    '--tgt': ('target_ports', 1),
    '--fs': ('fs', 1),
    '--src-fs': ('fs', 1),
    '--auth-type': ('nfs_client_auth', 'all'),
}

# Options with a value that is not looked up, free form or not known.
_FREE_FORM = frozenset([
    '--size', '--count', '--src-start', '--dst-start', '--name', '--in-user',
    '--in-pass', '--out-user', '--out-pass', '--exportpath', '--anonuid',
    '--anongid', '--root-host', '--ro-host', '--rw-host', '--dst-name',
    '--file', '--fileas', '--src', '--dst', '--job', '--strip-size',
    '--read-pct', '--path', '--slot', '--snap', '--backing-snapshot'
])

# Options with a choice of values, as the commands that have them list them.
# --state is checked by a function and not by choices, and --type is done on
# its own.
_CHOICE_OPTIONS = frozenset(
    a['name'] for c in cmds for a in c.get('args', []) + c.get('optional', [])
    if a.get('choices') and a['name'] != '--type') | frozenset(['--state'])


def _cmd_options(cmd):
    """
    The options a command has, in the order cmdline.py has them: the ones it
    requires and the ones it allows.
    :return: (list of option names, set of the ones that can be repeated)
    """
    names = []
    repeat = set()
    for arg in cmd.get('args', []) + cmd.get('optional', []):
        name = arg['name']
        names.append(name)
        if arg.get('action') == 'append':
            repeat.add(name)
    return names, repeat


_CMDS = dict((c['name'], c) for c in cmds)


def command_options(name):
    """
    The options of a command, or of an alias of one, as they are offered.
    An alias of 'list --type <TYPE>' has the type already.
    :param name: The sub command as typed
    :return: (list of options, set of the repeatable ones) or None if the
             sub command is not one lsmcli has
    """
    implied = []
    if name in aliases:
        words = aliases[name].split(" ")
        name = words[0]
        implied = [w for w in words[1:] if w.startswith('--')]
    if name not in _CMDS:
        return None
    names, repeat = _cmd_options(_CMDS[name])
    names = [n for n in names if n not in implied]
    return names, repeat


def command_name(name):
    """The sub command an alias stands for, else the name."""
    if name in aliases:
        return aliases[name].split(" ")[0]
    return name


def option_choices(cmd, option):
    """
    The values an option of a sub command takes, if it is a choice. The ones
    that do not depend on the sub command are those of the first that has them
    when the sub command is not one that has the option.
    :return: List of values, empty if there are none
    """
    found = []
    for c in cmds:
        for arg in c.get('args', []) + c.get('optional', []):
            if arg['name'] == option and arg.get('choices'):
                found.append((c['name'], list(arg['choices'])))
    if option == '--state':
        return list(LED_STATES)
    for name, choices in found:
        if name == command_name(cmd):
            return choices
    if option == '--policy' or not found:
        return []
    return found[0][1]


def global_options():
    """
    The global options of lsmcli, as (short options, long options), the
    hidden ones and the ones that are never offered left out.
    """
    short = []
    long_ = []
    for action in _ACTIONS:
        if action.help == argparse.SUPPRESS:
            continue
        for flag in action.option_strings:
            if flag not in _NEVER_OFFERED:
                (long_ if flag.startswith('--') else short).append(flag)
    # -h is the one the user is least likely to want.
    short.sort(key=lambda f: f == '-h')
    return short, long_


# ---------------------------------------------------------------------------
# Reading a word the way the shell does, without evaluating anything.
# ---------------------------------------------------------------------------


class QuoteWalk(object):
    """
    The one reading of quotes and backslashes that everything here that goes
    through a word is made of: iterating gives (position, text, state) for
    each character, or for the two of a $' or a backslash and the one it
    escapes, the state being the quote the character is in, none, single,
    double or ansi, as it was before it. A backslash escapes the next
    character, except in single quotes. After the last, state is the quote
    that is left open. Only the first of an escape is given, for the caller
    to read the escaped one at position + 1.
    """

    def __init__(self, text):
        self.text = text
        self.state = 'none'

    def __iter__(self):
        text = self.text
        size = len(text)
        i = 0
        while i < size:
            c = text[i]
            state = self.state
            if c == '\\' and state != 'single':
                yield i, c, state
                i += 2
                continue
            if state == 'none':
                if c == "'":
                    self.state = 'single'
                elif c == '"':
                    self.state = 'double'
                elif c == '$' and text[i + 1:i + 2] == "'":
                    c = "$'"
                    self.state = 'ansi'
            elif c == {'single': "'", 'double': '"', 'ansi': "'"}[state]:
                self.state = 'none'
            yield i, c, state
            i += len(c)


def unescape(word):
    """
    Turn a word as the user typed it, which may be escaped or quoted, into the
    value it stands for, the way the shell would read it but without
    evaluating anything. Quoted and unquoted parts can follow each other, eg.
    'a'"b"\\ c.
      unquoted        a backslash escapes the next character
      "double quoted" a backslash escapes only \\ " $ and `, else it is kept
      'single quoted' nothing is escaped
      $'ANSI-C'       what is between the quotes as it is, but for a
                      backslash in front of a \\ or '. The escapes of it, eg.
                      \\t, are not turned into what they stand for.
    A quote left open is fine, as that is the word being completed, and so is
    a backslash at the very end.
    """
    out = []
    for i, c, state in QuoteWalk(word):
        nxt = word[i + 1:i + 2]
        if c == '\\' and state != 'single':
            if state == 'none':
                out.append(nxt)
            else:
                # Only these are escaped, else the backslash stays.
                escaped = '\\"$`' if state == 'double' else "\\'"
                out.append(nxt if nxt and nxt in escaped else c + nxt)
        elif state == 'none':
            if c not in ("'", '"', "$'"):
                out.append(c)
        elif c != {'single': "'", 'double': '"', 'ansi': "'"}[state]:
            out.append(c)
    return ''.join(out)


_SAFE = re.compile(r'[^ \t\n\\\'"!$&()*;<>?\[\]`{|}^]')
_CONTROL = re.compile('[\x00-\x1f\x7f]')


def escape(value):
    """
    Write a value so that the shell reads it back as the value, in a word that
    has no quote open: a backslash in front of whatever the shell would
    otherwise take for more than a character, as printf %q has them. A value
    with a control character, which printf %q writes as $'...', is not one to
    escape, see Completer.values().
    """
    out = []
    for pos, c in enumerate(value):
        if not _SAFE.match(c) or (pos == 0 and c in '~#'):
            out.append('\\')
        out.append(c)
    return ''.join(out) or "''"


# ---------------------------------------------------------------------------
# The line.
# ---------------------------------------------------------------------------


class Run(object):
    """
    A word of the line as the shell reads it: all that is between white
    space, quotes and escapes allowed. A word that bash breaks at a character
    of COMP_WORDBREAKS, such as the : of a URI, is more than one of its words,
    see Line.pieces().
    """

    def __init__(self, text, start, virtual=False):
        self.text = text
        self.start = start
        self.end = start + len(text)
        # The empty word at the cursor, where there is white space.
        self.virtual = virtual


def split_line(line):
    """
    Split a line into words at white space that is not quoted or escaped.
    :return: List of (text, start offset)
    """
    words = []
    start = None
    for i, c, state in QuoteWalk(line):
        if state == 'none' and c in ' \t':
            if start is not None:
                words.append((line[start:i], start))
                start = None
        elif start is None:
            start = i
    if start is not None:
        words.append((line[start:], start))
    return words


def break_positions(text, breaks):
    """
    Where in a word bash breaks it: the positions of the characters of
    COMP_WORDBREAKS that are not quoted or escaped. bash keeps the ( of a
    $( in the word.
    """
    return [
        i for i, c, state in QuoteWalk(text) if state == 'none' and c in breaks
        and not (c == '(' and text[i - 1:i] == '$')
    ]


def pieces(text, breaks):
    """
    The words bash makes of a word: a character that is a word break is a
    word of its own.
    """
    out = []
    last = 0
    for pos in break_positions(text, breaks):
        if pos > last:
            out.append(text[last:pos])
        out.append(text[pos])
        last = pos + 1
    if last < len(text):
        out.append(text[last:])
    return out


class Completer(object):
    """
    Completes one line, see complete().
    """

    def __init__(self,
                 line,
                 point,
                 breaks,
                 lookup_ok=True,
                 auto_value=False,
                 default_uri_arg=None,
                 plugin_exe=None):
        self.line = line
        self.point = max(0, min(point, len(line)))
        # The quotes are for unescape(), readline does not break a word at
        # one that opens a quote.
        self.breaks = breaks if breaks else DEFAULT_BREAKS
        self.breaks = ''.join(c for c in self.breaks if c not in '\'"')
        self.lookup_ok = lookup_ok
        self.auto_value = auto_value
        self.default_uri_arg = default_uri_arg
        self.plugin_exe = plugin_exe
        self.cmd_names = (list(_CMDS) + list(aliases))
        self.short_opts, self.long_opts = global_options()
        self._read_line()

    # The line ------------------------------------------------------------

    def _read_line(self):
        """Find the words, and the one the cursor is in."""
        self.runs = [Run(t, s) for t, s in split_line(self.line)]
        self.cword = None
        for idx, run in enumerate(self.runs):
            if run.start <= self.point <= run.end:
                self.cword = idx
                break
        if self.cword is None:
            at = len([r for r in self.runs if r.end < self.point])
            self.runs.insert(at, Run('', self.point, virtual=True))
            self.cword = at

        # The piece of the cursor word the cursor is in. bash breaks the word
        # at every character of COMP_WORDBREAKS, a break being a word of its
        # own, and gives the whole word the cursor is in, or the one that
        # starts at the cursor.
        cur = self.runs[self.cword]
        off = self.point - cur.start
        self.breaks_at = break_positions(cur.text, self.breaks)
        if off < len(cur.text) and off in self.breaks_at:
            self.piece = (off, off + 1)
        elif off == len(cur.text) and off - 1 in self.breaks_at and off > 0:
            self.piece = (off - 1, off)
        else:
            before = [b for b in self.breaks_at if b < off]
            after = [b for b in self.breaks_at if b >= off]
            self.piece = ((before[-1] + 1) if before else 0,
                          after[0] if after else len(cur.text))

    def word(self, idx):
        """The word as the shell reads it, '' where there is none."""
        return unescape(self.runs[idx].text) if idx >= 0 else ''

    # Reading the global options and the sub command ---------------------

    @staticmethod
    def _takes_value(word):
        """
        Does a word give a global option that takes a value, as -u or --uri,
        or -Hbu, which is -H -b -u?
        :return: None if not, else whether the value is the next word, and not
                 glued on to this one
        """
        found = _SHORT_RE.match(word)
        if found:
            return not found.group(3)
        found = _LONG_RE.match(word)
        if found:
            return not found.group(2)
        return None

    def scan(self):
        """
        Find the sub command, the first word before the cursor that is not an
        option or the value of a global option, and whether the word being
        completed is a value of a global option, for which there is nothing to
        offer. The first word, the command itself, is never looked at. Global
        options can be given after the sub command too, so the one before the
        cursor is looked at whatever the sub command is.

        Sets:
          cmd       the sub command, empty if there is none yet
          in_value  True if the word being completed is a value of a global
                    option
        """
        self.cmd = ''
        i = 1
        while i < self.cword:
            word = self.word(i)
            takes = self._takes_value(word)
            if takes is None and word and not word.startswith('-'):
                self.cmd = word
                break
            i += 2 if takes else 1
        self.in_value = (self._takes_value(self.word(self.cword - 1)) is True
                         or self._takes_value(self.word(self.cword)) is False)

    def typed_uri(self):
        """
        Does the line have a -u or --uri in it, wherever, however it is given:
        as a word or with its value glued on, among other short options such
        as -Hbu, or abbreviated as argparse allows? The target the line is
        for is then not the one of lsmcli's set up, which is all that is ever
        looked at. A word that merely contains a u after a - is one as well,
        and it is not looked at further.
        """
        for idx in range(1, len(self.runs)):
            word = self.word(idx)
            if word.startswith('--'):
                name = word[2:].split('=')[0]
                if name and 'uri'.startswith(name):
                    return True
            elif word.startswith('-') and 'u' in word:
                return True
        return False

    # Look-ups -----------------------------------------------------------

    def values(self, list_type, column):
        """
        The values of the requested column of a list, as candidates. There are
        none if the word being completed has a quote or a $ in it, as what is
        offered is escaped for a word that has none, nor if the line has a -u
        or --uri in it. Values with a control character are left out, as they
        could not be put on the line as they are.
        :param list_type: What to list, as for lsmcli list --type
        :param column: Column number to use as the value, or 'all' to use
                       every field of every line
        :return: The candidates, escaped for the word being completed
        """
        word = self.runs[self.cword].text
        if not self.lookup_ok or any(c in word for c in _QUOTING):
            return []
        if self.typed_uri():
            return []
        plain = unescape(word[:self.piece[1]])
        skip = self._skip(word[:self.piece[1]])
        rows = fetch_rows(list_type, self.default_uri_arg, self.plugin_exe)
        out = []
        for fields in rows:
            if column == 'all':
                items = fields
            else:
                items = fields[column - 1:column]
            for item in items:
                if (item and not _CONTROL.search(item)
                        and item.startswith(plain)):
                    out.append(escape(item[skip:]))
        return out

    def _skip(self, text):
        """
        How much of a value is not offered. Readline replaces only what
        follows the last word break character at or before the cursor, eg. a
        : in the middle of an iSCSI name or a WWPN, which is why such a value
        is more than one of the words bash hands over, so what is before it
        stays on the line and what is offered has to start after it in the
        value.
        """
        bps = break_positions(text, self.breaks)
        if not bps:
            return 0
        return len(unescape(text[:bps[-1] + 1]))

    # Completing ---------------------------------------------------------

    def complete(self):
        """
        :return: List of candidates
        """
        # There is no previous word for the command itself, and nothing to
        # offer.
        if self.cword < 1:
            return []
        self.scan()
        # The value of a global option, eg. the URI, we know nothing about.
        if self.in_value:
            return []

        # The word being completed is all of the words of it up to the cursor
        # put together, eg. an iSCSI name or a WWPN, which bash breaks at
        # every :, and the word before it is the whole word before that one,
        # eg. --init.
        cur = unescape(self.runs[self.cword].text[:self.piece[1]])
        prev = self.word(self.cword - 1)
        prev2 = self.word(self.cword - 2) if self.cword > 2 else ''

        # Never, see _SECRET_OPTIONS.
        if prev in _SECRET_OPTIONS:
            return []

        # These operations can potentially be slow and cause hangs depending
        # on the plugin and configuration.
        if self.auto_value and prev in _LOOKUPS:
            return self.values(*_LOOKUPS[prev])

        def match(words):
            return [w for w in words if w.startswith(cur)]

        if prev == '--type':
            return match([c.lower() for c in list_choices])
        if prev in _FREE_FORM:
            # These we cannot look up, so don't offer any values. That is also
            # --snap and --backing-snapshot, which only take the snapshots of
            # a file system, and no --fs is taken from the line.
            return []
        if prev in _CHOICE_OPTIONS or prev == '--state':
            return match(option_choices(self.cmd, prev))

        # Specific listing case where you need a fs too. --type takes the name
        # in either case, and the sub command is the one found, not a fixed
        # position.
        if (self.cword >= 2 and prev.lower() == 'snapshots'
                and prev2 == '--type' and self.cmd == 'list'):
            return match(['--fs'])

        # Whatever the sub command allows that is not on the line already.
        options = command_options(self.cmd)
        if options is not None:
            names, repeat = options
            used = self._used_words()
            return match([n for n in names if n in repeat or n not in used])

        # No sub command yet, so offer global options and the commands.
        # Always, as the words typed before can be any global options, and
        # with a value or without.
        if not self.cmd:
            if cur.startswith('--'):
                return match(self.long_opts)
            if cur.startswith('-'):
                return match(self.short_opts + self.long_opts)
            return match(self.short_opts + self.long_opts + self.cmd_names)
        return []

    def _used_words(self):
        """The words of the line, but for the one being completed."""
        words = []
        for idx, run in enumerate(self.runs):
            if run.virtual:
                continue
            split = pieces(run.text, self.breaks)
            if idx == self.cword:
                pos = 0
                keep = []
                for piece in split:
                    # The piece the cursor is in is what is being completed.
                    if pos == self.piece[0] and pos + len(piece) \
                            == self.piece[1]:
                        pos += len(piece)
                        continue
                    keep.append(piece)
                    pos += len(piece)
                split = keep
            words.extend(split)
        return set(words)


# ---------------------------------------------------------------------------
# Asking lsm for the values.
# ---------------------------------------------------------------------------

# The plugin run for --fork_plugin, for the watchdog to kill.
_forked_plugin = []

# Held by whoever ends the process or starts to write the candidates, so that
# what is written is all of it or none, see main().
_end_lock = threading.Lock()
_writing = []


def _kill_plugin(process):
    """
    Kill a plugin that was run for --fork_plugin and all it started, which is
    in a session of its own for that, see fetch_rows().
    """
    try:
        os.killpg(process.pid, signal.SIGKILL)
    except OSError:
        try:
            process.kill()
        except OSError:
            pass


def fetch_rows(list_type, wrapper_uri=None, plugin_exe=None):
    """
    The records of a list, as lsmcli list --type <list_type> has them, one list
    of the fields that matter for completion to a record. Only lists, never
    changes anything, and only of the array lsmcli would use: the one of the
    wrapper in front of it, else of LSMCLI_URI and ~/.lsmcli.
    :param list_type: systems, pools, volumes, disks, access_groups, fs,
                      exports, nfs_client_auth or target_ports
    :param wrapper_uri: The URI of what put options in front of lsmcli
    :param plugin_exe: The plugin to run in place of lsmd, a developer option
    :return: List of lists of strings, empty if it cannot be told
    """
    uri = wrapper_uri or default_uri()
    if uri is None:
        return []
    password = os.getenv('LSMCLI_PASSWORD')

    class _Holder(object):
        plugin_fork = None

    holder = _Holder()
    client = None
    done = False
    try:
        if plugin_exe:
            # In a session of its own, so that all of what it starts can be
            # killed with it. lsmcli itself leaves it where it is, so that
            # a Ctrl-C reaches it.
            _fork_exec_plugin(holder, plugin_exe, new_session=True)
            _forked_plugin.append(holder.plugin_fork.plugin_process)
        client = Client(uri, password, (LOOKUP_TIMEOUT - 1) * 1000)
        rows = _list(client, list_type)
        done = True
        return rows
    finally:
        if client is not None:
            try:
                client.close()
            except Exception:
                pass
        if holder.plugin_fork is not None:
            if not done:
                # Closing waits for the plugin to be gone, which one that
                # hangs is not.
                _kill_plugin(holder.plugin_fork.plugin_process)
            try:
                holder.plugin_fork.close()
            except Exception:
                pass


# The lists that are the ids of what a call of the same name gives.
_ID_LISTS = ('systems', 'pools', 'volumes', 'disks', 'fs', 'exports',
             'target_ports')


def _list(client, list_type):
    """See fetch_rows."""
    if list_type in _ID_LISTS:
        return [[item.id] for item in getattr(client, list_type)()]
    if list_type == 'nfs_client_auth':
        return [list(client.export_auth())]
    if list_type == 'access_groups':
        # A row for each initiator, as lsmcli list has them.
        return [[g.id, g.name, init] for g in client.access_groups()
                for init in g.init_ids or ['']]
    return []


# ---------------------------------------------------------------------------
# The command.
# ---------------------------------------------------------------------------


def _wrapper_options(prefix):
    """
    What a wrapper script put in front of lsmcli: -u/--uri and --fork_plugin.
    :return: (uri, plugin executable)
    """
    uri = None
    plugin = None
    i = 0
    while i < len(prefix):
        arg = prefix[i]
        if arg in ('-u', '--uri') and i + 1 < len(prefix):
            uri = prefix[i + 1]
            i += 1
        elif arg == '--fork_plugin' and i + 1 < len(prefix):
            plugin = prefix[i + 1]
            i += 1
        elif arg.startswith('--uri='):
            uri = arg[len('--uri='):]
        elif arg.startswith('--fork_plugin='):
            plugin = arg[len('--fork_plugin='):]
        elif arg.startswith('-u') and len(arg) > 2:
            uri = arg[2:]
        i += 1
    return uri, plugin


def complete(line,
             point,
             breaks,
             lookup_ok=True,
             auto_value=False,
             wrapper_uri=None,
             plugin_exe=None):
    """
    :param line: The command line, COMP_LINE
    :param point: Where the cursor is in it, COMP_POINT
    :param breaks: COMP_WORDBREAKS
    :param lookup_ok: False where lsmcli is an alias or a function, as what
                      it adds to the line is not known
    :param auto_value: Look up values, LSMCLI_AUTO_COMPLETE_VALUE=1
    :return: List of candidates
    """
    return Completer(line, point, breaks, lookup_ok, auto_value, wrapper_uri,
                     plugin_exe).complete()


def _watchdog():
    """
    Ends the process, unless it has started to write the candidates, which it
    then leaves to finish: what is written is all of it or none. As nothing
    has been written the end is a quiet one. The process is ended, as what it
    waits for can be a call into the library that no signal gets it out of, and
    the plugin that was run for --fork_plugin, and what it started, is killed,
    as it would be left behind.
    """
    with _end_lock:
        if _writing:
            return
        for process in _forked_plugin:
            _kill_plugin(process)
        os._exit(0)


def _start_watchdog():
    """Give the whole completion LOOKUP_TIMEOUT seconds, and then end it."""
    timer = threading.Timer(LOOKUP_TIMEOUT, _watchdog)
    timer.daemon = True
    timer.start()


def main(prefix, args):
    """
    Run as lsmcli __complete.
    :param prefix: The arguments in front of __complete
    :param args: The arguments after it
    :return: The exit status, always 0
    """
    try:
        # Nothing but candidates goes to stdout, and nothing is to be written
        # over the command line of the user.
        devnull = os.open(os.devnull, os.O_WRONLY)
        os.dup2(devnull, 2)
        point = None
        breaks = ''
        lookup_ok = True
        line = None
        i = 0
        while i < len(args):
            arg = args[i]
            if arg == '--':
                line = args[i + 1] if i + 1 < len(args) else ''
                break
            if arg == '--point' and i + 1 < len(args):
                point = int(args[i + 1])
                i += 1
            elif arg == '--breaks' and i + 1 < len(args):
                breaks = args[i + 1]
                i += 1
            elif arg == '--no-lookup':
                lookup_ok = False
            i += 1
        if line is None:
            return 0
        if point is None:
            point = len(line)
        auto_value = os.environ.get('LSMCLI_AUTO_COMPLETE_VALUE') == '1'
        wrapper_uri, plugin_exe = _wrapper_options(prefix)

        _start_watchdog()
        replies = complete(line, point, breaks, lookup_ok, auto_value,
                           wrapper_uri, plugin_exe)
        with _end_lock:
            # From here the watchdog leaves the end to this.
            _writing.append(True)
        if replies:
            out = ''.join(r + '\n' for r in replies)
            sys.stdout.buffer.write(out.encode('utf-8', 'surrogateescape'))
            sys.stdout.buffer.flush()
    except BaseException:
        pass
    return 0
