/* GPT-5 prompted implementation of Python-like "dict" */
#include <Arduino.h>
//#include <string.h>

#define MAX_KEY_LEN 32

/* example Dict usage

    // with int values
    Dict<int> d1;
    d1["a"] = 123;
    d1["b"] = 456;
    Serial.println(d1["a"]);  // 123

    // with float values
    Dict<float> d2;
    d2["pi"] = 3.14159;
    Serial.println(d2["pi"], 6);

    // with string values - use StringBuffer wrapper class (char[32] in this example)
    Dict<StringBuffer<32>> d;
    d["msg"] = "hello world";  // works like Python dict

    Serial.println(d["msg"]);  // implicit conversion to const char*
*/

// Wrapper for fixed-size char buffer with assignment operator
template <size_t N>
class StringBuffer {
private:
    char _data[N];

public:
    StringBuffer() { _data[0] = '\0'; }

    // Assignment from const char*
    StringBuffer& operator=(const char* rhs) {
        strncpy(_data, rhs, N - 1);
        _data[N - 1] = '\0';
        return *this;
    }

    // Assignment from another StringBuffer
    StringBuffer& operator=(const StringBuffer& rhs) {
        strncpy(_data, rhs._data, N - 1);
        _data[N - 1] = '\0';
        return *this;
    }

    // Implicit conversion to const char*
    operator const char*() const {
        return _data;
    }

    // Direct access
    char* c_str() { return _data; }
    const char* c_str() const { return _data; }

    // Provide a print overload for Arduino - NOT WORKING - attempted to obviate need for ".c_str()" accessor 
    //friend size_t printTo(Stream &s, const StringBuffer &b) {
    //    return s.print(b._data);
   // }
};

template <typename T, size_t KEY_LEN = MAX_KEY_LEN>
class Dict {
    public:
        Dict(size_t initialCapacity = 8)
            : _capacity(initialCapacity), _count(0) {
            _entries = new Entry[_capacity];
            for (size_t i = 0; i < _capacity; i++) {
                _entries[i].key[0] = '\0';
            }
        }

        ~Dict() { delete[] _entries; }

        T& operator[](const char* key) {
            for (size_t i = 0; i < _count; i++) {
                if (strcmp(_entries[i].key, key) == 0) {
                    return _entries[i].value;
                }
            }
            if (_count >= _capacity) {
                resize(_capacity * 2);
            }
            strncpy(_entries[_count].key, key, KEY_LEN - 1);
            _entries[_count].key[KEY_LEN - 1] = '\0';
            _count++;
            return _entries[_count - 1].value;
        }

        bool contains(const char* key) const {
            for (size_t i = 0; i < _count; i++) {
                if (strcmp(_entries[i].key, key) == 0) return true;
            }
            return false;
        }

        size_t size() const { return _count; }
        
    private:
        struct Entry {
            char key[KEY_LEN];
            T value;
        };

        Entry* _entries;
        size_t _capacity;
        size_t _count;

        void resize(size_t newCapacity) {
            Entry* newEntries = new Entry[newCapacity];
            for (size_t i = 0; i < _count; i++) {
                newEntries[i] = _entries[i];
            }
            delete[] _entries;
            _entries = newEntries;
            _capacity = newCapacity;
        }
};

void test_dict() {
    // with int values
    Dict<int> d1;
    d1["a"] = 123;
    Serial.printf("Dict with int %d\n", d1["a"]);  // 123

    // with float values
    Dict<float> d2;
    d2["pi"] = 3.14159;
    Serial.printf("Dict with float %.6f\n", d2["pi"]);

    // with string values - use StringBuffer wrapper class (char[32] in this example)
    Dict<StringBuffer<32>> d;
    d["msg"] = "hello world";  // works like Python dict


    Serial.printf("Dict with string %s\n", d["msg"].c_str());
    //NOT WORKING - need implicit conversion accessor
    //Serial.printf("Dict with string implicit %s\n", d["msg"]);

    //with Property pointer
    Dict<Property*> dp;
    Node* node = new Node();
    node->setId("test_node");
    Property* prop = new Property(node);
    prop->setId("temperature");
    dp["temp_property"] = prop;
    Property* dict_prop = dp["temp_property"];
    Serial.printf("Dict with Property pointer: id:'%s'\n", dict_prop->id());
}