#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
#
# Copyright (C) 2026 Red Hat, Inc.
#
# Tests the lsmcli bash completion in a real interactive bash, on a pty, with
# a stub 'lsmcli' that has values containing spaces, quotes and the like. What
# test_completion.sh checks is what the completion function puts in COMPREPLY,
# but what readline then does with it depends on the quote the word was started
# with: inside one it replaces only what follows the quote. So type a line,
# press TAB, run it, and compare the word the shell finally reads with the
# value that was meant.

set -u

# Automake's exit code for a skipped test.
SKIP=77

srcdir=${srcdir:-$(dirname "$0")}
completion_script="$(readlink -f "${srcdir}/lsmcli")"

if ! command -v python3 > /dev/null ; then
    echo "Skipping: python3 not available"
    exit ${SKIP}
fi

tmp_dir=$(mktemp -d) || exit 1
trap 'rm -rf "${tmp_dir}"' EXIT

# Run as 'lsmcli list ...' it gives the values, else it prints its arguments,
# one to a line in <>, which is how the test sees the words the shell read.
cat > "${tmp_dir}/lsmcli" <<'STUB'
#!/usr/bin/env bash
if [[ ${1-} != list ]] ; then
    echo BEGIN
    printf '<%s>\n' "$@"
    echo END
    exit 0
fi
# A look-up that hangs, until it is interrupted. The file tells that it is.
if [[ -n ${LSM_PTY_HANG-} ]] ; then
    echo $$ > "${LSM_PTY_HANG}.pid"
    : > "${LSM_PTY_HANG}"
    exec sleep 60
fi
sep=''
for arg in "$@" ; do
    case "${arg}" in -t?*) sep="${arg#-t}" ;; esac
done
for name in 'my vol' 'a"q' "it's" '$x' 'back\slash' $'t\tb' plain1 'x!bogus' 'iq:x' ; do
    printf '%s%sdesc\n' "${name}" "${sep}"
done
STUB
chmod +x "${tmp_dir}/lsmcli"

# bash and readline as they come, whatever the user's own settings are.
: > "${tmp_dir}/inputrc"

# Each case is the text typed, TAB, and the one word that was meant. Python
# reads it from the environment, one case to a line, as: typed<TAB>meant, with
# \n and \t standing for newline and tab so the table stays readable.
export LSM_PTY_SCRIPT="${completion_script}"
export LSM_PTY_DIR="${tmp_dir}"
export INPUTRC="${tmp_dir}/inputrc"

python3 -I - <<'PYEOF'
import os
import pty
import re
import select
import signal
import sys
import time

SKIP = 77
TIMEOUT = 10

# Word typed so far | value that has to be read back.
CASES = [
    ("my", "my vol"),
    ('"my', "my vol"),
    ("'my", "my vol"),
    ('a', 'a"q'),
    ('"a', 'a"q'),
    ("'a", 'a"q'),
    ("it", "it's"),
    ('"it', "it's"),
    ("'it", "it's"),
    ("\\$", "$x"),
    ('"$', "$x"),
    ("'$", "$x"),
    ("back", "back\\slash"),
    ('"back', "back\\slash"),
    ("'back", "back\\slash"),
    ("$'t", "t\tb"),
    ("t", "t\tb"),
    ('"t', "t\tb"),
    ("plain", "plain1"),
    # A : is a word break character, so the value is more than one of the
    # words bash hands over. Only what follows the last one before the cursor
    # is replaced, which is what is offered of the value.
    ("iq", "iq:x"),
    ("iq:", "iq:x"),
    ("iq:x", "iq:x"),
    # ! is history expansion unless it is escaped or in single quotes.
    ("x", "x!bogus"),
    ('"x', "x!bogus"),
    ("'x", "x!bogus"),
    ("$'x", "x!bogus"),
    # Closed and open parts in one word.
    ('my\\ "vo', "my vol"),
    ("my\\ 'vo", "my vol"),
    ("'my'\\ vo", "my vol"),
    ('"my"\' vo', "my vol"),
]


def run_case(typed, meant):
    """Return None if the shell read the word meant, else what went wrong."""
    env = dict(os.environ, PS1="PROMPT> ", TERM="dumb",
               LSMCLI_AUTO_COMPLETE_VALUE="1",
               PATH=os.environ["LSM_PTY_DIR"] + ":" + os.environ["PATH"])
    pid, fd = pty.fork()
    if pid == 0:
        # Started as a background job, SIGINT is ignored and is inherited.
        signal.signal(signal.SIGINT, signal.SIG_DFL)
        os.execvpe("bash", ["bash", "--norc", "--noprofile", "-i"], env)

    def read_until(pattern):
        data = ""
        end = time.time() + TIMEOUT
        while not re.search(pattern, data):
            wait = end - time.time()
            if wait <= 0:
                return None, data
            ready, _, _ = select.select([fd], [], [], wait)
            if not ready:
                continue
            try:
                chunk = os.read(fd, 4096)
            except OSError:
                return None, data
            if not chunk:
                return None, data
            data += chunk.decode(errors="replace")
        return data, data

    try:
        ok, seen = read_until(r"PROMPT> $")
        if ok is None:
            return "no prompt: %r" % seen
        os.write(fd, ("source '%s'\n" % os.environ["LSM_PTY_SCRIPT"]).encode())
        ok, seen = read_until(r"PROMPT> $")
        if ok is None:
            return "sourcing hung: %r" % seen
        # History expansion is on in an interactive shell, and !bogus is what
        # would be taken for this entry if a ! got through.
        os.write(fd, b"set -H; history -s 'bogus entry'\n")
        ok, seen = read_until(r"PROMPT> $")
        if ok is None:
            return "history setup hung: %r" % seen
        # Readline takes the TAB and the Enter after it in turn.
        os.write(fd, ("lsmcli volume-delete --vol " + typed + "\t\n").encode())
        ok, seen = read_until(r"\nEND\r?\n")
        if ok is None:
            return "the command never ran: %r" % seen
        # The arguments the command got: volume-delete, --vol, and the value.
        lines = seen.split("BEGIN\r\n")[-1].split("END")[0]
        args = re.findall(r"<(.*?)>\r?\n(?=<|$)", lines, re.S)
        if args != ["volume-delete", "--vol", meant]:
            return "read %r, not %r" % (args, meant)
        return None
    finally:
        try:
            os.kill(pid, signal.SIGKILL)
            os.waitpid(pid, 0)
        except OSError:
            pass
        os.close(fd)


