
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
unsigned long lastTempSend;

// debug mode, when true, will send all packets received from the heatpump to topic heatpump_debug_topic
// this can also be set by sending "on" to heatpump_debug_set_topic
bool _debugMode = false;


void setup() {
// setup HA topics

  
  pinMode(redLedPin, OUTPUT);
  digitalWrite(redLedPin, HIGH);
  pinMode(blueLedPin, OUTPUT);
  digitalWrite(blueLedPin, HIGH);

  WiFi.setHostname(client_id);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    // wait 500ms, flashing the blue LED to indicate WiFi connecting...
    digitalWrite(blueLedPin, LOW);
    delay(250);
    digitalWrite(blueLedPin, HIGH);
    delay(250);
  }

  

  // startup mqtt connection
  mqtt_client.setServer(mqtt_server, mqtt_port);
  mqtt_client.setCallback(mqttCallback);
  mqttConnect();
  haConfig();

  // connect to the heatpump. Callbacks first so that the hpPacketDebug callback is available for connect()
  hp.setSettingsChangedCallback(hpSettingsChanged);
  hp.setStatusChangedCallback(hpStatusChanged);
  hp.setPacketCallback(hpPacketDebug);

#ifdef OTA
  ArduinoOTA.setHostname(client_id);
  ArduinoOTA.setPassword(ota_password);
  ArduinoOTA.begin();
#endif

  hp.connect(&Serial1);

  lastTempSend = millis();
}

