#include <SD.h>
#include <FS.h>
#include <driver/uart.h>
#include <AsyncTCP.h>              /* https://github.com/me-no-dev/AsyncTCP */
#include <ESPAsyncWebServer.h>     /* https://github.com/me-no-dev/ESPAsyncWebServer */
#include <WebSocketsClient.h>      /* https://github.com/Links2004/arduinoWebSockets */
#include <dsmr.h>                  /* https://github.com/matthijskooijman/arduino-dsmr */
#include <esp32-hal-log.h>

static const char* TAG = "smartMeterLogger-esp32";

#include "setup.h"
#include "index_htm_gz.h"
#include "dagelijks_htm_gz.h"

#if defined(SH1106_OLED)
#include <SH1106.h>                /* Install via 'Manage Libraries' in Arduino IDE -> https://github.com/ThingPulse/esp8266-oled-ssd1306 */
#else
#include <SSD1306.h>               /* In same library as SH1106 */
#endif

#define  SAVE_TIME_MIN (1)         /* data save interval in minutes */

const char*     WS_RAW_URL = "/raw";
const char*     WS_EVENTS_URL = "/events";

WebSocketsClient ws_bridge;
AsyncWebServer http_server(80);
AsyncWebSocket ws_server_raw(WS_RAW_URL);
AsyncWebSocket ws_server_events(WS_EVENTS_URL);
HardwareSerial smartMeter(UART_NR);

#if defined(SH1106_OLED)
SH1106 oled(OLED_ADDRESS, I2C_SDA_PIN, I2C_SCL_PIN);
#else
SSD1306 oled(OLED_ADDRESS, I2C_SDA_PIN, I2C_SCL_PIN);
#endif

struct {
    uint32_t low;
    uint32_t high;
    uint32_t gas;
} current;

time_t bootTime;
bool oledFound{false};

const char* HEADER_MODIFIED_SINCE = "If-Modified-Since";

static inline __attribute__((always_inline)) bool htmlUnmodified(const AsyncWebServerRequest* request, const char* date) {
    return request->hasHeader(HEADER_MODIFIED_SINCE) && request->header(HEADER_MODIFIED_SINCE).equals(date);
}

void connectToWebSocketBridge() {
    ws_bridge.onEvent(ws_bridge_onEvents);
    ws_bridge.begin(WS_BRIDGE_HOST, WS_BRIDGE_PORT, WS_BRIDGE_URL);
}

const char* CACHE_CONTROL_HEADER{"Cache-Control"};
const char* CACHE_CONTROL_NOCACHE{"no-store, max-age=0"};

void updateFileHandlers(const tm& now) {
    static char path[16];
    snprintf(path, sizeof(path), "/%i/%i/%i.log", now.tm_year + 1900, now.tm_mon + 1, now.tm_mday);

    ESP_LOGD(TAG, "Current logfile: %s", path);

    static AsyncCallbackWebHandler* currentLogFileHandler;
    http_server.removeHandler(currentLogFileHandler);
    currentLogFileHandler = &http_server.on(path, HTTP_GET, [] (AsyncWebServerRequest * const request) {
        if (!SD.exists(path)) return request->send(404);
        AsyncWebServerResponse* const response = request->beginResponse(SD, path);
        response->addHeader(CACHE_CONTROL_HEADER, CACHE_CONTROL_NOCACHE);
        request->send(response);
        ESP_LOGD(TAG, "Request for current logfile");
    });

    static AsyncStaticWebHandler* staticFilesHandler;
    http_server.removeHandler(staticFilesHandler);
    staticFilesHandler = &http_server.serveStatic("/", SD, "/").setCacheControl("public, max-age=604800, immutable");
}

