# smartMeterLogger-esp32

## About

`smartMeterLogger-esp32` connects to a smart meter (slimme meter) and logs the electricity use per minute to an sdcard. Saved data can be viewed in a html5 compatible browser on your phone/laptop/desktop.

Compiles in the Arduino IDE.

## Screenshots

![overzicht vandaag android](img/screenshot_android_vandaag.png)


![huidig](img/screenshot_pc.png)


![overzicht vandaag pc](img/screenshot_vandaag_pc.png)

## How to use

1.  Format an sdcard with a fat32 filesystem and insert the card in the reader. 
2.  Open the sketch and change your credentials in `wifisetup.h`.
3.  (Optional) In `smartMeterLogger-esp32.ino` uncomment `#define SH1106_OLED` if you compile for sh1106 instead of ssd1306 and set the i2c pins (and address) for your oled screen.<br>If you do not use a oled you can leave this setting as it is.
4.  Save all files and flash the sketch to your esp32.
5.  Connect your esp32 to the smart meter.<br>Take note that to read from the smartmeter the `DATA` signal has to be [inverted and level shifted](#level-shifter--inverter).
6.  If you added a ssd1306/sh1106 oled screen, the ip address (or an error) will be visible on the screen.<br>If there is no oled you can check the ip address on the serial port in the Arduino IDE.
7.  Browse to the ip address of your esp32 to see your current energy use.

If you have a garbled screen you most likely compiled for the wrong oled type.<br>Try to comment/uncomment `#define SH1106_OLED` to solve this.

## Needed libraries

- [https://github.com/me-no-dev/AsyncTCP](https://github.com/me-no-dev/AsyncTCP)
- [https://github.com/me-no-dev/ESPAsyncWebServer](https://github.com/me-no-dev/ESPAsyncWebServer)
- [https://github.com/matthijskooijman/arduino-dsmr](https://github.com/matthijskooijman/arduino-dsmr)

Download and install these in the Arduino libraries folder.

The driver library for a ssd1306/sh1106 oled can be installed with the Arduino library manager. Use the ThingPulse driver.

## DSMR v5 P1 port standard specifications

[DSMR v5.0.2 P1 Companion Standard.pdf](https://github.com/matthijskooijman/arduino-dsmr/blob/master/specs/DSMR%20v5.0.2%20P1%20Companion%20Standard.pdf)

## Needed or supported hardware

### ESP32: LilyGo TTGO T7 with external antenna

SmartMeterLogger is developed on LilyGo TTGO T7 boards. The LilyGo TTGO T7 has an external antenna connector and a decent 3.3v LDO.<br>Without an external antenna the WiFi signal tends to be too poor to be of any use over longer distances and/or through several walls. The board will have to modified slightly to enable the external antenna. 

![T7 pic](img/t7.jpg)

Below you can see how to enable the external antenna. Move the zero ohm resistor from position 1-2 to position 3-4. Or remove the resistor and solder position 3-4 closed.
<br>Another nice thing with an external antenna is that the current needed to operate the logger drops below 250mA so the logger can be powered by the smartmeter.

![external-config](https://user-images.githubusercontent.com/17033305/78676790-34fd1080-78e7-11ea-8bb0-aee88efe75a6.jpg)

See [this LilyGo issue](https://github.com/LilyGO/ESP32-MINI-32-V1.3/issues/4#issuecomment-610394847) about the external antenna.

### SD card reader

![card reader](img/sdboard.png)

Using the default pins.
Use an EMPTY fat32 formatted sdcard on the first boot. 

### Level shifter / inverter

To invert and level shift the signal you can use a bc547 transistor with some resistors. For example like this:

![invert-and-level-shift](https://willem.aandewiel.nl/wp-content/uploads/2019/04/DSMR_LevelShifter_Circuit-300x251.png)

See [willem.aandewiel.nl/dsmr-logger-v4-slimme-meter-uitlezer/](https://willem.aandewiel.nl/index.php/2019/04/09/dsmr-logger-v4-slimme-meter-uitlezer/)

### Some board variants

![board no oled](img/board_no_oled.png)

![board sh1106](img/board_sh1106.png)

![board ssd1306](img/board_ssd1306.png)
