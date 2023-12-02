#include <Adafruit_NeoPixel.h>
#include <WiFi.h>
#include <esp_adc_cal.h>
#include <sntp.h>
#include <esp_crt_bundle.h>
#include <ssl_client.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>

#include "WiFi-Manager.h"

#if __has_include("private.h")

#include "private.h" // (you can put the information below in a separate header)

#else

// Hive MQ credentials for MQTT broker
#define mqttServer "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx.xx.xx.hivemq.cloud"
#define mqttUser ".........."
#define mqttPass ".........."

// Optional Auto-provisioning customization
#define DEFAULT_WIFI_SSID ".........."
#define DEFAULT_WIFI_PASS ".........."
#define DEFAULT_WIFI_OWNER "........."

// WiFi Training strings (customize here)
#define WIFI_HOST_NAME "Wedding-Puck"
#define ACK_MESSAGE "<p>Your input has been received. Thank you.</p>"
#define ACK_PAGE_TITLE "The Wedding Puck"

// Optional Anniversary celebration info
#define WEDDING_DAY 14
#define WEDDING_MONTH 10
#define WEDDING_YEAR 2023

// These are the hotspot names you can use to secretly signal pucks to glow (or provision, etc)
#define FIREWORKS_SSID "wedding-fireworks"
#define PROVISION_SSID "wedding-provision"
#define DEPROVISION_SSID "wedding-deprovision"

// After initiating a glow, you have to wait 23 hours to be eligible to do it again
#define SECONDS_BETWEEN_GLOWS (23 * 60 * 60)

// The length of a "glow" (5 minutes)
#define FIREWORKS_LENGTH_SECONDS (5 * 60)

#endif

// Pin assignments
#define NEOPIXELPIN 18
#define BATTERYPIN 2
#define TOUCHPIN 5
#define LEDPIN 17

// ESP32-S3 touch pin sensitivity
#define TOUCH_THRESHOLD 30000

// How many NeoPixels are attached to the ESP32?
#define NUMPIXELS 9

Adafruit_NeoPixel pixels(NUMPIXELS, NEOPIXELPIN, NEO_GRB + NEO_KHZ800);
WiFiClientSecure wifiClient;
PubSubClient mqttClient(wifiClient);
volatile bool everTouched = false;
volatile bool ntpComplete = false;
volatile bool inboundGlow = false;

const char mqttGlowTopic[] = "/devices/glow";
const char mqttAnnounceTopic[] = "/devices/announce";
enum { CMD_NONE, CMD_WEDDING_FIREWORKS, CMD_DEPROVISION, CMD_PROVISION };

void LedGlideBlocking(unsigned long duration, int r1, int g1, int b1);
void LedRotate(unsigned long speed, int r, int g, int b);
void LedPulse(unsigned long speed, int r, int g, int b);
bool EverTouched();

const int brownr = 0x96/3, browng = 0x4b/3, brownb = 0/3;  // brownish

class MyWiFiManager : public WiFiManager
{
  public:
    virtual bool OnWaitForSSIDConnect() const;
    virtual bool OnWaitForURLConnect() const;
    virtual void OnConnecting() const;
};

void setup()
{
  static MyWiFiManager wifimgr;

  Serial.begin(115200);

  double voltage = GetBatteryVoltage(BATTERYPIN);
  if (voltage < 3.5)
  {
    RenderVoltage(voltage);
  }
  else
  {
    unsigned long startmillis = millis();
    pinMode(LEDPIN, OUTPUT);

    pixels.begin();

    touchAttachInterrupt(TOUCHPIN, []() { everTouched = true; }, TOUCH_THRESHOLD);

    HandleStartup(wifimgr);

    unsigned long elapsed = millis() - startmillis;
    Serial.printf("Total time awake = %lu ms\n", elapsed);
  }

  DeepSleep(60000);
}

void loop() {}