void setup() {
    Serial.begin(115200);
    Serial.printf("\n\nsmartMeterLogger-esp32\n\nconnecting to %s...\n", WIFI_NETWORK);

    if (!SD.begin())
        Serial.println("SD card mount failed");

    /* check if oled display is present */
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
    Wire.beginTransmission(OLED_ADDRESS);
    const uint8_t error = Wire.endTransmission();
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
    WiFi.onEvent(WiFiEvent);
    Serial.printf("connected to '%s' as %s\n", WIFI_NETWORK, WiFi.localIP().toString().c_str());

    if (oledFound) {
        oled.clear();
        oled.drawString(oled.width() >> 1, 0, WiFi.localIP().toString());
        oled.drawString(oled.width() >> 1, 25, "Syncing NTP...");
        oled.display();
    }
    Serial.println("syncing NTP");

    /* sync the clock with ntp */
    configTzTime(TIMEZONE, NTP_POOL);

    tm now {
        0
    };

    while (!getLocalTime(&now, 0))
        delay(10);

    /* websocket setup */
    ws_server_raw.onEvent(ws_server_onEvent);
    http_server.addHandler(&ws_server_raw);

    ws_server_events.onEvent(ws_server_onEvent);
    http_server.addHandler(&ws_server_events);

    /* webserver setup */
    time(&bootTime);
    static char modifiedDate[30];
    strftime(modifiedDate, sizeof(modifiedDate), "%a, %d %b %Y %X GMT", gmtime(&bootTime));

    static const char* HTML_MIMETYPE{"text/html"};

    static const char* HEADER_LASTMODIFIED{"Last-Modified"};

    static const char* CONTENT_ENCODING_HEADER{"Content-Encoding"};
    static const char* CONTENT_ENCODING_GZIP{"gzip"};

    http_server.on("/robots.txt", HTTP_GET, [](AsyncWebServerRequest * const request) {
        request->send(200, HTML_MIMETYPE, "User-agent: *\nDisallow: /\n");
    });

    http_server.on("/", HTTP_GET, [](AsyncWebServerRequest * const request) {
        if (htmlUnmodified(request, modifiedDate)) return request->send(304);
        AsyncWebServerResponse* const response = request->beginResponse_P(200, HTML_MIMETYPE, index_htm_gz, index_htm_gz_len);
        response->addHeader(HEADER_LASTMODIFIED, modifiedDate);
        response->addHeader(CONTENT_ENCODING_HEADER, CONTENT_ENCODING_GZIP);
        request->send(response);
    });

    http_server.on("/daggrafiek", HTTP_GET, [](AsyncWebServerRequest * const request) {
        if (htmlUnmodified(request, modifiedDate)) return request->send(304);
        AsyncWebServerResponse* const response = request->beginResponse_P(200, HTML_MIMETYPE, dagelijks_htm_gz, dagelijks_htm_gz_len);
        response->addHeader(HEADER_LASTMODIFIED, modifiedDate);
        response->addHeader(CONTENT_ENCODING_HEADER, CONTENT_ENCODING_GZIP);
        request->send(response);
    });

    http_server.on("/jaren", HTTP_GET, [](AsyncWebServerRequest * const request) {
        File root = SD.open("/");
        // TODO: check that the folders are at least plausibly named for a /year thing
        if (!root || !root.isDirectory()) return request->send(503);
        File item = root.openNextFile();
        if (!item) return request->send(404);
        AsyncResponseStream* const response = request->beginResponseStream(HTML_MIMETYPE);
        while (item) {
            if (item.isDirectory())
                response->printf("%s\n", item.name());
            item = root.openNextFile();
        }
        response->addHeader(CACHE_CONTROL_HEADER, CACHE_CONTROL_NOCACHE);
        request->send(response);
    });

    http_server.on("/maanden", HTTP_GET, [](AsyncWebServerRequest * const request) {
        const char* year{"jaar"};
        if (!request->hasArg(year)) return request->send(400);
        if (!SD.exists(request->arg(year))) return request->send(404);
        // TODO: check that the folders are at least plausibly named for a /year/month thing
        File path = SD.open(request->arg(year));
        if (!path || !path.isDirectory()) return request->send(503);
        File item = path.openNextFile();
        if (!item) return request->send(404);
        AsyncResponseStream* const response = request->beginResponseStream(HTML_MIMETYPE);
        while (item) {
            if (item.isDirectory())
                response->printf("%s\n", item.name());
            item = path.openNextFile();
        }
        response->addHeader(CACHE_CONTROL_HEADER, CACHE_CONTROL_NOCACHE);
        request->send(response);
    });

    http_server.on("/dagen", HTTP_GET, [](AsyncWebServerRequest * const request) {
        const char* month{"maand"};
        if (!request->hasArg(month)) return request->send(400);
        if (!SD.exists(request->arg(month))) return request->send(404);
        // TODO: check that the file is at least plausibly named for a /year/month/day thing
        File path = SD.open(request->arg(month));
        if (!path || !path.isDirectory()) return request->send(503);
        File item = path.openNextFile();
        if (!item) return request->send(404);
        AsyncResponseStream* const response = request->beginResponseStream(HTML_MIMETYPE);
        while (item) {
            if (!item.isDirectory())
                response->printf("%s\n", item.name());
            item = path.openNextFile();
        }
        response->addHeader(CACHE_CONTROL_HEADER, CACHE_CONTROL_NOCACHE);
        request->send(response);
    });

    updateFileHandlers(now);

    http_server.onNotFound([](AsyncWebServerRequest * const request) {
        request->send(404);
    });

    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");

    http_server.begin();

    if (USE_WS_BRIDGE)
        connectToWebSocketBridge();
    else {
        smartMeter.begin(BAUDRATE, SERIAL_8N1, RXD_PIN);
        Serial.printf("listening for smartMeter RXD_PIN = %i baudrate = %i\n", RXD_PIN, BAUDRATE);
    }

    Serial.printf("saving average use every %i minutes\n", SAVE_TIME_MIN);
}

