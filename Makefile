include emulator_configuration.mk

.PHONY: clean
clean:
	pebble clean

.PHONY: build
build:
	pebble build || pebble build

.PHONY: kill_emulator
kill_emulator:
	pebble kill

.PHONY: wipe_emulator
wipe_emulator:
	pebble wipe

# Wipes the emulator, reinstalls, then immediately disables idle auto-exit -
# a wiped watch's built-in idle timeout is short enough that a manual
# multi-step test walkthrough (each `pebble` CLI call has multiple seconds of
# overhead) can silently get auto-exited mid-sequence otherwise. `pebble
# install` auto-launches the app, so no extra button press is needed here -
# blindly pressing select risks landing on the (now-empty, post-wipe) list's
# "+ New timer" row and creating a stray draft timer instead. Always use this
# instead of bare `pebble wipe` when about to drive the emulator by hand.
.PHONY: wipe_and_prep_emulator
wipe_and_prep_emulator:
	pebble kill || true
	pebble wipe
	pebble install --emulator emery
	sleep 3
	$(MAKE) send_emulator_configuration

.PHONY: install_emulator
install_emulator:
	pebble install --emulator emery

.PHONY: install_cloudpebble
install_cloudpebble:
	pebble install --cloudpebble

.PHONY: build_and_install_emulator
build_and_install_emulator: build install_emulator

.PHONY: build_and_install_cloudpebble
build_and_install_cloudpebble: build install_cloudpebble

.PHONY: send_emulator_configuration
send_emulator_configuration:
	pebble send-app-message --emulator emery \
		--app-uuid 1df6fc5c-261d-49c7-b339-6ea60cbe6649 \
		--int $(EMULATOR_CFG_INT_ARGS)

.PHONY: send_emulator_timers
send_emulator_timers:
	pebble send-app-message --emulator emery \
		--app-uuid 1df6fc5c-261d-49c7-b339-6ea60cbe6649 \
		--string 10000="$$(printf '10 s\03710\036Egg 9 min\037540\036Egg 12 min\037720')" \

.PHONY: long_press_select_emulator
long_press_select_emulator:
	pebble emu-button --emulator emery push select && \
	sleep 0.3 && \
	pebble emu-button --emulator emery release select

.PHONY: create_screenshots
create_screenshots:
	scripts/create_screenshots.sh

.PHONY: test_core
test_core:
	gcc -I src/c tests/test_timer_calc.c src/c/timer_calc.c -o /tmp/pebble-another-timer-test_core
	/tmp/pebble-another-timer-test_core

# Rebuild whenever tests/functional/docker/Containerfile or
# run-sequence-in-container.sh change - NOT needed for app source changes,
# since run_container.sh copies the repo fresh into every container run.
.PHONY: build_functional_test_image
build_functional_test_image:
	podman build --platform linux/amd64 -t pebble-another-timer-tests \
		-f tests/functional/docker/Containerfile .

# Plain pass/fail run of the whole functional suite, containerized (see
# tests/functional/docker/README.md for why this is required rather than
# just recommended) - no golden comparison.
.PHONY: test_functional
test_functional:
	tests/functional/run_all.sh

# What CI should run: the full suite, compared against the committed
# golden baseline.
.PHONY: test_functional_golden
test_functional_golden:
	tests/functional/run_all.sh --golden-dir tests/functional/golden

# Re-approves the current output of every sequence as the new golden
# baseline - run this after a deliberate, reviewed UI change, never as a
# way to make a failing test_functional_golden pass without looking at why.
.PHONY: update_functional_golden
update_functional_golden:
	tests/functional/run_all.sh --golden-dir tests/functional/golden --update-golden