/// @brief Handle general touch startup and two special cases if held a longer time
///        6 seconds = WiFi Training
///        20 seconds = Provision with default SSID
/// @param wifimgr
/// @return true if extended touch was handled
bool HandleSpecialTouchStartup(MyWiFiManager &wifimgr)
{
  if (!EverTouched())
    return false;
  
  unsigned long start = millis();
  LedGlideBlocking(1000, brownr, browng, brownb); // glide to brownish over 1 second
  Serial.printf("Touch startup!\n");

  // If user holds button for 6 seconds, this is a sign to enter WiFi training mode 
  while (CurrentlyTouched() && millis() - start < 6000);

  // Still touched after 6 seconds?
  if (CurrentlyTouched())
  {
    Serial.printf(" - still touched after 6 seconds\n");
    LedGlideBlocking(2000, 0, 0, 255);  // Glide to blue

    // If still holding after 20 seconds, auto-provision with default SSID/password instead
    while (CurrentlyTouched() && millis() - start < 20000);

    // Still touched after 20 seconds?
    if (CurrentlyTouched()) // Provision if still touched after 20 seconds
    {
      Serial.printf(" - still touched after 20 seconds: Provision!\n");
      Provision(wifimgr);
    }
    else // Gather credentials (touched > 6 but < 20 seconds)
    {
      Serial.printf(" - WiFi training mode\n");
      WiFi.scanNetworks(true);
      int networksFound;
      while ((networksFound = WiFi.scanComplete()) < 0)
        LedPulse(1000, 0, 0, 255);  // Blue pulsing
      wifimgr.GatherCredentials(WIFI_HOST_NAME, ACK_PAGE_TITLE, ACK_MESSAGE);
    }
    LedGlideBlocking(1000, brownr, browng, brownb); // glide back to brownish over 1 second
    return true;
  }
  return false;
}

