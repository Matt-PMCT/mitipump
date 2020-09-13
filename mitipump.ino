
#ifdef ESP32
#include <WiFi.h>
#else
#include <ESP8266WiFi.h>
#endif
#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <HeatPump.h>

#include "mitipump.h"

#ifdef OTA
#ifdef ESP32
#include <WiFiUdp.h>
#include <ESPmDNS.h>
#else
#include <ESP8266mDNS.h>
#endif
#include <ArduinoOTA.h>
#endif

// wifi, mqtt and heatpump client instances
WiFiClient espClient;
PubSubClient mqtt_client(espClient);
HeatPump hp;
uint8_t sendRemoteTemp = 0; // 0 = no, 1 = yes
float lastRemoteTemp = 0;
unsigned long lastRemoteTempRcvd;
unsigned long lastTempSend;
unsigned long waitCount = 0;                 // counter
uint8_t conn_stat = 0;                       // Connection status for WiFi and MQTT:
                                             // Original Code: https://esp32.com/viewtopic.php?t=3851
                                             // status |   WiFi   |    MQTT
                                             // -------+----------+------------
                                             //      0 |   down   |    down
                                             //      1 | starting |    down
                                             //      2 |    up    |    down
                                             //      3 |    up    |  starting
                                             //      4 |    up    | finalising
                                             //      5 |    up    |     up
//unsigned long lastTask = 0;                  // counter in example code for conn_stat <> 5



// debug mode, when true, will send all packets received from the heatpump to topic heatpump_debug_topic
// this can also be set by sending "on" to heatpump_debug_set_topic
bool _debugMode = false;
bool serialDebugMode = false;


void setup() {
  if (serialDebugMode == true) {
    Serial.begin(115200);
  }
  pinMode(redLedPin, OUTPUT);
  digitalWrite(redLedPin, HIGH);

  lastTempSend = millis();
}

void startWifi() {
  WiFi.persistent(false);
  WiFi.disconnect();
  WiFi.mode(WIFI_OFF);
  WiFi.mode(WIFI_STA);
  delay(1000);
  WiFi.setHostname(client_id);
  WiFi.begin(ssid, password);
}

void startMqtt() {
  mqtt_client.setServer(mqtt_server, mqtt_port);
  mqtt_client.setCallback(mqttCallback);
  mqttConnect();
  haConfig();
  conn_stat = 4;
}

void startHeatPump() {
  // connect to the heatpump. Callbacks first so that the hpPacketDebug callback is available for connect()
  hp.setSettingsChangedCallback(hpSettingsChanged);
  hp.setStatusChangedCallback(hpStatusChanged);
  hp.setPacketCallback(hpPacketDebug);

  //Huzzah 32 needs SerialONE, "Serial" is for the USB port, non-Huzzah boards may be different.
  hp.connect(&Serial1);
}

void hpSettingsChanged() {
  // send room temp, operating info and all information
  heatpumpSettings currentSettings = hp.getSettings();
  heatpumpStatus currentStatus = hp.getStatus();
  
  const size_t bufferSizeInfo = JSON_OBJECT_SIZE(6);
  DynamicJsonDocument rootInfo(bufferSizeInfo);

  rootInfo["roomTemperature"] = hp.CelsiusToFahrenheit(hp.getRoomTemperature());
  rootInfo["temperature"]     = hp.CelsiusToFahrenheit(currentSettings.temperature);
  rootInfo["operating"]       = currentStatus.operating;
  rootInfo["fan"]             = currentSettings.fan;
  rootInfo["vane"]            = currentSettings.vane;
  rootInfo["mode"]            = getPowerAndModeToString(currentSettings);

  char bufferInfo[512];
  serializeJson(rootInfo, bufferInfo);
  if (rootInfo["roomTemperature"] != 32) {
    if (!mqtt_client.publish(ha_state_topic, bufferInfo, true)) {
      mqtt_client.publish(ha_debug_topic, "failed to publish to room temp and operation status to heatpump/status topic");
    }
  } else {
    mqtt_client.publish(ha_debug_topic, "Room temp 32, no state message published");
  }
}

