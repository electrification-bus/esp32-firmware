#pragma once

// Optional convenience constants for the registered eBus vocabulary.
//
// The Homie `type` attribute is free-form; the SDK stays generic and accepts ANY type
// string (like the python-sdk). These constants are purely a convenience so callers can
// write `EBUS_DEVICE_BESS` instead of a hand-typed (and easy-to-mistype) literal. They
// are NOT required and impose no validation.
//
// Canonical registries (the source of truth — update these when the registries change):
//   energy.ebus.device.*      specification/registries/device-types.md
//   energy.ebus.capability.*  specification/registries/capability-types.md
//   https://github.com/electrification-bus/specification/tree/main/registries
//
// Device `type` is a device's role; node `type` (a capability) describes what a node
// exposes. Buffer sizing for these long strings is HOMIE_TYPE_MAXLEN (see homie.h).

// --- Device types (energy.ebus.device.*) -------------------------------------
#define EBUS_DEVICE_DISTRIBUTION_ENCLOSURE "energy.ebus.device.distribution-enclosure" // panel / load center, parent of circuits & feeds
#define EBUS_DEVICE_CIRCUIT                "energy.ebus.device.circuit"                 // one branch circuit within an enclosure
#define EBUS_DEVICE_LUGS                   "energy.ebus.device.lugs"                    // a feed point (upstream/downstream lugs)
#define EBUS_DEVICE_BESS                   "energy.ebus.device.bess"                    // Battery Energy Storage System
#define EBUS_DEVICE_PV                     "energy.ebus.device.pv"                      // photovoltaic inverter
#define EBUS_DEVICE_EVSE                   "energy.ebus.device.evse"                    // EV Supply Equipment
#define EBUS_DEVICE_MID                    "energy.ebus.device.mid"                     // Microgrid Interconnect Device
#define EBUS_DEVICE_BRIDGE                 "energy.ebus.device.bridge"                  // standalone proxy host for non-native devices

// --- Capability (node) types (energy.ebus.capability.*) ----------------------
#define EBUS_CAP_INFO          "energy.ebus.capability.info"          // device identity & metadata
#define EBUS_CAP_CONFIG        "energy.ebus.capability.config"        // settable runtime configuration
#define EBUS_CAP_STATUS        "energy.ebus.capability.status"        // operational status
#define EBUS_CAP_CONNECTION    "energy.ebus.capability.connection"    // wiring topology (up/downstream)
#define EBUS_CAP_METER         "energy.ebus.capability.meter"         // power/energy/voltage/current measurements
#define EBUS_CAP_SWITCH        "energy.ebus.capability.switch"        // switchable on/off control
#define EBUS_CAP_DOOR          "energy.ebus.capability.door"          // enclosure door state
#define EBUS_CAP_GRID          "energy.ebus.capability.grid"          // grid connection / islanding state
#define EBUS_CAP_GRID_FORMING  "energy.ebus.capability.grid-forming"  // per-inverter grid-forming capability/state
#define EBUS_CAP_SOC           "energy.ebus.capability.soc"           // state-of-charge & energy (storage)
#define EBUS_CAP_PCS           "energy.ebus.capability.pcs"           // UL 3141 Power Control System (CSL)
#define EBUS_CAP_POWER_FLOWS   "energy.ebus.capability.power-flows"   // aggregate flows (grid/battery/solar/load)
#define EBUS_CAP_PRIORITY      "energy.ebus.capability.priority"      // per-circuit load-shed policy
#define EBUS_CAP_SHED          "energy.ebus.capability.shed"          // enclosure-wide shed-policy controls
#define EBUS_CAP_SHED_FORECAST "energy.ebus.capability.shed-forecast" // backup-runtime forecast
#define EBUS_CAP_DOE           "energy.ebus.capability.doe"           // utility dynamic operating envelope
#define EBUS_CAP_DEMAND        "energy.ebus.capability.demand"        // peak/average demand (demand-charge billing)
#define EBUS_CAP_POWER_QUALITY "energy.ebus.capability.power-quality" // THD/TDD, harmonics, unbalance, PQ metrics