/// @brief Decide what to do on startup.  Depends on whether start was triggered by timer or touch
/// @param wifimgr
/// @return void
void HandleStartup(MyWiFiManager &wifimgr)
{
  // Was it a touch wakeup?
  esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
  if (wakeup_reason == ESP_SLEEP_WAKEUP_TOUCHPAD || CurrentlyTouched())
    everTouched = true;
  
  // Quick green flash indicates a true restart (should be rare)
  if (wakeup_reason == ESP_SLEEP_WAKEUP_UNDEFINED)
  {
    LedGlideBlocking(500, 0, 255, 0);
    LedGlideBlocking(500, 0, 0, 0);
    Serial.printf("Welcome to the Wedding Puck\n");
    Serial.printf(" - Initial startup\n");
  }

  // Get the system clock.  What time did we wake up?
  time_t now = time(NULL);
  struct tm tmWakeup;
  localtime_r(&now, &tmWakeup);

  // Handle startup that happens from touch
  bool specialTouchHandled = HandleSpecialTouchStartup(wifimgr);

  if (!specialTouchHandled)
  {
    // Every year on the anniversary, do an all-day celebration!
#if defined(WEDDING_DAY) && defined(WEDDING_MONTH) && defined(WEDDING_YEAR)
    if (tmWakeup.tm_mday == WEDDING_DAY && tmWakeup.tm_mon + 1 == WEDDING_MONTH && tmWakeup.tm_year + 1900 > WEDDING_YEAR)
      AnniversaryFireworks();
#endif
    // Try to connect to the configured WiFi
    if (wifimgr.Connect())
    {
      // Test if we need to resync our clock with NTP servers (at initial startup or every 24 hours)
      bool waitForNTPCompletion = InitiatedNTPSync(tmWakeup);

      TryConnectMQTT(wifimgr, wakeup_reason);

      // Try to initiate a glow?
      Serial.printf("Try to initiate glow? %s\n", EverTouched() ? "Yes" : "No");
      bool initiateGlow = EverTouched() && TryInitiateGlow(wifimgr);
      Serial.printf(" - Initiated glow? %s\n", initiateGlow ? "Yes!" : EverTouched() ? "No, not permitted" : "No, not applicable");

      // Check for new subscriber messages to possibly set inboundGlow from inbound message
      Serial.printf("Checking for MQTT subscriber messages...\n");
      for (unsigned long start=millis(); millis() - start < 2000; )
        mqttClient.loop();

      // Check to see if we should do a glow
      Serial.printf("Need to perform glow? %s\n", inboundGlow || initiateGlow ? "Yes" : "No");
      if (inboundGlow || initiateGlow)
      {
        inboundGlow = false;
        LedGlideBlocking(1000, 255, 255, 255);
        WeddingFireworks(FIREWORKS_LENGTH_SECONDS);
      }

      // If we need to wait for the NTP process to complete, do it here
      if (waitForNTPCompletion)
      {
        if (!ntpComplete)
          Serial.printf("Waiting for NTP sync...\n");
        for (unsigned long start = millis(); !ntpComplete && millis() - start < 30000; )
          ;
      }
    }

    else // if Connect failed, it's usually because there is no good WiFi SSID/password
    {
      if (EverTouched())  // signal this with a short purple "error" glow
        LedErrorPulse(2000, 255, 0, 255);
    }
  }

  if (AnyLedsOn())
    LedGlideBlocking(2000, 0, 0, 0);        // fade back to black
    

  // Step 2: Check for fancy SSID commands
  int nNetworksAvailable = WiFi.scanComplete();
  int cmd = GetSSIDCommand(nNetworksAvailable);

  switch (cmd)
  {
    case CMD_NONE:
    default:
      Serial.printf(" - No SSID command issued\n");
      break;

    case CMD_WEDDING_FIREWORKS:
      // Do wedding fireworks for 1 minute repeatedly until SSID no longer available
      LedGlideBlocking(1000, 255, 255, 255); 
      do 
      { 
        WeddingFireworks(60);
        nNetworksAvailable = WiFi.scanNetworks();
      } while (GetSSIDCommand(nNetworksAvailable) == CMD_WEDDING_FIREWORKS);
      Serial.printf(" - Wedding fireworks complete\n");
      LedGlideBlocking(2000, 0, 0, 0); // Glide back to black
      break;

    case CMD_DEPROVISION:
      Serial.printf(" - Provisioning\n");
      wifimgr.EraseCredentials();
      break;

    case CMD_PROVISION:
      Serial.printf(" - Provisioning\n");
      Provision(wifimgr);
      LedGlideBlocking(2000, 0, 0, 0); // Glide back to black
      break;
  }

  Serial.printf("The time is %d-%d-%04d %02d:%02d:%02d\n", 
    tmWakeup.tm_mday, tmWakeup.tm_mon + 1, tmWakeup.tm_year + 1900, tmWakeup.tm_hour, tmWakeup.tm_min, tmWakeup.tm_sec);
}

void MqttCallback(char *topic, byte *payload, unsigned int length) 
{
  String payloadStr = "";
  for (int i = 0; i < length; i++)
    payloadStr += (char)payload[i];

  Serial.printf("MQTT Message arrived [%s]\n - %s [NEW]\n", topic, payloadStr.c_str());
  String lastInboundGlowMessage = WiFiManager::ReadFile("/lastinbound");
  Serial.printf(" - %s [LAST]\n", lastInboundGlowMessage.c_str());
  
  // Is it a new message, i.e., should we glow?
  if (String(topic) == "/devices/glow" && lastInboundGlowMessage != payloadStr)
  {
      inboundGlow = true;
      WiFiManager::WriteFile("/lastinbound", payloadStr.c_str());
  }
}

/// @brief Scan the available WiFi SSIDs and see if any is a secret command
/// @param nNetworksAvailable - if you have already done a WiFi Scan, send a non-zero value here for number of networks found
/// @return CMD_NONE (0) if no secret command found, else the ID of the command

