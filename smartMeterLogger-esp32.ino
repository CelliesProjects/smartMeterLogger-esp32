//#define SH1106_OLED              /* uncomment to compile for SH1106 instead of SSD1306 */

#include <SD.h>
#include <FS.h>
#include <driver/uart.h>
#include <AsyncTCP.h>              /* https://github.com/me-no-dev/AsyncTCP */
#include <ESPAsyncWebServer.h>     /* https://github.com/me-no-dev/ESPAsyncWebServer */
#include <WebSocketsClient.h>      /* https://github.com/Links2004/arduinoWebSockets */
#include <dsmr.h>                  /* https://github.com/matthijskooijman/arduino-dsmr */



#define USE_WS_BRIDGE              true                      /* true = use a dsmr websocket bridge - false = use a dsmr smartmeter */

const char*    WS_BRIDGE_HOST =    "192.168.0.177";          /* bridge adress */
const uint16_t WS_BRIDGE_PORT =    80;                       /* bridge port */
const char*    WS_BRIDGE_URL =     "/raw";                   /* bridge url */

#include "wifisetup.h"
#include "index_htm.h"

#if defined(SH1106_OLED)
#include <SH1106.h>                /* Install via 'Manage Libraries' in Arduino IDE -> https://github.com/ThingPulse/esp8266-oled-ssd1306 */
#else
#include <SSD1306.h>               /* In same library as SH1106 */
#endif

#define  SAVE_TIME_MIN                 (1)              /* data is saved every(currentMinute % SAVE_TIME_MIN) */

/* settings for smartMeter */
#define RXD_PIN                        (26)
#define BAUDRATE                       (115200)
#define UART_NR                        (UART_NUM_2)

/* settings for a ssd1306/sh1106 oled screen */
#define OLED_ADDRESS                   (0x3C)
#define I2C_SDA_PIN                    (21)
#define I2C_SCL_PIN                    (22)

/* settings for ntp time sync */
const char* NTP_POOL =                  "nl.pool.ntp.org";
const char* TIMEZONE =                  "CET-1CEST,M3.5.0/2,M10.5.0/3"; /* Central European Time - see http://www.remotemonitoringsystems.ca/time-zone-abbreviations.php */

#define SET_STATIC_IP                   false            /* If SET_STATIC_IP is set to true then STATIC_IP, GATEWAY, SUBNET and PRIMARY_DNS have to be set to some sane values */

const IPAddress STATIC_IP(192, 168, 0, 90);              /* This should be outside your router dhcp range! */
const IPAddress GATEWAY(192, 168, 0, 1);                 /* Set to your gateway ip address */
const IPAddress SUBNET(255, 255, 255, 0);                /* Usually 255,255,255,0 check in your router or pc connected to the same network */
const IPAddress PRIMARY_DNS(192, 168, 0, 30);            /* Check in your router */
const IPAddress SECONDARY_DNS(192, 168, 0, 50);          /* Check in your router */

const char*     WS_RAW_URL = "/raw";
const char*     WS_CURRENT_URL = "/current";

WebSocketsClient ws_bridge;
AsyncWebServer  server(80);
AsyncWebSocket  ws_raw(WS_RAW_URL);
AsyncWebSocket  ws_current(WS_CURRENT_URL);
HardwareSerial  smartMeter(UART_NR);

#if defined(SH1106_OLED)
SH1106          oled(OLED_ADDRESS, I2C_SDA_PIN, I2C_SCL_PIN);
#else
SSD1306         oled(OLED_ADDRESS, I2C_SDA_PIN, I2C_SCL_PIN);
#endif

time_t          bootTime;
bool            oledFound{false};

const char* HEADER_MODIFIED_SINCE = "If-Modified-Since";

static inline __attribute__((always_inline)) bool htmlUnmodified(const AsyncWebServerRequest* request, const char* date) {
  return request->hasHeader(HEADER_MODIFIED_SINCE) && request->header(HEADER_MODIFIED_SINCE).equals(date);
}
void connectToWebSocketBridge() {
  ws_bridge.onEvent(ws_bridge_onEvents);
  ws_bridge.begin(WS_BRIDGE_HOST, WS_BRIDGE_PORT, WS_BRIDGE_URL);
  ws_bridge.enableHeartbeat(500, 300, 4);
}

