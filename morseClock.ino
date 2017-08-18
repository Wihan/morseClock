#include <Wire.h>
#include <Adafruit_NeoPixel.h>
#include <AnalogTouch.h>
#include <RTClib.h>

#include <avr/wdt.h>


//PROJECT CONSTANTS
#define BOARD_LED     LED_BUILTIN
#define ALARM_LED     5
#define NUMPIXELS     20
#define PIXELS_IN_ROW 5
#define PIXELS_IN_COL 4

//UART BAUD/s
#define BAUD_RATE 115200

//LED MATRIX CONFIG
#define LED_DATA_PIN  11


//CLOCK CONSTANTS
#define MILLIS_PER_SEC  1000
#define MICROS_PER_SEC  100000
//#define MICROS_PER_SEC  1000000


//CAPACTIVE TOUCH
#define pinAnalog A7
// Slow down the automatic calibration cooldown
#define offset 5
#if offset > 6
#error "Too big offset value"
#endif


//AMBIENT LIGHT DETECTION
#define photoResistor A0

//BUZZER
#define buzzerPin 6
boolean isBuzzing = 0;

//SET TIME INPUT BUTTONS
//Falling Edge Detection
#define FE(signal, state) (state=(state<<1)|(signal&1)&3)==2

//Rising Edge Detection
#define RE(signal, state) (state=(state<<1)|(signal&1)&3)==1

const byte hourSetPin = 9;
byte hourSetState;

const byte minuteSetPin = 8;
byte minuteSetState;

const byte alarmSetPin = 5;
byte alarmSetState;

const byte colorSetPin = 4;
byte colorSetState;

const byte alarmOnOffPin = 2;
byte alarmOnOffState;


//LED MATRIX WIRING LAYOUT
//led[0][3] Reserved for AM/PM indicator
//led[0][4] Reserved for Alarm ON/OFF indicator
#define LED_HOUR_TENS 0
#define LED_HOUR_ONES 1
#define LED_MINUTE_TENS 2
#define LED_MINUTE_ONES 3

int ledMatrix[PIXELS_IN_COL][PIXELS_IN_ROW] = {
  {0, 1, 2, 3, 4},
  {9, 8, 7, 6, 5},
  {10, 11, 12, 13, 14},
  {19, 18, 17, 16, 15}
};


int morseNum[10][5] = {
  {0, 0, 0, 0, 0}, //0
  {1, 0, 0, 0, 0}, //1
  {1, 1, 0, 0, 0}, //2
  {1, 1, 1, 0, 0}, //3
  {1, 1, 1, 1, 0}, //4
  {1, 1, 1, 1, 1}, //5
  {0, 1, 1, 1, 1}, //6
  {0, 0, 1, 1, 1}, //7
  {0, 0, 0, 1, 1}, //8
  {0, 0, 0, 0, 1}  //9
};

//CAP TOUCH REFs
int ref0, ref1;

//CLOCK VARIABLES
byte hours;
byte minutes;
byte seconds;

byte alarmHours = 8;
byte alarmMinutes = 0;
boolean alarmOn = false;
boolean is24H = true;

unsigned long diff;
unsigned long startTime;

RTC_DS1307 rtc;
char daysOfTheWeek[7][12] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};



Adafruit_NeoPixel pixels = Adafruit_NeoPixel(NUMPIXELS, LED_DATA_PIN, NEO_RGB + NEO_KHZ800);

//COLOR CONFIG
#define TOTAL_COLORS  7
uint32_t colorList[] = {
  pixels.Color(255, 0, 0),    //RED
  pixels.Color(255, 255, 0),  //YELLOW
  pixels.Color(0, 255, 0),    //GREEN
  pixels.Color(0, 255, 255),  //CYAN
  pixels.Color(0, 0, 255),    //BLUE
  pixels.Color(255, 0, 255),  //MAGENTA
  pixels.Color(255, 255, 255) //WHITE
};
byte selectedColor = 0;

