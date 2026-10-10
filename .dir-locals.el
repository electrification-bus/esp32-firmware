;;; Directory-local Emacs settings for esp32-firmware.
;;; Applies to all files under this directory.
;;;
;;; Counterpart to .vscode/ for Emacs users:
;;;   - VSCode picks up .vscode/{extensions,settings,tasks}.json automatically.
;;;   - Emacs picks up .dir-locals.el automatically (with one-time safety prompt
;;;     the first time you open a file under this tree — hit `!` to whitelist).
;;;
;;; Currently sets two things:
;;;   - compile-command — runs `./ebus-esp32 build` from the repo root, regardless
;;;     of which subdirectory the current buffer lives in. The
;;;     `$(git rev-parse --show-toplevel)` shell substitution finds the root
;;;     dynamically, so a fresh clone elsewhere works without edits.
;;;   - 4-space indent style for C / C++ (matches the existing codebase).
;;;
;;; For full IDE-quality editing (autocomplete, jump-to-def, inline errors),
;;; you'll also want Eglot + clangd hooks in your personal `~/.emacs`.
;;; clangd auto-discovers `compile_commands.json` at this repo's root —
;;; regenerate it with `pio run -t compiledb` whenever it gets stale.

((nil          . ((compile-command                    . "cd $(git rev-parse --show-toplevel) && ./ebus-esp32 build")
                  (projectile-project-compilation-cmd . "./ebus-esp32 build")))
 (c-mode       . ((c-basic-offset                     . 4)
                  (indent-tabs-mode                   . nil)))
 (c++-mode     . ((c-basic-offset                     . 4)
                  (indent-tabs-mode                   . nil)))
 (c-ts-mode    . ((c-ts-mode-indent-offset            . 4)
                  (indent-tabs-mode                   . nil)))
 (c++-ts-mode  . ((c++-ts-mode-indent-offset          . 4)
                  (indent-tabs-mode                   . nil))))
