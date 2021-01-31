/* system setup file for smartMeterLogger-esp32 */

const char* WIFI_NETWORK =         "xxx";
const char* WIFI_PASSWORD =        "xxx";

/* settings for smartMeter */
#define RXD_PIN                    (36)
#define BAUDRATE                   (115200)
#define UART_NR                    (UART_NUM_2)

#define USE_WS_BRIDGE              false                     /* true = connect to a dsmr websocket bridge - false = connect to a dsmr smartmeter */

const char*    WS_BRIDGE_HOST =    "192.168.0.106";          /* bridge name or ip*/
const uint16_t WS_BRIDGE_PORT =    80;                       /* bridge port */
const char*    WS_BRIDGE_URL =     "/raw";                   /* bridge url */

#define SET_STATIC_IP false                                  /* If SET_STATIC_IP is set to true then STATIC_IP, GATEWAY, SUBNET and PRIMARY_DNS have to be set to some sane values */

const IPAddress STATIC_IP          (192, 168, 0, 90);        /* This should be outside your router dhcp range! */
const IPAddress GATEWAY            (192, 168, 0, 1);         /* Set to your gateway ip address */
const IPAddress SUBNET             (255, 255, 255, 0);       /* Usually 255,255,255,0 check in your router or pc connected to the same network */
const IPAddress PRIMARY_DNS        (192, 168, 0, 30);        /* Check in your router */
const IPAddress SECONDARY_DNS      (0, 0, 0, 0);             /* Check in your router */

/* settings for ntp time sync */
const char* NTP_POOL =             "nl.pool.ntp.org";
const char* TIMEZONE =             "CET-1CEST,M3.5.0/2,M10.5.0/3"; /* Central European Time - see http://www.remotemonitoringsystems.ca/time-zone-abbreviations.php */

/* settings for a ssd1306/sh1106 oled screen */
//#define SH1106_OLED                                      /* uncomment to compile for SH1106 instead of SSD1306 */

#define OLED_ADDRESS               (0x3C)
#define I2C_SDA_PIN                (21)
#define I2C_SCL_PIN                (22)
