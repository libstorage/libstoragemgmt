#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
#
# Copyright (C) 2026 Red Hat, Inc.
#
# Unit tests for lsm/lsmcli/completion.py, the code behind the bash completion.
# test_completion.py checks what a user sees, this the pieces of it that can
# be checked on their own: what it reads out of cmdline.py, and what it does
# with a word as bash reads it. The Python bindings are staged from the build
# tree, see stage_lsm.sh, so this skips itself when they were not built.

import os
import subprocess
import sys
import tempfile
import unittest

SKIP = 77
BREAKS = " \t\n\"'@><=;|&(:"


class Reading(unittest.TestCase):
    """How a word is read, against what bash makes of it."""

    def bash_reads(self, word):
        out = subprocess.run(["bash", "-c", "printf %s " + word],
                             capture_output=True,
                             check=False,
                             env=dict(os.environ, LC_ALL="C.UTF-8"))
        return out.stdout.decode("utf-8", "surrogateescape")

    def test_unescape_is_what_bash_reads(self):
        from lsm.lsmcli.completion import unescape
        for word in [
                r"a\ b", "'a b'", '"a b"', r'"a\"b\$c\d"', r"a'b'\"c\"",
                "$'a'b", r"x\\y", r"'a'\''b'", "$'a b'c"
        ]:
            self.assertEqual(unescape(word), self.bash_reads(word), word)

    def test_unescape_leaves_ansi_c_escapes_as_typed(self):
        from lsm.lsmcli.completion import unescape
        self.assertEqual(unescape(r"$'t\tb'"), r"t\tb")
        self.assertEqual(unescape(r"$'a\'b'"), "a'b")

    def test_unescape_takes_an_open_quote_and_a_hanging_backslash(self):
        from lsm.lsmcli.completion import unescape
        for word, value in [("'a b", "a b"), ('"a b', "a b"), ("$'a b", "a b"),
                            ("x'a b", "xa b"), ("abc\\", "abc")]:
            self.assertEqual(unescape(word), value, word)

    def test_escape_reads_back_as_the_value(self):
        from lsm.lsmcli.completion import escape
        for name in [
                "sys three", "it's", 'a"q', "$x", "`x`", "back\\slash",
                "a\\\\b", "*", "a&b", "!x", "a!b!", "~x", "#x", "{a,b}", "a;b",
                "(x)", "<x>"
        ]:
            self.assertEqual(self.bash_reads(escape(name)), name, name)


