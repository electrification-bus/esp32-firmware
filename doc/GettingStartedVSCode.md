# Getting Started — VSCode / PlatformIO IDE workflow

This is the VSCode-centric onboarding. If you'd rather work from a terminal (`./ebus-esp32`, `pio` CLI, Emacs/vim), see [`GettingStartedCLI.md`](GettingStartedCLI.md) instead — same project, different editor surface.

> **Configuration overlays**: both onboarding paths use per-developer overlay files for MQTT credentials, PlatformIO build tweaks, etc. After the setup steps below, read [`Configuration.md`](Configuration.md) once to understand where your personal `config.yml` and `platformio_local.ini` live and how they're merged with the repo's tracked defaults.

## Overview
This walks you from a fresh machine to building and flashing the firmware in VSCode.

> **Builds run through `./ebus-esp32` in VSCode's integrated terminal, not the extension's
> Build button.** The tracked PlatformIO config lives at
> [`platformio/platformio.ini`](../platformio/platformio.ini) rather than the repo root, so the
> PlatformIO extension does not adopt this folder as one of its projects — deliberately:
> when it did, its background `pio` runs used the root config, which never lists a
> component's `generator.lib_deps`, so it pruned the libraries a CLI build had just
> installed and wiped `.pio` mid-build. The header in `platformio/platformio.ini` has the
> detail.
>
> Practical effect: the env switcher, Project Tasks tree and Build/Upload buttons are not
> available here, and PlatformIO's IntelliSense index is not populated — run
> `./ebus-esp32 compiledb` and use clangd for code navigation. If you would rather have
> the IDE workflow, copy `platformio/platformio.ini` to the repo root (`.gitignore` keeps it
> out of git) and accept the occasional wiped build tree.

The project's `./ebus-esp32 generate` step runs automatically as a PlatformIO pre-build
hook, so editing `device.yml` and building is enough. (Everything here also works from any
terminal — see [`GettingStartedCLI.md`](GettingStartedCLI.md).)

## Dev Environment Installation Guide
### Prerequisites
- A computer with internet connection
- An ESP32 board — the default target is the **Olimex ESP32-POE-ISO**; the full list of supported boards/envs is in the README's *Hardware Targets* table
- USB cable for connecting the development board to your computer

### Step 1: Install Visual Studio Code
1. Download Visual Studio Code from https://code.visualstudio.com/
2. Follow the installation instructions for your operating system:
  - **Windows:** Run the installer and follow the prompts
  - **macOS:** Drag the application to your Applications folder

### Step 2: Install PlatformIO Extension and Serial Monitor extension

1. Open VSCode
2. Click on the Extensions icon in the left sidebar (or press Ctrl+Shift+X)
3. Search for "PlatformIO IDE"
4. Click "Install" on the PlatformIO IDE extension
5. Wait for the installation to complete (this may take a few minutes)
6. Search for "Serial Monitor" and select the one from Microsoft (if there are multiple)
7. Click "Install" on the PlatformIO IDE extension, wait for installation to compleete.
8. Restart VSCode when prompted

### Step 3: Clone the Repository
1. Open a terminal/command prompt
2. Navigate to the directory where you want to store the project
3. Clone the repository using git:
4. `git clone https://github.com/electrification-bus/esp32-sdk.git`

### Step 4: Open the Project in VSCode
1. In VSCode, choose **File → Open Folder…** and select the cloned repository folder. Use this rather than PlatformIO's "Open Project"; the note at the top of this page explains why.
2. Open the integrated terminal (**View → Terminal**). Every command below runs there, from the repo root.

### Step 5: Select the board environment
1. Set `environment:` in the `device:` section of `device.yml` to the PlatformIO env matching your board. The default is **`esp32-poe-iso`** (Olimex ESP32-POE-ISO); `./ebus-esp32 envs` lists the rest, and the README's *Hardware Targets* table describes them.
2. You normally don't edit `platformio/platformio.ini` to switch boards. For per-developer build/upload tweaks, use `platformio_local.ini` (see [`Configuration.md`](Configuration.md)).

### Step 6: Build and Flash the Firmware
1. Connect your ESP32 to your computer via USB.
2. `./ebus-esp32 build` compiles the firmware and filesystem image. The first build downloads the platform and libraries and can take several minutes.
3. `./ebus-esp32 run` builds and flashes both; add `--port <port>` if more than one board is attached (`./ebus-esp32 devices` lists them).
4. `./ebus-esp32 monitor --serial` opens the USB serial console.

## Custom tasks

This project ships one task in [`.vscode/tasks.json`](../.vscode/tasks.json). It wraps the LSP helper for regenerating the compilation database.

Before you build for the first time, create your per-developer config overlay from a terminal (this is a manual step, not a VSCode task). There is no template to copy: write `config-overlay.yml` in the repo root with only the keys you want to override, using `device.yml`'s own sections and key names. `device.device_passphrase` is required, because `generate` refuses an empty or `changeme` value:

```yaml
# config-overlay.yml
device:
  device_passphrase: a-passphrase-of-your-own
wifi:
  ssid: my-network
  password: hunter2
mqtt:
  host: broker.example.com
  username: me
  password: s3cret
```

Its non-empty fields are merged into the generated `data/config.yml` automatically on every build (a PlatformIO pre-build hook runs `./ebus-esp32 generate` first), so there's no separate sync step.

Trigger the custom task via **Cmd-Shift-P → "Tasks: Run Task"** (Ctrl-Shift-P on Windows/Linux):

| Task | Equivalent CLI | When to use |
|---|---|---|
| **LSP: Regenerate compile_commands.json** | `./ebus-esp32 compiledb` | After fresh clone, after `lib_deps` changes, or when IntelliSense gets confused |

See [`Configuration.md`](Configuration.md) for the full per-developer overlay model.

### Troubleshooting

- If you encounter upload errors, ensure that:
  - The correct USB port is selected (`./ebus-esp32 run --port <port>`, or `upload_port` in `platformio_local.ini`)
  - You have proper USB drivers installed for your development board
  - Your board is in bootloader mode (if required)
- Check the PlatformIO documentation for additional help: https://docs.platformio.org/
