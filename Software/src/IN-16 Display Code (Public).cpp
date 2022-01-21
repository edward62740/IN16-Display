#include "Arduino.h"
#include <Wire.h>
#include "NTC_PCA9698.h"
#include <WiFi.h>
#include <NTPClient.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#define DEVICE "ESP32"
#include <InfluxDbClient.h>
#include <InfluxDbCloud.h>
#include <esp_wifi.h>
#include "num2disp.h"
#include <EEPROM.h>
#include <WS2812FX.h>

String processor(const String &var);
String outputState(int output);
void tubes(void *pvParameters);
void leds(void *pvParameters);
bool num2disp_gpio_write(uint8_t pin, bool data);

/* InfluxDB Connection Parameters */
#define WIFI_SSID "."
#define WIFI_PASSWORD "."
#define INFLUXDB_URL "."
#define INFLUXDB_TOKEN "."
#define INFLUXDB_ORG "."
#define INFLUXDB_BUCKET "."
#define TZ_INFO "."

// define the number of bytes you want to access
#define EEPROM_SIZE 1
String hostname = "IN-16 DISPLAY";
int intblk = 0;
#ifdef __cplusplus
extern "C"
{
#endif

  uint8_t temprature_sens_read();

#ifdef __cplusplus
}
#endif

TimerHandle_t cathodeProtectionTimer;
TimerHandle_t ifdbTimer;
bool run_protection = false;
bool post = false;
bool changed = false;
bool state = false;

String query = "from(bucket: \"MESH\") |> range(start: -10m) |> filter(fn: (r) => r._measurement == \"CO2\") |> filter(fn: (r) => r.UID == \"xxxx\") |> filter(fn: (r) => r._field == \"CO2\") |> last()";
/* Task handles */
static TaskHandle_t serp;
static TaskHandle_t disp;


uint32_t value = 0;
uint32_t prev_value = 123400;
unsigned long previousMillis = 0;
#define DISPLAY_UPDATE_RATE_MS 5000
#define CATHODE_PROTECTION_INTERVAL_MS 600000
#define CATHODE_PROTECTION_RUNTIME_MS 10000
WS2812FX ws2812fx = WS2812FX(4, 21, NEO_GRB + NEO_KHZ800); //(LED_COUNT,LED_PIN,NEO_GRB + NEO_KHZ800)
NumericalDisplay_t tube1;
uint8_t pinout1[10] = {39, 30, 31, 32, 33, 34, 35, 36, 37, 38};
NumericalDisplay_t tube2;
uint8_t pinout2[10] = {29, 20, 21, 22, 23, 24, 25, 26, 27, 28};
NumericalDisplay_t tube3;
uint8_t pinout3[10] = {19, 10, 11, 12, 13, 14, 15, 16, 17, 18};
NumericalDisplay_t tube4;
uint8_t pinout4[10] = {9, 0, 1, 2, 3, 4, 5, 6, 7, 8};
NumericalDisplay_t tube5;
NumericalDisplay_t tube6;

const char *PARAM_INPUT_1 = "output";
const char *PARAM_INPUT_2 = "state";

/* InfluxDB Data Points */
Point Display("IN-16 Display");

PCA9698 gp0(0x20, 19, 18, 100000); // instantiate PCA9698(I2C address, SDA, SCL, I2C speed)
InfluxDBClient client(INFLUXDB_URL, INFLUXDB_ORG, INFLUXDB_BUCKET, INFLUXDB_TOKEN, InfluxDbCloud2CACert);
AsyncWebServer server(80);

