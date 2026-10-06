# Proposal: finish runtime configuration via NVS

**Status:** partially implemented.

## What exists today

`data/config.yml` is generated from `device.yml` (plus an optional `config-overlay.yml`) and flashed in the LittleFS image. At boot, `portal_prefs_load_fallbacks()` ([`include/platform/portal_prefs.h`](../../include/platform/portal_prefs.h)) overrides it with any values saved in NVS through the device's web UI:

| Setting | Saved by | Stored in NVS |
|---|---|---|
| WiFi SSID and password | `POST /api/wifi` (WiFi-AP builds) | yes |
| MQTT host, port, username and password | `POST /api/mqtt` | yes |
| Device passphrase | `POST /api/passphrase` | yes |
| Firmware and filesystem image URLs | `POST /api/firmware`, `POST /api/filesystem` | yes, unless `persist=0` |

NVS is outside the filesystem partition, so these values survive `./ebus-esp32 push --fs-only` and filesystem OTA. `POST /api/reset-prefs` clears them so the next boot uses `config.yml` alone. A recovery copy of the last good passphrase is kept in NVS so the admin endpoints still work when no config can be loaded. The boot-time config dump redacts secrets before printing, because the serial log is also served on the TCP console.

## Remaining problems

1. **Secrets still ship in the filesystem image.** Any MQTT password or passphrase in `device.yml` is written in cleartext to `data/config.yml` and flashed to every unit built from it, and can be read back from flash over USB.
2. **The access point's lifetime is not tied to provisioning.** WiFi-AP builds start an open access point at every boot and keep it running after the station connects, whether or not the device is already configured.
3. **Saved settings are not validated.** `POST /api/mqtt` persists the new broker settings before any connection attempt, so a typo leaves the device unable to reach a broker until someone reaches the web UI again.
4. **There is no hardware reset.** Clearing NVS requires the passphrase. A device whose passphrase is lost needs a USB reflash with `./ebus-esp32 push --erase`.

## Proposed changes

- **Credentials only in NVS for field builds.** Add a build mode in which `generate` leaves secrets out of `config.yml`, so the image carries defaults only and each unit is provisioned through the web UI (or through [DeviceProvisioningAPI.md](DeviceProvisioningAPI.md)).
- **Provisioning-driven access point.** Start the access point only when no usable network configuration exists (no station credentials that connect within a timeout and no Ethernet link), and stop it once the station or Ethernet is up. Give it a per-device password when one is available.
- **Validate before persisting.** Apply submitted WiFi and MQTT settings in RAM, attempt the connection, and write them to NVS only on success; otherwise revert and report the failure.
- **Hardware reset.** Clear the portal settings when a board button or strap pin is held at boot, on boards that have one.

## Open questions

1. **Schema evolution.** When firmware adds a new NVS key, load each key independently with a default (simplest) or store a schema version and migrate?
2. **Reset input.** Which GPIO on each supported board is safe to read at boot without conflicting with Ethernet or flash strapping (see [GPIOPins.md](../GPIOPins.md))?
3. **Fleet provisioning.** Per-unit configuration for many devices likely belongs in [DeviceProvisioningAPI.md](DeviceProvisioningAPI.md) or in manufacturing-time NVS images, not in the web UI.
