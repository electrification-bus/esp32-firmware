# Security policy

## Reporting a vulnerability

Report vulnerabilities privately through GitHub's private vulnerability reporting: open the repository's **Security** tab and choose **Report a vulnerability**, or go directly to <https://github.com/electrification-bus/esp32-firmware/security/advisories/new>. Do not open a public issue, discussion or pull request for a vulnerability.

Include the board, the PlatformIO environment, the commit you built, and the steps to reproduce. Remove Wi-Fi, MQTT and device passphrase values from any `device.yml`, `config-overlay.yml` or log you attach.

## Supported versions

Only the `main` branch is supported. Fixes land on `main`; there are no maintained release branches.
