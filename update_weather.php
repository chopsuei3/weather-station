<?php


// Read soil moisture and temperature from another Pi in JSON format
// Disabled currently

/*
$soilUrl = 'http://10.1.10.121/read_soil_moisture.php';
$ch = curl_init();
curl_setopt($ch, CURLOPT_URL, $soilUrl);
curl_setopt($ch, CURLOPT_RETURNTRANSFER, true );
$result = curl_exec($ch);
curl_close($ch);
$result_arr = json_decode($result, true);
//print_r($result_arr);
//echo $result_arr["data"]["date"];
$soiltempf = $result_arr["data"]["soiltempf"];
$soilmoisture = $result_arr["data"]["bulkec"] * 100;
*/

// Open CSV file to log weather parameters
$csvlog = fopen("current_conditions.csv","a") or die("unable to open file!");


// Function to find a string between 2 strings

function get_string_between($string, $start, $end){
    $string = " ".$string;
    $ini = strpos($string,$start);
    if ($ini == 0) return "";
    $ini += strlen($start);
    $len = strpos($string,$end,$ini) - $ini;
    return substr($string,$ini,$len);
}

// Include your weather station credentials
include 'weather_config.php';

$weather = array();

// Read weather parameter string from serial port via Python script
exec("python /home/pi/read_arduino.py", $weather);

$i = 0;

check_result:
if(!empty($weather[0])){

	// Set weather upload parameters and variables
	$currentTime = new DateTime('NOW');
	$currentTime->sub(new DateInterval('PT7H'));
	$winddir = get_string_between($weather[0],"winddir=",",");
	$windspeedmph = get_string_between($weather[0],"windspeedmph=",",");
	$windgustmph = get_string_between($weather[0],"windgustmph=",",");
	$windgustdir = get_string_between($weather[0],"windgustdir=",",");
	$windspdmph_avg2m = get_string_between($weather[0],"windspdmph_avg2m=",",");
	$winddir_avg2m = get_string_between($weather[0],"winddir_avg2m=",",");
	$windgustmph_10m = get_string_between($weather[0],"windgustmph_10m=",",");
	$windgustdir_10m = get_string_between($weather[0],"windgustdir_10m=",",");
	$rain = get_string_between($weather[0],"rainin=",",");
	$dailyRain = get_string_between($weather[0],"dailyrainin=",",");
	$pressureMb = get_string_between($weather[0],"pressure=",",");
	$pressure = $pressureMb * 0.02953;
	$tempF = get_string_between($weather[0],"tempf=",",");
	$humidity = get_string_between($weather[0],"humidity=",",");
	$solarRadiation = get_string_between($weather[0],"solarradiation=",",");


	// Calculate dew point
	// Reference http://ag.arizona.edu/azmet/dewpoint.html
	$tempC = ($tempF - 32) * (5/9);
	$L = log($humidity/100);
	$M = $tempC * 17.27;
	$N = $tempC + 237.3;
	$B = ($L + ($M/$N))/17.27;
	$dewPointC = (237.3 * $B)/(1 - $B);
	$dewPointF = round(($dewPointC * (9/5)) + 32,1);

	// Prepare weather variables to append to weather station upload URL
	$weatherData = "&winddir=" . $winddir . "&windspeedmph=" . $windspeedmph . "&windgustmph=" . $windgustmph . "&windgustdir=" . $windgustdir . "&windspdmph_avg2m=" . $windspdmph_avg2m . "&winddir_avg2m=" . $winddir_avg2m . "&windgustmph_10m=" . $windgustmph_10m . "&windgustdir_10m=" . $windgustdir_10m . "&humidity=" . $humidity . "&tempf=" . $tempF . "&dewptf=" . $dewPointF . "&rainin=" . $rain . "&dailyrainin=" . $dailyRain . "&baromin=" . $pressure;
	//echo $weatherData;

	// Prepare URL with weather parameters and options
	$url = "http://weatherstation.wunderground.com/weatherstation/updateweatherstation.php?ID=$stationId&PASSWORD=$stationPassword&dateutc=now$weatherData$options";

	// Get cURL resource
	$curl = curl_init();

	// Set some options - we are passing in a useragent too here
	curl_setopt_array($curl, array(
		CURLOPT_RETURNTRANSFER => 1,
		CURLOPT_URL => $url)
	);

	// Send the request & save response to $resp
	$resp = curl_exec($curl);

	// Close request to clear up some resources
	curl_close($curl);

	// Store the values in CSV log
	$update = "\"" . $currentTime->format('n/j/Y G:i:s') . "\",\"" . $tempF . "\",\"" . $humidity . "\",\"" . $dewPointF . "\",\"" . $pressure . "\",\"" . $solarRadiation . "\"\n";
	fwrite($csvlog,$update);
	fclose($csvlog);
}
else{
		while($i<5){
		exec("python /home/pi/test.py", $weather);
		$i++;
		goto check_result;
		}
	}

?>