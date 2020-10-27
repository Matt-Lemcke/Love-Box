//Included Libraries
#include <ESP8266WiFi.h>
#include <BlynkSimpleEsp8266.h>
#include <TM1637Display.h>
#include <NTPClient.h>
#include <WiFiUdp.h>

//Pin definitions
#define ButtonPin D1
#define StatusPin D6
#define CLK D3
#define DIO D2

//Blynk authorization token
char auth[] = "###";

//Wifi credentials
char ssid[] = "###";
char pass[] = "###";

// Define NTP Client to get time
const long utcOffsetInSeconds = -4*3600;
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", utcOffsetInSeconds);

// Display object of for TM1637:
TM1637Display display = TM1637Display(CLK, DIO);

// Array that turns all segments off:
const uint8_t blank[] = {0x00, 0x00, 0x00, 0x00};

// Array that shows error message
const uint8_t error[] = {
  SEG_G,
  SEG_G, 
  SEG_G, 
  SEG_G
};

//Define data type to store timestamps
struct Time
{
  int h, m, s;
};

//Define data type to store dates
struct Date 
{ 
    int d, m, y; 
};

//Define deep sleep start and end times
Time startSleep = {0,0,0};
Time endSleep = {0,0,0};
unsigned int sleepTime = 0;

// Counts number of leap years before the given date 
int countLeapYears(Date d) 
{ 
    int years = d.y; 
    
    if (d.m <= 2) 
        years--; 

    return years / 4 - years / 100 + years / 400; 
} 
  
// Returns number of days between two given dates
int getDifference(Date dt1, Date dt2) 
{ 
    const int monthDays[12] = {31, 28, 31, 30, 31, 30, 
                           31, 31, 30, 31, 30, 31};  

    // COUNT TOTAL NUMBER OF DAYS BEFORE FIRST DATE 'dt1'
    long int n1 = dt1.y*365 + dt1.d; 
    for (int i=0; i<dt1.m - 1; i++) 
        n1 += monthDays[i]; 
    n1 += countLeapYears(dt1); 
  
    // COUNT TOTAL NUMBER OF DAYS BEFORE 'dt2' 
  
    long int n2 = dt2.y*365 + dt2.d; 
    for (int i=0; i<dt2.m - 1; i++) 
        n2 += monthDays[i]; 
    n2 += countLeapYears(dt2); 
  
    // return difference between two counts 
    return (n2 - n1); 
}

// Get current date
Date getDate(){
  Date d;
  String formattedDate = timeClient.getFormattedDate();
  int splitT = formattedDate.indexOf("T");
  String dayStamp = formattedDate.substring(0, splitT);
  d.y = dayStamp.substring(0,4).toInt();
  d.m = dayStamp.substring(5,7).toInt();
  d.d = dayStamp.substring(8,10).toInt();
  return d;
}

//Get current Time
Time getTime(){
  Time t;
  String formattedTime = timeClient.getFormattedTime();
  t.h = formattedTime.substring(0,2).toInt();
  t.m = formattedTime.substring(3,5).toInt();
  t.s = formattedTime.substring(6,8).toInt();
  return t;
}

// Toggle heart LEDs for animation on call
void heartLED()
{
  //6 for 6 months hehe
  for(int jenpp = 0; jenpp<6; jenpp++){
    digitalWrite(D8, HIGH);
    digitalWrite(D4, LOW);
    digitalWrite(D5, LOW);
    delay(500);
    digitalWrite(D8, LOW);
    digitalWrite(D4, HIGH);
    digitalWrite(D5, HIGH);
    delay(500);
  }
  digitalWrite(D4, LOW);
  digitalWrite(D5, LOW);
  delay(100);
  digitalWrite(D8, HIGH);
  digitalWrite(D4, HIGH);
  digitalWrite(D5, HIGH);
  delay(5000);
  digitalWrite(D8, LOW);
  digitalWrite(D4, LOW);
  digitalWrite(D5, LOW);
}