int GetSSIDCommand(int nNetworksAvailable)
{
  if (nNetworksAvailable == 0)
    nNetworksAvailable = WiFi.scanNetworks();

  Serial.printf("Looking for WiFi SSID commands\n");
  Serial.printf(" - %lu: %d WiFi networks detected.\n", millis(), nNetworksAvailable);
  struct { const char *name; int cmd;} table[] = 
  {
      FIREWORKS_SSID, CMD_WEDDING_FIREWORKS,
      DEPROVISION_SSID, CMD_DEPROVISION,
      PROVISION_SSID, CMD_PROVISION,
  };

  for (int i=0; i<nNetworksAvailable; ++i)
    for (int j=0; j<sizeof(table) / sizeof(table[0]); ++j)
      if (WiFi.SSID(i) == table[j].name)
      {
        Serial.printf(" - Command found: %s (%d)\n", table[j].name, table[j].cmd);
        return table[j].cmd;
      }
  return CMD_NONE;
}

void WeddingFireworks(int seconds)
{
  // from https://www.springtree.net/audio-visual-blog/rgb-led-color-mixing/
  static const uint8_t nicecolors[][3] = 
  {
    { 255, 255, 255 }, // white
    { 255, 150, 64 }, // orange
    { 80, 210, 255 }, // light blue
    { 255, 64, 255 }, // magenta
    { 64, 255, 96 }, // blue green
    { 255, 255, 64 }, // yellow
    { 230, 128, 128 }, // light pink
    { 128, 255, 64 },    // yellow green
  };
  int ncolors = sizeof(nicecolors) / sizeof(nicecolors[0]);
  int cur = 0;
  Serial.printf("Wedding Fireworks!\n");
  digitalWrite(LEDPIN, HIGH);
  for (unsigned long start = millis(); millis() - start < 1000UL * seconds; )
  {
    uint8_t r = nicecolors[cur][0];
    uint8_t g = nicecolors[cur][1];
    uint8_t b = nicecolors[cur][2];
    LedGlideBlocking(3000, r, g, b);
    cur = (cur + 1) % ncolors;
  }
  digitalWrite(LEDPIN, LOW);
  LedGlideBlocking(2000, 255, 255, 255);
}

void AnniversaryFireworks()
{
  static const uint8_t nicecolors[][3] = 
  {
    { 255, 255, 255 }, // white
    { 255, 150, 64 }, // orange
    { 80, 210, 255 }, // light blue
    { 255, 64, 255 }, // magenta
    { 64, 255, 96 }, // blue green
    { 255, 255, 64 }, // yellow
    { 230, 128, 128 }, // light pink
    { 128, 255, 64 },    // yellow green
  };
  int r[NUMPIXELS], g[NUMPIXELS], b[NUMPIXELS];
  int tabsize = sizeof(nicecolors) / sizeof(nicecolors[0]);
  int minutes = 24 * 60;
  Serial.printf(" - Anniversary Fireworks!\n");

  // Given current color, next color, and time, calculate all the pixels.
  unsigned long prev = millis();
  unsigned long now = millis();
  for (unsigned long start = now; now - start < 1000UL * 60UL * minutes; prev = now, now = millis())
  {
    // initialize
    int changerate = 2000;
    int index = (now / changerate) % tabsize;
    int index2 = (index + 1) % tabsize;
    int n = now % changerate;
    double f = (double)n / changerate;
    for (int i=0; i<NUMPIXELS; ++i)
    {
      r[i] = (1-f) * nicecolors[index][0] + f * nicecolors[index2][0];
      g[i] = (1-f) * nicecolors[index][1] + f * nicecolors[index2][1];
      b[i] = (1-f) * nicecolors[index][2] + f * nicecolors[index2][2];
    }

    // Pulse and rotate/sparkle
    int pulserate = 4000;
    n = now % pulserate;
    double factor = 2 * n < pulserate ? 2.0 * n / pulserate : 2.0 * (pulserate - n - 1) / pulserate;
    factor = 0.2 + factor * 0.8;

    int rotatespeed = 100;
    int pixoff = (now / rotatespeed) % NUMPIXELS;
    for (int i=0; i<NUMPIXELS; i++)
    {
      double factor2 = pixoff == i || pixoff == (i + NUMPIXELS / 2) % NUMPIXELS ? 0.7 * factor : factor;
      r[i] = (int)(factor2 * r[i]);
      g[i] = (int)(factor2 * g[i]);
      b[i] = (int)(factor2 * b[i]);
    }

    for (int i=0; i<NUMPIXELS; ++i)
      pixels.setPixelColor(i, pixels.Color(r[i], g[i], b[i]));
    pixels.show();
  }
  LedGlideBlocking(2000, 255, 255, 255);
}

