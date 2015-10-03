/*
 Weather Shield Example
 By: Nathan Seidle
 SparkFun Electronics
 Date: November 16th, 2013
 License: This code is public domain but you buy me a beer if you use this and we meet someday (Beerware license).

 Much of this is based on Mike Grusin's USB Weather Board code: https://www.sparkfun.com/products/10586

 This code reads all the various sensors (wind speed, direction, rain gauge, humidty, pressure, light, batt_lvl)
 and reports it over the serial comm port. This can be easily routed to an datalogger (such as OpenLog) or
 a wireless transmitter (such as Electric Imp).

 Measurements are reported once a second but windspeed and rain gauge are tied to interrupts that are
 calcualted at each report.

 This code reads all the various sensors (wind speed, direction, rain gauge, humidty, pressure, light, batt_lvl)
 and sends it to the serial port, which then forwards that data to Raspberry Pi that does some processing then
 bounces the weather data to Wunderground.

 We use A0 for wind direction.

 This code assumes the GPS module is not used.
 */

#include <avr/wdt.h> //We need watch dog for this program
#include <SoftwareSerial.h> //Connection to Imp
#include <Wire.h> //I2C needed for sensors
#include "MPL3115A2.h" //Pressure sensor
#include "HTU21D.h" //Humidity sensor

// Add support for BMP180 temp/pressure sensor
#include <SFE_BMP180.h> // BMP180 sensor

SoftwareSerial imp(8, 9); // RX, TX into RaspberryPi Pin TX, RX

//MPL3115A2 myPressure; //Create an instance of the pressure sensor
SFE_BMP180 myPressure;
HTU21D myHumidity; //Create an instance of the humidity sensor

#define ALTITUDE 5.0 // Altitude of location

//Hardware pin definitions
//-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
// digital I/O pins
const byte WSPEED = 3;
const byte RAIN = 2;
const byte STAT1 = 7;

// analog I/O pins
const byte WDIR = A0;
const byte LIGHT = A4;
const byte BATT = A2;
const byte REFERENCE_3V3 = A3;

//Global Variables
//-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
long lastSecond; //The millis counter to see when a second rolls by
unsigned int minutesSinceLastReset; //Used to reset variables after 24 hours. Imp should tell us when it's midnight, this is backup.
byte seconds; //When it hits 60, increase the current minute
byte seconds_2m; //Keeps track of the "wind speed/dir avg" over last 2 minutes array of data
byte minutes; //Keeps track of where we are in various arrays of data
byte minutes_10m; //Keeps track of where we are in wind gust/dir over last 10 minutes array of data

long lastWindCheck = 0;
volatile long lastWindIRQ = 0;
volatile byte windClicks = 0;

//We need to keep track of the following variables:
//Wind speed/dir each update (no storage)
//Wind gust/dir over the day (no storage)
//Wind speed/dir, avg over 2 minutes (store 1 per second)
//Wind gust/dir over last 10 minutes (store 1 per minute)
//Rain over the past hour (store 1 per minute)
//Total rain over date (store one per day)

byte windspdavg[120]; //120 bytes to keep track of 2 minute average
#define WIND_DIR_AVG_SIZE 120
int winddiravg[WIND_DIR_AVG_SIZE]; //120 ints to keep track of 2 minute average
float windgust_10m[10]; //10 floats to keep track of largest gust in the last 10 minutes
int windgustdirection_10m[10]; //10 ints to keep track of 10 minute max
volatile float rainHour[60]; //60 floating numbers to keep track of 60 minutes of rain

//These are all the weather values that wunderground expects:
int winddir; // [0-360 instantaneous wind direction]
float windspeedmph; // [mph instantaneous wind speed]
float windgustmph; // [mph current wind gust, using software specific time period]
int windgustdir; // [0-360 using software specific time period]
float windspdmph_avg2m; // [mph 2 minute average wind speed mph]
int winddir_avg2m; // [0-360 2 minute average wind direction]
float windgustmph_10m; // [mph past 10 minutes wind gust mph ]
int windgustdir_10m; // [0-360 past 10 minutes wind gust direction]
float humidity; // [%]
double tempf; // [temperature F]
double tempc; // [temperature F]
float rainin; // [rain inches over the past hour)] -- the accumulated rainfall in the past 60 min
volatile float dailyrainin; // [rain inches so far today in local time]
//float baromin = 30.03;// [barom in] - It's hard to calculate baromin locally, do this in the agent
double pressure;
//float dewptf; // [dewpoint F] - It's hard to calculate dewpoint locally, do this in the agent