// Check V3 on reconnect
BLYNK_CONNECTED()
{
  Blynk.syncVirtual(V3);
}

// Run heart animation when directed by phone app
BLYNK_WRITE(1)
{ 
  int pinValue = param.asInt();
  if(pinValue == 1){
    heartLED();
  }
}

// Reset device with short deep sleep
BLYNK_WRITE(2)
{ 
  int pinValue = param.asInt();
  if(pinValue == 1){
    display.clear();
    ESP.deepSleep(10e6);
  }
}

// Updates deep sleep hours from phone app
BLYNK_WRITE(3)
{ 
  TimeInputParam t(param);
  if (t.hasStartTime()){
    startSleep.h = t.getStartHour();
    startSleep.m = t.getStartMinute();
    startSleep.s = t.getStartSecond();
  }
  if (t.hasStopTime()){
    endSleep.h = t.getStopHour();
    endSleep.m = t.getStopMinute();
    endSleep.s = t.getStopSecond();
  }
  unsigned int startTotal = startSleep.h*3600 + startSleep.m*60 + startSleep.s;
  unsigned int endTotal = endSleep.h*3600 + endSleep.m*60 + endSleep.s;

  if(startTotal <= endTotal){
    sleepTime = endTotal - startTotal;
  }
  else{
    sleepTime = 86400 - startTotal + endTotal;
  }
}

// Initialize variables
Date currDate;
Date startDate = {16,3,2020};
Time currTime;
bool updated = true;

void setup()
{
  // Clear the display and set max brightness
  display.clear();
  display.setBrightness(7);
  
  // Define pin functions
  pinMode(ButtonPin, INPUT);
  pinMode(StatusPin, OUTPUT);
  pinMode(D8, OUTPUT);
  pinMode(D5, OUTPUT);

  // Connect to internet
  WiFi.begin(ssid, pass);
  while ( WiFi.status() != WL_CONNECTED ) {
    delay ( 500 );
    digitalWrite(StatusPin, HIGH);
  }
  digitalWrite(StatusPin, LOW);

  // Start time fetch
  timeClient.begin();
  timeClient.update();

  // Initialize current date
  currDate = getDate();

  currTime = getTime();

  // Connect to blynk server
  Blynk.begin(auth, ssid, pass);

  // Update days display on app
  Blynk.virtualWrite(V0, getDifference(startDate, currDate));

  // Signal that startup was complete
  for(int i=0; i<3; i++){
    digitalWrite(StatusPin, HIGH);
    delay(250);
    digitalWrite(StatusPin, LOW);
    delay(250);
  }
  // Update days display on app and box display
  int days = getDifference(startDate, currDate);
  Blynk.virtualWrite(V0, days);
  display.showNumberDec(days);
}

void loop()
{
  // Run blynk functions
  Blynk.run();
  
  // Update time
  timeClient.update();

  // Get current time
  currTime = getTime();

  // Signal if disconnected from app
  if (!Blynk.connected()){
    digitalWrite(StatusPin, HIGH);
  }
  else{
    digitalWrite(StatusPin, LOW);
  }

  // Update days counter at midnight
  if (currTime.h == 0){
    if (!updated){
      updated = true;
      currDate = getDate();

      //Update display on box and app
      int days = getDifference(startDate, currDate);
      if (days < 10000){
        Blynk.virtualWrite(V0, days);
        display.showNumberDec(days);
      }
      // Display error message if number exceeds display length
      else{
        Blynk.virtualWrite(V0, days);
        display.setSegments(error);
        Blynk.notify("Days exceed display length");
      }
    }
  }
  else if (updated){
    updated = false;
  }

  if ((currTime.h == startSleep.h)&&(currTime.m == startSleep.m)&&(currTime.s == startSleep.s)){
    display.clear();
    ESP.deepSleep(sleepTime * 1e6);
    
  }

  // Send notification to app when button is pressed
  if (digitalRead(ButtonPin)){
    Blynk.notify("I love you!");
  } 
}
