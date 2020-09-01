/*
  This demo can uses VSCP events to report state of a mailbox sensor to a server. The
  server can be either a VSCP daemon or a MQTT broker. Apart from mail notification
  current battery voltage and current temperature is reported. For demonstation the
  temperature is sent both as a level I and a level II event.

  The hardware is put to deep sleep again when the lid of the mailbox is closed to 
  save on battery. 

  

  Inspiration fetched from
  https://lajtronix.eu/2019/07/28/simple-esp8266-e-mail-sensor-switch/
  https://gist.github.com/LajtEU/8e3c82d1cb4c680d949c65e01f655b52#file-esp-12f_mailbox_e-mail_notification_sensor-ino

  VSCP
  ====

  Events sent
  -----------
  Woken up:  CLASS1.INFORMATION (20), VSCP_TYPE_INFORMATION_WOKEN_UP (29)
  Lid open: CLASS1.INFORMATION (20), VSCP_TYPE_INFORMATION_ON (3)
  Temp:     CLASS1.MEASUREMENT (10), VSCP_TYPE_MEASUREMENT_TEMPERATURE (6)
  Battery Voltage: CLASS1.MEASUREMENT (10),
  VSCP_TYPE_MEASUREMENT_ELECTRICAL_POTENTIAL (16)
  RSSI: CLASS1.DATA (15), VSCP_TYPE_DATA_SIGNAL_QUALITY (6)

  VSCP is a framework for IoT/m2m hardware that can communicate over many 
  different mediums and protocols. This library can be used to communicate 
  with a VSCP remote host in an easy and intuitive way. The library supports 
  all Arduino Ethernet Client compatible hardware.

  https://www.vscp.org

  Copyright (c) Ake Hedman, Grodans Paradis AB, akhe@grodansparadis.com, MIT License

*/

// Subscribe on Linux host (or use MQTT explorer)
// mosquitto_sub -h 192.168.1.7 -p 1883 -u vscp -P secret -t vscp/#

#define DEBUG // Enable debug output on serial channel

// Select one of VSCP or MQTT 
#define VSCP // Enable VSCP events
//#define MQTT // Enable VSCP events over MQTT

#define ONEWIRE   // Comment out to remove 1-wire support

int holdPin = 4; // defines GPIO 4 as the hold pin (will hold ESP-12F enable
                 // pin high until we power down)

//#include <GDBStub.h>    // Uncomment for debugger

#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>

#include <vscptcpclient.h>    // Must be present for event definitions

// MQTT
#ifdef MQTT
#include <PubSubClient.h>
#endif

// Tempsensor
#ifdef ONEWIRE
#include <DallasTemperature.h>
#include <OneWire.h>
#endif

// Prototypes
#ifdef MQTT
boolean sendMQTT(vscpEventEx *pex);
#endif

static const char VSCP_ETH_PREFIX[] PROGMEM = "FF:FF:FF:FF:FF:FF:FF:FE:";
char vscp_guid[50];

const char *ssid = "grodansparadis"; // Enter the SSID of your WiFi Network.
const char *password =
    "brattbergavagen17!"; // Enter the Password of your WiFi Network.

// MQTT connection credentials
#ifdef MQTT
const IPAddress mqtt_server(192,168,1,7);
const int16_t mqtt_port = 1883;
const char *mqtt_user = "vscp";
const char *mqtt_password = "secret";
char mqtt_topic_template[80];  // vscp/guid/class/type
#endif

// VSCP connection credentials
#ifdef VSCP
const char *vscp_server = "192.168.1.7";
const int16_t vscp_port = 9598;
const char *vscp_user = "admin";
const char *vscp_password = "secret";
#endif

WiFiClient espClient;
// WiFiClientSecure espClient;

#ifdef VSCP
vscpTcpClient vscp(vscp_server, vscp_port, espClient);
#else
vscpTcpClient vscp(espClient);
#endif

#ifdef MQTT
PubSubClient mqttClient(mqtt_server, mqtt_port, espClient);
#endif

// GPIO where the DS18B20 is connected to
#ifdef ONEWIRE
const int oneWireBus = 5; // (D1 on NodeMCU)
#endif

