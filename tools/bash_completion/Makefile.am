EXTRA_DIST = lsmcli test_completion.py test_complete_unit.py \
	test_sim_completion.sh test_pty_completion.sh stage_lsm.sh \
	stub_complete.py

if WITH_BASH_COMPLETION
bashcompletion_DATA = lsmcli
# The completion is done by lsmcli, so every test needs the Python bindings and
# skips itself when they were not built, and when there is no python3 or pty.
# test_sim_completion.sh also needs the simulator plugin.
TESTS = test_completion.py test_complete_unit.py test_sim_completion.sh \
	test_pty_completion.sh
endif
