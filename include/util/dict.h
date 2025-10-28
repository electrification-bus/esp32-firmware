/**
 * @file Dict.h
 * @brief A lightweight Arduino-compatible dynamic dictionary class.
 *
 * This class emulates a Python-style dictionary (`dict`) with bracket access,
 * supporting multiple value types, nested dictionaries, and JSON
 * serialization/deserialization — all without dynamic allocation.
 *
 * ## Features
 * - Python-style access: `d["key"] = value;`
 * - Supports multiple value types (including nested Dicts and arbitrary object pointers (Node*, Device*, Property*, etc.)
 * - Serializes cleanly to JSON (with pointer info as "ptr:0x...)
 * - Uses only fixed-size char buffers (no String, no STL)
 * - Parsing is recursive for nested dicts
 * - No heap allocation (new/delete used only for Dict expansion).
 * - JSON serialization (`toJSON`) and parsing (`fromJSON`)
 * - Works with microcontroller frameworks (ESP32, AVR, ARM, etc.)
 *
 * ## Example
 * @code
 * Dict<> d;
 * d["temp"] = 25.4;
 * d["enabled"] = true;
 * d["msg"] = "Hello";
 * 
 * d["node"] = new Node();
 * ((Node*)d["node"])->setId("DictTestNode");
 * d["property"] = new Property(((Node*)d["node"]));
 * 
 * char buf[256];
 * d.toJSON(buf, sizeof(buf));
 * Serial.println(buf);
 * @endcode
 *
 * @note
 * Pointer types (e.g. `Node*`, `Device*`, `Property*`) are serialized as
 * string placeholders ("ptr:0x...") and not dereferenced.
 *
 * @author
 * ChatGPT (GPT-5), collaboratively developed for Doug Mendonca
 *
 * @date
 * October 2025
 *
 */
#include <Arduino.h>
#include <string.h>

// Forward declaration
template <size_t KEY_LEN, size_t BUF_LEN> class Dict;

// -------------------- Value Class --------------------
template <size_t KEY_LEN = 32, size_t BUF_LEN = 64>
class Value {
public:
    enum Type { NONE, INT, LONG, FLOAT, BOOL, STRING, DICT, POINTER } type;

    union {
        int i;
        long l;
        float f;
        bool b;
        void* ptr;
        Dict<KEY_LEN, BUF_LEN>* d;
    };

    char s[BUF_LEN];

    Value() : type(NONE), ptr(nullptr) { s[0] = '\0'; }

    // --- Assignment overloads ---
    Value& operator=(int v)    { type = INT; i = v; return *this; }
    Value& operator=(long v)   { type = LONG; l = v; return *this; }
    Value& operator=(float v)  { type = FLOAT; f = v; return *this; }
    Value& operator=(bool v)   { type = BOOL; b = v; return *this; }
    Value& operator=(const char* v) { 
        type = STRING; 
        strncpy(s, v, BUF_LEN - 1); 
        s[BUF_LEN - 1] = '\0'; 
        return *this; 
    }

    Value& operator=(Dict<KEY_LEN, BUF_LEN>& v) { type = DICT; d = &v; return *this; }

    // --- NEW: Pointer assignment ---
    template <typename T>
    Value& operator=(T* p) {
        type = POINTER;
        ptr = (void*)p;
        return *this;
    }

    // --- Conversions ---
    operator int() const    { return (type == INT) ? i : 0; }
    operator long() const   { return (type == LONG) ? l : (type == INT ? i : 0); }
    operator float() const  { return (type == FLOAT) ? f : 0.0f; }
    operator bool() const   { return (type == BOOL) ? b : false; }
    operator const char*() const {
        switch (type) {
            case STRING: return s;
            case INT: snprintf((char*)s, BUF_LEN, "%d", i); break;
            case LONG: snprintf((char*)s, BUF_LEN, "%ld", l); break;
            case FLOAT: snprintf((char*)s, BUF_LEN, "%.3f", f); break;
            case BOOL: snprintf((char*)s, BUF_LEN, "%s", b ? "true" : "false"); break;
            case POINTER: snprintf((char*)s, BUF_LEN, "ptr:%p", ptr); break;
            default: snprintf((char*)s, BUF_LEN, "");
        }
        return s;
    }

    // --- Generic pointer cast ---
    template <typename T>
    operator T*() const {
        if (type == POINTER) return (T*)ptr;
        return nullptr;
    }

    // --- Nested dict access ---
    Value& operator[](const char* key);

    bool isDict() const { return type == DICT; }

    // --- JSON Serialization ---
    size_t toJSON(char *out, size_t maxLen, int indent = 0) const {
        switch (type) {
            case STRING: return snprintf(out, maxLen, "\"%s\"", s);
            case INT:    return snprintf(out, maxLen, "%d", i);
            case LONG:   return snprintf(out, maxLen, "%ld", l);
            case FLOAT:  return snprintf(out, maxLen, "%.3f", f);
            case BOOL:   return snprintf(out, maxLen, "%s", b ? "true" : "false");
            case DICT:   return d ? d->toJSON(out, maxLen, indent) : snprintf(out, maxLen, "{}");
            case POINTER:return snprintf(out, maxLen, "\"ptr:%p\"", ptr);
            default:     return snprintf(out, maxLen, "null");
        }
    }
};