const char* getPowerAndModeToString(heatpumpSettings currentSettings) {
  String hppower = String(currentSettings.power);
  String hpmode = String(currentSettings.mode);

  hppower.toLowerCase();
  hpmode.toLowerCase();

  if (hpmode == "fan") {
    return "fan_only";
  }
  else if (hpmode == "auto") {
    return "heat_cool";
  }
  else if (hppower == "off") {
    return "off";
  }
  else {
    return hpmode.c_str();
  }
}

void hpStatusChanged(heatpumpStatus currentStatus) {
  // send room temp, operating info and all information
  heatpumpSettings currentSettings = hp.getSettings();
  
  const size_t bufferSizeInfo = JSON_OBJECT_SIZE(6);
  DynamicJsonDocument rootInfo(bufferSizeInfo);

  rootInfo["roomTemperature"] = hp.CelsiusToFahrenheit(hp.getRoomTemperature());
  rootInfo["temperature"]     = hp.CelsiusToFahrenheit(currentSettings.temperature);
  rootInfo["operating"]       = currentStatus.operating;
  rootInfo["fan"]             = currentSettings.fan;
  rootInfo["vane"]            = currentSettings.vane;
  rootInfo["mode"]            = getPowerAndModeToString(currentSettings);

  char bufferInfo[512];
  serializeJson(rootInfo, bufferInfo);

  if (rootInfo["roomTemperature"] != 32) {
    if (!mqtt_client.publish(ha_state_topic, bufferInfo, true)) {
      mqtt_client.publish(ha_debug_topic, "failed to publish to room temp and operation status to ha_state_topic topic");
    }
  } else {
    mqtt_client.publish(ha_debug_topic, "Room temp 32, no state message published");
  }
}

void hpPacketDebug(byte* packet, unsigned int length, char* packetDirection) {
  if (_debugMode) {
    String message;
    for (int idx = 0; idx < length; idx++) {
      if (packet[idx] < 16) {
        message += "0"; // pad single hex digits with a 0
      }
      message += String(packet[idx], HEX) + " ";
    }

    const size_t bufferSize = JSON_OBJECT_SIZE(6);
    DynamicJsonDocument root(bufferSize);

    root[packetDirection] = message;

    char buffer[512];
    serializeJson(root, buffer);

    if (!mqtt_client.publish(ha_debug_topic, buffer)) {
      mqtt_client.publish(ha_debug_topic, "failed to publish to heatpump/debug topic");
    }
  }
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  // Copy payload into message buffer
  char message[length + 1];
  for (int i = 0; i < length; i++) {
    message[i] = (char)payload[i];
  }
  message[length] = '\0';

  //Handle the Home Assistant Topics
  if (strcmp(topic, ha_power_set_topic) == 0) {
    hp.setPowerSetting(message);
    hp.update();
  } else if (strcmp(topic, ha_mode_set_topic) == 0) {
    const size_t bufferSize = JSON_OBJECT_SIZE(2);
    StaticJsonDocument<bufferSize> root;
    //root["mode"] = message;
    //String mqttOutput;
    //serializeJson(root, mqttOutput);
    //mqtt_client.publish(ha_state_topic, mqttOutput.c_str());
    String modeUpper = message;
    modeUpper.toUpperCase();
    if (modeUpper == "HEAT_COOL") {
      modeUpper = "AUTO";
    }
    else if (modeUpper == "FAN_ONLY") {
      modeUpper = "FAN";
    } 
    else if (modeUpper == "OFF") {
      hp.setPowerSetting("OFF");
    } else {
      hp.setPowerSetting("ON");
    }
    hp.setModeSetting(modeUpper.c_str());
    hp.update();
  } else if (strcmp(topic, ha_temp_set_topic) == 0) {
    float temperature = strtof(message, NULL);
    const size_t bufferSize = JSON_OBJECT_SIZE(2);
    StaticJsonDocument<bufferSize> root;
    //root["temperature"] = message;
    //String mqttOutput;
    //serializeJson(root, mqttOutput);
    //mqtt_client.publish(ha_state_topic, mqttOutput.c_str());
    hp.setTemperature(hp.FahrenheitToCelsius(temperature));
    hp.update();
  } 
  else if (strcmp(topic, ha_fan_set_topic) == 0) {
    const size_t bufferSize = JSON_OBJECT_SIZE(2);
    StaticJsonDocument<bufferSize> root;
    //root["fan"] = message;
    //String mqttOutput;
    //serializeJson(root, mqttOutput);
    //mqtt_client.publish(ha_state_topic, mqttOutput.c_str());
    hp.setFanSpeed(message);
    hp.update();
  }
  else if (strcmp(topic, ha_vane_set_topic) == 0) {
    const size_t bufferSize = JSON_OBJECT_SIZE(2);
    StaticJsonDocument<bufferSize> root;
    //root["vane"] = message;
    //String mqttOutput;
    //serializeJson(root, mqttOutput);
    //mqtt_client.publish(ha_state_topic, mqttOutput.c_str());
    hp.setVaneSetting(message);
    hp.update();
  }
  else if (strcmp(topic, ha_remTemp_set_topic) == 0) {
    lastRemoteTempRcvd = millis();
    lastRemoteTemp = hp.FahrenheitToCelsius(strtof(message, NULL));
    const size_t bufferSize = JSON_OBJECT_SIZE(2);
    StaticJsonDocument<bufferSize> root;
    sendRemoteTemp = 1; // set this value to routinely update the remote temp
    if (_debugMode) {
      mqtt_client.publish(ha_debug_topic, "Setting Remote Temp");
    }
    hp.setRemoteTemperature(lastRemoteTemp); // send it now though
    hp.update(); 
  }
  else if (strcmp(topic, ha_debug_set_topic) == 0) { //if the incoming message is on the heatpump_debug_set_topic topic...
    if (strcmp(message, "on") == 0) {
      _debugMode = true;
      mqtt_client.publish(ha_debug_topic, "debug mode enabled");
    } else if (strcmp(message, "off") == 0) {
      _debugMode = false;
      mqtt_client.publish(ha_debug_topic, "debug mode disabled");
    } else {
      mqtt_client.publish(ha_debug_topic, "Debug topic requires RAW on or off to set");
    }
  } 
  else {
    mqtt_client.publish(ha_debug_topic, strcat("heatpump: wrong mqtt topic: ", topic));
  }
}

