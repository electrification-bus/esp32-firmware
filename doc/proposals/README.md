# Design proposals

Forward-looking design notes for features that are **not yet shipped** (or only partially). They capture intended direction and open questions — they are NOT descriptions of current behavior. For what the firmware does today, see the docs one level up in [`doc/`](../).

| Proposal | Status | Summary |
|---|---|---|
| [DeviceProvisioningAPI.md](DeviceProvisioningAPI.md) | not implemented | A REST endpoint to exchange a device passphrase for per-device MQTT broker credentials. |
| [OTAActivation.md](OTAActivation.md) | partially implemented | OTA disabled by default, opened via an authenticated REST activation window (vs. always-listening firmware-url). |
| [RuntimeConfigViaNVS.md](RuntimeConfigViaNVS.md) | not implemented | NVS-backed runtime credential/config provisioning (boot-time priority, captive-portal lifecycle). |

When a proposal ships, fold its real behavior into the relevant `doc/` page and either delete the proposal or leave a one-line "implemented in X" stub here.
