# Mitipump
An  Adafruit Huzzah32 Feather Arduino based controller for Home Assistant and Mitsubishi Split AC units. Significantly cheaper than the Kumo Cloud add-ons.

## Thanks
Home Assistant Integration would not exist without the @gysmo38 repository available here: https://github.com/gysmo38/mitsubishi2MQTT

Requires and based on examples in the excellent HeatPump library by @SwiCago available here: https://github.com/SwiCago/HeatPump

## Details
Designed to be plug and play with an Adafruit Huzzah32 Feather and Home Assistant as a climate device with MQTT. No voltage regulator needed since the Mitsubishi outputs 5vdc and Huzzah32 can handle the small draw through its on board regulator with no real loads.

If you purchase the pre-made pigtails (http://www.usastore.revolectrix.com/Products_2/Cellpro-4s-Charge-Adapters_2/Cellpro-JST-PA-Battery-Pigtail-10-5-Position) you can simply solder them straight to the Huzzah32. 

1. +5VDC wire to the USB pinout. (Brown on my cable)
2. Ground wire to the GND pinout. (Orange on my cable)
3. Unit Rx wire to the Tx pinout. (Red on my cable)
4. Unit Tx wire to the Rx pinout. (Blue on my cable)

Personally, I purchased a male-female set of 4 pin connectors to act as a disconnect for the unit so I don't have to take the whole cover off if I need to re-program/replace/restart an Arduino. I wire the male side to my Arduino, the female side to the pigtail above, plugged the pigtail in and ran it down so I can access it taking just the small bottom right cover off.

## Technical Notes
1. A Huzzah32 has two serial outputs, this code specifically uses the pinouts via the line Serial1
2. Fill in your details via the .h file for your network and change the hvac_lr for each unit
3. I'm not very good at C so please feel free to let me know if I did something silly or you know how to do it better.

## Installation With Pictures
1. Get a Huzzah32 board and pigtals, download the project, modify the .h variable if desired, and flash it to the Arduino
2. Optional: Solder a second 2nd set of pigtails to act as a disconnect so you don't need to pull the cover off if you need to replace the Ardunio. ![img](https://www.mattemerson.com/ard/1.jpg)
3. Solder the pigtails to the Huzzah32
4. Remove the cover from your Mitsubishi Mini Split indoor unit (youtube has videos)
5. Attach your Arduino ![img](https://www.mattemerson.com/ard/2.jpg) ![img](https://www.mattemerson.com/ard/3.jpg)
6. If MQTT is setup on Home Assistant and you configured the Aurduino correct it should auto-discover and be available to work