static uint32_t average{0};
static uint32_t numberOfSamples{0};

void saveAverage(const tm& timeinfo) {
    const String message {
        String(time(NULL)) + " " + String(average / numberOfSamples)
    };

    ws_server_events.textAll("electric_saved\n" + message);

    String path{'/' + String(timeinfo.tm_year + 1900)}; /* add the current year to the path */

    File folder = SD.open(path);
    if (!folder && !SD.mkdir(path)) {
        ESP_LOGE(TAG, "could not create folder %s", path);
    }

    path.concat("/" + String(timeinfo.tm_mon + 1));     /* add the current month to the path */

    folder = SD.open(path);
    if (!folder && !SD.mkdir(path)) {
        ESP_LOGE(TAG, "could not create folder %s", path);
    }

    path.concat("/" + String(timeinfo.tm_mday) + ".log");   /* add the filename to the path */

    static bool booted{true};

    if (booted || !SD.exists(path)) {
        const String startHeader{
            "#" +
            String(bootTime) + " " +
            current.low + " " +
            current.high + " " +
            current.gas
        };

        ESP_LOGD(TAG, "writing start header '%s' to '%s'", startHeader.c_str(), path.c_str());

        appendToFile(path.c_str(), startHeader.c_str());
        booted = false;
    }

    ESP_LOGD(TAG, "%i samples - saving '%s' to file '%s'", numberOfSamples, message.c_str(), path.c_str());

    appendToFile(path.c_str(), message.c_str());

    average = 0;
    numberOfSamples = 0;
}

static unsigned long lastMessageMs = millis();

void loop() {
    ws_server_raw.cleanupClients();
    ws_server_events.cleanupClients();

    static tm now;
    getLocalTime(&now);
    if ((59 == now.tm_sec) && !(now.tm_min % SAVE_TIME_MIN) && (numberOfSamples > 2))
        saveAverage(now);

    static uint8_t currentMonthDay = now.tm_mday;
    if (currentMonthDay != now.tm_mday) {
        updateFileHandlers(now);
        currentMonthDay = now.tm_mday;
    }

    if (USE_WS_BRIDGE) {
        ws_bridge.loop();

        static const auto TIMEOUT_MS = 8000;

        if (ws_bridge.isConnected() && millis() - lastMessageMs > TIMEOUT_MS) {
            ESP_LOGW(TAG, "WebSocket bridge has received no data for %.2f seconds - reconnecting...", TIMEOUT_MS / 1000.0);
            ws_bridge.disconnect();
            lastMessageMs = millis();
        }
    }
    else {
        if (smartMeter.available()) {
            static const auto BUFFERSIZE = 1024;
            static char telegram[BUFFERSIZE];

            const unsigned long START_MS = millis();
            static const auto TIMEOUT_MS = 100;
            int size = 0;
            auto bytes = smartMeter.available();

            while (millis() - START_MS < TIMEOUT_MS && size + bytes < BUFFERSIZE) {
                size += bytes ? smartMeter.read(telegram + size, bytes) : 0;
                delay(5);
                bytes = smartMeter.available();
            }

            ESP_LOGD(TAG, "telegram received - %i bytes:\n%s", size, telegram);
            process(telegram, size);
        }
    }
    delay(1);
}

char currentUseString[200];

