#include <peripheral/peripheral.h>
#include <string.h>

Peripheral::Peripheral(const char* id, PeripheralKind kind)
    : _id(id), _kind(kind), _mutex(xSemaphoreCreateRecursiveMutexStatic(&_mutex_buf)) {}

void Peripheral::lock() {
    xSemaphoreTakeRecursive(_mutex, portMAX_DELAY);
}

void Peripheral::unlock() {
    xSemaphoreGiveRecursive(_mutex);
}

Peripheral* peripheral_find(const char* id, PeripheralKind kind) {
    if (id == nullptr || id[0] == '\0') return nullptr;
    for (size_t i = 0; i < peripheral_count; i++) {
        Peripheral* p = peripheral_table[i];
        if (p->kind() == kind && strcmp(p->id(), id) == 0) return p;
    }
    return nullptr;
}

void setup_peripherals() {
    for (size_t i = 0; i < peripheral_count; i++) {
        PeripheralLock l(*peripheral_table[i]);
        peripheral_table[i]->setup();
    }
}

void loop_peripherals() {
    for (size_t i = 0; i < peripheral_count; i++) {
        PeripheralLock l(*peripheral_table[i]);
        peripheral_table[i]->loop();
    }
}