//These are not wunderground values, they are just for us
float batt_lvl = 11.8;
float light_lvl = 0.72;

// volatiles are subject to modification by IRQs
volatile unsigned long raintime, rainlast, raininterval, rain;

//-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=

//Interrupt routines (these are called by the hardware interrupts, not by the main code)
//-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
void rainIRQ()
// Count rain gauge bucket tips as they occur
// Activated by the magnet and reed switch in the rain gauge, attached to input D2
{
	raintime = millis(); // grab current time
	raininterval = raintime - rainlast; // calculate interval between this and last event

	if (raininterval > 10) // ignore switch-bounce glitches less than 10mS after initial edge
	{
		dailyrainin += 0.011; //Each dump is 0.011" of water
		rainHour[minutes] += 0.011; //Increase this minute's amount of rain

		rainlast = raintime; // set up for next event
	}
}

void wspeedIRQ()
// Activated by the magnet in the anemometer (2 ticks per rotation), attached to input D3
{
	if (millis() - lastWindIRQ > 10) // Ignore switch-bounce glitches less than 10ms (142MPH max reading) after the reed switch closes
	{
		lastWindIRQ = millis(); //Grab the current time
		windClicks++; //There is 1.492MPH for each click per second.
	}
}

void setup()
{
	wdt_reset(); //Pet the dog
	wdt_disable(); //We don't want the watchdog during init

	Serial.begin(9600);
	imp.begin(19200);

	pinMode(WSPEED, INPUT_PULLUP); // input from wind meters windspeed sensor
	pinMode(RAIN, INPUT_PULLUP); // input from wind meters rain gauge sensor

	pinMode(WDIR, INPUT);
	pinMode(LIGHT, INPUT);
	pinMode(BATT, INPUT);
	pinMode(REFERENCE_3V3, INPUT);

	pinMode(STAT1, OUTPUT);

	midnightReset(); //Reset rain totals

	//Configure the pressure sensor
	myPressure.begin(); // Get sensor online

	//Configure the humidity sensor
	myHumidity.begin();

		seconds = 0;
	lastSecond = millis();

	// attach external interrupt pins to IRQ functions
	attachInterrupt(0, rainIRQ, FALLING);
	attachInterrupt(1, wspeedIRQ, FALLING);

	// turn on interrupts
	interrupts();

	Serial.println("Wimp Weather Station online!");
	reportWeather();

	//  wdt_enable(WDTO_1S); //Unleash the beast
}