void RenderVoltage(float v)
{
  uint32_t color = 
    v < 3.5 ? pixels.Color(255, 0, 0) :    // red
    v < 3.6 ? pixels.Color(255, 0xA5, 0) : // orange
    v < 3.7 ? pixels.Color(255, 255, 0) :  // yellow
    v < 3.8 ? pixels.Color(255, 0, 255) :  // magenta
    v < 3.9 ? pixels.Color(0, 0, 255) :    // blue
    v < 4.0 ? pixels.Color(0, 255, 255) :  // cyan
    pixels.Color(0, 255, 0);               // green

    LedFlash(color, 250, 100);
}

double GetBatteryVoltage(int pin)
{
  uint32_t mV = readADC_Cal(analogRead(pin)) * 2 + readADC_Cal(analogRead(pin)) * 2 + readADC_Cal(analogRead(pin)) * 2;
  return (double)mV / 3000.0;
}

uint32_t readADC_Cal(int ADC_Raw)
{
    esp_adc_cal_characteristics_t adc_chars;

    esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_11, ADC_WIDTH_BIT_12, 1100, &adc_chars);
    return esp_adc_cal_raw_to_voltage(ADC_Raw, &adc_chars);
}

void DeepSleep(unsigned mSecs)
{
  digitalWrite(LEDPIN, LOW);
  uint64_t sleepUs = 1000 * (uint64_t)mSecs;
  Serial.printf("%lu: Sleeping for %lu milliseconds\r\n", millis(), mSecs);

#if false // This prevents sleep from happening properly!  Don't do it!
  Serial.flush();
  Serial.end();
#endif

  for (int i=0; i<NUMPIXELS; ++i)
    pixels.setPixelColor(i, pixels.Color(0, 0, 0));
  pixels.show();
  pinMode(NEOPIXELPIN, INPUT);

  WiFi.disconnect();
  WiFi.mode(WIFI_OFF);

  esp_sleep_enable_timer_wakeup(sleepUs);
  touchSleepWakeUpEnable(T5, TOUCH_THRESHOLD);
  esp_deep_sleep_start();
}

bool CurrentlyTouched()
{
  bool touched = touchRead(TOUCHPIN) >= TOUCH_THRESHOLD;
  if (touched)
    everTouched = true;
  return touched;
}

bool EverTouched() { return CurrentlyTouched() || everTouched; }

// Glow logic
bool TryInitiateGlow(MyWiFiManager &wifimgr)
{
  time_t now = time(NULL);
  String lastInitiationTimeString = WiFiManager::ReadFile("/lastoutbound");
  time_t lastInitiationTime = lastInitiationTimeString.toInt();
  Serial.printf(" - Last Init was %ld, current time is %ld\n", lastInitiationTime, now);
  if (now - lastInitiationTime >= SECONDS_BETWEEN_GLOWS)
  {
    WiFiManager::WriteFile("/lastoutbound", String(now).c_str());
    mqttClient.publish(mqttGlowTopic, ConstructMQTTMessage(wifimgr).c_str(), true);
    return true;
  }

  LedErrorPulse(2000, 255, 0, 0);         // A red error indicator
  return false;
}

String ConstructMQTTMessage(WiFiManager &wifimgr)
{
  return String("{ \"MAC\": \"") + WiFi.macAddress() + "\", \"owner\": \"" + wifimgr.GetOwner() + "\", \"timestamp\": \"" + time(NULL) + "\"}";
}

