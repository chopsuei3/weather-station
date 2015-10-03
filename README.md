# weather-station
-----------------
Initially based off this idea - https://learn.sparkfun.com/tutorials/weather-station-wirelessly-connected-to-wunderground.
I've broken out some of the sensors from the Weather Shield to try to get more accurate temperature and light readings.

Parts and sensors
-----------------
Raspberry Pi Model A+
Arduino Uno (or Redboard)
Weather Meters from Sparkfun (https://www.sparkfun.com/products/8942)
Weather Shield from Sparkfun
BMP180 Temp/Pressure/Humidity sensor
Photorresistor


Other Resources
---------------
https://outsidescience.wordpress.com/2012/11/03/diy-science-measuring-light-with-a-photodiode-ii/
https://learn.adafruit.com/photocells/using-a-photocell
https://blog.udemy.com/arduino-ldr/
https://books.google.com/books?id=L1gXTViC-J4C&lpg=PA49&ots=NdePhy2l6g&dq=PDB-C139&pg=PA47#v=onepage&q=PDB-C139&f=false


Repos Used
----------
https://github.com/sparkfun/Wimp_Weather_Station
https://github.com/sparkfun/Weather_Shield
https://github.com/sparkfun/BMP180_Breakout_Arduino_Library


Usage
-----
There are 2 parts of the code included which are meant to be run on an Arduino Uno (or Redboard) and a Raspberry Pi running Debian Wheezy.

The .ino sketch can be loaded on to the Arduino using the standard loading process.
You'll have to add the included BMP180 library files into your Arduino environment to compile.

The scripts can be loaded onto the Rasp Pi via scp, ftp, git, it doesn't really matter how you get them there.

The Arduino and Weather Shield will measure - 
- Wind speed and direction
- Rain (tipping bucket, to be modified with optical tipping bucket)

The Arduino also has a BMP180 sensor attached via I2C and will measure - 
- Temperature
- Humidity
- Barometric pressure

The Arduino also has a photoresistor circuits attached to measure 
- Light