void setup() {
  Serial.begin(115200);
  Serial.printf("\n\nsmartMeterLogger-esp32\n\nconnecting to %s...\n", WIFI_NETWORK);

  if (!SD.begin())
    Serial.println("SD card mount failed");

  /* check if oled display is present */
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  Wire.beginTransmission(OLED_ADDRESS);
  uint8_t error = Wire.endTransmission();
  if (error)
    Serial.println("no SSD1306/SH1106 oled found.");
  else {
    oledFound = true;
    oled.init();
    oled.flipScreenVertically();
    oled.setContrast(10, 5, 0);
    oled.setTextAlignment(TEXT_ALIGN_CENTER);
    oled.setFont(ArialMT_Plain_16);
    oled.drawString(oled.width() >> 1, 0, "Connecting..");
    oled.display();
  }

  /* try to set a static IP */
  if (SET_STATIC_IP && !WiFi.config(STATIC_IP, GATEWAY, SUBNET, PRIMARY_DNS, SECONDARY_DNS))
    Serial.println("Setting static IP failed");

  WiFi.begin(WIFI_NETWORK, WIFI_PASSWORD);
  WiFi.setSleep(false);

  while (!WiFi.isConnected())
    delay(10);
  Serial.printf("connected to '%s' as %s\n", WIFI_NETWORK, WiFi.localIP().toString().c_str());

  if (oledFound) {
    oled.clear();
    oled.drawString(oled.width() >> 1, 0, WiFi.localIP().toString());
    oled.display();
  }

  /* sync the clock with ntp */
  configTzTime(TIMEZONE, NTP_POOL);

  struct tm timeinfo {
    0
  };

  while (!getLocalTime(&timeinfo, 0))
    delay(10);

  time(&bootTime);

  /* websocket setup */
  ws_raw.onEvent(ws_server_onEvent);
  server.addHandler(&ws_raw);

  ws_current.onEvent(ws_server_onEvent);
  server.addHandler(&ws_current);

  /* webserver setup */
  static char modifiedDate[30];
  static const char* HTML_MIMETYPE{"text/html"};
  static const char* HEADER_LASTMODIFIED{"Last-Modified"};

  strftime(modifiedDate, sizeof(modifiedDate), "%a, %d %b %Y %X GMT", gmtime(&bootTime));

  server.on("/", HTTP_GET, [](AsyncWebServerRequest * request) {
    if (htmlUnmodified(request, modifiedDate)) return request->send(304);
    AsyncWebServerResponse *response = request->beginResponse_P(200, HTML_MIMETYPE, index_htm, index_htm_len);
    response->addHeader(HEADER_LASTMODIFIED, modifiedDate);
    request->send(response);
  });

  server.serveStatic("/", SD, "/");

  server.onNotFound([](AsyncWebServerRequest * request) {
    request->send(404);
  });
  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
  server.begin();

  if (USE_WS_BRIDGE)
    /* start listening on the websocket bridge */
    connectToWebSocketBridge();
  else {
    /* start listening on the smartmeter */
    smartMeter.begin(BAUDRATE, SERIAL_8N1, RXD_PIN);
    Serial.printf("listening on HardwareSerial(%i) with RXD_PIN=%i\n", UART_NR, RXD_PIN);
  }

  Serial.printf("saving average use every %i minutes\n", SAVE_TIME_MIN);
}

static uint32_t average{0};
static uint32_t numberOfSamples{0};

void saveAverage(const tm& timeinfo) {
  String path{'/' + String(timeinfo.tm_year + 1900)}; /* add the current year to the path */

  File folder = SD.open(path);
  if (!folder && !SD.mkdir(path))
    ESP_LOGE(TAG, "could not create folder %s", path);

  path.concat("/" + String(timeinfo.tm_mon + 1));     /* add the current month to the path */

  folder = SD.open(path);
  if (!folder && !SD.mkdir(path))
    ESP_LOGE(TAG, "could not create folder %s", path);

  path.concat("/" + String(timeinfo.tm_mday) + ".log");   /* add the filename to the path */

  /* write a start header to the current log file if we just booted */
  static bool booted{true};
  if (booted) {
    const String startHeader{"#" + String(bootTime)};

    ESP_LOGI(TAG, "writing start header '%s' to '%s'", startHeader.c_str(), path.c_str());

    appendToFile(path.c_str(), startHeader.c_str());
    booted = false;
  }

  const String message {
    String(time(NULL)) + " " + String(average / numberOfSamples)
  };

  ESP_LOGI(TAG, "%i samples - saving '%s' to file '%s'", numberOfSamples, message.c_str(), path.c_str());

  appendToFile(path.c_str(), message.c_str());

  average = 0;
  numberOfSamples = 0;
}

void loop() {
  ws_raw.cleanupClients();
  ws_current.cleanupClients();

  /* save the average power consumption to SD every 'SAVE_TIME_MIN' minutes */
  static struct tm now;
  getLocalTime(&now);
  if ((numberOfSamples > 1) && !(now.tm_min % SAVE_TIME_MIN) && (59 == now.tm_sec))
    saveAverage(now);

  if (USE_WS_BRIDGE) {
    ws_bridge.loop();
  }
  else {
    static String telegram{""};
    while (smartMeter.available()) {
      const char incomingChar = smartMeter.read();
      telegram.concat(incomingChar);
      if ('!' == incomingChar) {
        /* checksum reached, wait for and read 6 more bytes then the telegram is received completely - see DSMR 5.0.2 ¶ 6.2 */
        while (smartMeter.available() < 6)
          delay(1);
        while (smartMeter.available())
          telegram.concat((char)smartMeter.read());
        process(telegram);
        telegram = "";
      }
    }
  }
  delay(1);
}

char currentUseString[200];

