#pragma once

// Optional hooks for a project's own code (its src/), in every role: setup_app() once, at
// the end of setup(), with the peripherals and features up; loop_app() on every loop(), on
// the loop task. The firmware's defaults do nothing; define either to replace it.
void setup_app();
void loop_app();
