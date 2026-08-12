#include "Globals.h"

uint32_t   TimeNow;
bool       NET_Connected   = false;
uint32_t   NET_LastCheck   = 0;
__testmode TestMode     = _testmode_none;
SCHED::TaskId TestMode_tID = SCHED::TASK_ID_NONE;

namespace LED { LEDx State; }

Adafruit_NeoPixel stripFront(LED_START_I_UCOM + LED_UCOM_NUM, LED_PIN_FRONT, NEO_BRG + NEO_KHZ800);
Adafruit_NeoPixel stripBack(LED_BED_NUM + LED_LAMP_NUM, LED_PIN_BACK, NEO_BRG + NEO_KHZ800);
Adafruit_NeoPixel stripHB(LED_HB_NUM, LED_PIN_HB, NEO_GRB + NEO_KHZ800);

const unsigned int LIGHT_SENS_LUX[3] = {45, 75, 125};
namespace LISENS { LISENSx State; }

namespace HB { HBx State; }

namespace TV { TVx State; }

namespace MOTION { MOTIONx State; }

namespace APP { ARDx Ard; }
namespace APP { AMx Am; }
namespace APP { APPx State = {{0}, {0}, 0, true}; /* SelectedCacheDirty = true */ }
WiFiUDP   APP_UDP;
IPAddress APP_RECV_IP(0, 0, 0, 0);

namespace DIF { DIFx State = {}; }
WiFiUDP   DIF_UDP;
IPAddress DIF_TARGET_IP(192, 168, 1, 203);

const char* const DIF_MODE_NAMES[DIF_MODE_MAX] PROGMEM = {
    "CONT", "10 SEC", "2H AFTER SLEEP", "4H AFTER SLEEP"
};
const char* const DIF_EFFECT_NAMES[DIF_EFFECT_COUNT + 1] PROGMEM = {
    "STATIC", "FADE", "PULSE", "RANDOM", "RAINBOW", "SPARKLE", "FIRE", "BOUNCE", "CONFETTI"
};
const char* const DIF_STRIP_STATUS_NAMES[11] PROGMEM = {
    "OFF", "STATIC", "DUAL", "FADE", "PULSE", "RANDOM", "RAINBOW", "SPARKLE", "FIRE", "BOUNCE", "CONFETTI"
};

namespace TELNET { TELNETx State; }
WiFiServer TELNET_Srv(TELNET_PORT);
WiFiClient TELNET_Cli;

/* fps:                             15   25   30   60   90   120  150  200  240 */
const uint8_t LED_FPS_TABLE[LED_FPS_OPTIONS_TOTAL] PROGMEM = { 67,  40,  33,  17,  11,  8,   7,   5,   4  };

namespace EE { EEx State; }
uint8_t EE_SET[EE_MEM_X];
uint8_t EE_Changed[(EE_MEM_X + 7) / 8] = {0};
uint8_t EE_ColorChanged[(LED_NUM + 7) / 8] = {0};
uint8_t EE_AmbientChanged[(LED_NUM + 7) / 8] = {0};
bool    EE_UdpChanged    = false;
bool    EE_MotionChanged = false;

const int UDPRAW_BuffSize = 100;
uint8_t   UDPRAW_Buffer[100];
namespace UDPRAW { UDPRAWx State; }
WiFiUDP   UDPRAW_UDP;

namespace BME { BME280x State; }
BME280I2C bme280sensor;

// Same shared, gitignored _rmk_Shared/WiFiCredentials.h the Diffuser port
// already uses (see that port's Globals.h/DEF.h) - defines
// WIFI_SSID_VALUE/WIFI_PASS_VALUE, never committed. Included here (not via
// the sketch's DEF.h) since this is where the storage actually lives.
#include <WiFiCredentials.h>
const char WIFI_SSID[] PROGMEM = WIFI_SSID_VALUE;
const char WIFI_PASS[] PROGMEM = WIFI_PASS_VALUE;
const char MQTT_DEVICE_ID[] PROGMEM = MQTT_DEVICE_ID_VALUE;
namespace NET { NETx Wifi; }
namespace NET { DATEx Date; }
WiFiUDP   RTC_UDP;
NTPClient RTC_TimeClient(RTC_UDP);
uint8_t   NET_WifiSt = netWifiOK;
uint8_t   RTC_Status = rtcOK;

namespace MQTT { MQTTx State = {}; }
WiFiSSLClient MQTT_Net;
PubSubClient  MQTT_Cli(MQTT_Net);
namespace MQTTCRED { MQTTCREDx State = {}; }
