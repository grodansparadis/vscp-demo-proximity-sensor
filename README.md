# vscp-demo-proximity-sensor
Proximity sensor based on deep sleep battery powered ESP8266

This project uses a proximity sensor to detect movement and send events either to a VSCP host ot a MQTT broker whhen movement is detected.

As the setup is battery powered the system is in deep sleep most of the time and only awakens when the lid is opened.