void ws_server_onEvent(AsyncWebSocket* server, AsyncWebSocketClient* client, AwsEventType type, void * arg, uint8_t *data, size_t len) {
  switch (type) {

    case WS_EVT_CONNECT :
      ESP_LOGD(TAG, "[%s][%u] connect", server->url(), client->id());
      if (0 == strcmp(WS_CURRENT_URL, server->url()))
        client->text(currentUseString);
      break;

    case WS_EVT_DISCONNECT :
      ESP_LOGD(TAG, "[%s][%u] disconnect", server->url(), client->id());
      break;

    case WS_EVT_ERROR :
      ESP_LOGE(TAG, "[%s][%u] error(%u): %s", server->url(), client->id(), *((uint16_t*)arg), (char*)data);
      break;

    case WS_EVT_DATA : {
        AwsFrameInfo * info = (AwsFrameInfo*)arg;
        // here all data is contained in a single packet - and since we only connect and listen we do not check for multi-packet or multi-frame telegrams
        if (info->final && info->index == 0 && info->len == len) {
          if (info->opcode == WS_TEXT) {
            data[len] = 0;
            ESP_LOGD(TAG, "ws message from client %i: %s", client->id(), reinterpret_cast<char*>(data));
          }
        }
      }
      break;

    default : ESP_LOGE(TAG, "unhandled ws event type");
  }
}

void ws_bridge_onEvents(WStype_t type, uint8_t * payload, size_t length) {
  switch (type) {

    case WStype_CONNECTED :
      Serial.printf("connected to ws://%s:%i%s\n", WS_BRIDGE_HOST, WS_BRIDGE_PORT, WS_BRIDGE_URL);
      break;

    case WStype_DISCONNECTED :
      Serial.println("websocket bridge down - reconnecting");
      connectToWebSocketBridge();
      break;

    case WStype_TEXT :
      ESP_LOGD(TAG, "payload: %s", payload);
      payload[length] = 0;
      process(reinterpret_cast<char*>(payload));
      break;

    case WStype_ERROR :
      ESP_LOGE(TAG, "websocket bridge error");
      break;

    case WStype_PING :
      ESP_LOGD(TAG, "received ping");
      break;

    case WStype_PONG :
      ESP_LOGD(TAG, "received pong");
      break;

    default : ESP_LOGE(TAG, "unhandled websocket bridge event");
  }
}

bool appendToFile(const char * path, const char * message) {
  ESP_LOGD(TAG, "appending to file: %s", path);

  File file = SD.open(path, FILE_APPEND);
  if (!file) {
    ESP_LOGD(TAG, "failed to open %s for appending", path);
    return false;
  }
  if (!file.println(message)) {
    ESP_LOGD(TAG, "failed to write %s", path);
    return false;
  }

  file.close();
  return true;
}

void process(const String& telegram) {

  ws_raw.textAll(telegram);

  using decodedFields = ParsedData <
                        /* FixedValue */ energy_delivered_tariff1,
                        /* FixedValue */ energy_delivered_tariff2,
                        /* String */ electricity_tariff,
                        /* FixedValue */ power_delivered,
                        /* TimestampedFixedValue */ gas_delivered
                        >;
  decodedFields data;
  const ParseResult<void> res = P1Parser::parse(&data, telegram.c_str(), telegram.length());
  /*
    if (res.err)
      ESP_LOGE(TAG, "Error decoding telegram\n%s", res.fullError(telegram.c_str(), telegram.c_str() + telegram.length()).c_str());

    if (!data.all_present())
      ESP_LOGE(TAG, "Could not decode all fields");
  */

  if (!res.err && data.all_present()) {

    static struct {
      uint32_t t1Start;
      uint32_t t2Start;
      uint32_t gasStart;
    } today;

    /* out of range value to make sure the next check updates the first time */
    static uint8_t currentMonthDay{40};

    static struct tm timeinfo;
    getLocalTime(&timeinfo);

    /* check if we changed day and update starter values if so */
    if (currentMonthDay != timeinfo.tm_mday) {
      today.t1Start = data.energy_delivered_tariff1.int_val();
      today.t2Start = data.energy_delivered_tariff2.int_val();
      today.gasStart = data.gas_delivered.int_val();
      currentMonthDay = timeinfo.tm_mday;
    }

    average += data.power_delivered.int_val();
    numberOfSamples++;

    snprintf(currentUseString, sizeof(currentUseString), "%i\n%i\n%i\n%i\n%i\n%i\n%i\n%s",
             data.power_delivered.int_val(),
             data.energy_delivered_tariff1.int_val(),
             data.energy_delivered_tariff2.int_val(),
             data.gas_delivered.int_val(),
             data.energy_delivered_tariff1.int_val() - today.t1Start,
             data.energy_delivered_tariff2.int_val() - today.t2Start,
             data.gas_delivered.int_val() - today.gasStart,
             (data.electricity_tariff.equals("0001")) ? "laag" : "hoog"
            );

    ws_current.textAll(currentUseString);

    if (oledFound) {
      oled.clear();
      oled.setFont(ArialMT_Plain_16);
      oled.drawString(oled.width() >> 1, 0, WiFi.localIP().toString());
      oled.setFont(ArialMT_Plain_24);
      oled.drawString(oled.width() >> 1, 18, String(data.power_delivered.int_val()) + "W");
      oled.display();
    }
  }
}