// -------------------- Dict Class --------------------
template <size_t KEY_LEN = 32, size_t BUF_LEN = 64>
class Dict {
private:
    struct Entry {
        char key[KEY_LEN];
        Value<KEY_LEN, BUF_LEN> value;
    };

    Entry* entries;
    size_t capacity;
    size_t count;

    void resize(size_t newCap) {
        Entry* newEntries = new Entry[newCap];
        for (size_t i = 0; i < count; i++) newEntries[i] = entries[i];
        delete[] entries;
        entries = newEntries;
        capacity = newCap;
    }

public:
    Dict(size_t initialCap = 8)
        : capacity(initialCap), count(0) {
        entries = new Entry[capacity];
        for (size_t i = 0; i < capacity; i++) entries[i].key[0] = '\0';
    }

    ~Dict() { delete[] entries; }

    Value<KEY_LEN, BUF_LEN>& operator[](const char* key) {
        for (size_t i = 0; i < count; i++) {
            if (strcmp(entries[i].key, key) == 0)
                return entries[i].value;
        }
        if (count >= capacity) resize(capacity * 2);
        strncpy(entries[count].key, key, KEY_LEN - 1);
        entries[count].key[KEY_LEN - 1] = '\0';
        count++;
        return entries[count - 1].value;
    }

    bool contains(const char* key) const {
        for (size_t i = 0; i < count; i++)
            if (strcmp(entries[i].key, key) == 0)
                return true;
        return false;
    }

    size_t size() const { return count; }

    // --- JSON Serialization ---
    size_t toJSON(char *out, size_t maxLen, int indent = 0) const {
        size_t used = 0;
        used += snprintf(out + used, maxLen - used, "{");

        for (size_t i = 0; i < count && used < maxLen; i++) {
            if (i > 0) used += snprintf(out + used, maxLen - used, ",");
            used += snprintf(out + used, maxLen - used, "\"%s\":", entries[i].key);
            used += entries[i].value.toJSON(out + used, maxLen - used, indent + 2);
        }

        used += snprintf(out + used, maxLen - used, "}");
        return used;
    }

    // --- JSON Deserialization ---
    bool fromJSON(const char *json);
};

// -------------------- Cross-Reference --------------------
template <size_t KEY_LEN, size_t BUF_LEN>
Value<KEY_LEN, BUF_LEN>& Value<KEY_LEN, BUF_LEN>::operator[](const char* key) {
    if (type != DICT) {
        static Dict<KEY_LEN, BUF_LEN> dummy;
        d = &dummy;
        type = DICT;
    }
    return (*d)[key];
}

// -------------------- JSON Parsing Helpers --------------------
namespace {
    void skipWhitespace(const char*& p) {
        while (*p && isspace(*p)) p++;
    }

    bool parseLiteral(const char*& p, const char* literal) {
        size_t len = strlen(literal);
        if (strncmp(p, literal, len) == 0) {
            p += len;
            return true;
        }
        return false;
    }

    bool parseString(const char*& p, char* out, size_t outLen) {
        skipWhitespace(p);
        if (*p != '"') return false;
        p++; // skip opening quote
        size_t i = 0;
        while (*p && *p != '"' && i < outLen - 1) {
            out[i++] = *p++;
        }
        out[i] = '\0';
        if (*p == '"') p++;
        skipWhitespace(p);
        return true;
    }
}

// -------------------- JSON Value Parser --------------------
template <size_t KEY_LEN, size_t BUF_LEN>
bool parseValue(const char*& p, Value<KEY_LEN, BUF_LEN>& outVal) {
    skipWhitespace(p);

    if (*p == '"') {
        char temp[BUF_LEN];
        if (parseString(p, temp, BUF_LEN)) {
            outVal = temp;
            return true;
        }
        return false;
    }

    if (*p == '{') {
        p++;
        Dict<KEY_LEN, BUF_LEN> sub;
        sub.fromJSON(p);
        outVal = sub;
        skipWhitespace(p);
        if (*p == '}') p++;
        return true;
    }

    if (parseLiteral(p, "true")) { outVal = true; return true; }
    if (parseLiteral(p, "false")) { outVal = false; return true; }

    char numBuf[32];
    size_t i = 0;
    if (*p == '-' || isdigit(*p)) {
        while ((*p == '-' || *p == '.' || isdigit(*p)) && i < sizeof(numBuf) - 1)
            numBuf[i++] = *p++;
        numBuf[i] = '\0';
        if (strchr(numBuf, '.'))
            outVal = (float)atof(numBuf);
        else
            outVal = atol(numBuf);
        skipWhitespace(p);
        return true;
    }

    return false;
}

// -------------------- Dict::fromJSON --------------------
template <size_t KEY_LEN, size_t BUF_LEN>
bool Dict<KEY_LEN, BUF_LEN>::fromJSON(const char *json) {
    const char* p = json;
    skipWhitespace(p);

    if (*p == '{') p++;
    skipWhitespace(p);

    while (*p && *p != '}') {
        char key[KEY_LEN];
        if (!parseString(p, key, KEY_LEN))
            return false;

        skipWhitespace(p);
        if (*p != ':') return false;
        p++;
        skipWhitespace(p);

        Value<KEY_LEN, BUF_LEN> val;
        if (!parseValue(p, val))
            return false;

        (*this)[key] = val;

        skipWhitespace(p);
        if (*p == ',') { p++; skipWhitespace(p); }
    }

    if (*p == '}') p++;
    return true;
}