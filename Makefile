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
# First-time config bootstrap:
#   make config-init            # creates ~/.config/eBus/config.yml from example
#   $EDITOR ~/.config/eBus/config.yml
#   make config-status          # confirm overlay present, target in sync
#
# PIO_ENV (not ENV) is used to avoid collision with the shell's ENV
# variable, which on macOS is commonly preset to ~/.kshrc.

PIO_ENV ?= esp32-poe-iso
PIO     ?= ./bin/pio
PORT    ?=

UPLOAD_PORT_FLAG  := $(if $(PORT),--upload-port $(PORT))
MONITOR_PORT_FLAG := $(if $(PORT),--port $(PORT))

# Per-developer config overlay. The overlay file is the source of truth;
# data/config.yml is a derived staging file that gets baked into the
# LittleFS image. The overlay path is overridable via CONFIG_DIR.
CONFIG_DIR     ?= $(HOME)/.config/eBus
CONFIG_OVERLAY := $(CONFIG_DIR)/config.yml
CONFIG_TARGET  := data/config.yml
CONFIG_EXAMPLE := data/config.yml.example

.DEFAULT_GOAL := help

.PHONY: help build flash fs uploadfs flash-all monitor clean devices envs check \
        config-status config-init config-stage compiledb monitors generate

help: ## Show this help and exit
	@printf 'esp32-sdk make targets (default PIO_ENV=$(PIO_ENV)):\n\n'
	@awk 'BEGIN {FS = ":.*## "} /^[a-zA-Z_-]+:.*## / {printf "  \033[36m%-12s\033[0m %s\n", $$1, $$2}' $(MAKEFILE_LIST)
	@printf '\nVariables:\n'
	@printf '  PIO_ENV=<env>     PlatformIO env (default: $(PIO_ENV))\n'
	@printf '  PORT=<dev>        Override serial port (default: pio auto-detect)\n'
	@printf '  CONFIG_DIR=<dir>  Config overlay dir (default: $(CONFIG_DIR))\n'

generate: ## Generate src/homie.cpp + data/homie.yml + data/config.yml from device.yml
	python3 scripts/generate.py device.yml

build: generate ## Compile firmware (auto-generates from device.yml first)
	$(PIO) run -e $(PIO_ENV)

flash: ## Upload firmware to device over USB (auto-pauses/restarts tmux monitor)
	@./bin/_flash-coord pause "$(PORT)"
	@trap './bin/_flash-coord restart "$(PORT)"' EXIT INT TERM; \
	 $(PIO) run -e $(PIO_ENV) -t upload $(UPLOAD_PORT_FLAG)

fs: config-stage ## Build LittleFS image from data/ (no upload)
	$(PIO) run -e $(PIO_ENV) -t buildfs

uploadfs: config-stage ## Upload LittleFS image over USB (auto-pauses/restarts tmux monitor)
	@./bin/_flash-coord pause "$(PORT)"
	@trap './bin/_flash-coord restart "$(PORT)"' EXIT INT TERM; \
	 $(PIO) run -e $(PIO_ENV) -t uploadfs $(UPLOAD_PORT_FLAG)

flash-all: config-stage ## Upload firmware + filesystem (auto-pauses/restarts tmux monitor)
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

config-status: ## Show config overlay paths and sync state
	@printf 'overlay path:   %s\n' '$(CONFIG_OVERLAY)'
	@if [ -f '$(CONFIG_OVERLAY)' ]; then \
		printf 'overlay state:  present (%s bytes)\n' "$$(wc -c < '$(CONFIG_OVERLAY)' | tr -d ' ')"; \
	else \
		printf 'overlay state:  MISSING — run `make config-init`\n'; \
	fi
	@printf 'target path:    %s\n' '$(CONFIG_TARGET)'
	@if [ -f '$(CONFIG_TARGET)' ]; then \
		if [ -f '$(CONFIG_OVERLAY)' ] && cmp -s '$(CONFIG_TARGET)' '$(CONFIG_OVERLAY)'; then \
			printf 'target state:   in sync with overlay\n'; \
		else \
			printf 'target state:   present but DRIFTED from overlay (run `make config-stage`)\n'; \
		fi \
	else \
		printf 'target state:   MISSING (run `make config-stage` or any fs target)\n'; \
	fi
	@printf 'example path:   %s\n' '$(CONFIG_EXAMPLE)'

config-init: ## Bootstrap personal config overlay from data/config.yml.example
	@if [ -f '$(CONFIG_OVERLAY)' ]; then \
		printf 'overlay already exists: %s\n' '$(CONFIG_OVERLAY)'; \
		printf 'edit it directly; this target will not clobber\n'; \
	else \
		mkdir -p '$(CONFIG_DIR)'; \
		cp '$(CONFIG_EXAMPLE)' '$(CONFIG_OVERLAY)'; \
		printf 'created %s\n' '$(CONFIG_OVERLAY)'; \
		printf 'edit it with your MQTT broker, credentials, device id, etc.\n'; \
		printf 'then any of: make fs / make uploadfs / make flash-all\n'; \
	fi

config-stage: $(CONFIG_TARGET) ## Copy overlay → data/config.yml (idempotent)

$(CONFIG_TARGET): $(CONFIG_OVERLAY)
	@printf 'staging %s -> %s\n' '$(CONFIG_OVERLAY)' '$(CONFIG_TARGET)'
	@cp '$(CONFIG_OVERLAY)' '$(CONFIG_TARGET)'

$(CONFIG_OVERLAY):
	@printf 'error: config overlay missing at %s\n' '$(CONFIG_OVERLAY)' >&2
	@printf 'bootstrap with: make config-init\n' >&2
	@exit 1