void setup() {
  Serial.begin(BAUD_RATE);    // initialize serial communication
  pixels.begin(); // This initializes the NeoPixel library.

  if (! rtc.begin()) {
    Serial.println("Couldn't find RTC");
    while (1);
  }

  if (! rtc.isrunning()) {
    Serial.println("RTC is NOT running!");
    // following line sets the RTC to the date & time this sketch was compiled
     rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
    // This line sets the RTC with an explicit date & time, for example to set
    // January 21, 2014 at 3am you would call:
    // rtc.adjust(DateTime(2014, 1, 21, 3, 0, 0));
  }

  pinMode(hourSetPin, INPUT_PULLUP);
  pinMode(minuteSetPin, INPUT_PULLUP);
  pinMode(alarmSetPin, INPUT_PULLUP);
  pinMode(colorSetPin, INPUT_PULLUP);
  pinMode(alarmOnOffPin, INPUT_PULLUP);

  alarmSetState = 1;
  hourSetState = 1;
  minuteSetState = 1;
  colorSetState = 1;
  alarmOnOffState = 1;
  
  pinMode(buzzerPin, OUTPUT);
  pinMode(BOARD_LED, OUTPUT);
  pinMode(ALARM_LED, OUTPUT);

  analogReference(EXTERNAL);
  
  hours = 0;
  minutes = 0;
  seconds = 0; //clock defaults
  diff = 0;
  startTime = micros();
  delay(1000); // allow everything to settle
}

void loop() {
  bool touched = detectTouch();
  updatePixelsWithRTCTime();
  evaluateButtons();
  readAmbientLight(); 
 
}

void evaluateButtons() {

  //alarmSetButton is not low, alarm default HIGH when not pressed
  if ( digitalRead(alarmSetPin) ) {
//    Serial.println("Setting Clock");
    if (isSetMinutePressed()) {
      uint32_t now = rtc.now().unixtime();
      rtc.adjust(DateTime(now+60));
    }

    if (isSetHourPressed()) {
      uint32_t now = rtc.now().unixtime();
      rtc.adjust(DateTime(now+3600));
    }
    //write new hour and minute value to RTC
  }
  else {
//    Serial.println("Setting alarm");
    if (isSetMinutePressed()) {
      ++alarmMinutes;
      Serial.print("Alarm Minutes: ");
      Serial.println(alarmMinutes);
    }

    if (isSetHourPressed()) {
      ++alarmHours;
      Serial.print("Alarm Hours: ");
      Serial.println(alarmHours);
    }
  }

  //TOGGLE ALARM
  if (isToggleAlarmPressed()) {
    alarmOn = !alarmOn;
//    isBuzzing = !isBuzzing;
//    activateBuzzer(isBuzzing);
    Serial.print("Alarm ON?: ");
    Serial.println(alarmOn);
  }

  if (isCycleColorPressed()) {
    //rotate through colors
    selectedColor = (selectedColor + 1) % TOTAL_COLORS;
    Serial.print("Color: ");
    Serial.println(selectedColor);
  }

  // toggle 24h, 12h
  if (digitalRead(hourSetPin) && isSetMinutePressed() ) {
    is24H = !is24H;
    if (!is24H) {
      if (alarmHours > 12) {
        alarmHours = alarmHours - 12;
      }
    }
  }
//delay(100);
}

void activateBuzzer(boolean buzz) {
  if (buzz) {
    tone(buzzerPin,440,250);  
  } else {
    noTone(buzzerPin);
  }

}

int readAmbientLight() {
//  10 dark - 600 light - 1000 flash 
  return analogRead(photoResistor);  
}

boolean isSetHourPressed() {
  //all buttons are active LOW
  return RE(digitalRead(hourSetPin), hourSetState);
}

boolean isSetMinutePressed() {
  return RE(digitalRead(minuteSetPin), minuteSetState);
}


boolean isSetAlarmPressed() {
  return RE(digitalRead(alarmSetPin), alarmSetState);
}


boolean isCycleColorPressed() {
  return RE(digitalRead(colorSetPin), colorSetState);
}


