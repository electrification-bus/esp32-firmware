#pragma once

#define BME280_SDA_PIN 13
#define BME280_SCL_PIN 16

#ifdef CONFIG_ETH_ENABLED //TODO remove olimex-specific #define
#define RS485_RX_1             GPIO_NUM_36
#define RS485_TX_1             GPIO_NUM_4
#else
#define RS485_RX_1             GPIO_NUM_6  // RX here maps to RS485 HW-519 module's silk screen "RXD"
#define RS485_TX_1             GPIO_NUM_7  // TX here maps to RS485 HW-519 module's silk screen "TXD"
#endif