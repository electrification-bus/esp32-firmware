# Getting Started — VSCode / PlatformIO IDE workflow

This is the VSCode-centric onboarding. If you'd rather work from a terminal (Makefile, `pio` CLI, Emacs/vim), see [`GettingStarted_CLI.md`](GettingStarted_CLI.md) instead — same project, different editor surface.

> **Configuration overlays**: both onboarding paths use per-developer overlay files for MQTT credentials, PlatformIO build tweaks, etc. After the setup steps below, read [`Configuration.md`](Configuration.md) once to understand where your personal `config.yml` and `platformio_local.ini` live and how they're merged with the repo's tracked defaults.

## Overview
This is what this is

## Dev Environment Installation Guide
### Prerequisites
- A computer with internet connection
- ESP32 dev board variant <TBD>
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
4. `git clone https://github.com/<TBD>

### Step 4: Open the Project in VSCode
1. In VSCode, click on the PlatformIO icon in the left sidebar
2. Select "Open Project" from the PlatformIO home screen
3. Navigate to the cloned repository folder and select it
4. Wait for VSCode to load the project and initialize PlatformIO

### Step 5: Configure the Project
1. Wait for PlatformIO to download all required dependencies (libraries)
  **IMPORTANT:** Set the environment to <TBD>
  - Open the platformio.ini file in the project root
  - Make sure the environment section contains [<TBD>] or similar
  - If not, add or modify the environment section to match the <TBD>

### Step 6: Build and Flash the Firmware
1. Connect your ESP32 to your computer via USB
2. In VSCode, click on the PlatformIO icon in the left sidebar
3. Select "Project Tasks" from the menu
4. Under "General", click "Build" to compile the project
5. After successful build, click "Upload" to flash the firmware to your device
6. Monitor the progress in the terminal window at the bottom of VSCode

## Custom tasks

Beyond the standard PlatformIO IDE "Project Tasks" sidebar (Build / Upload / Upload Filesystem Image / Monitor / etc.), this project ships four extra tasks in [`.vscode/tasks.json`](../.vscode/tasks.json) that aren't covered by the PIO extension. They wrap the per-developer overlay + LSP helpers that only exist as Makefile targets.

Trigger via **Cmd-Shift-P → "Tasks: Run Task"** (Ctrl-Shift-P on Windows/Linux):

| Task | Wraps | When to use |
|---|---|---|
| **Config: Bootstrap personal overlay** | `make config-init` | Once after a fresh clone — creates `~/.config/eBus/config.yml` from the example template |
| **Config: Show overlay status** | `make config-status` | Sanity-check that your overlay is present and `data/config.yml` is in sync |
| **Config: Stage overlay → data/config.yml** | `make config-stage` | Re-sync `data/config.yml` after editing the overlay (PlatformIO's fs build also auto-stages it) |
| **LSP: Regenerate compile_commands.json** | `make compiledb` | After fresh clone, after `lib_deps` changes, or when IntelliSense gets confused |

See [`Configuration.md`](Configuration.md) for the full per-developer overlay model that these tasks support.

> **Windows note**: these tasks shell out to `make`. Git for Windows' Git Bash bundles it; alternatively `choco install make` or `winget install GnuWin32.Make`. macOS / Linux have it out of the box.

### Troubleshooting

- If you encounter upload errors, ensure that:
  - The correct USB port is selected (can be changed in platformio.ini)
  - You have proper USB drivers installed for your development board
  - Your board is in bootloader mode (if required)
- Check the PlatformIO documentation for additional help: https://docs.platformio.org/