class FromCmdline(unittest.TestCase):
    """What is read out of cmdline.py is what it parses with."""

    def test_every_command_and_alias_has_options(self):
        from lsm.lsmcli import cmdline, completion
        for cmd in cmdline.cmds:
            names, repeat = completion.command_options(cmd["name"])
            wanted = [
                a["name"]
                for a in cmd.get("args", []) + cmd.get("optional", [])
            ]
            for option in wanted:
                self.assertIn(option, names, cmd["name"])
            self.assertTrue(repeat <= set(names))
        for alias, target in cmdline.aliases.items():
            names, _ = completion.command_options(alias)
            full, _ = completion.command_options(target.split(" ")[0])
            self.assertTrue(set(names) <= set(full), alias)
        self.assertIsNone(completion.command_options("nothing"))

    def test_alias_of_list_has_its_type(self):
        from lsm.lsmcli import completion
        self.assertNotIn("--type", completion.command_options("ls")[0])
        self.assertIn("--type", completion.command_options("list")[0])

    def test_repeatable_options_are_the_appended_ones(self):
        from lsm.lsmcli import completion
        self.assertEqual(
            completion.command_options("fs-export")[1],
            {"--root-host", "--ro-host", "--rw-host"})

    def test_force_is_recognised_but_never_offered(self):
        from lsm.lsmcli import completion
        short, long_ = completion.global_options()
        self.assertNotIn("-f", short)
        self.assertNotIn("--force", long_)
        for cmd in completion._CMDS:
            self.assertNotIn("--force",
                             completion.command_options(cmd)[0], cmd)
        # Still read as the option it is, for the rest of the line.
        self.assertIn("f", completion._SHORT_NO_VALUE)

    def test_global_option_tables_are_from_argparse(self):
        from lsm.lsmcli import completion
        self.assertEqual(sorted(completion._SHORT_WITH_VALUE), ["t", "u", "w"])
        self.assertEqual(sorted(completion._LONG_WITH_VALUE),
                         ["fork_plugin", "terse", "uri", "wait"])
        self.assertEqual(sorted(completion._SHORT_NO_VALUE), sorted("PHefbs"))

    def test_every_option_with_a_value_has_one_way_of_completing(self):
        from lsm.lsmcli import cmdline, completion
        ways = {
            "lookup": set(completion._LOOKUPS),
            "free": set(completion._FREE_FORM),
            "choices": set(completion._CHOICE_OPTIONS) | {"--type"},
        }
        for cmd in cmdline.cmds:
            for arg in cmd.get("args", []) + cmd.get("optional", []):
                name = arg["name"]
                found = [w for w, names in ways.items() if name in names]
                self.assertEqual(len(found), 1, "%s %s" % (cmd["name"], name))

    def test_secrets_are_never_completed(self):
        from lsm.lsmcli import completion
        completion.fetch_rows = lambda *a, **k: [["x", "y", "z"]]
        for line in [
                "lsmcli iscsi-chap --in-pass ", "lsmcli x --out-pass x",
                "lsmcli -u ", "lsmcli --uri s", "lsmcli --uri=s",
                "lsmcli -Hbu ", "lsmcli -usim://u:p"
        ]:
            self.assertEqual(
                completion.complete(line, len(line), BREAKS, auto_value=True),
                [], line)

    def test_global_options_hide_the_hidden(self):
        from lsm.lsmcli import completion
        short, long_ = completion.global_options()
        self.assertIn("-u", short)
        self.assertIn("--uri", long_)
        self.assertIn("--help", long_)
        self.assertNotIn("--fork_plugin", long_)

    def test_choices(self):
        from lsm.lsmcli import completion
        self.assertEqual(
            completion.option_choices("volume-replicate-range", "--rep-type"),
            ["CLONE", "COPY"])
        self.assertEqual(completion.option_choices("vwcpu", "--policy"),
                         ["WB", "AUTO", "WT"])
        self.assertEqual(completion.option_choices("x", "--policy"), [])
        self.assertIn(
            "IDENT_ON",
            completion.option_choices("local-led-slot-set", "--state"))


