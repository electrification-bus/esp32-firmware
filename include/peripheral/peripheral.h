#pragma once

#include <stddef.h>
#include <stdint.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// A peripheral (doc/Peripherals.md): hardware a device.yml lists under peripherals:, which
// app code and features use directly, in any role. `generate` declares each one as a
// static object, named after its id in the generated <peripherals.h> for a project's src/,
// and lists it in a table that peripheral<T>(id) searches for code that cannot include that
// header (a feature such as OA3).
//
// Any task may call a peripheral. Each call takes the peripheral's own mutex; a sequence of
// calls that must not interleave with another task's (a redraw) holds a PeripheralLock.
// Transactions on a shared I2C or SPI bus are serialized by the framework's HAL locks.

enum class PeripheralKind : uint8_t {
    kOther,
    kDisplay,
};

class Peripheral {
public:
    Peripheral(const char* id, PeripheralKind kind);

    Peripheral(const Peripheral&) = delete;
    Peripheral& operator=(const Peripheral&) = delete;

    const char* id() const { return _id; }
    PeripheralKind kind() const { return _kind; }

    // Hardware bring-up, once, from setup_peripherals(); and periodic work from the loop.
    virtual void setup() {}
    virtual void loop() {}

    // Recursive: a call made while holding a PeripheralLock takes the mutex again.
    void lock();
    void unlock();

protected:
    ~Peripheral() = default;

private:
    const char* _id;
    PeripheralKind _kind;
    StaticSemaphore_t _mutex_buf;
    SemaphoreHandle_t _mutex;
};

// Holds a peripheral's mutex for a scope.
class PeripheralLock {
public:
    explicit PeripheralLock(Peripheral& p) : _p(p) { _p.lock(); }
    ~PeripheralLock() { _p.unlock(); }

    PeripheralLock(const PeripheralLock&) = delete;
    PeripheralLock& operator=(const PeripheralLock&) = delete;

private:
    Peripheral& _p;
};

// The peripheral with this id and kind, or nullptr when device.yml lists none.
Peripheral* peripheral_find(const char* id, PeripheralKind kind);

template <class T>
T* peripheral(const char* id) {
    return static_cast<T*>(peripheral_find(id, T::kKind));
}

// Every peripheral's setup() and loop(), in device.yml's order (main.cpp).
void setup_peripherals();
void loop_peripherals();

// Generated (.ebus/src/peripherals.cpp): every peripheral in device.yml.
extern Peripheral* const peripheral_table[];
extern const size_t peripheral_count;
