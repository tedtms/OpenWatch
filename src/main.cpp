#include <GxEPD.h>
#include <GxDEPG0150BN/GxDEPG0150BN.h>    // 1.54" b/w 200x200
// #include <GxGDEH0154Z90/GxGDEH0154Z90.h>  // 1.54" b/w/r 200x200
#include <GxIO/GxIO_SPI/GxIO_SPI.h>
#include <GxIO/GxIO.h>
#include "IMG.h"
#include <WiFi.h>
#include "time.h"
#include "HTTPClient.h"
#include <ArduinoJson.h>
#include <AceButton.h>
#include <Adafruit_I2CDevice.h>
#include <ESP32Time.h>
#include <soc/soc.h>
#include <soc/rtc_cntl_reg.h>

#include "secrets.h" //be sure to create this file within the src/ directory and populate the following lines:
//const char *ssid = "";      //WiFi SSID;
//const char *password = "";  //WiFi SSID passkey;
//const char *host = "";      //Weather API host
//const char *apiKey = "";    //Weather API key 
//const char *apiId = "";     //Weather API user ID
//const char *station = "";   //Weather API station ID

#define PIN_MOTOR 4
#define PIN_KEY 35
#define PWR_EN 5
#define Backlight 33
#define Bat_ADC 34

#define SPI_SCK 14
#define SPI_DIN 13
#define EPD_CS 15
#define EPD_DC 2
#define SRAM_CS -1
#define EPD_RESET 17
#define EPD_BUSY 16

using namespace ace_button;

GxIO_Class io(SPI, /*CS*/ EPD_CS, /*DC=*/EPD_DC, /*RST=*/EPD_RESET);
GxEPD_Class display(io, /*RST=*/EPD_RESET, /*BUSY=*/EPD_BUSY);
struct tm timeinfo;

const char *ntpServer1 = "time.nist.gov";
const char *ntpServer2 = "pool.ntp.org";
const long gmtOffset_sec = -5 * 60 * 60;
const int daylightOffset_sec = 3600;


HTTPClient http_client;
String req;
String rsp;
StaticJsonDocument<5000> doc;
AceButton button(PIN_KEY);
ESP32Time rtc;

bool SleepFlag = false;
bool WIFIUpdataFlag = false;
bool GPSUpdataFlag = false;
bool DisplayFullUpdata = true;
uint8_t RealTime = 0, LastTime = 0;
float Lat = 0, Long = 0, Speed = 0;
uint8_t Sat = 0;

void Task1code(void *pvParameters);
void Task2code(void *pvParameters);
void Task3code(void *pvParameters);
// The event handler for the button.
void handleEvent(AceButton * /* button */, uint8_t eventType,
                 uint8_t buttonState)
{

  // Print out a message for all events.
  Serial.print(F("handleEvent(): eventType: "));
  Serial.print(eventType);
  Serial.print(F("; buttonState: "));
  Serial.println(buttonState);

  // Control the LED only for the Pressed and Released events.
  // Notice that if the MCU is rebooted while the button is pressed down, no
  // event is triggered and the LED remains off.
  switch (eventType)
  {
  case AceButton::kEventReleased:
    break;
  case AceButton::kEventPressed:
    break;
  case AceButton::kEventClicked:
    break;
  case AceButton::kEventDoubleClicked:
    WIFIUpdataFlag = 1;
    break;
  case AceButton::kEventLongPressed:
    SleepFlag = 1;
    break;
  default:
    break;
  }
}

void printLocalTime(uint16_t x, uint16_t y)
{
  //display.fillRect(x, y, 200, 70, GxEPD_WHITE); //clean
  display.setTextColor(GxEPD_BLACK);
  display.setTextSize(2);
  display.setTextWrap(false);
  display.setCursor(x, y);
  display.print(&timeinfo, "%a, %b%d, %Y");
  display.setCursor(x, y + 20);
  display.setTextSize(7);
#ifdef _GxGDEH0154Z90_H_
  display.setTextColor(GxEPD_RED);
#endif
  display.printf("%02d", rtc.getHour());
  display.setCursor(x + 80, y + 20);
  display.print(":");
  display.setCursor(x + 120, y + 20);
  display.printf("%02d", rtc.getMinute());

  //output time to serial
  //Serial.println(&timeinfo, "%A, %B %d %Y %H:%M:%S");
}