boolean isToggleAlarmPressed() {
  return RE(digitalRead(alarmOnOffPin), alarmOnOffState);
}


void updatePixelsWithRTCTime() {
   diff = micros() - startTime;

  if (diff >= MICROS_PER_SEC) {
    startTime = micros();
    DateTime now = rtc.now();
    Serial.println(now.second());
    setMinute(now.minute());
    if (!is24H) {
      if (now.hour() > 12) {
        setHour(now.hour()-12);
      }
    }  
    else {
      setHour(now.hour());    
    }

    int oldAmbientLight = readAmbientLight();
    Serial.println("Ambient Light: ");
    
    int newAmbientLight = map(oldAmbientLight, 0, 1024, 10, 255);
    Serial.println(newAmbientLight);
    
    pixels.setBrightness(newAmbientLight);
    pixels.show();
  }
}

// use internal clock
void internalClock() {
  diff = micros() - startTime;

  if (diff >= MICROS_PER_SEC) {
    startTime = micros();
    seconds += 1;
    Serial.println(diff);
    setMinute(minutes);
    setHour(hours);
  }

  if (seconds == 60) {
    seconds = 0;
    minutes += 1;
    setMinute(minutes);
  }

  if (minutes == 60) {
    minutes = 0;
    hours += 1;
    setMinute(minutes);
    setHour(hours);
  }

  if ((hours == 24 && is24H) || (hours == 12 && !is24H) ) {
    hours = 0;
    setHour(hours);
  }
  pixels.show();
}

bool detectTouch() {
  // No second parameter will use 1 sample
  uint16_t value = analogTouchRead(pinAnalog, 10);
  //value = analogTouchRead(pinAnalog, 100);

  // Self calibrate
  static uint16_t ref = 0xFFFF;
  if (value < (ref >> offset))
    ref = (value << offset);
  // Cool down
  else if (value > (ref >> offset))
    ref++;

  // Print touched?
  bool touched = (value - (ref >> offset)) > 20;
//    Serial.print(touched);
//    Serial.print("\t");

  // Print calibrated value
//    Serial.print(value - (ref >> offset));
//    Serial.print("\t");

  // Print raw value
//    Serial.print(value);
//    Serial.print("\t");

  // Print raw ref
//    Serial.print(ref >> offset);
//    Serial.print("\t");
//    Serial.println(ref);

  //Light Up When Touched
  digitalWrite(BOARD_LED, touched);

  return touched;
}

void setMinute(int minute) {
  Serial.print("Setting Minute To: ");
  Serial.println(minute);
  int ones = minute % 10;
  int tens = (minute / 10) % 10;

  //set minute's tens
  for (int i = 0; i < PIXELS_IN_ROW; ++i) {
    //do a lookup of the LED number in the ledMatrix table
    setPixelState(ledMatrix[LED_MINUTE_TENS][i], morseNum[tens][i]); //minutes are pixel [10:14]
    setPixelState(ledMatrix[LED_MINUTE_ONES][i], morseNum[ones][i]); //minutes are pixel [19:15]
  }

}


void setHour(int hour) {
  Serial.print("Setting hour To: ");
  Serial.println(hour);
  int ones = hour % 10;
  int tens = (hour / 10) % 10;

  //set hour's tens; [2:4] are reserved
  setPixelState(ledMatrix[LED_HOUR_TENS][0], morseNum[tens][0]); //hours are pixel [0:1]
  setPixelState(ledMatrix[LED_HOUR_TENS][1], morseNum[tens][1]); //hours are pixel [0:1]

  for (int i = 0; i < PIXELS_IN_ROW; ++i) {
    setPixelState(ledMatrix[LED_HOUR_ONES][i], morseNum[ones][i]); //hours are pixel [9:5]
  }

}

void setPixelState(int i, int ledState) {
  if (ledState) {
    pixels.setPixelColor(i, colorList[selectedColor]); //white
  } else {
    pixels.setPixelColor(i, pixels.Color(0, 0, 0)); //off
  }
}