void mqttConnect() {
  // Loop until we're reconnected
  if (!mqtt_client.connected()) {
    // Attempt to connect
    if (mqtt_client.connect(client_id, mqtt_username, mqtt_password)) {
      mqtt_client.publish(ha_debug_topic, "Connected to MQTT Server");
      mqtt_client.subscribe(ha_debug_set_topic);
      mqtt_client.subscribe(ha_power_set_topic);
      mqtt_client.subscribe(ha_mode_set_topic);
      mqtt_client.subscribe(ha_fan_set_topic);
      mqtt_client.subscribe(ha_temp_set_topic);
      mqtt_client.subscribe(ha_vane_set_topic);
      mqtt_client.subscribe(ha_remTemp_set_topic);
    } else {
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

void haConfig() {

  // send HA config packet
  // setup HA payload device
  const size_t capacity = 3 * JSON_ARRAY_SIZE(5) + JSON_ARRAY_SIZE(7) + JSON_OBJECT_SIZE(23) + JSON_OBJECT_SIZE(50);
  DynamicJsonDocument haConfig(capacity);

  haConfig["name"]                          = client_id;
  haConfig["mode_cmd_t"]                    = ha_mode_set_topic;
  haConfig["mode_stat_t"]                   = ha_state_topic;
  haConfig["mode_stat_tpl"]                 = "{{ value_json.mode if (value_json is defined and value_json.mode is defined and value_json.mode|length) else 'off' }}"; //Set default value for fix "Could not parse data for HA"
  haConfig["temp_cmd_t"]                    = ha_temp_set_topic;
  haConfig["temp_stat_t"]                   = ha_state_topic;
  haConfig["temp_stat_tpl"]                 = "{{ value_json.temperature if (value_json is defined and value_json.temperature is defined and value_json.temperature|int > 16) else '26' }}"; //Set default value for fix "Could not parse data for HA"
  haConfig["curr_temp_t"]                   = ha_state_topic;
  haConfig["curr_temp_tpl"]                 = "{{ value_json.roomTemperature if (value_json is defined and value_json.roomTemperature is defined and value_json.roomTemperature|int > 16) else '26' }}"; //Set default value for fix "Could not parse data for HA"
  haConfig["min_temp"]                      = "61";
  haConfig["max_temp"]                      = "86";
  haConfig["unique_id"]                      = client_id;

  JsonArray haConfigModes = haConfig.createNestedArray("modes");
  haConfigModes.add("heat_cool"); //native AUTO mode
  haConfigModes.add("cool");
  haConfigModes.add("dry");
  haConfigModes.add("heat");
  haConfigModes.add("fan_only");  //native FAN mode
  haConfigModes.add("off");

  JsonArray haConfigFan_modes = haConfig.createNestedArray("fan_modes");
  haConfigFan_modes.add("AUTO");
  haConfigFan_modes.add("QUIET");
  haConfigFan_modes.add("1");
  haConfigFan_modes.add("2");
  haConfigFan_modes.add("3");
  haConfigFan_modes.add("4");

  JsonArray haConfigSwing_modes = haConfig.createNestedArray("swing_modes");
  haConfigSwing_modes.add("AUTO");
  haConfigSwing_modes.add("1");
  haConfigSwing_modes.add("2");
  haConfigSwing_modes.add("3");
  haConfigSwing_modes.add("4");
  haConfigSwing_modes.add("5");
  haConfigSwing_modes.add("SWING");
  haConfig["pow_cmd_t"]                     = ha_power_set_topic;
  haConfig["fan_mode_cmd_t"]                = ha_fan_set_topic;
  haConfig["fan_mode_stat_t"]               = ha_state_topic;
  haConfig["fan_mode_stat_tpl"]             = "{{ value_json.fan if (value_json is defined and value_json.fan is defined and value_json.fan|length) else 'AUTO' }}"; //Set default value for fix "Could not parse data for HA"
  haConfig["swing_mode_cmd_t"]              = ha_vane_set_topic;
  haConfig["swing_mode_stat_t"]             = ha_state_topic;
  haConfig["swing_mode_stat_tpl"]           = "{{ value_json.vane if (value_json is defined and value_json.vane is defined and value_json.vane|length) else 'AUTO' }}"; //Set default value for fix "Could not parse data for HA"

  haConfig["action_topic"]                  = ha_state_topic;
  haConfig["action_template"]               = "{% set values = {'off':'off', 'heat':'heating', 'cool':'cooling', 'dry':'drying', 'fan_only':'fan'} %}{% if value_json is defined and value_json.mode|length %}{% if value_json.mode == 'off' %}{{'off'}}{% else %}{% if value_json.operating is sameas true %}{{ values[value_json.mode] if value_json.mode in values.keys() else 'idle'}}{% else %}{{'idle'}}{% endif %}{% endif %}{% else %}{{'idle'}}{% endif %}";
  
  JsonObject haConfigDevice = haConfig.createNestedObject("device");

  haConfigDevice["ids"]   = client_id;
  haConfigDevice["name"]  = client_id;
  haConfigDevice["sw"]    = "Mitsu2MQTT .3ME";
  haConfigDevice["mdl"]   = "HVAC MITUBISHI";
  haConfigDevice["mf"]    = "MITSUBISHI";


  String mqttOutput;
  serializeJson(haConfig, mqttOutput);
  mqtt_client.beginPublish(ha_config_topic, mqttOutput.length(), true);
  mqtt_client.print(mqttOutput);
  mqtt_client.endPublish();
}

void loop() {
  if ((WiFi.status() != WL_CONNECTED) && (conn_stat != 1)) { conn_stat = 0; }
  if ((WiFi.status() == WL_CONNECTED) && !mqtt_client.connected() && (conn_stat != 3))  { conn_stat = 2; }
  if ((WiFi.status() == WL_CONNECTED) && mqtt_client.connected() && (conn_stat != 5)) { conn_stat = 4;}
  switch (conn_stat) {
    case 0:                                                       // MQTT and WiFi down: start WiFi
      if (serialDebugMode == true) {
        Serial.println("MQTT and WiFi down: start WiFi");
      }
      startWifi();
      conn_stat = 1;
      break;
    case 1:                                                       // WiFi starting, do nothing here
      if (serialDebugMode == true) {
        Serial.println("WiFi starting (status:" + String(WiFi.status()) + ", wait : "+ String(waitCount));
      }
      if (waitCount > 150) {
        if (serialDebugMode == true) {
          Serial.println("WiFi restarting");
        }
        WiFi.disconnect();
        waitCount = 0;
        conn_stat = 0;
      }
      waitCount++;
      break;
    case 2:                                                       // WiFi up, MQTT down: start MQTT
      if (serialDebugMode == true) {
        Serial.println("WiFi up, MQTT down: start MQTT");
      }
      startMqtt();
      conn_stat = 3; 
      waitCount = 0;
      break;
    case 3:                                                       // WiFi up, MQTT starting, do nothing here
      if (serialDebugMode == true) {
        Serial.println("WiFi up, MQTT starting, wait : "+ String(waitCount));
      }
      waitCount++;
      if (waitCount > 50) {
        startMqtt();
        waitCount = 0;
      }
      delay(5000);
      break;
    case 4:                                                       // WiFi up, MQTT up: finish MQTT configuration
      if (serialDebugMode == true) {
        Serial.println("WiFi up, MQTT up: finish MQTT configuration");
      }
#ifdef OTA
      ArduinoOTA.setHostname(client_id);
      ArduinoOTA.setPassword(ota_password);
      ArduinoOTA.onStart([]() {
        mqtt_client.publish(ha_debug_topic, "OTA Update Started");
      });
      ArduinoOTA.onEnd([]() {
        mqtt_client.publish(ha_debug_topic, "OTA Update Completed");
      });
      ArduinoOTA.onError([](ota_error_t error) {
        Serial.printf("Error[%u]: ", error);
        if (error == OTA_AUTH_ERROR) mqtt_client.publish(ha_debug_topic, "OTA Error: Auth Fail");
        else if (error == OTA_BEGIN_ERROR) mqtt_client.publish(ha_debug_topic, "OTA Error: Begin Fail");
        else if (error == OTA_CONNECT_ERROR) mqtt_client.publish(ha_debug_topic, "OTA Error: Connect Fail");
        else if (error == OTA_RECEIVE_ERROR) mqtt_client.publish(ha_debug_topic, "OTA Error: Recieve Fail");
        else if (error == OTA_END_ERROR) mqtt_client.publish(ha_debug_topic, "OTA Error: End Fail");
      });
      ArduinoOTA.begin();
#endif
      startHeatPump();
      conn_stat = 5;
      digitalWrite(redLedPin, LOW);                    
      break;
  }
// end of non-blocking connection setup section

// start section with tasks where WiFi/MQTT is required
  if (conn_stat == 5) {
    hp.sync();
    if (millis() > (lastTempSend + SEND_ROOM_TEMP_INTERVAL_MS)) { // only send the temperature every 60s
      hpStatusChanged(hp.getStatus());
      lastTempSend = millis();
      if (sendRemoteTemp == 1) {
        if (_debugMode) {
          mqtt_client.publish(ha_debug_topic, "Setting Remote Temp");
        }
        //also send the remote temp, must send it every minute to keep unit using it, or it will fall back to unit temp
        hp.setRemoteTemperature(lastRemoteTemp);
        hp.update();
        if (millis() > (lastRemoteTempRcvd + ROOM_TEMP_RCV_TIMEOUT_MS)) { //consider remote temp recieved expired (switch back to unit)
          sendRemoteTemp = 0; // change to do not send
          hp.setRemoteTemperature(0); //this will return the unit to using its internal temp (as would not sending one)
        }
      }
    }
    ArduinoOTA.handle();
    mqtt_client.loop();
  } else {
    // flashing the red LED to indicate WiFi / MQTT connecting...
    digitalWrite(redLedPin, HIGH);
    delay(conn_stat * 200);
    digitalWrite(redLedPin, LOW);
  }
  
// end of section for tasks where WiFi/MQTT are required

// start section for tasks which should run regardless of WiFi/MQTT
  //if (millis() - lastTask > 1000) {                                 // Print message every second (just as an example)
    //Serial.println("print this every second");
    //lastTask = millis();
  //}
// end of section for tasks which should run regardless of WiFi/MQTT
  delay(500);
#ifdef OTA
  ArduinoOTA.handle();
#endif
}