const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html>
<head>
  <title>CO2 Display Output</title>
  
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <link rel="icon" href="data:,">
  <style>
    html {font-family: Arial; display: inline-block; text-align: center;}
    h2 {font-size: 3.0rem;}
    p {font-size: 3.0rem;}
    body {max-width: 600px; margin:0px auto; padding-bottom: 25px;}
    .switch {position: relative; display: inline-block; width: 120px; height: 68px} 
    .switch input {display: none}
    .slider {position: absolute; top: 0; left: 0; right: 0; bottom: 0; background-color: #ccc; border-radius: 6px}
    .slider:before {position: absolute; content: ""; height: 52px; width: 52px; left: 8px; bottom: 8px; background-color: #2700ff; -webkit-transition: .4s; transition: .4s; border-radius: 3px}
    input:checked+.slider {background-color: #b30000}
    input:checked+.slider:before {-webkit-transform: translateX(52px); -ms-transform: translateX(52px); transform: translateX(52px)}
  </style>
</head>
<body>

  <h2>CO2 Display Output</h2>
     <h4>CO2 (ppm):  <span id="co2">%CO2%</span></h4>
     <h4>CPU Temperature:  <span id="cputemp">%temp%</span></h4>

  %BUTTONPLACEHOLDER%
  
<script>
setInterval(updateValues, 1000, "co2");
setInterval(updateValues, 1000, "cputemp");

function toggleCheckbox(element) {
  var xhr = new XMLHttpRequest();
  if(element.checked){ xhr.open("GET", "/update?output="+element.id+"&state=1", true); }
  else { xhr.open("GET", "/update?output="+element.id+"&state=0", true); }
  xhr.send();
}
function updateValues(value) {
  var xhttp = new XMLHttpRequest();
  xhttp.onreadystatechange = function() {
    if (this.readyState == 4 && this.status == 200) {
      document.getElementById(value).innerHTML = this.responseText;
    }
  };
  xhttp.open("GET", "/" + value, true);
  xhttp.send();
}
</script>
</body>
</html>
)rawliteral";

// Replaces placeholder with button section in your web page
String processor(const String &var)
{
  //Serial.println(var);
  if (var == "BUTTONPLACEHOLDER")
  {
    String buttons = "";
    buttons += "<h4> Sensor 1   <---->   Sensor 2 </h4><label class=\"switch\"><input type=\"checkbox\" onchange=\"toggleCheckbox(this)\" id=\"2\" " + outputState(2) + "><span class=\"slider\"></span></label>";
    return buttons;
  }
  return String();
}

String outputState(int output)
{
  if (state)
  {
    return "checked";
  }
  else
  {
    return "";
  }
}

void vTimerCallback1(TimerHandle_t cathodeProtectionTimer)
{
  run_protection = true;
}

void vTimerCallback2(TimerHandle_t ifdbTimer)
{
  post = true;
}
void setup()
{

  Serial.begin(115200);
  EEPROM.begin(EEPROM_SIZE);
  // put your setup code here, to run once:
  if (EEPROM.read(0) == 0)
  {
    state = false;
    query = "from(bucket: \"xxxx\") |> range(start: -30m) |> filter(fn: (r) => r._measurement == \"xxxx\") |> filter(fn: (r) => r.UID == \"xxxx\") |> filter(fn: (r) => r._field == \"xxxx\") |> last()";
  }
  else
  {
    state = true;
    query = "from(bucket: \"xxxx\") |> range(start: -30m) |> filter(fn: (r) => r._measurement == \"xxxx\") |> filter(fn: (r) => r.UID == \"xxxx\") |> filter(fn: (r) => r._field == \"xxxx\") |> last()";
  }
  num2disp_createInstanceNumericalDisplay(&tube1, pinout1);
  num2disp_createInstanceNumericalDisplay(&tube2, pinout2);
  num2disp_createInstanceNumericalDisplay(&tube3, pinout3);
  num2disp_createInstanceNumericalDisplay(&tube4, pinout4);
  num2disp_createInstanceFullDisplay(&tube1, &tube2, &tube3, &tube4, &tube5, &tube6, 4, 0);
  delay(100);
  Serial.println("[INIT] STARTUP INITIALIZATION OK");

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  WiFi.mode(WIFI_STA);
  WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE, INADDR_NONE);
  WiFi.setHostname(hostname.c_str()); //define hostname
  while (WiFi.status() != WL_CONNECTED)
  {
    Serial.println("[INIT] ERROR CONNECTING TO WIFI");
    WiFi.disconnect();
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    vTaskDelay(2000);
  }
  Serial.println("[INIT] WIFI CONNECTION OK");

  timeSync(TZ_INFO, "pool.ntp.org", "time.nis.gov");

  //client.setHTTPOptions(HTTPOptions().httpReadTimeout(200));
  client.setHTTPOptions(HTTPOptions().connectionReuse(true));
  // Check server connection
  if (client.validateConnection())
  {
    Serial.print("Connected to InfluxDB: ");
    Serial.println(client.getServerUrl());
  }
  else
  {
    Serial.print("InfluxDB connection failed: ");
    Serial.println(client.getLastErrorMessage());
  }
  Serial.println("[INIT] IFDB CONNECTION OK");
  ifdbTimer = xTimerCreate("Timer2", 120000, pdTRUE, (void *)0, vTimerCallback2);
  xTimerStart(ifdbTimer, 0);
  xTaskCreatePinnedToCore(
      tubes,   //Task Function
      "tubes", //Name of Task
      10000,   //Stack size of task
      NULL,    //Parameter of the task
      1,       //Priority of the task
      NULL,    //Task handle to keep track of the created task
      0);      //Target Core

  xTaskCreatePinnedToCore(
      leds,   //Task Function
      "leds", //Name of Task
      10000,  //Stack size of task
      NULL,   //Parameter of the task
      1,      //Priority of the task
      NULL,   //Task handle to keep track of the created task
      1);     //Target Core

  //Core 1 Config

  // Route for root / web page
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
            { request->send_P(200, "text/html", index_html, processor); });

  // Send a GET request to <ESP_IP>/update?output=<inputMessage1>&state=<inputMessage2>
  server.on("/update", HTTP_GET, [](AsyncWebServerRequest *request)
            {
              String inputMessage1;
              String inputMessage2;
              // GET input1 value on <ESP_IP>/update?output=<inputMessage1>&state=<inputMessage2>
              if (request->hasParam(PARAM_INPUT_1) && request->hasParam(PARAM_INPUT_2))
              {
                inputMessage1 = request->getParam(PARAM_INPUT_1)->value();
                inputMessage2 = request->getParam(PARAM_INPUT_2)->value();
                digitalWrite(inputMessage1.toInt(), inputMessage2.toInt());
                if (inputMessage1.toInt() == 2 && inputMessage2.toInt() == 0)
                {
                  query = "from(bucket: \"xxxx\") |> range(start: -30m) |> filter(fn: (r) => r._measurement == \"xxxx\") |> filter(fn: (r) => r.UID == \"xxxx\") |> filter(fn: (r) => r._field == \"xxxx\") |> last()";
                  changed = true;
                  state = false;
                  EEPROM.write(0, state);
                  EEPROM.commit();
                  Serial.print(EEPROM.read(0));
                }
                else if (inputMessage1.toInt() == 2 && inputMessage2.toInt() == 1)
                {
                  query = "from(bucket: \"xxxx\") |> range(start: -30m) |> filter(fn: (r) => r._measurement == \"xxxx\") |> filter(fn: (r) => r.UID == \"xxxx\") |> filter(fn: (r) => r._field == \"xxxx\") |> last()";
                  changed = true;
                  state = true;
                  EEPROM.write(0, state);
                  EEPROM.commit();
                  Serial.print(EEPROM.read(0));
                }
              }
              else
              {
                inputMessage1 = "No message sent";
                inputMessage2 = "No message sent";
              }
              request->send(200, "text/plain", "OK");
            });

  // Start server
  server.begin();
  vTaskDelete(NULL);
}