// Setup a oneWire instance to communicate with any OneWire devices
#ifdef ONEWIRE
OneWire oneWire(oneWireBus);
#endif

// Pass our oneWire reference to Dallas Temperature sensor
#ifdef ONEWIRE
DallasTemperature sensors(&oneWire);
#endif

ADC_MODE(ADC_VCC); // ADC connected to VCC



///////////////////////////////////////////////////////////////////////////////
// setup
//

void setup() 
{
  int rv;

  pinMode(holdPin, OUTPUT);    // sets GPIO 4 to output
  digitalWrite(holdPin, HIGH); // sets GPIO 4 to high (this holds EN pin
                               // high when the button is released).
                               
  strcpy(vscp_guid,"FF:FF:FF:FF:FF:FF:FF:FE:");
  strcat(vscp_guid,WiFi.macAddress().c_str());
  strcat(vscp_guid,":00:00");

  // Construct MQTT topic template
#ifdef MQTT  
  mqttClient.setBufferSize(400);
  strcpy(mqtt_topic_template,"vscp/");
  strcat(mqtt_topic_template,vscp_guid);
  strcat(mqtt_topic_template,"/%d/%d");  // class, type  
#endif  

#ifdef DEBUG
  Serial.begin(115200);  
  delay(200); // Needs some time to stabilize
#endif

  //gdbstub_init();     // Uncomment for debug

  // Set WiFi to station mode and disconnect from an AP if it was Previously
  // connected
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);

#ifdef DEBUG
  Serial.println();
  Serial.println();
  Serial.print("Connecting To: ");
  Serial.println(ssid);
#endif  

  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print("*");
  }

#ifdef DEBUG
  Serial.println();
  Serial.println("WiFi Connected.");

  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  Serial.print("ESP Board MAC Address:  ");
  Serial.println("FF:FF:FF:FF:FF:FF:FF:FE:" + WiFi.macAddress() + ":00:00");
  Serial.println(vscp_guid);
#endif

#ifdef ONEWIRE
  sensors.requestTemperatures();
  float tempC = sensors.getTempCByIndex(0);
#else
  float tempC = 0.0;  
#endif  
#ifdef DEBUG  
  Serial.print(tempC);
  Serial.println(" C");
#endif  

  // Get VCC (battery) voltage.
  uint_fast16_t vcc = ESP.getVcc();
#ifdef DEBUG  
  Serial.print(vcc);
  Serial.println(" mV");
#endif  

  int32_t rssi = WiFi.RSSI();
  Serial.print("RSSI:  ");
  Serial.print(rssi);
  Serial.println(" dBm");

  // Give system a chance to work.
  yield();

  // Attempt to connect 
#ifdef MQTT  

#ifdef DEBUG
  Serial.println("Attempting MQTT connection...");
#endif

  boolean bMQTT = false;
  if (mqttClient.connect(vscp_guid, mqtt_user, mqtt_password)) {
#ifdef DEBUG    
    Serial.println("\tConnected to MQTT broker.");
#endif    
    bMQTT = true;
  }
  else {
#ifdef DEBUG    
    Serial.println("\tFailed to connect to MQTT broker.");
#endif    
  }
#endif
  

  // Connect to the VSCP remote host
#ifdef DEBUG  
  Serial.println(F("Attempting VSCP remote host connection..."));
#endif  

#ifdef VSCP
  if ( VSCP_ERROR_SUCCESS == ( rv = vscp.connect(vscp_user,vscp_password) ) ) {
#ifdef DEBUG    
    Serial.println("\tConnected to VSCP remote host");
#endif    
  }
  else {
#ifdef DEBUG    
    Serial.print("Failed to connect to remote host, ");
    Serial.print("Error=");
    Serial.println(rv);
#endif    
  }
