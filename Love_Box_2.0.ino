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

//Define states
#define STANDBY_STATE 0
#define SELECT_STATE 1
#define OFFLINE_STATE 2
#define SEND_MSG_STATE 3
#define UPDATE_STATE 4

#define PING_TIMER_PERIOD 30000
#define BUTTON_TIMER_PERIOD 500

#define DEBOUNCE_DELAY 10

// 7 segment letter displays
#define DISP_M SEG_E|SEG_G|SEG_C|SEG_A
#define DISP_O SEG_E|SEG_G|SEG_C|SEG_D
#define DISP_R SEG_E|SEG_G
#define DISP_N SEG_E|SEG_G|SEG_C
#define DISP_I SEG_C
#define DISP_T SEG_F|SEG_E|SEG_D|SEG_G
#define DISP_E SEG_A|SEG_F|SEG_G|SEG_E|SEG_D
#define DISP_H SEG_F|SEG_E|SEG_G|SEG_C|SEG_B
#define DISP_G SEG_A|SEG_B|SEG_C|SEG_D|SEG_G|SEG_F
#define DISP_S SEG_A|SEG_F|SEG_G|SEG_C|SEG_D
#define DISP_A SEG_A|SEG_B|SEG_C|SEG_F|SEG_E|SEG_G
#define DISP_D SEG_B|SEG_G|SEG_E|SEG_D|SEG_C
#define DISP_none 0x00

#define NUM_MESSAGES 4

uint8_t curr_state = STANDBY_STATE, next_state = STANDBY_STATE, prev_state = STANDBY_STATE;
uint8_t message_sel = NUM_MESSAGES;

bool prevButtonStatus = false;
bool longBtnPress = false;
bool shortBtnPress = false;
unsigned long lastDebounce;

/*
 * -----------------------------------------------------------------------------------
 * Wifi settings
 * -----------------------------------------------------------------------------------
 */

char* messages[] = {"Good Morning", "Good Night", "I'm Hungry", "I'm Sad"};
const uint8_t disp_message[][4] = {{DISP_M,DISP_O,DISP_R,DISP_N},{DISP_N,DISP_I,DISP_T,DISP_E},{DISP_H,DISP_N,DISP_G,DISP_R},{DISP_S,DISP_A,DISP_D,DISP_none}};


//Blynk authorization token
char auth[] = "###";

//Wifi credentials
char ssid[] = "###";
char pass[] = "###";

// Define NTP Client to get time
const long utcOffsetInSeconds = -4*3600;
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", utcOffsetInSeconds);


/*
 * -----------------------------------------------------------------------------------
 * Display settings
 * -----------------------------------------------------------------------------------
 */

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

/*
 * ----------------------------------------------------------------------------------
 * Date and time settings
 * ----------------------------------------------------------------------------------
 */

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
Date currDate;
Date startDate = {16,3,2020};
Time currTime;
bool updated = true;
unsigned long ping_timer;
unsigned long ping_timer_start = millis();
unsigned long button_timer;
unsigned long button_timer_start = millis();

/*
 * ----------------------------------------------------------------------------------
 * Blynk Functions
 * ----------------------------------------------------------------------------------
 */
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

/*
 * ----------------------------------------------------------------------------------
 * Setup loop
 * ----------------------------------------------------------------------------------
 */

void setup() {
  // Clear the display and set max brightness
  display.clear();
  display.setBrightness(7);
  
  // Define pin functions
  pinMode(StatusPin, OUTPUT);
  pinMode(D8, OUTPUT);
  pinMode(D5, OUTPUT);

  // Attach interrupt to button pin
  attachInterrupt(digitalPinToInterrupt(ButtonPin), ButtonIntCallback, CHANGE);

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
  int days = getDifference(startDate, currDate);

  // Connect to blynk server
  Blynk.begin(auth, ssid, pass);

  // Update days display on app
  Blynk.virtualWrite(V0, days);

  // Signal that startup was complete
  for(int i=0; i<3; i++){
    digitalWrite(StatusPin, HIGH);
    delay(250);
    digitalWrite(StatusPin, LOW);
    delay(250);
  }
  
  updateDays(days);

  Serial.begin(9600);

}

/*
 * ----------------------------------------------------------------------------------
 * While loop
 * ----------------------------------------------------------------------------------
 */

