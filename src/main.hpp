#ifndef MAIN_HPP
#define MAIN_HPP

//--- Headers ---//
#include <Arduino.h>                          /// Arduino libraries header.
#include <Ticker.h>                           /// Timer interrupt hadnler.
#include <ESP8266WiFi.h>                      /// Wifi driver.
#include <ENC28J60lwIP.h>                     /// Ethernet driver.
#include <WiFiClientSecure.h>                 /// TCP client with SSL.
#include <PubSubClient.h>                     /// MQTT client.
#include <LittleFS.h>                         /// Use FLASH filesystem.
#include "secrets.hpp"                        /// MQTT settings and cert for SSL communication.

//--- Constants ---//
static const char SW_VERSION[] PROGMEM              = "V1.0.0";                     // Actual software version.
static const char OK_STATE[] PROGMEM                = " [ OK ]";                    // OK status.
static const char ERR_STATE[] PROGMEM               = " [ ERR ]";                   // Error status.
static const char certFileLocation[] PROGMEM        = "/cert/mosq-ca.crt";          // Used cert location on FS.
static const char certBackupFileLocation[] PROGMEM  = "/cert/mosq-ca.crt.bkp";      // Cert backup location on FS.

static constexpr const uint8_t LED                  = D8;           // Status LED.
static constexpr const uint8_t SPI_CS               = D0;           // Ethernet shield SPI CS.
static constexpr const uint8_t RAD                  = D2;           // Radiation meter.

#define LED_T (GPO  ^=  (1 << LED))                 // LED pin toggle.
#define LED_H (GPOS |=  (1 << LED))                 // LED pin high.
#define LED_L (GPOC |=  (1 << LED))                 // LED pin low.
#define NOP __asm__("nop\n\t");                     // 1 CPU cycle delay.

// Monitor the internal VCC level, it varies with WiFi load.
// Don't connect anything to the analog input pin!
ADC_MODE(ADC_VCC);

//--- Structs ---//

//--- Enums ---//

//--- Functions ---//

/// @brief 
/// @param topic 
/// @param payload 
/// @param length 
void onMqttPublish(const char* topic, uint8_t* payload, int length);

/// @brief 
/// @param  
void tick(void);

/// @brief Reset the MCU.
void RestartESP();

/// @brief 
/// @param  
/// @return 
IRAM_ATTR void Counter();

#endif // MAIN_HPP