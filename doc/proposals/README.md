# Design proposals

Forward-looking design notes for features that are **not yet shipped** (or only partially). They capture intended direction and open questions — they are NOT descriptions of current behavior. For what the firmware does today, see the docs one level up in [`doc/`](../).

| Proposal | Status | Summary |
|---|---|---|
| [DeviceProvisioningAPI.md](DeviceProvisioningAPI.md) | not implemented | A REST endpoint to exchange a device passphrase for per-device MQTT broker credentials. |
| [OTAActivation.md](OTAActivation.md) | implemented | Stub: the OTA window shipped as `POST /api/ota`; see [ESP32OTA.md](../ESP32OTA.md). |
| [RuntimeConfigViaNVS.md](RuntimeConfigViaNVS.md) | partially implemented | Portal settings already override `config.yml` from NVS; remaining: secrets out of the filesystem image, an access point tied to provisioning, validate-before-save, and a hardware reset. |
| [DriverLayering.md](DriverLayering.md) | partially implemented | BLE transport (`lib/ble/ble_client`) and generic `ble/gatt_device` shipped; JBD driver moved to `examples/bms/`; generator now synthesizes properties from `device.yml` (`modbus/register_device`). Policy split into `control/` still outstanding. |
| [LibraryExtraction.md](LibraryExtraction.md) | partially implemented | Split the Homie layer and drivers into a standalone CMake + PlatformIO library behind three seams (MQTT port, log, settable registry). Stage 0 (dead includes) done; decoupling stages 1–4 and the repo split outstanding. |

When a proposal ships, fold its real behavior into the relevant `doc/` page and either delete the proposal or leave a one-line "implemented in X" stub here.
