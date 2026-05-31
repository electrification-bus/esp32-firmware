# esp32-sdk — CLI build / flash / monitor wrappers around PlatformIO.
#
# Designed for a terminal + Emacs workflow (M-x compile RET make build RET);
# the PlatformIO VSCode extension is unrelated and unaffected.
#
# Examples:
#   make build                  # compile firmware for the default env
#   make flash                  # upload firmware over USB
#   make fs uploadfs            # build LittleFS image then upload it
#   make flash-all              # firmware + filesystem in one shot
#   make monitor                # open serial monitor (Ctrl-T h for menu)
#   make build PIO_ENV=esp32-s3-devkitc-1     # one-off env override
#   make flash PORT=/dev/cu.usbserial-21130   # one-off port override
#
# PIO_ENV (not ENV) is used to avoid collision with the shell's ENV
# variable, which on macOS is commonly preset to ~/.kshrc.

PIO_ENV ?= esp32-poe-iso
PIO     ?= ./bin/pio
PORT    ?=

UPLOAD_PORT_FLAG  := $(if $(PORT),--upload-port $(PORT))
MONITOR_PORT_FLAG := $(if $(PORT),--port $(PORT))

.DEFAULT_GOAL := help

.PHONY: help build flash fs uploadfs flash-all monitor clean devices envs check

help: ## Show this help and exit
	@printf 'esp32-sdk make targets (default PIO_ENV=$(PIO_ENV)):\n\n'
	@awk 'BEGIN {FS = ":.*## "} /^[a-zA-Z_-]+:.*## / {printf "  \033[36m%-12s\033[0m %s\n", $$1, $$2}' $(MAKEFILE_LIST)
	@printf '\nVariables:\n'
	@printf '  PIO_ENV=<env>  PlatformIO env (default: $(PIO_ENV))\n'
	@printf '  PORT=<dev>     Override serial port (default: pio auto-detect)\n'

build: ## Compile firmware
	$(PIO) run -e $(PIO_ENV)

flash: ## Upload firmware to device over USB
	$(PIO) run -e $(PIO_ENV) -t upload $(UPLOAD_PORT_FLAG)

fs: ## Build LittleFS image from data/ (no upload)
	$(PIO) run -e $(PIO_ENV) -t buildfs

uploadfs: ## Upload LittleFS image to device over USB
	$(PIO) run -e $(PIO_ENV) -t uploadfs $(UPLOAD_PORT_FLAG)

flash-all: ## Upload firmware + filesystem in one invocation
	$(PIO) run -e $(PIO_ENV) -t upload -t uploadfs $(UPLOAD_PORT_FLAG)

monitor: ## Open serial monitor (Ctrl-T h for menu, Ctrl-T q to quit)
	$(PIO) device monitor -e $(PIO_ENV) $(MONITOR_PORT_FLAG)

clean: ## Clean build artifacts for the current env
	$(PIO) run -e $(PIO_ENV) -t clean

devices: ## List attached serial devices
	$(PIO) device list --serial

envs: ## List PlatformIO envs defined in platformio.ini
	@$(PIO) project config --json-output 2>/dev/null | \
		python3 -c "import json,sys; [print(s[0][4:]) for s in json.load(sys.stdin) if s[0].startswith('env:')]"

check: ## Run pio static analysis (cppcheck)
	$(PIO) check -e $(PIO_ENV)
