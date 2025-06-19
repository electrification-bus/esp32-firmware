# Project Plan

## Goals

Initial focus is an ESP32 eBus-Homie system [operating in the “device role”](https://homieiot.github.io/specification/#roles)

The code will attempt to discover an eBus-Homie/MQTT broker via mDNS, details TBD.

Upon finding an advertised broker, it will connect, and begin publishing/representing itself on eBus-Homie, as a device, with nodes, and properties.
Details of the device/nodes/properties TBD

The solution/deliverable should take the form of a “toolkit” that someone can use to quickly specify an eBus-Homie device.
Doug proposes “deliverable is a PlatformIO build environment”, that seems good to me.

IMO, the eBus-Homie side is going to be pretty straightforward, but what are the kinds of things that we can support on the “back-side” of the ESP32?
Here is a brainstorm list:
* Current state of (one or more) DI pins
* Current state, and the ability to set/change (one or more DO) pins.
* Current state, and the ability to set/change a PWM output pin
* IDK what other cool Analog or Digital I/Os an ESP32 provides, but aspirationally we support all/most
* Counting pulses on DI pins and reporting those counts is something I could use right away

For “set able” output pins, another eBus-Homie entity in the “controller-role” will publish to "some-topic/set" which the ESP32 will have subscribed to,
and when the ESP32 receives a message on that set topic, it will do whatever needs to be done to implement what that “set” wanted.

To get a bit fancier, integrating Modbus capabilities on the back-end sounds pretty useful.
IDK anything about Modbus, so I will have to go think about it,
but I;m guessing that the ESP32 would go read all the modbus registers it knows about,
and publish their values to eBus-Homie.
And for write-able Modbus registers, these would become “set able” topics on eBus-Homie,
again when some other controller-role published to that “set” topic,
the ESP32 would have subscibed to it, and then to write that to the Modbus register.
Modbus support is not 100% required for the initial version, but we should scope this, and do it if/when we can.

Let’s not worry about the “throttle” idea initally, I think that will be super easy to layer in later on….

### mDNS

The device should resolve/claim its `.local` hostname, and this hostname should be configurable

### MQTT Broker Discovery via mDNS

The device should attempt to discover the eBus-Homie MQTT broker via mDNS service-type advertisements by the broker,
if found, attempt to connect to that broker. mDNS service-type details for MQTT are TBD.

MQTT authentication TBD, initially the user/password should be configurable.
Maybe someday there will be some automated/assisted way for the device to obtain these credentials.

### MQTT Transport

* MQTT (initially)
* MQTTS (future)

### Firmware Updates

The ability to update the ESP32 firmware over the network (AKA OTA) is a highly desirable feature.

Does PlatformIO provide that capability?

The ESPHome system/framework has excellent support for this.

### Debug Console

The ability to connect to the debug console over the network is highly desirable, ESPHome provides this capability

## Futures

* Develop a [CTA-2045/EcoPort UCM](https://github.com/electrification-bus/Open-EcoPort) based on this toolkit.
* Explore the value and/or difficultly/effort of supporting this toolkit on ESPHome
* If it is possible to host a MQTT broker on an ESP32, I am super interested to learn about that, and have that capability. If an ESP32 could BOTH do everything we talk about above as a eBus-Homie entity in the “device role” AND also be an MQTT broker, that would be HUGE.

# Declarative Specification of ESP32-eBus Devices

## ESPHome Example

Below is the complete specification for a ESPHome application that counts pulses from three water meters, and publishes the counts to MQTT.

Things to note:
* The ESP32 GPIO pins are defined and configured via this specfication
* ESPHome functional plugins/modules are referenced and used, i.e. `pulse_counter`

Aspirationally this project would provide a similar capability.
Note: If in order to support this, some sort of "compiler" from YAML to C++ would be required, that is potentially something that DCJ could develop.

```
esphome:
  name: esphome-web-90f4f8
  friendly_name: Water-Meters

esp32:
  board: esp32-poe-iso
  framework:
    type: arduino

# Enable logging
logger:
  level: DEBUG

# MQTT
mqtt:
  broker: 192.168.128.175
  username: foo
  password: SECRET

# mDNS discovery
mdns:
  disabled: false

ota:
  platform: esphome

ethernet:
  type: LAN8720
  mdc_pin: GPIO23
  mdio_pin: GPIO18
  clk_mode: GPIO17_OUT
  phy_addr: 0
  power_pin: GPIO12
  # manual_ip:
    # static_ip: 192.168.128.190
    # gateway: 192.168.128.1
    # subnet: 255.255.255.0

sensor:
  - platform: pulse_counter
    pin:
      number: 13
      inverted: true
      mode:
        input: true
        pullup: true
    name: "Water_House_Cold_PPM"
    update_interval: 5s
    total:
      name: "Water_House_Cold"
  - platform: pulse_counter
    pin:
      number: 5
      inverted: true
      mode:
        input: true
        pullup: true
    name: "Water_House_Hot_PPM"
    update_interval: 5s
    total:
      name: "Water_House_Hot"
  - platform: pulse_counter
    pin:
      number: 16
      inverted: true
      mode:
        input: true
        pullup: true
    name: "Water_Irrigation_PPM"
    update_interval: 5s
    total:
      name: "Water_Irrigation"
```

## eBus-Homie Device Representation

Both the Homie-prefix and Homie-version should be configuable.
Note that eBus specifies and uses the Homeie-prefix `ebus` (instead of `homie`)

### eBus-Homie Device

The `deviceID` should be configurable, the current `deviceID` of `esp32poe3A3D78` is fine for now.
The device should publish the `$description` attribute detailing the nodes and their properties (See below).
The device `type` in `$description` should be configurable, and is TBD.

### eBus-Homie Nodes

We will likely end up with several nodes.
Let's start with a a node to represent the ESP32 itself, so mcu
Then add properties underneath that.
Almost anything OK.
* macaddress
* anything about the model number of the MCU, vendor, etc.
* version number of software
ATM, just publish SOMETHING that is easy, even if you have to make something up.
So, as a result, there should be something like this published:
`homie/5/esp32poe3A3D78/mcu/some-nodeID/some-propertyID` and its value is some string

## eBus-Homie Schemas

A eBus-Homie entity supporting the "device role" publishes a `$description` of it's nodes and properties, which are JSON objects, an example:
```
"temperature": {
        "name": "Engine temperature",
        "unit": "°C",
        "datatype": "float",
        "format": "-20:120"
      }
```
A YAML specification for an ESP32-eBus device would include the information required to generate the property's `$description` schema.


## Modbus Register Schemas

When we support Modbus backends, then ideally they are included/supported in the declarative system specification.

Here is a [brainstormed example, from ChatGPT](https://chatgpt.com/share/684ad914-4da8-800f-93d1-2e03d1b7a498)

Developing a comprehensive device declarative device specification including ESP32 config, Homie/eBus schemas, and Modbus register schemas seems within the realm of possibility...

```
type: object
properties:
  name:
    type: string
    description: Human-readable name of the register
  address:
    type: integer
    description: Modbus register address (0-based)
  function:
    type: integer
    enum: [1, 2, 3, 4]
    description: Modbus function code (1=coil, 2=input, 3=holding, 4=input register)
  type:
    type: string
    enum: [int16, uint16, int32, uint32, float32]
    description: Data type of the register
  unit:
    type: string
    description: Unit of measurement (e.g., V, A, W)
  scale:
    type: number
    description: Multiplier to apply to raw value
  description:
    type: string
    description: Optional human-friendly description of the register
required:
  - name
  - address
  - function
  - type
```

## Confidentiality

If we need an NDA, we can of course do one, but I would prefer that we try to avoid the need.
I have no interest nor desire to know about your bucket algorithm, especially if that is your special intellectual property.
Seems like this is something you can use and layer into products you may provide based on this tookit,
my guess is your bucket stuff is a cool way to implement “throttle-like” things.  Bully for you, and fine by me,
but unless there is a real need for me to see any of that, I’d prefer not to be contaminated with. your IP.

Same goes for me, if there is something secret, I will either keep you out of it, or we can do an NDA.
This project may become interesting to SPAN, but I’m not doing to for them, they aren’t paying for this,
this is MINE! (or OURS!)

## Intellectual Property Ownership

The idea is virtually all the results will become open-source software, that I will host under the electrification-bus GitHub org.
My intention is that you guys would be clearly identified as the developers of the code you write (for compensation), unless you DON’T WANT THAT.
and that I would be clearly identified as the person who funded this work.
If it turns out I end up doing any code development, I get credited for that also.

I haven’t thought about which license to use, but it is not going to be a viral GPL,
you (or anyone else) will be free to incorporate this into products and/or services
you sell.  So, if you want to implement your buckets thing to manage energy and sell that to someone, more power to you.