void loop()
{
	wdt_reset(); //Pet the dog

	//Keep track of which minute it is
	if(millis() - lastSecond >= 1000)
	{
		lastSecond += 1000;

		//Take a speed and direction reading every second for 2 minute average
		if(++seconds_2m > 119) seconds_2m = 0;

		//Calc the wind speed and direction every second for 120 second to get 2 minute average
		windspeedmph = get_wind_speed();
		winddir = get_wind_direction();
		windspdavg[seconds_2m] = (int)windspeedmph;
		winddiravg[seconds_2m] = winddir;
		//if(seconds_2m % 10 == 0) displayArrays();

		//Check to see if this is a gust for the minute
		if(windspeedmph > windgust_10m[minutes_10m])
		{
			windgust_10m[minutes_10m] = windspeedmph;
			windgustdirection_10m[minutes_10m] = winddir;
		}

		//Check to see if this is a gust for the day
		//Resets at midnight each night
		if(windspeedmph > windgustmph)
		{
			windgustmph = windspeedmph;
			windgustdir = winddir;
		}

		//Blink stat LED briefly to show we are alive
		digitalWrite(STAT1, HIGH);
		//reportWeather(); //Print the current readings. Takes 172ms.
		delay(25);
		digitalWrite(STAT1, LOW);

		//If we roll over 60 seconds then update the arrays for rain and windgust
		if(++seconds > 59)
		{
			seconds = 0;

			if(++minutes > 59) minutes = 0;
			if(++minutes_10m > 9) minutes_10m = 0;

			rainHour[minutes] = 0; //Zero out this minute's rainfall amount
			windgust_10m[minutes_10m] = 0; //Zero out this minute's gust

			minutesSinceLastReset++; //It's been another minute since last night's midnight reset
		}
	}
	
	//Wait for the RaspPi to ping us with the ! character
	if(imp.available())
	{
		byte incoming = imp.read();
		if(incoming == '!')
		{
			reportWeather(); //Send all the current readings out the imp and to its agent for posting to wunderground. Takes 196ms
			//Serial.print("Pinged!");
		
		}
		else if(incoming == '@') //Special character from Imp indicating midnight local time
		{
			midnightReset(); //Reset a bunch of variables like rain and daily total rain
			//Serial.print("Midnight reset");
		}
		else if(incoming == '#') //Special character from Imp indicating a hardware reset
		{
			//Serial.print("Watchdog reset");
			delay(5000); //This will cause the system to reset because we don't pet the dog
		}
	}
	
	delay(100); //Update every 100ms. No need to go any faster.
}

//When the imp tells us it's midnight, reset the total amount of rain and gusts
void midnightReset()
{
	dailyrainin = 0; //Reset daily amount of rain

	windgustmph = 0; //Zero out the windgust for the day
	windgustdir = 0; //Zero out the gust direction for the day

	minutes = 0; //Reset minute tracker
	seconds = 0;
	lastSecond = millis(); //Reset variable used to track minutes

	minutesSinceLastReset = 0; //Zero out the backup midnight reset variable
}

//Calculates each of the variables that wunderground is expecting
void calcWeather()
{
	//current winddir, current windspeed, windgustmph, and windgustdir are calculated every 100ms throughout the day

	//Calc windspdmph_avg2m
	float temp = 0;
	for(int i = 0 ; i < 120 ; i++)
		temp += windspdavg[i];
	temp /= 120.0;
	windspdmph_avg2m = temp;

	//Calc winddir_avg2m, Wind Direction
	//You can't just take the average. Google "mean of circular quantities" for more info
	//We will use the Mitsuta method because it doesn't require trig functions
	//And because it sounds cool.
	//Based on: http://abelian.org/vlf/bearings.html
	//Based on: http://stackoverflow.com/questions/1813483/averaging-angles-again
	long sum = winddiravg[0];
	int D = winddiravg[0];
	for(int i = 1 ; i < WIND_DIR_AVG_SIZE ; i++)
	{
		int delta = winddiravg[i] - D;

		if(delta < -180)
			D += delta + 360;
		else if(delta > 180)
			D += delta - 360;
		else
			D += delta;

		sum += D;
	}
	winddir_avg2m = sum / WIND_DIR_AVG_SIZE;
	if(winddir_avg2m >= 360) winddir_avg2m -= 360;
	if(winddir_avg2m < 0) winddir_avg2m += 360;


	//Calc windgustmph_10m
	//Calc windgustdir_10m
	//Find the largest windgust in the last 10 minutes
	windgustmph_10m = 0;
	windgustdir_10m = 0;
	//Step through the 10 minutes
	for(int i = 0; i < 10 ; i++)
	{
		if(windgust_10m[i] > windgustmph_10m)
		{
			windgustmph_10m = windgust_10m[i];
			windgustdir_10m = windgustdirection_10m[i];
		}
	}

	//Calc humidity
	humidity = myHumidity.readHumidity();

	//Calc tempf from pressure sensor
        tempf = getTemperature();       

	//Serial.print(" TempP:");
	//Serial.print(tempf, 2);

	//Total rainfall for the day is calculated within the interrupt
	//Calculate amount of rainfall for the last 60 minutes
	rainin = 0;
	for(int i = 0 ; i < 60 ; i++)
		rainin += rainHour[i];

	//Calc pressure
        pressure = getPressure();

	//Calc light level
	light_lvl = get_light_level();

	//Calc battery level
	batt_lvl = get_battery_level();

}