bool TryConnectMQTT(WiFiManager &wifimgr, esp_sleep_wakeup_cause_t wakeup_reason)
{
  wifiClient.setInsecure();
  mqttClient.setServer(mqttServer, 8883);
  mqttClient.setCallback(MqttCallback);

  for (unsigned long start = millis(); millis() - start < 30000 && !mqttClient.connected(); )
  {
    // Create a random client ID
    String clientId = "ESP32Client-";
    clientId += WiFi.macAddress();

    // Attempt to connect
    bool cleanSession = wakeup_reason == ESP_SLEEP_WAKEUP_UNDEFINED;
    Serial.printf("Attempting MQTT connection (%s session) with device %s...", cleanSession ? "clean" : "dirty", clientId.c_str());
    if (mqttClient.connect(clientId.c_str(), mqttUser, mqttPass)) 
    {
      Serial.printf("connected!\n");

      // First time connected, publish an announcement...
      if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_UNDEFINED)
      {
        Serial.printf("Publishing to topic %s\n", mqttAnnounceTopic);
        mqttClient.publish(mqttAnnounceTopic, ConstructMQTTMessage(wifimgr).c_str(), true);
      }

      // ... and resubscribe
      Serial.printf(" - subscribing to topic %s\n", mqttGlowTopic);
      mqttClient.subscribe(mqttGlowTopic, 1);
    } 
    else 
    {
      Serial.printf("failed, rc=%d, try again in 5 seconds\n", mqttClient.state());

      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
  return mqttClient.connected();
}

// NTP Time-related functions
void OnNTPTimeAvailable(struct timeval *t)
{
    struct tm tmLocal;
    if (!getLocalTime(&tmLocal))
      log_e("What? No time available?\n");
    else
      Serial.printf("Got time adjustment from NTP: %d-%d-%04d %02d:%02d:%02d\n", 
        tmLocal.tm_mday, tmLocal.tm_mon + 1, tmLocal.tm_year + 1900, tmLocal.tm_hour, tmLocal.tm_min, tmLocal.tm_sec);
    ntpComplete = true;
}

bool InitiatedNTPSync(const struct tm &tm)
{
  // Every night at midnight (or at first startup) do NTP sync
  if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_UNDEFINED ||
    (tm.tm_hour == 0 && (tm.tm_min == 0 || tm.tm_min == 1)) || 
    tm.tm_year + 1900 <= 1970)
  {
    static const char ntpServer1[] = "pool.ntp.org";
    static const char ntpServer2[] = "time.nist.gov";

    // set notification call-back function
    sntp_set_time_sync_notification_cb(OnNTPTimeAvailable);

    /**
     * NTP server address could be acquired via DHCP,
     *
     * NOTE: This call should be made BEFORE esp32 aquires IP address via DHCP,
     * otherwise SNTP option 42 would be rejected by default.
     * NOTE: configTime() function call if made AFTER DHCP-client run
     * will OVERRIDE aquired NTP server address
     */
    sntp_servermode_dhcp(1);    // (optional)

    configTime(0, 0, ntpServer1, ntpServer2);
    return true;
  }
  return false;
}

// Commands from "Special" SSIDs
void Provision(MyWiFiManager &wifimgr)
{
  LedGlideBlocking(2000, 0, 255, 0);  // Glide to green
#if defined(DEFAULT_WIFI_SSID) && defined(DEFAULT_WIFI_PASS) && defined(DEFAULT_WIFI_OWNER)
  wifimgr.ProvisionCredentials(DEFAULT_WIFI_SSID, DEFAULT_WIFI_PASS, DEFAULT_WIFI_OWNER);
#endif
  delay(2000);
}

// LED manipulation
void LedErrorPulse(int duration, int r, int g, int b)
{
  int speed = 1000;
  LedGlideBlocking(500, r, g, b);     // glide to that color
  for (unsigned long start = millis(); millis() - start < duration; )
  {
    LedGlideBlocking(speed / 2, r / 2, g / 2, b / 2);
    LedGlideBlocking(speed / 2, r, g, b);
  }
}

bool AnyLedsOn()
{
  for (int i=0; i<NUMPIXELS; ++i)
    if (pixels.getPixelColor(i) != 0)
      return true;
  return false;
}

