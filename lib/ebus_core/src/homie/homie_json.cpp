#include <homie/homie_json.h>
#include <homie/Property.h>
#include <homie/Node.h>
#include <homie/homie.h>        // HOMIE_* key macros
#include <util/jsonUtils.h>     // jsonExists
#include <string.h>

// Bodies moved verbatim from Property::from_dict / Property::serializeInto /
// Node::serializeInto, rewritten to use the public API so they need no friendship.

void property_from_dict(Property& property, JsonObject* props_obj) {
    if (jsonExists((*props_obj)[HOMIE_ID]) && (*props_obj)[HOMIE_ID].is<const char*>()) {
        property.setId((*props_obj)[HOMIE_ID].as<const char*>());
    }
    if (jsonExists((*props_obj)[HOMIE_NAME]) && (*props_obj)[HOMIE_NAME].is<const char*>()) {
        property.setName((*props_obj)[HOMIE_NAME].as<const char*>());
    }
    if (jsonExists((*props_obj)[HOMIE_DATATYPE]) && (*props_obj)[HOMIE_DATATYPE].is<const char*>()) {
        property.setDatatype((*props_obj)[HOMIE_DATATYPE].as<const char*>());
    }
    if (jsonExists((*props_obj)[HOMIE_FORMAT]) && (*props_obj)[HOMIE_FORMAT].is<const char*>()) {
        property.setFormat((*props_obj)[HOMIE_FORMAT].as<const char*>());  // required for enum/color (C1)
    }
    if (jsonExists((*props_obj)[HOMIE_UNIT]) && (*props_obj)[HOMIE_UNIT].is<const char*>()) {
        property.setUnit((*props_obj)[HOMIE_UNIT].as<const char*>());
    }
    if (jsonExists((*props_obj)[HOMIE_SETTABLE]) && (*props_obj)[HOMIE_SETTABLE].is<bool>()) {
        property.setSettable((*props_obj)[HOMIE_SETTABLE].as<bool>());
    }
    if (jsonExists((*props_obj)[HOMIE_RETAINED]) && (*props_obj)[HOMIE_RETAINED].is<bool>()) {
        property.setRetained((*props_obj)[HOMIE_RETAINED].as<bool>());
    }
    if (jsonExists((*props_obj)[HOMIE_VALUE])) {
        if ((*props_obj)[HOMIE_VALUE].is<bool>()) {
            property.setValue((*props_obj)[HOMIE_VALUE].as<bool>());
        }
        if ((*props_obj)[HOMIE_VALUE].is<float>()) {
            property.setValue((*props_obj)[HOMIE_VALUE].as<float>());
        }
        if ((*props_obj)[HOMIE_VALUE].is<const char*>()) {
            property.setValue((*props_obj)[HOMIE_VALUE].as<const char*>());
        }
        if ((*props_obj)[HOMIE_VALUE].is<int>()) {
            property.setValue((*props_obj)[HOMIE_VALUE].as<int>());
        }
        //TODO all types
    }
}

void property_serialize_into(Property& property, JsonObject& obj) {
    obj[HOMIE_NAME]     = property.name();
    obj[HOMIE_DATATYPE] = property.datatype();
    if (property.settable())          obj[HOMIE_SETTABLE] = true;
    if (!property.retained())         obj[HOMIE_RETAINED] = false;
    if (strlen(property.format()) != 0) obj[HOMIE_FORMAT] = property.format();
    if (strlen(property.unit())   != 0) obj[HOMIE_UNIT]   = property.unit();
}

void node_serialize_into(Node& node, JsonObject& obj) {
    obj[HOMIE_NAME] = node.name();
    obj[HOMIE_TYPE] = node.type();
    JsonObject props = obj[HOMIE_PROPERTIES].to<JsonObject>();
    for (int i = 0; i < node.numProperties(); i++) {
        Property* p = node.propertyAt(i);
        if (!p) continue;
        JsonObject prop_obj = props[p->id()].to<JsonObject>();
        property_serialize_into(*p, prop_obj);
    }
}