void loop() {

  // Move states
  if(next_state != curr_state){
    prev_state = curr_state;
    curr_state = next_state;
  }
  
  // Run blynk functions
  Blynk.run();

  // Signal if disconnected from app
  if (!Blynk.connected()){
    digitalWrite(StatusPin, HIGH);
  }
  else{
    digitalWrite(StatusPin, LOW);
  }

  //Update timer
  ping_timer = millis();

  if(WiFi.status() != WL_CONNECTED){
      next_state = OFFLINE_STATE;
      curr_state = OFFLINE_STATE;
  }

  // Idle state
  if(curr_state == STANDY_STATE){
    //Run on timer period
    if(ping_timer-ping_timer_start > PING_TIMER_PERIOD){
      next_state = UPDATE_STATE;
    }
    if(longBtnPress){
      next_state = SELECT_STATE;
    }
    if(shortBtnPress){
      next_state = SEND_MSG_STATE;
    }
    if(WiFi.status() != WL_CONNECTED){
      next_state = OFFLINE_STATE;
    }
  }
  else if(curr_state == UPDATE_STATE){
      // Update time
      timeClient.update();
      Serial.println("Time updated");
    
      // Get current time
      currTime = getTime();
  
      // Update days counter at midnight
      if (currTime.h == 0){
        if (!updated){
          updated = true;
          currDate = getDate();
    
          //Update display on box and app
          int days = getDifference(startDate, currDate);
          updateDays(days);
        }
      }
      else if (updated){
        updated = false;
      }
  
      //Enter deep sleep
      if ((currTime.h == startSleep.h)&&(currTime.m == startSleep.m)){
        display.clear();
        ESP.deepSleep(sleepTime * 1e6);
      }

      // Reset timer
      ping_timer_start = millis();

      next_state = STANDBY_STATE
  }
  else if(curr_state == SELECT_STATE){
    if(message_sel >= NUM_MESSAGES){
      message_sel = 0;
      display.clear();
      display.setSegments(disp_message[message_sel]);
    }
    if(longBtnPress){
      next_state = SEND_MSG_STATE;
    }
    else if(shortBtnPress){
      //Cycle through messages
      if(message_sel < NUM_MESSAGES-1){
        message_sel++;
      }
      else{
        message_sel = 0;
      }
      display.clear();
      display.setSegments(disp_message[message_sel]);
      Serial.println(message_sel);
    }
  }
  else if(curr_state == SEND_MSG_STATE){
    if(message_sel < NUM_MESSAGES){
      Serial.println(messages[message_sel]);
      Blynk.notify(messages[message_sel]);
      updateDays(getDifference(startDate, currDate));
    }
    else{
      Serial.println("I love you! -Jen");
      Blynk.notify("I love you! -Jen");
    }
    next_state = STANDBY_STATE
    
  }
  else if(curr_state == OFFLINE_STATE){
    if (Blynk.connected()){
      next_state = STANDBY_STATE;
    }
  }
  else{
    display.setSegments(error);
    Blynk.notify("State error");
  }

}


/*
 * ----------------------------------------------------------------------------------
 * Interrupt Service Routines
 * ----------------------------------------------------------------------------------
 */

ICACHE_RAM_ATTR void ButtonIntCallback(){
  // Send notification to app when button is pressed
  bool buttonStatus = digitalRead(ButtonPin);

  // Debounce button
  if(buttonStatus != prevButtonStatus){
    lastDebounce = millis();
  }
  if((millis()-lastDebounce)>DEBOUNCE_DELAY){
    //On button press
    if(buttonStatus){
      //Start timer
      button_timer_start = millis();
    }
    //On button release
    if(!buttonStatus){
      button_timer = millis();
  
      //Check if button was held
      if(button_timer - button_timer_start > BUTTON_TIMER_PERIOD){
        //Check if user is selecting messages
        if(curr_state == SELECT_STATE){
          //send selected message
          curr_state = IDLE_STATE;
          Serial.println(messages[message_sel]);
          Blynk.notify(messages[message_sel]);
          updateDays(getDifference(startDate, currDate));
          ping_timer_start = millis();
        }
        else{
          curr_state = SELECT_STATE;
          display.clear();
          display.setSegments(disp_message[message_sel]);
          ping_timer_start = millis();
        }  
      }
      //Button was pressed
      else{
        // Check if user is selecting messages
        if(curr_state == SELECT_STATE){
          //Cycle through messages
          if(message_sel < NUM_MESSAGES-1){
            message_sel++;
          }
          else{
            message_sel = 0;
          }
          Serial.println(message_sel);
  
          //Dispay selected message
          display.clear();
          display.setSegments(disp_message[message_sel]);
  
          //Reset ping timer to check for user inactivity
          ping_timer_start = millis();
        }
        else{
          Serial.println("I love you! -Jen");
          Blynk.notify("I love you! -Jen");
        }
      }
      
    }
  }
  
}

/*
 * ----------------------------------------------------------------------------------
 * Helper Functions
 * ----------------------------------------------------------------------------------
 */
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
  for(int i= 0; i<6; i++){
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

void updateDays(int days){
  display.clear();
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
