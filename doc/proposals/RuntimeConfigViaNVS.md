# Proposal: runtime credential provisioning via NVS

**Status:** design note — not implemented. Filed 2026-05-31 alongside the file-based config overlay (Proposal 2, implemented).

## Context

This SDK currently loads device configuration (MQTT broker, WiFi credentials, device id, Homie role, OTA passphrase, etc.) from a YAML file baked into the LittleFS partition (`data/config.yml`). The recently-landed Proposal 2 fixed the "developers clobber each other's `config.yml`" problem by making the file a per-developer overlay at `config-overlay.yml`, merged into the generated `data/config.yml` at build time. That solves the *development ergonomics* half of the problem. This proposal addresses the half it leaves open: secrets-handling and field deployment.

## Problems Proposal 2 does not solve

1. **Credentials are written to the LittleFS image in cleartext.** Anyone with USB serial access (or remote serial access via a TCP-to-serial bridge) can read `mqtt_password` and `device_passphrase` directly from the boot-time config dump (`CONFIG: YAML config:'/config.yml'` followed by the full parsed JSON). Fixing the cleartext-on-console log is a one-line firmware change, but the underlying issue — secrets live in a flashable artifact — remains.
2. **Rotating credentials requires a re-flash.** Changing the MQTT broker host or password for a deployed unit means rebuilding the LittleFS image, taking the unit offline, and re-uploading the filesystem partition (either over USB or HTTP OTA). This is fine for the dev loop but unacceptable for field-deployed units.
3. **Provisioning at scale is impossible.** Every unit ships with the same baked-in `data/config.yml`. To deploy 50 units to 50 different sites, today's workflow requires 50 distinct LittleFS images. Doug's captive portal exists but is bypassed in favour of the file-baked config when MQTT credentials are present.
4. **End users can't self-provision.** The current captive portal hands out an HTTP API, but credentials still come from the file. An end customer can't change the WiFi SSID or MQTT broker without us shipping them a new firmware build.

## Proposed approach

Make NVS (the ESP32's non-volatile-storage key-value partition) the primary credential store, and treat `data/config.yml` as a fallback / default-values source. Captive portal handles first-boot provisioning and ongoing rotation.

### Boot-time config resolution priority

```
1. NVS namespace "ebus_creds"  ← end-user / operator state
2. data/config.yml from LittleFS ← developer / factory defaults
3. Hardcoded compile-time defaults  ← last-resort safety net
```

On a freshly-flashed device, NVS is empty, so config.yml supplies defaults. After the user provisions via captive portal, NVS overrides — and `./ebus-esp32 push --fs-only` no longer wipes their provisioning. (Currently it does, because the filesystem partition gets replaced.)

### Captive portal lifecycle

- Boot in **AP mode** (ssid `ebus-<MAC-suffix>`, no password or factory-default password printed on a sticker) if:
  - NVS has no MQTT broker configured, AND
  - WiFi-station credentials in NVS fail to connect within N seconds, AND
  - Ethernet link is not up (POE-ISO falls through to WiFi fallback)
- Captive portal serves a single-page form: WiFi SSID/password, MQTT broker host/port/username/password, device id (or "auto-derive from MAC").
- On submit, validate by attempting WiFi connect + MQTT connect. Only persist to NVS on success.
- Provide a "factory reset" endpoint authenticated by `device_passphrase` that wipes the `ebus_creds` namespace and re-enters provisioning mode.

### Required firmware changes

| Area | Change |
|---|---|
| `include/config.h` (or new `include/config_store.h`) | Add a `ConfigStore` interface with `load()` / `store()` / `clear()` returning `bool`; provide three implementations: `NVSConfigStore`, `YAMLConfigStore`, `DefaultsConfigStore`. |
| Boot path | Replace the existing single-source config load with a priority chain: NVS → YAML → defaults. Log which source won, but never log secret values. |
| Captive portal HTTP server | Add `POST /api/credentials` (write to NVS), `POST /api/factory-reset` (wipe NVS, reboot), `GET /api/status` (which source is active, redacted). |
| Serial console dump | Redact `mqtt_password` and `device_passphrase` in the existing boot-time config dump. (Independent of NVS work — could land sooner.) |
| Partition table | Verify `partitions_ota.csv` has enough NVS space. Default NVS is ~24 KB; our key set is small (well under 1 KB), so almost certainly fine but worth confirming. |

### Open questions

1. **Schema versioning.** When firmware vN+1 adds a new field, how does it handle NVS state written by vN? Three options: (a) field-by-field load with defaults, (b) embedded `config_schema_version` int in NVS with explicit migration, (c) force re-provisioning on mismatch. (a) is simplest and probably right.
2. **Factory-reset trigger.** A hardware mechanism is more reliable than an HTTP endpoint for users who've lost their credentials. Options: long-press of the Olimex POE-ISO's USER button at boot (if there is one wired); GPIO short to ground (BTN pin held low during boot); JTAG-based wipe (developer-only).
3. **Fleet provisioning.** For deploying 50+ units, manual portal-per-device doesn't scale. Defer to a separate proposal — likely involving a signed config bundle uploaded via OTA, or per-unit NVS pre-provisioning during manufacturing.
4. **ESPHome variant.** If the C++ core is later extracted and wrapped in an ESPHome packaging, ESPHome already has its own NVS-backed `preferences` API. Our `ConfigStore` interface should be implementable on top of either ESPHome `preferences` or raw `nvs_flash` calls — keep the interface narrow enough that this is straightforward.

## Relationship to Proposal 2

Composable. After Proposal 3 lands, Proposal 2's overlay still serves a real purpose: it's where developers put their *defaults* for fresh-flashed units. The flow becomes:

```
clone repo → # optional: create config-overlay.yml with only the keys you want to override
→ edit config-overlay.yml with your dev defaults
→ ./ebus-esp32 push --fs-only → device boots, NVS empty, falls back to YAML defaults → MQTT connects
                                                                      → device runs
later, in the field:
end user opens captive portal → enters their own broker creds → NVS overrides YAML → device reboots
                                                                                   → connects to their broker
```

Proposal 3 doesn't replace Proposal 2 — it makes the YAML "default values" instead of "live config."

## Effort estimate

Rough order: a few days of focused firmware work for the core (ConfigStore abstraction, NVS-first load, captive portal form, factory reset). Open questions could expand scope (schema versioning, hardware factory-reset) but most can be deferred to v2. The cleartext-secrets-in-serial-log fix is a one-line change and could go in immediately as a tactical win.

## Things to discuss with Doug

- Does any existing code rely on `data/config.yml` being authoritative after boot (e.g. periodic re-read)? If so, the NVS-first model breaks that.
- Is the captive portal HTTP server (`PORTAL: Starting HTTP API server`) already wired for POST handling, or is it currently read-only?
- His preference on the ConfigStore abstraction shape — a single interface with three implementations, or three free-standing functions with a priority-chain caller?
- Is there a reason NVS isn't already used for credentials? (May be intentional — Doug may have already considered and rejected it.)
