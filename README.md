# smartMeterLogger-esp32

[![Codacy Badge](https://api.codacy.com/project/badge/Grade/5abe0cc3faa54a05a6acfe6b68e5eac6)](https://app.codacy.com/gh/CelliesProjects/smartMeterLogger-esp32?utm_source=github.com&utm_medium=referral&utm_content=CelliesProjects/smartMeterLogger-esp32&utm_campaign=Badge_Grade)

## About

`smartMeterLogger-esp32` connects to a smart meter (slimme meter) and logs the electricity use every minute to an sdcard. Saved data can be viewed in a html5 compatible browser on your phone/laptop/desktop. You can also compare the current day to a day in the past.

Compiles in the Arduino IDE.

See also the companion app [M5EnergyUse](https://github.com/CelliesProjects/M5-energyUse) that shows the current use and totals for the day on a M5Stack v1.

## Screenshots

![homepage](https://user-images.githubusercontent.com/24290108/190145362-c3f6d9c5-b6c4-4021-999b-009468b346af.png)

![daggrafiek](https://user-images.githubusercontent.com/24290108/190145677-0206855c-0bdd-43c4-a79b-dd22dcc20211.png)

![select](https://user-images.githubusercontent.com/24290108/190147164-d3d2fc5b-c7e4-45ad-9924-6707d72e6681.png)

![compare](https://user-images.githubusercontent.com/24290108/190146801-1e97c788-b5f3-4310-b483-e797b78c5fe2.png)

## How to setup and flash

Use the [latest Arduino IDE](https://github.com/arduino/arduino-ide/releases/latest) and [the latest ESP32 Arduino Core](https://github.com/espressif/arduino-esp32/releases/latest).

1.  Format an sdcard with a fat32 filesystem and insert the card in the reader. 
2.  Open the sketch and change your credentials and system setup in `setup.h`.
3.  (Optional) In `setup.h` uncomment `#define SH1106_OLED` if you compile for sh1106 instead of ssd1306 and set the i2c pins (and address) for your oled screen.<br>If you do not use a oled you can leave this setting as it is.
4.  Save all files and flash the sketch to your esp32.
5.  Connect your esp32 to the smart meter.<br>Take note that to read from the smartmeter the `DATA` signal has to be [inverted and level shifted](#level-shifter--inverter).
6.  If you added a ssd1306/sh1106 oled screen, the ip address (or an error) will be visible on the screen.<br>If there is no oled you can check the ip address on the serial port in the Arduino IDE.
7.  Browse to the ip address of your esp32 to see your current energy use.

If you have a garbled oled screen you most likely compiled for the wrong oled type.<br>Try to comment/uncomment `//#define SH1106_OLED` in `setup.h` to solve this.

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