void LedFlash(uint32_t color, unsigned long duration, unsigned long after)
{
  int r = (color >> 16) & 0xFF;
  int g = (color >> 8) & 0xFF;
  int b = (color >> 0) & 0xFF;
  LedGlideBlocking(100, r, g, b);
  if (duration > 200)
    delay(duration - 200);
  LedGlideBlocking(100, 0, 0, 0);
  delay(after);
}

// An instantaneous function
void LedRotate(unsigned long speed, int r, int g, int b)
{
  unsigned long t = millis() % speed;
  int whichpixel = t * NUMPIXELS / speed;
  int whichpixel2 = (whichpixel + 4) % NUMPIXELS;
  for (int i=0; i<NUMPIXELS; ++i)
    pixels.setPixelColor(i, i == whichpixel || i == whichpixel2 ? pixels.Color(r, g, b) : pixels.Color(r / 2, g / 2, b / 2));
  pixels.show();
}

// Another instantaneous function
void LedPulse(unsigned long speed, int r, int g, int b)
{
  double t = (millis() % speed) * 2 * PI / speed;
  double factor = 0.75 + sin(t) / 4; // between 0.5 and 1.0
  for (int i=0; i<NUMPIXELS; ++i)
    pixels.setPixelColor(i, pixels.Color((int)(factor * r), (int)(factor * g), (int)(factor * b)));
  pixels.show();
}

void LedRotateBlocking(unsigned long duration, unsigned long speed, int r, int g, int b)
{
    for (unsigned long start = millis(); millis() - start < duration; )
      LedRotate(speed, r, g, b);
}

// Blocking function lasts 'duration' ms, fades from current color to specified one.
void LedGlideBlocking(unsigned long duration, int r1, int g1, int b1)
{
  int r0[NUMPIXELS], g0[NUMPIXELS], b0[NUMPIXELS];
  int rv[NUMPIXELS], gv[NUMPIXELS], bv[NUMPIXELS];

  for (int i=0; i<NUMPIXELS; ++i)
  {
    uint32_t color = pixels.getPixelColor(i);
    r0[i] = (color >> 16) & 0xFF;
    rv[i] = r1 - r0[i] + (r1 < r0[i] ? -1 : 1);
    g0[i] = (color >> 8) & 0xFF;
    gv[i] = g1 - g0[i] + (g1 < g0[i] ? -1 : 1);
    b0[i] = (color >> 0) & 0xFF;
    bv[i] = b1 - b0[i] + (b1 < b0[i] ? -1 : 1);
  }
  Serial.printf(" - Transition from %d %d %d to %d %d %d\n", r0[0], g0[0], b0[0], r1, g1, b1);

  unsigned long now = millis();
  for (unsigned long start = now; now - start < duration; )
  {
    unsigned long diff = now - start;
    for (int i=0; i<NUMPIXELS; ++i)
    {
      int r = (int)(r0[i] + rv[i] * (long)diff / (long)duration);
      int g = (int)(g0[i] + gv[i] * (long)diff / (long)duration);
      int b = (int)(b0[i] + bv[i] * (long)diff / (long)duration);
      pixels.setPixelColor(i, pixels.Color(r, g, b));
    }
    pixels.show();
    now = millis();
  }

  // Make sure pixels always show final colour
  for (int i=0; i<NUMPIXELS; ++i)
    pixels.setPixelColor(i, pixels.Color(r1, g1, b1));
  pixels.show();
}

bool MyWiFiManager::OnWaitForSSIDConnect() const
{
  LedPulse(1000, 0, 0, 255);  // Blue pulsing
  return true; 
}

bool MyWiFiManager::OnWaitForURLConnect() const
{
  LedPulse(1000, 0, 0, 255);  // Blue pulsing
  return true; 
}

void MyWiFiManager::OnConnecting() const
{
  static bool firstTime = true;
  if (EverTouched())
  {
    if (firstTime && esp_sleep_get_wakeup_cause() != ESP_SLEEP_WAKEUP_TOUCHPAD)
    {
      LedGlideBlocking(1000, brownr, browng, brownb); // glide to brownish over 1 second
      firstTime = false;
    }
    else
    {
      LedRotate(2000, brownr, browng, brownb);
    }
  }
}