def alive(pid):
    """Is there a process, not counting one that is only waiting to be reaped."""
    try:
        with open("/proc/%d/stat" % pid) as handle:
            return handle.read().rsplit(")", 1)[1].split()[0] != "Z"
    except (OSError, IndexError):
        return False


def run_interrupted():
    """A completion that is interrupted leaves the shell as it was."""
    hang = os.path.join(os.environ["LSM_PTY_DIR"], "hanging")
    env = dict(os.environ, PS1="PROMPT> ", TERM="dumb",
               LSMCLI_AUTO_COMPLETE_VALUE="1", LSM_PTY_HANG=hang,
               PATH=os.environ["LSM_PTY_DIR"] + ":" + os.environ["PATH"])
    pid, fd = pty.fork()
    if pid == 0:
        # Started as a background job, SIGINT is ignored and is inherited.
        signal.signal(signal.SIGINT, signal.SIG_DFL)
        os.execvpe("bash", ["bash", "--norc", "--noprofile", "-i"], env)

    def read_until(pattern):
        data = ""
        end = time.time() + TIMEOUT
        while not re.search(pattern, data):
            wait = end - time.time()
            if wait <= 0:
                return None
            ready, _, _ = select.select([fd], [], [], min(wait, 0.2))
            if ready:
                try:
                    chunk = os.read(fd, 4096)
                except OSError:
                    return None
                data += chunk.decode(errors="replace")
        return data

    try:
        for line in ("source '%s'" % os.environ["LSM_PTY_SCRIPT"],
                     "shopt -s nocasematch", "IFS=a"):
            if read_until(r"PROMPT> $") is None:
                return "no prompt before %r" % line
            os.write(fd, (line + "\n").encode())
        if read_until(r"PROMPT> $") is None:
            return "no prompt after setting up"
        os.write(fd, b"lsmcli volume-delete --vol \t")
        end = time.time() + TIMEOUT
        while not os.path.exists(hang) and time.time() < end:
            time.sleep(0.05)
        if not os.path.exists(hang):
            return "the look-up never started"
        os.write(fd, b"\x03")
        # What is left, one command, as the line may hold what was typed.
        # Bash 5.3 clears the line and prints the prompt again, while 5.2
        # leaves the line as it was with a ^C echoed at the end of it, so
        # either is waited for. The echo only says the key got there, so the
        # answer to the command below is what tells that the shell is back,
        # and the look-up is what tells that it is not.
        if read_until(r"(\^C|\x03|PROMPT> $)") is None:
            return "no response to Ctrl-C"
        os.write(fd, b"\x15IFS=$' \\t\\n'; echo NC_$(shopt -q nocasematch && echo yes || echo no)"
                 b" VARS=$(compgen -v | grep -c '^_lsm')\n")
        seen = read_until(r"VARS=\d+\r?\n")
        if seen is None:
            return "no answer after Ctrl-C"
        if "NC_yes" not in seen:
            return "nocasematch was not on after Ctrl-C: %r" % seen
        if not re.search(r"VARS=0\r?\n", seen):
            return "variables left after Ctrl-C: %r" % seen
        # The look-up that was running is not left running.
        end = time.time() + 5
        with open(hang + ".pid") as handle:
            child = int(handle.read())
        while alive(child) and time.time() < end:
            time.sleep(0.05)
        if alive(child):
            os.kill(child, signal.SIGKILL)
            return "the look-up (%d) is still running" % child
        return None
    finally:
        try:
            os.kill(pid, signal.SIGKILL)
            os.waitpid(pid, 0)
        except OSError:
            pass
        os.close(fd)


def main():
    try:
        failures = 0
        for typed, meant in CASES:
            problem = run_case(typed, meant)
            if problem:
                failures += 1
                print("FAIL: %r: %s" % (typed, problem))
        problem = run_interrupted()
        if problem:
            failures += 1
            print("FAIL: interrupted look-up: %s" % problem)
        if failures:
            print("%d pty completion test(s) failed" % failures)
            return 1
        print("All pty completion tests passed")
        return 0
    except OSError as err:
        print("Skipping: no pty to run bash on: %s" % err)
        return SKIP


sys.exit(main())
PYEOF
exit $?
