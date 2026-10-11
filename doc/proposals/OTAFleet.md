# OTA trust and fleet updates

Per-device OTA credentials, signed images, a LAN update server for a fleet, and devices outside the developer's network that phone home for updates. Today's mechanics are in [ESP32OTA.md](../ESP32OTA.md).

## Today

| Question | Answer |
|---|---|
| Who starts an update | A person, with `ebus-esp32` or the setup page. Nothing on the device checks for updates on its own: `ota_start_update()` is called only from the HTTP handler (`src/platform/captive_portal.cpp:867`). |
| Push or pull | Both. `push --ota` uploads the images to the device; `push --pull --url` tells the device where to download them. |
| Device authenticates the host | Yes: the device passphrase (HTTP Basic as `admin`) or a session token, checked before anything is written (`captive_portal.cpp:1085`, `:1178`). |
| Host authenticates the device | No. Plain HTTP on port 80. |
| Credential and image on the wire | In clear. One passphrase per developer, shared by every device built with that overlay. |
| Image authenticity | Not checked ([#37](https://github.com/electrification-bus/esp32-firmware/issues/37)). |

## Per-device OTA credential

A captured passphrase today opens every device that shares it. A per-device credential confines a capture to one device:

```
device_key = HMAC-SHA256(fleet_secret, device_id)
```

`device_id` is the id the device already publishes (its `Device ID:` boot line, `<device-id>.local`, the Homie topic). The host learns it from mDNS, from `monitor --serial` (cached in `build/monitor-hosts.json`), or from a record made at the first USB flash.

Where the derivation runs:

| Option | Device holds | Cost |
|---|---|---|
| Device derives its key at boot | `fleet_secret` | Reading one device's flash yields every device's key. Flash encryption would be needed to hold the secret. |
| Host derives the key when it provisions the device (recommended) | its own `device_key` only | The key must reach the device at the first flash, for example in `config.yml` or NVS. Reading one device's flash yields one key. |

Either way the host keeps `fleet_secret` and computes any device's key on demand, so no per-device table is needed. Rotation means a new `fleet_secret` and re-provisioning every device.

Over plain HTTP the derived key is still captured by anyone watching that device's traffic. Two ways to keep it off the wire:

- HTTPS on the device's API ([#26](https://github.com/electrification-bus/esp32-firmware/issues/26)).
- Challenge-response: the device issues a nonce, and the host proves knowledge of `device_key` with an HMAC over the nonce and the request.

## Signed images

[#37](https://github.com/electrification-bus/esp32-firmware/issues/37): the build signs both images and the device refuses one whose signature does not verify. The credential controls who may start an update; the signature controls what may be installed. Both fleet designs below depend on it, because the images pass through servers the developer does not watch.

## LAN update server for a fleet

`ebus-esp32 serve` publishes one build for one pull. A fleet server extends it:

- **Holds versioned images** per environment, with their signatures.
- **Knows the fleet:** devices from mDNS (`_ebus._tcp`, and `_device-info._tcp`, whose TXT record carries `fw_version`: `lib/ebus_core/discovery/include/ebus/discovery/txt_records.h:103`).
- **Rolls an update out** by sending each device the existing pull request (`POST /api/v1/filesystem`, `/api/v1/firmware` with a URL), authenticated with that device's key, and watching `GET /api/v1/ota`.

The devices need no change for this beyond the credential and signature work.

## Phone-home updates from the cloud

A device outside the developer's network cannot be reached, so it asks:

1. On a timer, the device sends an authenticated HTTPS request to an update service: its id, environment and current version.
2. The service answers either "no update" or a manifest: image URLs (for example presigned S3 URLs), versions and signatures.
3. The device downloads through the existing pull path (`src/platform/ota.cpp`, which follows redirects and verifies HTTPS against `/certs/<Host>.ca`), checks the signatures, installs, and reports the result on its next check.

What this adds to the firmware:

- A phone-home client: interval, service URL and CA in `config.yml`, and a device credential (the per-device key above, or a client certificate).
- Refusing an unverified HTTPS connection to the update service, rather than the `setInsecure()` fallback the pull path uses today (`ota.cpp:178`).
- Recovery from a bad image. ESP-IDF's app rollback is already on in the precompiled framework (`CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y`), and Arduino marks a new image valid during startup unless the firmware overrides the weak `verifyRollbackLater()` (`cores/esp32/esp32-hal-misc.c`). Overriding it lets the firmware call `esp_ota_mark_app_valid_cancel_rollback()` only once it reaches the broker; an image still unconfirmed at the next restart is abandoned for the previous one.

## Where the pieces live

| Piece | Home |
|---|---|
| Per-device credential, signature checks, phone-home client | this repo |
| Signing at build time, key derivation, LAN fleet commands | `ebus-esp32`, this repo |
| Cloud update service and its AWS deployment | a repository of its own |

## Open questions

- How a device's key reaches it at provisioning, and how the host records the device id at that moment.
- Whether the LAN fleet server is an `ebus-esp32` command or a separate service.
- The phone-home protocol: an existing one, or a small manifest format of our own.