void ws_server_onEvent(AsyncWebSocket* server, AsyncWebSocketClient* client, AwsEventType type, void* arg, uint8_t* data, size_t len) {
    switch (type) {

        case WS_EVT_CONNECT :
            ESP_LOGD(TAG, "[%s][%u] connect", server->url(), client->id());
            if (0 == strcmp(WS_EVENTS_URL, server->url()))
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

void ws_bridge_onEvents(WStype_t type, uint8_t* payload, size_t length) {
    switch (type) {

        case WStype_CONNECTED :
            Serial.printf("connected to websocket bridge 'ws://%s:%i%s'\n", WS_BRIDGE_HOST, WS_BRIDGE_PORT, WS_BRIDGE_URL);
            lastMessageMs = millis();
            break;

        case WStype_DISCONNECTED :
            Serial.println("websocket bridge down - reconnecting");
            connectToWebSocketBridge();
            break;

        case WStype_TEXT :
            ESP_LOGD(TAG, "payload: %s", payload);
            process(reinterpret_cast<char*>(payload), length);
            lastMessageMs = millis();
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

bool appendToFile(const char* path, const char* message) {
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

void process(const char* telegram, const int size) {

    using decodedFields = ParsedData <
                          /* FixedValue */ energy_delivered_tariff1,
                          /* FixedValue */ energy_delivered_tariff2,
                          /* String */ electricity_tariff,
                          /* FixedValue */ power_delivered,
                          /* TimestampedFixedValue */ gas_delivered
                          >;
    decodedFields data;
    const ParseResult<void> res = P1Parser::parse(&data, telegram, size);
    /*
        if (res.err)
        ESP_LOGE(TAG, "Error decoding telegram\n%s", res.fullError(telegram, telegram + size);

        if (!data.all_present())
        ESP_LOGE(TAG, "Could not decode all fields");
    */
    if (res.err || !data.all_present())
        return;

    ws_server_raw.textAll(telegram);

    static struct {
        uint32_t t1Start;
        uint32_t t2Start;
        uint32_t gasStart;
    } today;

    current = {data.energy_delivered_tariff1.int_val(),
               data.energy_delivered_tariff2.int_val(),
               data.gas_delivered.int_val()
              };

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

    snprintf(currentUseString, sizeof(currentUseString), "current\n%i\n%i\n%i\n%i\n%i\n%i\n%i\n%s",
             data.power_delivered.int_val(),
             data.energy_delivered_tariff1.int_val(),
             data.energy_delivered_tariff2.int_val(),
             data.gas_delivered.int_val(),
             data.energy_delivered_tariff1.int_val() - today.t1Start,
             data.energy_delivered_tariff2.int_val() - today.t2Start,
             data.gas_delivered.int_val() - today.gasStart,
             (data.electricity_tariff.equals("0001")) ? "laag" : "hoog"
            );

    ws_server_events.textAll(currentUseString);

    if (oledFound) {
        oled.clear();
        oled.setFont(ArialMT_Plain_16);
        oled.drawString(oled.width() >> 1, 0, WiFi.localIP().toString());
        oled.setFont(ArialMT_Plain_24);
        oled.drawString(oled.width() >> 1, 18, String(data.power_delivered.int_val()) + "W");
        oled.display();
    }
}

void WiFiEvent(WiFiEvent_t event) {
    switch (event) {
        case SYSTEM_EVENT_STA_START:
            ESP_LOGD(TAG, "STA Started");
            //WiFi.setHostname( DEFAULT_HOSTNAME_PREFIX.c_str();
            break;
        case SYSTEM_EVENT_STA_CONNECTED:
            ESP_LOGD(TAG, "STA Connected");
            //WiFi.enableIpV6();
            break;
        case SYSTEM_EVENT_AP_STA_GOT_IP6:
            ESP_LOGD(TAG, "STA IPv6: ");
            ESP_LOGD(TAG, "%s", WiFi.localIPv6().toString());
            break;
        case SYSTEM_EVENT_STA_GOT_IP:
            ESP_LOGD(TAG, "STA IPv4: %s", WiFi.localIP());
            break;
        case SYSTEM_EVENT_STA_DISCONNECTED:
            ESP_LOGI(TAG, "STA Disconnected");
            WiFi.begin();
            break;
        case SYSTEM_EVENT_STA_STOP:
            ESP_LOGI(TAG, "STA Stopped");
            break;
        default:
            break;
    }
}