void tubes(void *pvParameters)
{
  Wire.begin(19, 18, 100000);
  gp0.configuration();     // soft reset and configuration
  gp0.portMode(0, OUTPUT); // set port directions
  gp0.portMode(1, OUTPUT);
  gp0.portMode(2, OUTPUT);
  gp0.portMode(3, OUTPUT);
  gp0.portMode(4, OUTPUT);
  for (int i = 0; i < 40; i++)
  {
    gp0.digitalWrite(i, LOW);
  }
  uint32_t x = 123400;
  num2disp_writeNumberToFullDisplay(x, x - 1, true);
  while (1)
  {
    //  Serial.print(WiFi.RSSI());

    unsigned long currentMillis = millis();

    if ((currentMillis - previousMillis >= 5000) || changed)
    {
      // save the last time you blinked the LED
      previousMillis = currentMillis;
      FluxQueryResult result = client.query(query);
      while (result.next())
      {
        Serial.println(EEPROM.read(0));
        double blocker = result.getValueByName("_value").getDouble();
        value = (uint32_t)blocker * 100;
        intblk = (int)blocker;
        server.on("/co2", HTTP_GET, [intblk](AsyncWebServerRequest *request)
                  { request->send(200, "text/plain", String(intblk)); });
        server.on("/cputemp", HTTP_GET, [intblk](AsyncWebServerRequest *request)
                  { request->send(200, "text/plain", String((temprature_sens_read() - 32) / 1.8)); });
        Serial.println(String(intblk));
        num2disp_writeNumberToFullDisplay(value, prev_value, true);
        prev_value = value;
      }
      Serial.print(result.getError());
      changed = false;
      if (result.getError() != "")
      {
        changed = true;
        Serial.print("ERROR");
        client.validateConnection();
      }
    }
    if (post)
    {
      num2disp_runCathodePoisoningProtection(50, CATHODE_PROTECTION_STYLE_SLOT);
      post = false;
      num2disp_clearNumberFromFullDisplay();
      num2disp_writeNumberToFullDisplay(value, prev_value, true);
    }
  }
}

void leds(void *pvParameters)
{
  ws2812fx.init();
  ws2812fx.setBrightness(255);
  ws2812fx.setSpeed(255);
  ws2812fx.setMode(FX_MODE_RAINBOW_CYCLE);
  ws2812fx.start();
  while (1)
  {
    delay(1);
    ws2812fx.service();
  }
}
void loop()
{
}

bool num2disp_gpio_write(uint8_t pin, bool data)
{
  gp0.digitalWrite(pin, data);
  return false;
}