class Completing(unittest.TestCase):
    """A line in, the candidates out, with the look-ups stood in for."""

    def setUp(self):
        from lsm.lsmcli import completion
        self.completion = completion
        self.asked = []
        self.saved = completion.fetch_rows

        def fetch(list_type, wrapper_uri=None, plugin_exe=None):
            self.asked.append((list_type, wrapper_uri))
            return [["vol-1"], ["vol 2"]]

        completion.fetch_rows = fetch

    def tearDown(self):
        self.completion.fetch_rows = self.saved

    def complete(self, line, auto=True, **kw):
        point = line.index("<CUR>") if "<CUR>" in line else len(line)
        line = line.replace("<CUR>", "")
        return self.completion.complete(line,
                                        point,
                                        BREAKS,
                                        auto_value=auto,
                                        **kw)

    def test_names_are_completed_without_a_look_up(self):
        self.assertIn("volume-delete", self.complete("lsmcli vol"))
        self.assertEqual(self.complete("lsmcli vd --v"), ["--vol"])
        self.assertEqual(self.asked, [])

    def test_values_are_looked_up_only_when_asked_to(self):
        line = "lsmcli vd --vol "
        self.assertEqual(self.complete(line, auto=False), [])
        self.assertEqual(self.asked, [])
        self.assertEqual(self.complete(line), ["vol-1", r"vol\ 2"])
        self.assertEqual(self.asked, [("volumes", None)])

    def test_a_value_wanted_is_not_options_when_not_looked_up(self):
        # --vol wants a value, so no option is offered in its place when the
        # look-up is off, not even one the command still allows.
        self.assertEqual(self.complete("lsmcli vm --vol ", auto=False), [])
        self.assertEqual(self.asked, [])

    def test_no_look_up_for_an_alias_or_function(self):
        self.assertEqual(self.complete("lsmcli vd --vol ", lookup_ok=False),
                         [])
        self.assertEqual(self.asked, [])
        self.assertEqual(self.complete("lsmcli vd --", lookup_ok=False),
                         ["--vol"])

    def test_the_wrapper_is_the_only_one_to_choose_the_uri(self):
        self.complete("lsmcli vd --vol ", wrapper_uri="sim://w")
        self.assertEqual(self.asked, [("volumes", "sim://w")])

    def test_no_look_up_with_a_uri_anywhere_on_the_line(self):
        for line in [
                "lsmcli -u sim:// vd --vol ", "lsmcli vd --vol <CUR> -u x",
                "lsmcli --uri sim:// vd --vol ", "lsmcli --uri=x vd --vol ",
                "lsmcli -usim:// vd --vol ", "lsmcli -Hbu x vd --vol ",
                "lsmcli vd --ur=x --vol ", "lsmcli -u --vol ",
                "lsmcli '-u' x vd --vol ", "lsmcli vd -u"
        ]:
            self.assertEqual(self.complete(line, wrapper_uri="sim://w"), [],
                             line)
        self.assertEqual(self.asked, [])

    def test_the_line_filters_nothing(self):
        for line in [
                "lsmcli vd --sys s --pool p --vol ", "lsmcli x --ag a --vol ",
                "lsmcli vd --vol=x --fs f --vol ", "lsmcli $X a>b --vol "
        ]:
            self.assertEqual(self.complete(line), ["vol-1", r"vol\ 2"], line)
        self.assertEqual(len(self.asked), 4)
        self.assertEqual(set(self.asked), {("volumes", None)})

    def test_no_values_in_or_after_a_quote_or_a_dollar(self):
        for word in [
                "'a", '"a', "$'a", "a'b'", 'a"b"', "$a", "a$", "`a", r"a\'",
                r"a\$"
        ]:
            self.assertEqual(self.complete("lsmcli vd --vol " + word), [],
                             word)
        self.assertEqual(self.asked, [])

    def test_backslashes_are_read_and_the_values_escaped(self):
        self.assertEqual(self.complete(r"lsmcli vd --vol vol\ "), [r"vol\ 2"])
        self.assertEqual(self.complete(r"lsmcli vd --vol vo\l-"), ["vol-1"])

    def test_names_are_completed_in_a_quote(self):
        self.assertEqual(self.complete('lsmcli "vol')[:1], ["volume-create"])
        self.assertEqual(self.complete("lsmcli vd '--v"), ["--vol"])

    def test_reply_is_trimmed_to_the_last_word_break(self):
        self.completion.fetch_rows = lambda *a, **k: [[
            "ag", "name", "iqn.1:abc"
        ]]
        self.assertEqual(self.complete("lsmcli ac --init iqn.1:"), ["abc"])
        self.assertEqual(self.complete("lsmcli ac --init iqn.1:a"), ["abc"])
        self.assertEqual(self.complete("lsmcli ac --init iqn"), ["iqn.1:abc"])

    def test_values_with_a_control_character_are_not_offered(self):
        self.completion.fetch_rows = lambda *a, **k: [["a\tb"], ["a\x7f"],
                                                      ["a b"]]
        self.assertEqual(self.complete("lsmcli vd --vol a"), [r"a\ b"])


