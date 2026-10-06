#include <Arduino.h>
#include <modbus/register_device.h>
#include <math.h>          // lroundf

// ModbusMaster's response buffer is 64 words, but ku8MaxBufferSize is private, so the
// limit is mirrored here. REGDEV_MAX_REGISTERS (16) is already below it, making this a
// guard rather than a live constraint — it only starts to matter if that is raised.
#define MODBUS_MAX_RUN_WORDS 64

// How long a queued read may stay outstanding before it is presumed lost. The worker
// answers every command it dequeues, but drops the answer if the response queue stays
// full for a second. A full 16-deep command queue of 2 s ModbusMaster timeouts drains in
// about 33 s, so 60 s only fires for a response that is never coming.
#define REGDEV_INFLIGHT_LIMIT_MS 60000

ModbusRegisterDevice::ModbusRegisterDevice(const char* homie_node_id, Stream& serial,
                                           int pin_rs485_a, int pin_rs485_b, uint8_t addr,
                                           const ModbusRegisterDesc* regs, size_t count,
                                           bool useAsyncQueue)
    : ModbusRTUEntity(homie_node_id, pin_rs485_a, pin_rs485_b), _regs(regs), _count(count) {
    _modbus_address = addr;
    _serial = &serial;
    _poll_timestamp = 0;
    _use_async_queue = useAsyncQueue;
    if (_count > REGDEV_MAX_REGISTERS) {
        Serial.printf("MODBUS[%s]: **ERROR -- %u registers, max %d — the rest are IGNORED "
                      "(raise REGDEV_MAX_REGISTERS)\n",
                      homie_node_id, (unsigned)_count, REGDEV_MAX_REGISTERS);
        _count = REGDEV_MAX_REGISTERS;
    }
}

// Collapse the register map into the fewest Modbus transactions that still read only
// registers the map actually names.
//
// Two rules, both deliberate. Runs never SPAN A GAP: reading across an address the device
// does not implement makes some slaves reject the whole request, so a missing address
// ends the run rather than being read and discarded. And a run never exceeds
// ModbusMaster's response buffer.
//
// device.yml order is arbitrary, so this sorts an index array rather than the descriptor
// table — the emitted table stays in the order it was written, which is what makes it
// diffable against a datasheet.
void ModbusRegisterDevice::plan_runs() {
    for (size_t i = 0; i < _count; i++) _order[i] = (uint8_t)i;

    // Insertion sort by (function, address). _count <= REGDEV_MAX_REGISTERS (16).
    for (size_t i = 1; i < _count; i++) {
        uint8_t key = _order[i];
        int j = (int)i - 1;
        while (j >= 0) {
            const ModbusRegisterDesc& a = _regs[_order[j]];
            const ModbusRegisterDesc& b = _regs[key];
            bool after = (a.holding != b.holding) ? (a.holding && !b.holding)
                                                  : (a.reg > b.reg);
            if (!after) break;
            _order[j + 1] = _order[j];
            j--;
        }
        _order[j + 1] = key;
    }

    _num_runs = 0;
    for (size_t i = 0; i < _count; i++) {
        const ModbusRegisterDesc& d = _regs[_order[i]];
        ModbusRun* run = _num_runs ? &_runs[_num_runs - 1] : nullptr;
        bool extends = run
                    && run->holding == d.holding
                    && d.reg == (uint16_t)(run->start_reg + run->count)
                    && run->count < MODBUS_MAX_RUN_WORDS;
        if (extends) {
            run->count++;
        } else {
            _runs[_num_runs++] = { d.reg, (uint8_t)i, 1, d.holding, 0, 0 };
        }
    }
}

bool ModbusRegisterDevice::setup() {
    ModbusRTUEntity::setup();   // brings up the shared RS485 bus
    for (size_t i = 0; i < _count; i++) {
        _props[i].setup(getNodeId(), _regs[i].id);
    }
    plan_runs();
    Serial.printf("MODBUS[%s]: %u register(s) at slave address %u, in %u transaction(s)\n",
                  getNodeId(), (unsigned)_count, _modbus_address, _num_runs);
    return true;
}

