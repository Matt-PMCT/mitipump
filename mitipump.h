/*
 * mitipump - An ESP32 Ardunio based controller for Home Assistant and Mitisubishi Split AC units
 *          - Home Assistant Integration would not exist without the @gysmo38 repository available here: https://github.com/gysmo38/mitsubishi2MQTT
 *          - Requires and based on the excellent HeatPump library by @SwiCago available here: https://github.com/SwiCago/HeatPump
 *          - Is designed to be plug and play with an Adafruit Huzzah32 Feather.
 */
//#define ESP32
//#define OTA
//const char* ota_password = "<YOUR OTA PASSWORD GOES HERE>";

// wifi settings
const char* ssid     = "<YOUR WIFI SSID GOES HERE>";
const char* password = "<YOUR WIFI PASSWORD GOES HERE>";

// mqtt server settings
const char* mqtt_server   = "<YOUR MQTT BROKER IP/HOSTNAME GOES HERE>";
const int mqtt_port       = 1883;
const char* mqtt_username = "<YOUR MQTT USERNAME GOES HERE>";
const char* mqtt_password = "<YOUR MQTT PASSWORD GOES HERE>";

// mqtt client settings
// Note PubSubClient.h has a MQTT_MAX_PACKET_SIZE of 128 defined, so either raise it to 256 or use short topics
const char* client_id                   = "hvac_lr"; // Must be unique on the MQTT network
const char* home_assistant_discovery_topic = "homeassistant/climate/hvac_lr/config";

const char* ha_power_set_topic      = "hvac_lr/power/set";
const char* ha_mode_set_topic       = "hvac_lr/mode/set";
const char* ha_temp_set_topic       = "hvac_lr/temp/set";
const char* ha_fan_set_topic        = "hvac_lr/fan/set";
const char* ha_vane_set_topic       = "hvac_lr/vane/set";
const char* ha_wideVane_set_topic   = "hvac_lr/wideVane/set";
const char* ha_state_topic          = "hvac_lr/state";
const char* ha_debug_topic          = "hvac_lr/debug";
const char* ha_debug_set_topic      = "hvac_lr/debug/set";
const char* ha_config_topic         = "homeassistant/climate/hvac_lr/config";

// pinouts
const int redLedPin  = 13; // Onboard LED = digital pin 13 (red LED on adafruit ESP32 huzzah)


// sketch settings
const unsigned int SEND_ROOM_TEMP_INTERVAL_MS = 60000;