class Command(unittest.TestCase):
    """lsmcli __complete as it is run."""

    def run_lsmcli(self, *args, **kw):
        return subprocess.run([
            sys.executable,
            os.path.join(self.srcdir, "..", "lsmcli", "lsmcli")
        ] + list(args),
                              capture_output=True,
                              check=False,
                              **kw)

    def test_candidates_and_nothing_else(self):
        out = self.run_lsmcli("__complete", "--point", "9", "--breaks", BREAKS,
                              "--", "lsmcli vc")
        self.assertEqual(out.returncode, 0)
        self.assertEqual(out.stdout.split(), [b"vc", b"vci"])

    def test_quiet_exit_0_on_any_error(self):
        for args in [[], ["--point"], ["--point", "x", "--", "lsmcli"],
                     ["--breaks", BREAKS], ["--", ""], ["--", "'"]]:
            out = self.run_lsmcli("__complete", *args)
            self.assertEqual((out.returncode, out.stderr), (0, b""), args)

    def test_a_value_named_complete_is_not_the_mode(self):
        from lsm.lsmcli.cmdline import _complete_arg_index
        self.assertEqual(_complete_arg_index(["lsmcli", "__complete"]), 1)
        self.assertEqual(
            _complete_arg_index(
                ["lsmcli", "-u", "sim://", "--fork_plugin", "x",
                 "__complete"]), 5)
        self.assertIsNone(
            _complete_arg_index(
                ["lsmcli", "volume-create", "--name", "__complete"]))
        self.assertIsNone(_complete_arg_index(["lsmcli", "-u", "__complete"]))

    def test_a_plugin_that_hangs_is_given_up_on(self):
        import time
        from lsm.lsmcli import completion
        with tempfile.TemporaryDirectory() as tmp:
            plugin = os.path.join(tmp, "hang")
            with open(plugin, "w") as handle:
                # No exec, so the sleep is a process of its own, and has a
                # duration that nothing else has.
                handle.write("#!/bin/sh\nsleep 61.7\n")
            os.chmod(plugin, 0o755)
            start = time.time()
            out = self.run_lsmcli("--fork_plugin",
                                  plugin,
                                  "-u",
                                  "sim://",
                                  "__complete",
                                  "--point",
                                  "16",
                                  "--",
                                  "lsmcli vd --vol ",
                                  env=dict(os.environ,
                                           LSMCLI_AUTO_COMPLETE_VALUE="1"))
            took = time.time() - start
        self.assertEqual((out.returncode, out.stdout), (0, b""))
        self.assertLess(took, completion.LOOKUP_TIMEOUT + 2)
        left = subprocess.run(["pgrep", "-f", "sleep 61[.]7"],
                              capture_output=True,
                              check=False)
        self.assertEqual(left.stdout, b"", "the plugin's sleep is left")

    def test_the_watchdog_leaves_what_is_being_written_alone(self):
        from lsm.lsmcli import completion
        completion._writing.append(True)
        try:
            # Would end this process if it did not.
            completion._watchdog()
        finally:
            completion._writing.clear()

    def test_the_watchdog_ends_what_is_late_and_quietly(self):
        import time
        code = ("import time\n"
                "from lsm.lsmcli import completion\n"
                "completion.LOOKUP_TIMEOUT = 0.3\n"
                "completion.complete = lambda *a, **k: (time.sleep(5), "
                "['x'])[1]\n"
                "import sys\n"
                "sys.exit(completion.main([], ['--', 'lsmcli vol']))\n")
        start = time.time()
        out = subprocess.run([sys.executable, "-c", code],
                             capture_output=True,
                             check=False)
        self.assertLess(time.time() - start, 3)
        self.assertEqual((out.returncode, out.stdout), (0, b""))

    def test_the_password_is_not_in_what_is_written(self):
        secret = "S3cr3tMarker"
        env = dict(os.environ,
                   LSMCLI_AUTO_COMPLETE_VALUE="1",
                   LSMCLI_PASSWORD=secret,
                   LSMCLI_URI="nosuchscheme://u:%s@h" % secret)
        for line in ["lsmcli vd --vol ", "lsmcli vol", "lsmcli -u "]:
            out = self.run_lsmcli("__complete",
                                  "--point",
                                  str(len(line)),
                                  "--",
                                  line,
                                  env=env)
            self.assertEqual(out.returncode, 0)
            self.assertNotIn(secret.encode(), out.stdout + out.stderr, line)

    def test_not_in_the_help(self):
        out = self.run_lsmcli("-h")
        self.assertNotIn(b"__complete", out.stdout + out.stderr)

    srcdir = os.path.dirname(os.path.abspath(__file__))


def main():
    srcdir = os.environ.get("srcdir",
                            os.path.dirname(os.path.abspath(__file__)))
    if os.environ.get("LSM_TEST_UNIT_STAGE"):
        sys.path.insert(0, os.environ["LSM_TEST_UNIT_STAGE"])
        unittest.main(argv=[sys.argv[0]], verbosity=1)
        return 1
    with tempfile.TemporaryDirectory() as root:
        stage = os.path.join(root, "stage")
        staged = subprocess.run(
            [os.path.join(srcdir, "stage_lsm.sh"), srcdir, stage], check=False)
        if staged.returncode != 0:
            return staged.returncode
        env = dict(os.environ,
                   LSM_TEST_UNIT_STAGE=stage,
                   PYTHONPATH=stage,
                   LD_LIBRARY_PATH=os.path.abspath(
                       os.path.join(os.getcwd(), "..", "..", "c_binding",
                                    ".libs")))
        env["srcdir"] = srcdir
        return subprocess.run(
            [sys.executable, os.path.abspath(__file__)], env=env,
            check=False).returncode


if __name__ == "__main__":
    sys.exit(main())