// Tried connecting to PBC-139 photocell circuit to read analog values between 0 and 1024
// Should be modified to read light sensor % and to multiply this by the expected incoming solar radiation based on lat/long *TO DO
float get_light_level()
{
//	float operatingVoltage = averageAnalogRead(REFERENCE_3V3);

	float lightSensor = averageAnalogRead(LIGHT);

//	operatingVoltage = 3.3 / operatingVoltage; //The reference voltage is 3.3V

//	lightSensor *= operatingVoltage;

	return(lightSensor);
}

//Returns the voltage of the raw pin based on the 3.3V rail
//The battery can ranges from 4.2V down to around 3.3V
//This function allows us to ignore what VCC might be (an Arduino plugged into USB has VCC of 4.5 to 5.2V)
//The weather shield has a pin called RAW (VIN) fed through through two 5% resistors and connected to A2 (BATT):
//3.9K on the high side (R1), and 1K on the low side (R2)
float get_battery_level()
{
	float operatingVoltage = averageAnalogRead(REFERENCE_3V3);

	float rawVoltage = averageAnalogRead(BATT);

	operatingVoltage = 3.30 / operatingVoltage; //The reference voltage is 3.3V

	rawVoltage *= operatingVoltage; //Convert the 0 to 1023 int to actual voltage on BATT pin

	rawVoltage *= 4.90; //(3.9k+1k)/1k - multiply BATT voltage by the voltage divider to get actual system voltage

	return(rawVoltage);
}

//Returns the instataneous wind speed
float get_wind_speed()
{
	float deltaTime = millis() - lastWindCheck; //750ms

	deltaTime /= 1000.0; //Covert to seconds

	float windSpeed = (float)windClicks / deltaTime; //3 / 0.750s = 4

	windClicks = 0; //Reset and start watching for new wind
	lastWindCheck = millis();

	windSpeed *= 1.492; //4 * 1.492 = 5.968MPH

	/* Serial.println();
	 Serial.print("Windspeed:");
	 Serial.println(windSpeed);*/

	return(windSpeed);
}

int get_wind_direction()
// read the wind direction sensor, return heading in degrees
{
	unsigned int adc;

	adc = averageAnalogRead(WDIR); // get the current reading from the sensor

	// The following table is ADC readings for the wind direction sensor output, sorted from low to high.
	// Each threshold is the midpoint between adjacent headings. The output is degrees for that ADC reading.
	// Note that these are not in compass degree order! See Weather Meters datasheet for more information.

	if (adc < 380) return (113);
	if (adc < 393) return (68);
	if (adc < 414) return (90);
	if (adc < 456) return (158);
	if (adc < 508) return (135);
	if (adc < 551) return (203);
	if (adc < 615) return (180);
	if (adc < 680) return (23);
	if (adc < 746) return (45);
	if (adc < 801) return (248);
	if (adc < 833) return (225);
	if (adc < 878) return (338);
	if (adc < 913) return (0);
	if (adc < 940) return (293);
	if (adc < 967) return (315);
	if (adc < 990) return (270);
	return (-1); // error, disconnected?
}

