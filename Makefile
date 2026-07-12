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