void printPower(uint16_t x, uint16_t y)
{
  pinMode(Bat_ADC, ANALOG);
  adcAttachPin(Bat_ADC);
  analogReadResolution(12);
  analogSetWidth(50);
  //display.fillRect(x - 32, y, 28, 12, GxEPD_WHITE); //clean
  display.drawRect(x, y, 28, 12, GxEPD_BLACK);
  display.drawRect(x + 28, y + 3, 3, 6, GxEPD_BLACK);
  int bat = 0;
  for (uint8_t i = 0; i < 20; i++)
  {
    bat += analogRead(Bat_ADC);
  }
  bat /= 20;
  float volt = (bat * 3.3 / 4096) * 2;
  volt = bat > 2600 ? 1 : volt / 4.2;
  display.fillRect(x + 2, y + 2, 24 * volt, 8, GxEPD_BLACK);
  display.setTextColor(GxEPD_BLACK);
  display.setTextSize(1);
  display.setCursor(x - 26, y + 4);
  display.print((int)(volt * 100));
  display.print("%");
}

void printWeather(uint16_t x, uint16_t y)
{
  const char *name = doc["response"]["place"]["name"];
  const char *weather = doc["response"]["ob"]["weatherShort"];
  int temperature = doc["response"]["ob"]["tempF"];
  //display.fillRect(x, y, 200, 44, GxEPD_WHITE); //clean
  display.setTextColor(GxEPD_BLACK);
  display.setTextSize(2);
  display.setCursor(x, y);
  display.println(name);

  display.setCursor(x + 158, y + 10);
  display.print(temperature);
  display.println("F");

  display.setTextSize(2);
  display.setCursor(x, y + 20);
  display.println(weather);
  
  // output weather conditions to serial
 // Serial.println(name);
  //Serial.println(weather);
  //Serial.println(temperature);
}

void setUpHttpClient()
{
  req = (String)host + "/observations/";
  req += station;
  req += "?format=json&plimit=1&filter=1min";
  req += "&client_id=";
  req += apiId;
  req += "&client_secret=";
  req += apiKey;
  Serial.println(req);
  if (http_client.begin(req))
  {
    Serial.println("HTTPclient setUp done!");
  }
}

//Example: https://api.aerisapi.com/observations/48127?format=json&plimit=1&filter=1min&client_id=[CLIENT_ID]&client_secret=[CLIENT_SECRET]
//Original: https://api.seniverse.com/v3/weather/now.json?key=your_private_key&location=beijing&language=zh-Hans&unit=c

void WIFIUpdataTime(bool EN)
{
  if (EN == true)
  {
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED)
    {
      Serial.print(".");
      delay(500);
    }

    //Synchronize RTC time
    while (!getLocalTime(&timeinfo))
    {
      //init and get the time
      configTime(gmtOffset_sec, daylightOffset_sec, ntpServer2, ntpServer1);
      Serial.println("Failed to obtain time , try again");
    }
    Serial.println(&timeinfo, "%A, %B %d %Y %H:%M:%S");
    //rtc.setTime(timeinfo.tm_sec, timeinfo.tm_min, timeinfo.tm_hour, timeinfo.tm_mday, timeinfo.tm_mon, timeinfo.tm_year + 1900);
    //disabled the above line, unsure why but under stock settings it always pushes the time off by one hour. Is not fixed by updating time zone or DST offsets.

    setUpHttpClient();

    int http_code = http_client.GET();
    if (http_code == HTTP_CODE_OK)
    {
      rsp = http_client.getString();
      //Serial.println(rsp);
      DeserializationError error = deserializeJson(doc, rsp);
      // Test if parsing succeeds.
      if (error)
      {
        Serial.print(F("deserializeJson() failed: "));
        Serial.println(error.f_str());
        return;
      }
    }
    //disconnect WiFi as it's no longer needed
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
  }
}