void ModbusRegisterDevice::publish_one(uint8_t desc_index, uint16_t raw) {
    const ModbusRegisterDesc& r = _regs[desc_index];
    float value = (r.is_signed ? (float)(int16_t)raw : (float)raw) * r.scale;

    // Publish through the overload matching the DECLARED datatype, not always the float
    // one. NodeProperty::set(float) renders %.4f, so a property declared boolean in
    // $description was going out as "1.0000" and an integer as "3.0000" — payloads that
    // contradict the property's own declaration.
    switch (r.datatype) {
        case REGDEV_BOOLEAN:
            // Off the RAW word: a scale on a boolean register is meaningless, and
            // scaling could turn a set bit into 0.0 by rounding.
            Serial.printf("MODBUS[%s]: %s = %s\n", getNodeId(), r.id, raw ? "true" : "false");
            _props[desc_index].set_value_and_publish(raw != 0);
            break;
        case REGDEV_INTEGER: {
            // Rounded, not truncated: a scaled reading of 23.5 is nearer 24 than 23.
            int as_int = (int)lroundf(value);
            Serial.printf("MODBUS[%s]: %s = %d\n", getNodeId(), r.id, as_int);
            _props[desc_index].set_value_and_publish(as_int);
            break;
        }
        default:
            Serial.printf("MODBUS[%s]: %s = %.4g\n", getNodeId(), r.id, value);
            _props[desc_index].set_value_and_publish(value);
            break;
    }
}

// Retract rather than publish a sentinel: a consumer cannot tell a real 0 from a failed
// read. See doc/WritingADriver.md.
void ModbusRegisterDevice::retract_run(const ModbusRun& run) {
    for (uint8_t k = 0; k < run.count; k++) _props[_order[run.first + k]].clearValue();
}

uint8_t ModbusRegisterDevice::poll() {
    uint8_t last = ku8MBSuccess;

    for (uint8_t i = 0; i < _num_runs; i++) {
        const ModbusRun& run = _runs[i];
        uint8_t rc = run.holding ? readHoldingRegisters(run.start_reg, run.count)
                                 : readInputRegisters(run.start_reg, run.count);
        if (rc != ku8MBSuccess) {
            Serial.printf("MODBUS[%s]: read of %u register(s) from %u failed (0x%02X)\n",
                          getNodeId(), run.count, run.start_reg, rc);
            retract_run(run);
            last = rc;
            continue;
        }
        // The run is contiguous by construction, so response word k is the descriptor at
        // _order[first + k].
        for (uint8_t k = 0; k < run.count; k++) {
            publish_one(_order[run.first + k], getResponseBuffer(k));
        }
    }
    return last;
}

// ---- async path -------------------------------------------------------------
// ModbusRTUEntity::loop() calls this instead of poll() when _use_async_queue is set.
// Each run becomes one queued read; the command id is retained so the response can be
// matched back exactly. Correlating on the id rather than on reg_address matters when a
// map names the same address twice — that produces two runs which are otherwise
// indistinguishable.
//
// A run whose read is still outstanding is NOT re-queued. Overwriting cmd_id would orphan
// the earlier read, and its response, success or failure, would then match nothing and be
// dropped; a dropped failure is a retraction that never happens, leaving a stale value
// published as if it were current.

void ModbusRegisterDevice::pollAsync() {
    uint32_t now = millis();
    for (uint8_t i = 0; i < _num_runs; i++) {
        ModbusRun& run = _runs[i];
        if (run.cmd_id != 0) {
            if (now - run.queued_ms < REGDEV_INFLIGHT_LIMIT_MS) continue;
            Serial.printf("MODBUS[%s]: no response to read of %u register(s) from %u "
                          "in %u ms, presumed lost\n", getNodeId(), run.count,
                          run.start_reg, (unsigned)(now - run.queued_ms));
            retract_run(run);
        }
        run.cmd_id = run.holding ? queueReadHoldingRegisters(run.start_reg, run.count)
                                 : queueReadInputRegisters(run.start_reg, run.count);
        run.queued_ms = now;
        if (run.cmd_id == 0) {
            // Not queued (command queue full or not ready): keep the published value and
            // retry on the next poll.
            Serial.printf("MODBUS[%s]: could not queue read of %u register(s) from %u\n",
                          getNodeId(), run.count, run.start_reg);
        }
    }
}

void ModbusRegisterDevice::handleAsyncResponse(ModbusResponse* resp) {
    if (!resp) return;
    for (uint8_t i = 0; i < _num_runs; i++) {
        ModbusRun& run = _runs[i];
        if (run.cmd_id == 0 || run.cmd_id != resp->command_id) continue;
        run.cmd_id = 0;   // consumed

        if (!resp->success) {
            Serial.printf("MODBUS[%s]: async read of %u register(s) from %u failed "
                          "(error=%d)\n", getNodeId(), run.count, run.start_reg,
                          resp->error_code);
            retract_run(run);
            return;
        }
        // Short reads are possible in principle; publish only what actually arrived
        // rather than reading past the end of the response buffer.
        uint8_t words = (resp->data_length < run.count) ? (uint8_t)resp->data_length : run.count;
        for (uint8_t k = 0; k < words; k++) {
            publish_one(_order[run.first + k], resp->data[k]);
        }
        return;
    }
}
