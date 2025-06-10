# Project Plan

## Goals

Initial focus is an ESP32 eBus/Homie operating in the “device role”, see: https://homieiot.github.io/specification/#roles

The code will attempt to discover an eBus/Homie/MQTT broker via mDNS, details TBD.

Upon finding an advertised broker, it will connect, and begin publishing/representing itself on eBus/Homie, as a device, with nodes, and properties.
Details of the device/nodes/properties TBD

The solution/deliverable should take the form of a “toolkit” that someone can use to quickly specify an eBus/Homie device.
Doug proposes “deliverable is a PlatformIO build environment”, that seems good to me.

IMO, the eBus/Homie side is going to be pretty straightforward, but what are the kinds of things that we can support on the “back-side” of the ESP32?
Here is a brainstorm list:
* Current state of (one or more) DI pins
* Current state, and the ability to set/change (one or more DO) pins.
* Current state, and the ability to set/change a PWM output pin
* IDK what other cool Analog or Digital I/Os an ESP32 provides, but aspirationally we support all/most
* Counting pulses on DI pins and reporting those counts is something I could use right away

For “set able” output pins, another eBus/Homie entity in the “controller-role” will publish to "some-topic/set" which the ESP32 will have subscribed to,
and when the ESP32 receives a message on that set topic, it will do whatever needs to be done to implement what that “set” wanted.

To get a bit fancier, integrating Modbus capabilities on the back-end sounds pretty useful.
IDK anything about Modbus, so I will have to go think about it,
but I;m guessing that the ESP32 would go read all the modbus registers it knows about,
and publish their values to eBus/Homie.
And for write-able Modbus registers, these would become “set able” topics on eBus/Homie,
again when some other controller-role published to that “set” topic,
the ESP32 would have subscibed to it, and then to write that to the Modbus register.
Modbus support is not 100% required for the initial version, but we should scope this, and do it if/when we can.

Let’s not worry about the “throttle” idea initally, I think that will be super easy to layer in later on….

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

## Futures

If it is possible to host a MQTT broker on an ESP32, I am super interested to learn about that, and have that capability.
If an ESP32 could BOTH do everything we talk about above as a eBus/Homie entity in the “device role” AND also be an MQTT broker,
that would be HUGE.

Explore the value and/or difficultly/effort of supporting this toolkit on ESPHome