#endif  

  
  vscpEventEx ex;

  // Construct woken up event to tell the world we are on-line
  // https://docs.vscp.org/spec/latest/#/./class1.information?id=type29
  // To indicate that we came up from deep sleep
  memset(&ex,0,sizeof(ex));
  ex.timestamp = millis()*1000;
  ex.head = VSCP_PRIORITY_NORMAL |  // No rush
            VSCP_HEADER16_DUMB |    // We have no registers/dm etc
            VSCP_HEADER16_GUID_TYPE_STANDARD; // Normal GUID
  ex.obid = 0;
  ex.timestamp = millis()*1000; // Leave blank to let server fill in
  ex.vscp_class = VSCP_CLASS1_INFORMATION; 
  ex.vscp_type = VSCP_TYPE_INFORMATION_WOKEN_UP;
  vscp.readGuidFromStr(ex.GUID,vscp_guid);
  ex.sizeData = 3;
  ex.data[0] = 0;          // Index
  ex.data[1] = 0;          // Zone
  ex.data[2] = 0;          // Subzone
  
#ifdef VSCP  
  if ( VSCP_ERROR_SUCCESS == ( rv = vscp.sendEventToRemote(ex) ) ) {
#ifdef DEBUG    
    Serial.println("VSCP: Sent woken up event");  
#endif    
  }
  else {
#ifdef DEBUG    
    Serial.print("Failed to send woken up event, ");
    Serial.print("Error=");
    Serial.println(rv);
#endif    
  }
#endif  


#ifdef MQTT
  if ( bMQTT ) {
    if ( sendMQTT(ex) ) {
#ifdef DEBUG      
      Serial.println("MQTT: Sent woken up event");  
#endif      
    }
    else {
#ifdef DEBUG      
      Serial.println("MQTT: Failed to send woken up event");
#endif      
    }
  }
#endif  
  

  // Construct temperature event as a VSCP Level I event
  // Level I events are compact and intended for lower end 
  // devices
  // https://docs.vscp.org/spec/latest/#/./vscp_measurements?id=class2measurement_str
  memset(&ex,0,sizeof(ex));
  ex.timestamp = millis()*1000;
  ex.head = VSCP_PRIORITY_NORMAL |  // No rush
            VSCP_HEADER16_DUMB |    // We have no registers/dm etc
            VSCP_HEADER16_GUID_TYPE_STANDARD; // Normal GUID
  ex.obid = 0;
  ex.timestamp = millis()*1000; // Leave blank to let server fill in
  ex.vscp_class = VSCP_CLASS1_MEASUREMENT; 
  ex.vscp_type = VSCP_TYPE_MEASUREMENT_TEMPERATURE;
  vscp.readGuidFromStr(ex.GUID,vscp_guid);

  ex.sizeData = 5;
  // Datacoding is explained here https://docs.vscp.org/spec/latest/#/./vscp_measurements
  ex.data[0] = 0xA8; // Floating point, Sensor=0, Unit = Degrees Celsius
  // Floating point values (as all numbers) are stored MSB first in VSCP
  byte *p = (byte *)&tempC;  
  ex.data[1] = *(p + 3);  // MSB of 32-bit floating point number (Big-endian)
  ex.data[2] = *(p + 2);     
  ex.data[3] = *(p + 3);
  ex.data[4] = *(p + 0);  // MSB of 32-bit floating point number (Big-endian)   
  
#ifdef VSCP  
  if ( VSCP_ERROR_SUCCESS == ( rv = vscp.sendEventToRemote(ex) ) ) {
#ifdef DEBUG    
    Serial.println("VSCP: Sent temperature event");  
#endif    
  }
  else {
#ifdef DEBUG    
    Serial.print("Failed to send temperature event, ");
    Serial.print("Error=");
    Serial.println(rv);
#endif    
  }
#endif  

#ifdef MQTT
  if ( bMQTT ) {
    if ( sendMQTT(ex) ) {
#ifdef DEBUG      
      Serial.println("MQTT: Sent temperature event");  
#endif      
    }
    else {
#ifdef DEBUG      
      Serial.println("MQTT: Failed to send temperature event");
#endif      
    }
  }
