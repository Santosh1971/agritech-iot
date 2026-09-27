#pragma once

// TTGO T-Call pin definitions
#ifndef MODEM_RST
#define MODEM_RST 5
#endif
#ifndef MODEM_PWKEY
#define MODEM_PWKEY 4
#endif
#ifndef MODEM_POWER_ON
#define MODEM_POWER_ON 23
#endif
#ifndef MODEM_TX
#define MODEM_TX 27
#endif
#ifndef MODEM_RX
#define MODEM_RX 26
#endif
/*
#ifndef I2C_SDA
#define I2C_SDA 21 // Power IC and RTC , This is for ESP32
#endif
#ifndef I2C_SCL
#define I2C_SCL 22
#endif
*/
#ifndef I2C_SDA
#define I2C_SDA 12 // D6=GPIO12   RTC , This is for ESP8266
#endif
#ifndef I2C_SCL
#define I2C_SCL 14  // D5=GPIO14
#endif


#ifndef BATT_V
#define BATT_V 35 // Half of battery voltage..
#endif

#ifndef LED_PIN_BLUE
#define LED_PIN_BLUE 19
#endif
#ifndef LED_PIN_RED
#define LED_PIN_RED 15 //2
#endif
#ifndef LED_PIN_YELLOW
#define LED_PIN_YELLOW 13
#endif

#ifndef LoRa_MOSI
#define LoRa_MOSI 12 // SDO  brown
#endif
#ifndef LoRa_MISO
#define LoRa_MISO 25 //SDI black
#endif

#ifndef LoRa_CLK
#define LoRa_CLK 14 // red
#endif
#ifndef LoRa_CS
#define LoRa_CS 33 // orange
#endif
#ifndef LoRa_IRQ
#define LoRa_IRQ 32 // orange
#endif

#ifndef IP5306_ADDR
#define IP5306_ADDR 0x75
#endif
#ifndef IP5306_REG_SYS_CTL0
#define IP5306_REG_SYS_CTL0 0x00
#endif

#ifndef Relay_CH1
#define Relay_CH1 18 // use it for Relay
#endif
#ifndef Relay_CH2
#define Relay_CH2 19 // connected to Blue LED in GSM TH
#endif

#ifndef DHT22_PIN
#define DHT22_PIN 13
#endif
