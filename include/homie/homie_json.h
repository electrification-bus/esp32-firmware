#pragma once
// JSON (de)serialization for the Homie tree, kept OUT of Property.h / Node.h.
//
// These were member functions, which forced <ArduinoJson.h> into those headers — and
// Property.h reaches driver translation units (any driver implementing
// entity_settable_callback(Property*) dereferences it). ArduinoJson is ~20,000
// preprocessed lines, so it dominated the compile cost of every such driver while being
// needed only by the $description publisher and the controller's discovery path.
//
// They are free functions rather than members precisely so the declaration can live here.
// Nothing needed private access: serialization reads public accessors and from_dict()
// writes through public setters, so no friendship is involved.
//
// Include this only where JSON is actually handled — src/homie/Device.cpp, the
// controller, and homie_json.cpp itself.

#include <ArduinoJson.h>

class Property;
class Node;

// Populate a Property from one entry of a $description properties object. Used by the
// CONTROLLER when building a device tree from a discovered description; a plain device
// never calls it.
void property_from_dict(Property& property, JsonObject* props_obj);

// Write a Property / Node into a $description object. Node recurses into its properties.
void property_serialize_into(Property& property, JsonObject& obj);
void node_serialize_into(Node& node, JsonObject& obj);