//Reports the weather string to the Imp
void reportWeather()
{
	calcWeather(); //Go calc all the various sensors

	imp.print("$,winddir=");
	imp.print(winddir);
	imp.print(",windspeedmph=");
	imp.print(windspeedmph, 1);
	imp.print(",windgustmph=");
	imp.print(windgustmph, 1);
	imp.print(",windgustdir=");
	imp.print(windgustdir);
	imp.print(",windspdmph_avg2m=");
	imp.print(windspdmph_avg2m, 1);
	imp.print(",winddir_avg2m=");
	imp.print(winddir_avg2m);
	imp.print(",windgustmph_10m=");
	imp.print(windgustmph_10m, 1);
	imp.print(",windgustdir_10m=");
	imp.print(windgustdir_10m);
	imp.print(",humidity=");
	imp.print(humidity, 1);
	imp.print(",tempf=");
	imp.print(tempf, 1);
	imp.print(",rainin=");
	imp.print(rainin, 2);
	imp.print(",dailyrainin=");
	imp.print(dailyrainin, 2);
	imp.print(",pressure="); //Don't print pressure= because the agent will be doing calcs on the number
	imp.print(pressure, 2);
	imp.print(",solarradiation=");
	imp.print(light_lvl, 2);
	
	imp.print(",");
	imp.println("#,");

	//Test string
	//Serial.println("$,winddir=270,windspeedmph=0.0,windgustmph=0.0,windgustdir=0,windspdmph_avg2m=0.0,winddir_avg2m=12,windgustmph_10m=0.0,windgustdir_10m=0,humidity=998.0,tempf=-1766.2,rainin=0.00,dailyrainin=0.00,-999.00,batt_lvl=16.11,light_lvl=3.32,#,");
}

//Takes an average of readings on a given pin
//Returns the average
int averageAnalogRead(int pinToRead)
{
	byte numberOfReadings = 8;
	unsigned int runningValue = 0;

	for(int x = 0 ; x < numberOfReadings ; x++)
		runningValue += analogRead(pinToRead);
	runningValue /= numberOfReadings;

	return(runningValue);
}

// Read pressure from BMP180 sensor

double getPressure()
{
  char status;
  double T,P,p0,a;

  // You must first get a temperature measurement to perform a pressure reading.
  
  // Start a temperature measurement:
  // If request is successful, the number of ms to wait is returned.
  // If request is unsuccessful, 0 is returned.

  status = myPressure.startTemperature();
  if (status != 0)
  {
    // Wait for the measurement to complete:

    delay(status);

    // Retrieve the completed temperature measurement:
    // Note that the measurement is stored in the variable T.
    // Use '&T' to provide the address of T to the function.
    // Function returns 1 if successful, 0 if failure.

    status = myPressure.getTemperature(T);
    if (status != 0)
    {
      // Start a pressure measurement:
      // The parameter is the oversampling setting, from 0 to 3 (highest res, longest wait).
      // If request is successful, the number of ms to wait is returned.
      // If request is unsuccessful, 0 is returned.

      status = myPressure.startPressure(3);
      if (status != 0)
      {
        // Wait for the measurement to complete:
        delay(status);

        // Retrieve the completed pressure measurement:
        // Note that the measurement is stored in the variable P.
        // Use '&P' to provide the address of P.
        // Note also that the function requires the previous temperature measurement (T).
        // (If temperature is stable, you can do one temperature measurement for a number of pressure measurements.)
        // Function returns 1 if successful, 0 if failure.

        status = myPressure.getPressure(P,T);
        if (status != 0)
        {
          return(P);
        }
        else Serial.println("error retrieving pressure measurement\n");
      }
      else Serial.println("error starting pressure measurement\n");
    }
    else Serial.println("error retrieving temperature measurement\n");
  }
  else Serial.println("error starting temperature measurement\n");
}

// Read temperature from BMP180 sensor
double getTemperature()
{
  char status;
  double T,P,p0,a;

  // You must first get a temperature measurement to perform a pressure reading.
  
  // Start a temperature measurement:
  // If request is successful, the number of ms to wait is returned.
  // If request is unsuccessful, 0 is returned.

  status = myPressure.startTemperature();
  if (status != 0)
  {
    // Wait for the measurement to complete:

    delay(status);

    // Retrieve the completed temperature measurement:
    // Note that the measurement is stored in the variable T.
    // Use '&T' to provide the address of T to the function.
    // Function returns 1 if successful, 0 if failure.

    status = myPressure.getTemperature(T);
    if (status != 0)
        {
          T = (T * 1.8) + 32;
          return(T);
        }
        else Serial.println("error retrieving temperature measurement\n");
  }
  else Serial.println("error starting temperature measurement\n");
}