#endif

  // Construct temperature event as a VSCP Level II event
  // Level II events are intended for higher end devices &
  // software applications
  memset(&ex,0,sizeof(ex));
  ex.timestamp = millis()*1000;
  ex.head = VSCP_PRIORITY_NORMAL |  // No rush
            VSCP_HEADER16_DUMB |    // We have no registers/dm etc
            VSCP_HEADER16_GUID_TYPE_STANDARD; // Normal GUID
  ex.obid = 0;
  ex.timestamp = millis()*1000; // Leave blank to let server fill in
  ex.vscp_class = VSCP_CLASS2_MEASUREMENT_STR; 
  ex.vscp_type = VSCP_TYPE_MEASUREMENT_TEMPERATURE;
  vscp.readGuidFromStr(ex.GUID,vscp_guid);

  ex.data[0] = 0; // Sensor index
  ex.data[1] = 0; // Zone
  ex.data[2] = 0; // Sub zone
  ex.data[3] = 1; // Unit = 1 = Degrees Celsius for a temperature
  // Print the floating point value
  sprintf((char *)(ex.data+3),"%2.3f",tempC);  
  ex.sizeData = 4 + strlen((char *)ex.data+4) + 1; // We include the terminating zero  

#ifdef VSCP  
  if ( VSCP_ERROR_SUCCESS == ( rv = vscp.sendEventToRemote(ex) ) ) {
#ifdef DEBUG    
    Serial.println("VSCP: Sent level I temperature event");  
#endif    
  }
  else {
#ifdef DEBUG    
    Serial.print("Failed to send level I temperature event, ");
    Serial.print("Error=");
    Serial.println(rv);
#endif    
  }
#endif

#ifdef MQTT
  if ( bMQTT ) {
    if ( sendMQTT(ex) ) {
#ifdef DEBUG      
      Serial.println("MQTT: Sent level I temperature event");  
#endif      
    }
    else {
#ifdef DEBUG      
      Serial.println("MQTT: Failed to send level I temperature event");
#endif      
    }
  }
#endif

  // Construct event to report battery voltage
  // We use a compact level I measurement event
  // https://docs.vscp.org/spec/latest/#/./class1.measurement?id=type16
  memset(&ex,0,sizeof(ex));
  ex.timestamp = millis()*1000;
  ex.head = VSCP_PRIORITY_NORMAL |  // No rush
            VSCP_HEADER16_DUMB |    // We have no registers/dm etc
            VSCP_HEADER16_GUID_TYPE_STANDARD; // Normal GUID
  ex.obid = 0;
  ex.timestamp = millis()*1000; // Leave blank to let server fill in
  ex.vscp_class = VSCP_CLASS1_MEASUREMENT; 
  ex.vscp_type = VSCP_TYPE_MEASUREMENT_ELECTRICAL_POTENTIAL;
  vscp.readGuidFromStr(ex.GUID,vscp_guid);

  ex.data[0] = 0x89; // Normalized Integer, Sensor=0, Unit = 0 = Volt
  ex.data[1] = 0x83; // 0x83 - Move decimal point left three steps
                     //        Divide with 1000.sizeData = 4 + strlen((char *)ex.data+4) + 1; // We include the terminating zero  
  p = (byte *)&vcc;
  ex.data[2] = *(p + 1);  // MSB 
  ex.data[3] = *(p + 0);  // LSB
  ex.sizeData = 4;

#ifdef VSCP
  if ( VSCP_ERROR_SUCCESS == ( rv = vscp.sendEventToRemote(ex) ) ) {
#ifdef DEBUG    
    Serial.println("VSCP: Sent battery voltage event");  
#endif    
  }
  else {
#ifdef DEBUG    
    Serial.print("Failed to send battery voltage event, ");
    Serial.print("Error=");
    Serial.println(rv);
#endif    
  }
#endif  

#ifdef MQTT
  if ( bMQTT ) {
    if ( sendMQTT(ex) ) {
#ifdef DEBUG      
      Serial.println("MQTT: Sent battery voltage event");  
#endif      
    }
    else {
#ifdef DEBUG      
      Serial.println("MQTT: Failed to send battery voltage event");
#endif      
    }
  }