void hpSettingsChanged() {
  // send room temp, operating info and all information
  heatpumpSettings currentSettings = hp.getSettings();
  
  const size_t bufferSizeInfo = JSON_OBJECT_SIZE(5);
  DynamicJsonDocument rootInfo(bufferSizeInfo);

  rootInfo["roomTemperature"] = hp.CelsiusToFahrenheit(hp.getRoomTemperature());
  rootInfo["temperature"]     = hp.CelsiusToFahrenheit(currentSettings.temperature);
  rootInfo["fan"]             = currentSettings.fan;
  rootInfo["vane"]            = currentSettings.vane;
  rootInfo["mode"]            = getPowerAndModeToString(currentSettings);

  char bufferInfo[512];
  serializeJson(rootInfo, bufferInfo);

  if (!mqtt_client.publish(ha_state_topic, bufferInfo, true)) {
    mqtt_client.publish(ha_debug_topic, "failed to publish to room temp and operation status to heatpump/status topic");
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
  
  const size_t bufferSizeInfo = JSON_OBJECT_SIZE(5);
  DynamicJsonDocument rootInfo(bufferSizeInfo);

  rootInfo["roomTemperature"] = hp.CelsiusToFahrenheit(hp.getRoomTemperature());
  rootInfo["temperature"]     = hp.CelsiusToFahrenheit(currentSettings.temperature);
  rootInfo["fan"]             = currentSettings.fan;
  rootInfo["vane"]            = currentSettings.vane;
  rootInfo["mode"]            = getPowerAndModeToString(currentSettings);

  char bufferInfo[512];
  serializeJson(rootInfo, bufferInfo);

  if (!mqtt_client.publish(ha_state_topic, bufferInfo, true)) {
    mqtt_client.publish(ha_debug_topic, "failed to publish to room temp and operation status to ha_state_topic topic");
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
    root["mode"] = message;
    String mqttOutput;
    serializeJson(root, mqttOutput);
    mqtt_client.publish(ha_state_topic, mqttOutput.c_str());
    String modeUpper = message;
    modeUpper.toUpperCase();
    hp.setModeSetting(modeUpper.c_str());
    hp.update();
  } else if (strcmp(topic, ha_temp_set_topic) == 0) {
    float temperature = strtof(message, NULL);
    const size_t bufferSize = JSON_OBJECT_SIZE(2);
    StaticJsonDocument<bufferSize> root;
    root["temperature"] = message;
    String mqttOutput;
    serializeJson(root, mqttOutput);
    mqtt_client.publish(ha_state_topic, mqttOutput.c_str());
    hp.setTemperature(hp.FahrenheitToCelsius(temperature));
    hp.update();
  } 
  else if (strcmp(topic, ha_fan_set_topic) == 0) {
    const size_t bufferSize = JSON_OBJECT_SIZE(2);
    StaticJsonDocument<bufferSize> root;
    root["fan"] = message;
    String mqttOutput;
    serializeJson(root, mqttOutput);
    mqtt_client.publish(ha_state_topic, mqttOutput.c_str());
    hp.setFanSpeed(message);
    hp.update();
  }
  else if (strcmp(topic, ha_vane_set_topic) == 0) {
    const size_t bufferSize = JSON_OBJECT_SIZE(2);
    StaticJsonDocument<bufferSize> root;
    root["vane"] = message;
    String mqttOutput;
    serializeJson(root, mqttOutput);
    mqtt_client.publish(ha_state_topic, mqttOutput.c_str());
    hp.setVaneSetting(message);
    hp.update();
  }
  else if (strcmp(topic, ha_debug_set_topic) == 0) { //if the incoming message is on the heatpump_debug_set_topic topic...
    if (strcmp(message, "on") == 0) {
      _debugMode = true;
      mqtt_client.publish(ha_debug_topic, "debug mode enabled");
    } else if (strcmp(message, "off") == 0) {
      _debugMode = false;
      mqtt_client.publish(ha_debug_topic, "debug mode disabled");
    }
  } 
  /*else if (strcmp(topic, heatpump_set_topic) == 0) { //if the incoming message is on the heatpump_set_topic topic...
    // Parse message into JSON
    const size_t bufferSize = JSON_OBJECT_SIZE(6);
    DynamicJsonDocument root(bufferSize);
    DeserializationError error = deserializeJson(root, message);

    if (error) {
      mqtt_client.publish(heatpump_debug_topic, "!root.success(): invalid JSON on heatpump_set_topic...");
      return;
    }

    // Step 3: Retrieve the values
    if (root.containsKey("power")) {
      const char* power = root["power"];
      hp.setPowerSetting(power);
    }

    if (root.containsKey("mode")) {
      const char* mode = root["mode"];
      hp.setModeSetting(mode);
    }

    if (root.containsKey("temperature")) {
      float temperature = root["temperature"];
      hp.setTemperature(hp.FahrenheitToCelsius(temperature));
    }

    if (root.containsKey("fan")) {
      const char* fan = root["fan"];
      hp.setFanSpeed(fan);
    }

    if (root.containsKey("vane")) {
      const char* vane = root["vane"];
      hp.setVaneSetting(vane);
    }

    if (root.containsKey("wideVane")) {
      const char* wideVane = root["wideVane"];
      hp.setWideVaneSetting(wideVane);
    }

    if (root.containsKey("remoteTemp")) {
      float remoteTemp = root["remoteTemp"];
      hp.setRemoteTemperature(hp.FahrenheitToCelsius(remoteTemp));
    }
    else if (root.containsKey("custom")) {
      String custom = root["custom"];

      // copy custom packet to char array
      char buffer[(custom.length() + 1)]; // +1 for the NULL at the end
      custom.toCharArray(buffer, (custom.length() + 1));

      byte bytes[20]; // max custom packet bytes is 20
      int byteCount = 0;
      char *nextByte;

      // loop over the byte string, breaking it up by spaces (or at the end of the line - \n)
      nextByte = strtok(buffer, " ");
      while (nextByte != NULL && byteCount < 20) {
        bytes[byteCount] = strtol(nextByte, NULL, 16); // convert from hex string
        nextByte = strtok(NULL, "   ");
        byteCount++;
      }

      // dump the packet so we can see what it is. handy because you can run the code without connecting the ESP to the heatpump, and test sending custom packets
      hpPacketDebug(bytes, byteCount, "customPacket");

      hp.sendCustomPacket(bytes, byteCount);
    }
    else {
      bool result = hp.update();

      if (!result) {
        mqtt_client.publish(heatpump_debug_topic, "heatpump: update() failed");
      }
    }
*/
  else {
    mqtt_client.publish(ha_debug_topic, strcat("heatpump: wrong mqtt topic: ", topic));
  }
}

void mqttConnect() {
  // Loop until we're reconnected
  while (!mqtt_client.connected()) {
    // Attempt to connect
    if (mqtt_client.connect(client_id, mqtt_username, mqtt_password)) {
      mqtt_client.subscribe(ha_debug_set_topic);
      mqtt_client.subscribe(ha_power_set_topic);
      mqtt_client.subscribe(ha_mode_set_topic);
      mqtt_client.subscribe(ha_fan_set_topic);
      mqtt_client.subscribe(ha_temp_set_topic);
      mqtt_client.subscribe(ha_vane_set_topic);
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
  haConfig["mode_stat_tpl"]                 = "{{ value_json.mode }}";
  haConfig["temp_cmd_t"]                    = ha_temp_set_topic;
  haConfig["temp_stat_t"]                   = ha_state_topic;
  haConfig["temp_stat_tpl"]                 = "{{ value_json.temperature }}";
  haConfig["curr_temp_t"]                   = ha_state_topic;
  haConfig["current_temperature_template"]  = "{{ value_json.roomTemperature }}";
  haConfig["min_temp"]                      = "61";
  haConfig["max_temp"]                      = "86";
  haConfig["unique_id"]                      = client_id;

  JsonArray haConfigModes = haConfig.createNestedArray("modes");
  haConfigModes.add("auto");
  haConfigModes.add("off");
  haConfigModes.add("cool");
  haConfigModes.add("heat");
  haConfigModes.add("dry");

  JsonArray haConfigFan_modes = haConfig.createNestedArray("fan_modes");
  haConfigFan_modes.add("AUTO");
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
  haConfig["fan_mode_stat_tpl"]             = "{{ value_json.fan }}";
  haConfig["swing_mode_cmd_t"]              = ha_vane_set_topic;
  haConfig["swing_mode_stat_t"]             = ha_state_topic;
  haConfig["swing_mode_stat_tpl"]           = "{{ value_json.vane }}";

  JsonObject haConfigDevice = haConfig.createNestedObject("device");

  haConfigDevice["ids"]   = client_id;
  haConfigDevice["name"]  = client_id;
  haConfigDevice["sw"]    = "Mitsu2MQTT .1ME";
  haConfigDevice["mdl"]   = "HVAC MITUBISHI";
  haConfigDevice["mf"]    = "MITSUBISHI";


  String mqttOutput;
  serializeJson(haConfig, mqttOutput);
  mqtt_client.beginPublish(ha_config_topic, mqttOutput.length(), true);
  mqtt_client.print(mqttOutput);
  mqtt_client.endPublish();
}

void loop() {
  if (!mqtt_client.connected()) {
    mqttConnect();
  }

  hp.sync();

  if (millis() > (lastTempSend + SEND_ROOM_TEMP_INTERVAL_MS)) { // only send the temperature every 60s
    hpStatusChanged(hp.getStatus());
    lastTempSend = millis();
  }

  mqtt_client.loop();

#ifdef OTA
  ArduinoOTA.handle();
#endif
}