void LogoDispaly(uint8_t x, uint8_t y)
{
  display.drawBitmap(x, y, tedtms, 200, 65, GxEPD_BLACK);
}

void setup()
{
  Serial.begin(115200);
  delay(10);
  SPI.begin(SPI_SCK, -1, SPI_DIN, EPD_CS);
  pinMode(PIN_MOTOR, OUTPUT);
  pinMode(PWR_EN, OUTPUT);
  digitalWrite(PWR_EN, HIGH);
  pinMode(PIN_KEY, INPUT_PULLUP);

  digitalWrite(PIN_MOTOR, HIGH);
  delay(100);
  digitalWrite(PIN_MOTOR, LOW);
  delay(50);
  digitalWrite(PIN_MOTOR, HIGH);
  delay(100);
  digitalWrite(PIN_MOTOR, LOW);

  Serial.println("Ted's EPaper Watch");
  display.init();
  display.setRotation(1);
  display.fillScreen(GxEPD_WHITE);
  display.drawBitmap(0, 0, opensource, 200, 200, GxEPD_BLACK);
  display.update();
  display.fillScreen(GxEPD_WHITE);
  WIFIUpdataTime(true);
  //display.eraseDisplay();

  // Configure the ButtonConfig with the event handler, and enable all higher
  // level events.
  ButtonConfig *buttonConfig = button.getButtonConfig();
  buttonConfig->setEventHandler(handleEvent);
  buttonConfig->setFeature(ButtonConfig::kFeatureClick);
  buttonConfig->setFeature(ButtonConfig::kFeatureDoubleClick);
  buttonConfig->setFeature(ButtonConfig::kFeatureLongPress);

  // Check if the button was pressed while booting
  if (button.isPressedRaw())
  {
    Serial.println(F("setup(): button was pressed while booting"));
  }
  Serial.println(F("setup(): ready"));

  xTaskCreate(Task1code, "Task1", 10000, NULL, 1, NULL);
  xTaskCreate(Task2code, "Task2", 10000, NULL, 2, NULL);
  xTaskCreate(Task3code, "Task3", 10000, NULL, 3, NULL);
}

void loop()
{
  //Sleep
  if (SleepFlag)
  {
    SleepFlag = false;
    display.setTextSize(2);
    display.setCursor(130, 184);
    display.print("Sleep");
    display.updateWindow(0, 0, GxEPD_WIDTH, GxEPD_HEIGHT, false);
    display.powerDown();
    while (digitalRead(PIN_KEY) == LOW)
      ;
    esp_sleep_enable_ext0_wakeup(GPIO_NUM_35, LOW); //1 = High, 0 = Low
    esp_deep_sleep_start();
  }
}

//Monitor for button presses
void Task1code(void *parameter)
{
  while (1)
  {
    button.check();
    vTaskDelay(10);
  }
  Serial.println("Ending task 1");
  vTaskDelete(NULL);
}

//Update time every 10ms
void Task2code(void *parameter)
{
  while (1)
  {
    getLocalTime(&timeinfo, 10);
    RealTime = timeinfo.tm_min;
    //if (LastTime != RealTime)
    {
      LastTime = RealTime;
      display.fillScreen(GxEPD_WHITE);
      WIFIUpdataTime(WIFIUpdataFlag);
      printWeather(0, 15);
      printPower(165, 5);
      printLocalTime(0, 65);
      //GPSUpdataTime(0, 135, GPSUpdataFlag);
      LogoDispaly(0, 135);
      if (DisplayFullUpdata == false)
      {
        display.updateWindow(0, 0, GxEPD_WIDTH, GxEPD_HEIGHT, false);
      }
      else
      {
        display.update();
        DisplayFullUpdata = false;
      }
      //display.powerDown();
    }
    vTaskDelay(10);
  }
  Serial.println("Ending task 2");
  vTaskDelete(NULL);
}

//Update weather and time via NTP every 15 minutes
void Task3code(void *parameter)
{
  while (1)
  {
    {
      WIFIUpdataTime(true);
    }
    vTaskDelay(900000);
  }
  Serial.println("Ending task 3");
  vTaskDelete(NULL);
}