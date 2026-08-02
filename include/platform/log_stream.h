#pragma once
/*
 * log_stream.h — console output tee: UART/USB *and* TCP ("serial over TCP").
 *
 * Problem: `pio device monitor` / `idf.py monitor` need a USB cable. Once a board
 * is installed in a panel that cable is gone, but the Serial.printf() diagnostics
 * are exactly what you need to debug it. ESPHome solves this by streaming the log
 * over the network; this is the same idea, using a transport both monitors already
 * speak natively.
 *
 * Design
 * ------
 *   Serial.printf(...)          ← 350+ existing call sites, unchanged
 *        |
 *        v
 *   LogStream::write()  ──┬──>  real UART / USB-CDC   (unchanged USB workflow)
 *                         └──>  static ring buffer  ──>  TCP clients (port 23)
 *
 * The tee is installed by redirecting the `Serial` token to our object. The Arduino
 * core already does exactly this (HardwareSerial.h: `#define Serial Serial0`), so we
 * #undef theirs and point it at ours — no source edits at 350 call sites. See the
 * bottom of this file, and `build_src_flags` in platformio.ini for how it is applied.
 *
 * Consuming the stream (no extra tooling — both monitors accept a pyserial URL):
 *   pio device monitor --port socket://192.168.1.50:23
 *   idf.py monitor -p socket://192.168.1.50:23
 *   nc 192.168.1.50 23          /  telnet 192.168.1.50 23
 *
 * A newly-connected client is first sent the ring buffer's contents, so you see the
 * boot log even though you connected seconds after boot.
 *
 * LIMITATION: ROM bootloader output and panic/backtrace dumps are written by code
 * that runs below this layer (and after the scheduler is gone), so they only ever
 * appear on the wire. A crash's last words need the USB cable. Everything up to the
 * crash is in the ring buffer and will have been streamed.
 *
 * Conforms to CLAUDE.md: no dynamic allocation, no String, no STL. Ring buffer,
 * client table and TX staging buffer are all statically sized.
 */
#include <Arduino.h>

// ---------------------------------------------------------------- knobs ----
// Override any of these in platformio.ini build_flags.

#ifndef LOG_TCP_PORT_DEFAULT
#define LOG_TCP_PORT_DEFAULT 23         // telnet port; raw stream, no telnet IAC
#endif

#ifndef LOG_RING_SIZE
#define LOG_RING_SIZE 8192              // MUST be a power of two (masked indexing)
#endif

#ifndef LOG_MAX_CLIENTS
#define LOG_MAX_CLIENTS 2               // extra connections evict the oldest
#endif

#ifndef LOG_TX_CHUNK
#define LOG_TX_CHUNK 512                // stack staging buffer per send()
#endif

#ifndef LOG_TX_CHUNKS_PER_LOOP
#define LOG_TX_CHUNKS_PER_LOOP 8        // cap work per log_tcp_loop() call
#endif

#ifndef LOG_CAPTURE_ESP_LOG
#define LOG_CAPTURE_ESP_LOG 1           // also tee ESP_LOGx / core CORE_DEBUG output
#endif

#ifndef LOG_ESP_LINE_MAX
#define LOG_ESP_LINE_MAX 256            // stack buffer for one ESP_LOGx line
#endif

// The real console object, captured while `Serial` still expands to the *core's*
// macro (Serial0 on ESP32, HWCDCSerial/USBSerial on S3 with CDC-on-boot). Returning
// auto& keeps the concrete type, so the variant-specific API stays available.
inline auto& log_console() { return Serial; }

/*
 * The tee itself. Derives from Stream (not just Print) so it remains a drop-in
 * wherever the real Serial was usable as a Stream*.
 */
class LogStream : public Stream {
  public:
    // --- Print / Stream ---------------------------------------------------
    size_t write(uint8_t c) override;
    size_t write(const uint8_t* buffer, size_t size) override;
    using Print::write;                 // keep write(const char*) et al. visible
    int  availableForWrite() override;
    void flush() override;
    int  available() override;
    int  read() override;
    int  peek() override;

    // --- HardwareSerial passthroughs so `Serial.` call sites still compile ---
    // NOTE: the config/pin overload ignores its extra arguments — HWCDC has no
    // such parameters, and this project only ever uses 8N1 on the default pins.
    void begin(unsigned long baud = 115200);
    void begin(unsigned long baud, uint32_t config, int8_t rxPin = -1, int8_t txPin = -1);
    void end();
    void setDebugOutput(bool en);
    operator bool() const { return true; }

    // Stop mirroring to the wire (TCP only). For boards where UART0 gets
    // repurposed, or to save the cycles once you are remote-only.
    void setUartEcho(bool en);
    bool uartEcho() const;
};

extern LogStream logStream;

// ------------------------------------------------------- TCP log server ----
// Plain C linkage-friendly API: callable from ESP-IDF code as well as Arduino.

// Start (or restart) the listener. port 0 = disabled. Safe to call before an
// interface has an IP: the socket binds to INADDR_ANY.
void     log_tcp_begin(uint16_t port);

// Stop the listener and drop all clients.
void     log_tcp_stop(void);

// Accept new clients and push buffered output. Call from the main loop.
// Never blocks: the listener and all client sockets are non-blocking.
void     log_tcp_loop(void);

bool     log_tcp_running(void);
int      log_tcp_clients(void);
uint16_t log_tcp_get_port(void);

// Copy the most recent bytes of the ring buffer into out (NUL-terminated).
// For a "recent log" REST endpoint or web view. Returns bytes written.
size_t   log_snapshot(char* out, size_t out_len);

/*
 * Redirect the `Serial` token at every call site.
 *
 * The Arduino core defines `Serial` as a macro (HardwareSerial.h), so undefining
 * it here is expected, not a workaround. Define LOG_NO_SERIAL_MACRO before this
 * header (or #undef Serial after it) in any translation unit that must reach the
 * hardware object directly — log_stream.cpp does exactly that.
 */
#ifndef LOG_NO_SERIAL_MACRO
#undef Serial
#define Serial logStream
#endif
