EXTRA_DIST = lsmcli test_completion.sh test_sim_completion.sh \
	test_pty_completion.sh test_cmdline_drift.sh

if WITH_BASH_COMPLETION
bashcompletion_DATA = lsmcli
# test_sim_completion.sh skips itself when the simulator plugin or the Python
# bindings were not built, and test_pty_completion.sh when there is no python3
# or pty. test_cmdline_drift.sh reads cmdline.py as text, so it needs no build.
TESTS = test_completion.sh test_sim_completion.sh test_pty_completion.sh \
	test_cmdline_drift.sh
endif
