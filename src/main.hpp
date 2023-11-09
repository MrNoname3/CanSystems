#ifndef __MAIN_HPP__
#define __MAIN_HPP__

#include <Arduino.h>
#include <SPI.h>
#include <ENC28J60lwIP.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>     // WiFiClient (-> TCPClient)
#include <ESP8266WiFi.h>
#include "cert.hpp"
#include <PubSubClient.h>
#include "mqttSettings.hpp"
#include <Ticker.h>                                       //For LED status

#include <ESP8266WebServer.h>
#include <ESP8266HTTPUpdateServer.h>

#include <ESP8266HTTPClient.h>

//////////////////////////////////////////////////
#include <WiFiUdp.h>
#include <WakeOnLan.h>
//////////////////////////////////////////////////

#define NOP     asm("nop")                            //1 clock delay
#define LED_H   (GPOS |=  (1 << LED))                 //LED ON
#define LED_L   (GPOC |=  (1 << LED))                 //LED OFF
#define LED_T   (GPO  ^=  (1 << LED))                 //LED Toggle

const uint8_t mac_size = 18;
char MAC_Address[mac_size] = { '\0' };

const char OK_state[] = "OK";
const char ERROR_state[] = "ERROR";

const char json_variables[] = {
  "{"
  "\"IP_P\":\"%s\","
  "\"IP_L\":\"%s\","
  "\"GW\":\"%s\","
  "\"NM\":\"%s\","
  "\"MAC\":\"%s\","
  "\"SW_ver\":\"%s\","
  "\"HW_ver\":\"%s\","  
  "\"VCC_Core\":\"%1.2fV\","
  "\"Started\":\"%s\""
  "}"
  };

// allows you to monitor the internal VCC level; it varies with WiFi load
// don't connect anything to the analog input pin(s)!
ADC_MODE(ADC_VCC); 

IPAddress DNS_Resolv(const char* host_p);
void ConnectionStatus(void);
void onMqttPublish(const char* topic, uint8_t* payload, int length);
void setClock(void);
const char* getClock(void);
void tick(void);
void RestartESP(void);
IRAM_ATTR void Counter(void);
float readVoltage(void);

#endif