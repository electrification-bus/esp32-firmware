# esp32-sdk — CLI build / flash / monitor wrappers around PlatformIO.
#
# Designed for a terminal + Emacs workflow (M-x compile RET make build RET);
# the PlatformIO VSCode extension is unrelated and unaffected.
#
# device.yml is the source of truth for the device definition. `make build`
# (and every flash target) runs `pio run`, whose pre-build hook regenerates
# src/homie.cpp + data/*.yml from device.yml first — so editing device.yml is
# enough; the generated files are gitignored. The ./ebus-esp32 CLI wraps the
# same generate + build + flash flow (see doc/GettingStarted_CLI.md); this
# Makefile additionally provides the tmux serial-port handoff coordination.
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
# First-time config bootstrap (optional — device.yml builds without it):
#   make config-init            # creates ~/.config/eBus/config.yml from example
#   $EDITOR ~/.config/eBus/config.yml         # set your broker / credentials
#   make config-status          # confirm overlay present
#
# PIO_ENV (not ENV) is used to avoid collision with the shell's ENV
# variable, which on macOS is commonly preset to ~/.kshrc.

PIO_ENV ?= esp32-poe-iso
PIO     ?= ./bin/pio
PORT    ?=

UPLOAD_PORT_FLAG  := $(if $(PORT),--upload-port $(PORT))
MONITOR_PORT_FLAG := $(if $(PORT),--port $(PORT))

# Per-developer config overlay (your broker, credentials, device id — secrets
# that must stay out of the tracked repo). data/config.yml is GENERATED from
# device.yml by the pre-build hook and gitignored; the overlay's non-empty
# fields are merged on top. The overlay path is overridable via CONFIG_DIR.
CONFIG_DIR     ?= $(HOME)/.config/eBus
CONFIG_OVERLAY := $(CONFIG_DIR)/config.yml
CONFIG_EXAMPLE := data/config.yml.example

.DEFAULT_GOAL := help

.PHONY: help generate build flash fs uploadfs flash-all monitor clean devices \
        envs check config-status config-init compiledb monitors

help: ## Show this help and exit
	@printf 'esp32-sdk make targets (default PIO_ENV=$(PIO_ENV)):\n\n'
	@awk 'BEGIN {FS = ":.*## "} /^[a-zA-Z_-]+:.*## / {printf "  \033[36m%-12s\033[0m %s\n", $$1, $$2}' $(MAKEFILE_LIST)
	@printf '\nVariables:\n'
	@printf '  PIO_ENV=<env>     PlatformIO env (default: $(PIO_ENV))\n'
	@printf '  PORT=<dev>        Override serial port (default: pio auto-detect)\n'
	@printf '  CONFIG_DIR=<dir>  Config overlay dir (default: $(CONFIG_DIR))\n'

generate: ## Regenerate src/homie.cpp + data/*.yml from device.yml (also auto-runs before every build)
	@./ebus-esp32 generate

build: ## Compile firmware (regenerates from device.yml first, via the pre-build hook)
	$(PIO) run -e $(PIO_ENV)

flash: ## Upload firmware to device over USB (auto-pauses/restarts tmux monitor)
	@./bin/_flash-coord pause "$(PORT)"
	@trap './bin/_flash-coord restart "$(PORT)"' EXIT INT TERM; \
	 $(PIO) run -e $(PIO_ENV) -t upload $(UPLOAD_PORT_FLAG)

fs: ## Build LittleFS image from data/ (no upload)
	$(PIO) run -e $(PIO_ENV) -t buildfs

uploadfs: ## Upload LittleFS image over USB (auto-pauses/restarts tmux monitor)
	@./bin/_flash-coord pause "$(PORT)"
	@trap './bin/_flash-coord restart "$(PORT)"' EXIT INT TERM; \
	 $(PIO) run -e $(PIO_ENV) -t uploadfs $(UPLOAD_PORT_FLAG)

flash-all: ## Upload firmware + filesystem (auto-pauses/restarts tmux monitor)
	@./bin/_flash-coord pause "$(PORT)"
	@trap './bin/_flash-coord restart "$(PORT)"' EXIT INT TERM; \
	 $(PIO) run -e $(PIO_ENV) -t upload -t uploadfs $(UPLOAD_PORT_FLAG)

monitor: ## Open serial monitor (records breadcrumb if in tmux, for flash coordination)
	@./bin/_monitor-record "$(PIO_ENV)" "$(PORT)"
	@trap './bin/_monitor-release "$(PORT)"' EXIT INT TERM; \
	 $(PIO) device monitor -e $(PIO_ENV) $(MONITOR_PORT_FLAG)

clean: ## Clean build artifacts for the current env
	$(PIO) run -e $(PIO_ENV) -t clean

devices: ## List attached serial devices
	$(PIO) device list --serial

monitors: ## List active serial monitor sessions (live breadcrumbs in .run/)
	@./bin/_monitors-list

envs: ## List PlatformIO envs defined in platformio.ini
	@$(PIO) project config --json-output 2>/dev/null | \
		python3 -c "import json,sys; [print(s[0][4:]) for s in json.load(sys.stdin) if s[0].startswith('env:')]"

check: ## Run pio static analysis (cppcheck)
	$(PIO) check -e $(PIO_ENV)

compiledb: ## Regenerate compile_commands.json for clangd / LSP
	$(PIO) run -e $(PIO_ENV) -t compiledb

config-status: ## Show config overlay state (data/config.yml is generated + gitignored)
	@printf 'overlay path:    %s\n' '$(CONFIG_OVERLAY)'
	@if [ -f '$(CONFIG_OVERLAY)' ]; then \
		printf 'overlay state:   present (%s bytes) — merged into data/config.yml on every build\n' "$$(wc -c < '$(CONFIG_OVERLAY)' | tr -d ' ')"; \
	else \
		printf 'overlay state:   MISSING (optional — device.yml still builds; run `make config-init` to add one)\n'; \
	fi
	@printf 'example path:    %s\n' '$(CONFIG_EXAMPLE)'
	@printf 'data/config.yml: generated from device.yml + overlay by the pre-build hook (gitignored)\n'

config-init: ## Bootstrap personal config overlay from data/config.yml.example
	@if [ -f '$(CONFIG_OVERLAY)' ]; then \
		printf 'overlay already exists: %s\n' '$(CONFIG_OVERLAY)'; \
		printf 'edit it directly; this target will not clobber\n'; \
	else \
		mkdir -p '$(CONFIG_DIR)'; \
		cp '$(CONFIG_EXAMPLE)' '$(CONFIG_OVERLAY)'; \
		printf 'created %s\n' '$(CONFIG_OVERLAY)'; \
		printf 'edit it with your MQTT broker, credentials, device id, etc.\n'; \
		printf 'it is merged into data/config.yml automatically on the next build\n'; \
	fi