#endif

  // Construct signal strength event as a VSCP Level I event

  memset(&ex,0,sizeof(ex));
  ex.timestamp = millis()*1000;
  ex.head = VSCP_PRIORITY_NORMAL |  // No rush
            VSCP_HEADER16_DUMB |    // We have no registers/dm etc
            VSCP_HEADER16_GUID_TYPE_STANDARD; // Normal GUID
  ex.obid = 0;
  ex.timestamp = millis()*1000; // Leave blank to let server fill in
  ex.vscp_class = VSCP_CLASS1_DATA; 
  ex.vscp_type = VSCP_TYPE_DATA_SIGNAL_QUALITY;
  vscp.readGuidFromStr(ex.GUID,vscp_guid);
  
  // Datacoding is explained here https://docs.vscp.org/spec/latest/#/./vscp_measurements
  // and units for this event
  // https://docs.vscp.org/spec/latest/#/./class1.data?id=type6
  ex.data[0] = 0x55; // String, Sensor=0, Unit = 2 = dBm
  sprintf((char *)(ex.data + 1),"%d",(int)rssi);
  ex.sizeData = 1 + strlen((char *)(ex.data + 1));  
  
#ifdef VSCP  
  if ( VSCP_ERROR_SUCCESS == ( rv = vscp.sendEventToRemote(ex) ) ) {
#ifdef DEBUG    
    Serial.println("VSCP: Sent rssi event");  
#endif    
  }
  else {
#ifdef DEBUG    
    Serial.print("Failed to send rssi event, ");
    Serial.print("Error=");
    Serial.println(rv);
#endif    
  }
#endif  

#ifdef MQTT
  if ( bMQTT ) {
    if ( sendMQTT(ex) ) {
#ifdef DEBUG      
      Serial.println("MQTT: Sent temperature event");  
#endif      
    }
    else {
#ifdef DEBUG      
      Serial.println("MQTT: Failed to send temperature event");
#endif      
    }
  }
#endif


  // Disconnect from VSCP host
#ifdef VSCP  
  vscp.disconnect();
#endif  

  // Disconnet from MQTT broker
#ifdef MQTT  
  mqttClient.disconnect();
#endif  

  espClient.stop();   // Wifi off

#ifdef DEBUG  
  Serial.println(F("disconnected"));
#endif  

  delay(30000);                       // wait for 10 sec before powering down (opening and closing mailbox cover)
  digitalWrite(holdPin, LOW);         // set GPIO 4 low this takes EN pin down & powers down the ESP.
  Serial.println("Ending it all...");
  delay(5000);                        // in case that mailbox cover is left open, wait for another 5 sec
  ESP.deepSleep(0,WAKE_RF_DEFAULT);   // going to deep sleep indefinitely until switch is depressed
  delay(100);                         // sometimes deepsleep command bugs so small delay is needed
  pinMode(holdPin, INPUT);
}

///////////////////////////////////////////////////////////////////////////////
// loop
//

void loop() 
{ 
  ; 
}

//////////////////////////////////////////////////////////////////////////////////
// sendMQTT
//

#ifdef MQTT
boolean sendMQTT(vscpEventEx &ex)
{
  char topic[70];
  char payload[330];

  // Create data array on string form
  char mqtt_data[50];
  char buf[5];
  memset(mqtt_data,0,sizeof(mqtt_data));
  
  for ( int i=0; i<ex.sizeData; i++) {    
    sprintf(buf,"0x%02X",ex.data[i]);
    strcat(mqtt_data, buf );
    if ( i != (ex.sizeData - 1 ) ) {
      strcat(mqtt_data,",");
    } 
  }          

  // Construct payload == JSON event
  memset(payload,0,sizeof(payload));
  sprintf(payload,
            VSCP_JSON_EVENT_TEMPLATE, 
            ex.head,
            (long unsigned int)ex.obid,
            "", // Leave blank, interface will set to current
            (long unsigned int)ex.timestamp,
            ex.vscp_class,
            ex.vscp_type,
            vscp_guid,
            mqtt_data,
            ""); 

  // Construct topic /vscp/guid/class/type
  memset(topic,0,sizeof(topic));
  sprintf(topic,
            mqtt_topic_template, 
            ex.vscp_class,
            ex.vscp_type);  
  return mqttClient.publish(topic, payload);
}
#endif