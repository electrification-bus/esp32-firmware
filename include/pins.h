#pragma once

#define BME280_SDA_PIN 13
#define BME280_SCL_PIN 16

#define SSR1_PIN      GPIO_NUM_3

#ifdef CONFIG_ETH_ENABLED //TODO this indicates an olimex - change to something specific to the olimex board
#define RS485_RX_1             GPIO_NUM_36
#define RS485_TX_1             GPIO_NUM_4
#else
#define RS485_RX_1             GPIO_NUM_6  // RX here maps to RS485 HW-519 module's silk screen "RXD"
#define RS485_TX_1             GPIO_NUM_7  // TX here maps to RS485 HW-519 module's silk screen "TXD"
#endif

/*
   -------------------------------------------------------------------
   CircuitSetup.us 6-CT Energy Meter - Chip Select Pins
   -------------------------------------------------------------------
   Each board has two ATM90E32 ICs:
   - CS1: Controls IC1 (CTs 1-3)
   - CS2: Controls IC2 (CTs 4-6)

   For multi-board (stacked) setups:
   - Main Board:   CS (pin 5) -> ESP32-S3 GPIO 15, CS2 (pin 4) -> GPIO 16
   - Stack Board 1: CS (pin 0) -> GPIO 47, CS2 (pin 16) -> GPIO 2
   - Stack Board 2: CS (pin 15) -> GPIO 21, CS2 (pin 17) -> GPIO 19

   Note: Pin mappings differ between ESP32 variants. Adjust for your board.
   -------------------------------------------------------------------
*/
#ifdef CONFIG_IDF_TARGET_ESP32S3
// ESP32-S3 pin mappings for CircuitSetup energy meter
#define CS1_BOARD1 15   // Main board IC1 (CTs 1-3) - CS.us silkscreen "CS"
#define CS2_BOARD1 16   // Main board IC2 (CTs 4-6) - CS.us silkscreen "CS2"
// Stack boards for multi-phase / high CT count setups (uncomment as needed)
// #define CS1_BOARD2 47   // Stack board 1 IC1 (CTs 7-9)
// #define CS2_BOARD2 2    // Stack board 1 IC2 (CTs 10-12)
// #define CS1_BOARD3 21   // Stack board 2 IC1 (CTs 13-15)
// #define CS2_BOARD3 19   // Stack board 2 IC2 (CTs 16-18)
#else
// Standard ESP32 DevKit pin mappings
#define CS1_BOARD1 27   // Main board IC1 (CTs 1-3)
#define CS2_BOARD1 26   // Main board IC2 (CTs 4-6)
#endif