#include <Wire.h>

#define FASTLED_INTERNAL // removes pragma message printing FastLED version
#include "FastLED.h"
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
#define REFRESH_PERIOD  16666 // 16.66 ms


//CAPACTIVE TOUCH
#define pinAnalog A7
// Slow down the automatic calibration cooldown
#define offset 2
#if offset > 6
#error "Too big offset value"
#endif


//AMBIENT LIGHT DETECTION
#define photoResistor A6

//BUZZER
#define buzzerPin 3


//SET TIME INPUT BUTTONS
//Falling Edge Detection
#define FE(signal, state) (state=(((state<<1)|(signal&1))&3))==2

/* Rising Edge Detection  */
#define RE(signal, state) (state=(((state<<1)|(signal&1))&3))==1

/*
  This is how the previous macros work:

  << is the left shift operator

  shifting a bit left by n positions
  0 << 1 = 0
  1 << 1 = 2
  will shift a bit 1 position to the left
  the same as multiplying by 2
  1 << 2 = 4 will shift a bit 2 positions to the left
  the same as multiplying by 4 etc

  | is the bitwise OR operator. (binary addition without a carry)
  1 | 1 = 1
  0 | 1 = 1
  1 | 1 = 1
  0 | 0 = 0

  6 | 3 = 7

    101
  | 011
    ---
    111

  & is the bitwise AND operator
  1 & 1 = 1
  1 & 0 = 0
  0 & 1 = 0
  0 & 0 = 0

  3 & 3 = 3
    111
  & 111
    ---
    111

  4 & 3 = 0
    100
  & 011
    ---
    000

  1 & 3 = 1
    001
  & 011
    ---
    001

  So lets start:


  state = 1 initialized as 1
  button is pulled HIGH and is not pressed
  state = (1<<1)|(HIGH&1)&3)
          (2|1) & 3
            3
  state = 3
  3 == 1 is  false

  state = 3
  button is pulled HIGH and is still not pressed
  state = (3<<1)|(HIGH&1)&3)
          (6|1) & 3
            7 & 3
  state = 3
  3 == 1 is false

  state = 3
  button is pulled LOW and is pressed
  state = (3<<1)|(LOW&1)&3)
          (6|0) & 3
            6 & 3
  state = 2
  2 == 1 is false (a falling edge just occured)

  state = 2
  button is pulled LOW and is still pressed
  state = (2<<1)|(LOW&1)&3)
          (4|0) & 3
            4 & 3
  state = 0
  0 == 1 false (no falling edge here, we are now low)

  state = 0
  button is pulled HIGH and is not pressed
  state = (0<<1)|(HIGH&1)&3)
          (0|1) & 3
            1 & 3
  state = 1
  1 == 1 true (a rising edge was detected)

  state = 1
  back to the start
*/

struct Buttons {
  const byte hourSetPin = 9;
  byte hourSetState = 1;

  const byte minuteSetPin = 8;
  byte minuteSetState = 1;

  const byte setAlarmPin = 5;
  byte setAlarmState = 1;

  const byte toggleAlarmOnOffPin = 2;
  byte alarmOnOffState = 1;

  const byte colorSetPin = 4;
  byte colorSetState = 1;

} buttons;

const byte is12h24hJumperPin = 13;

//LED MATRIX WIRING LAYOUT
//led[0][3] Reserved for AM/PM indicator
//led[0][4] Reserved for Alarm ON/OFF indicator
#define AM_PM_LED         3
#define ALARM_ON_OFF_LED  4
#define LED_HOUR_TENS     0
#define LED_HOUR_ONES     1
#define LED_MINUTE_TENS   2
#define LED_MINUTE_ONES   3

// note the layout of the matrix, corresponds a snaked layout on the board
const byte ledMatrix[PIXELS_IN_COL][PIXELS_IN_ROW] = {
  {0, 1, 2, 3, 4},
  {9, 8, 7, 6, 5},
  {10, 11, 12, 13, 14},
  {19, 18, 17, 16, 15}
};


const boolean morseNum[10][5] = {
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

struct Alarm {
  byte alarmHour = 0;
  byte alarmMinute = 0;
  byte snoozeMinutes = 0;

  const byte alarmHourAddr = 0;
  const byte alarmMinuteAddr = 1;
  boolean isBuzzing = 0;
  boolean alarmIsOn = false;

  const byte alarmIsOnAddr = 2;
} alarm;

boolean is24H = true;

RTC_DS1307 rtc;
//char daysOfTheWeek[7][12] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};

CRGB pixels[NUMPIXELS];

//COLOR CONFIG
byte currentColorHue = 0;
byte colorAddress = 3;


unsigned long startTime = 0;


void setup() {
  Serial.begin(BAUD_RATE);    // initialize serial communication
  FastLED.addLeds<WS2812B, LED_DATA_PIN, RGB>(pixels, NUMPIXELS); // This initializes the FAST LED library.

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

  pinMode(buttons.hourSetPin,  INPUT_PULLUP);
  pinMode(buttons.minuteSetPin, INPUT_PULLUP);
  pinMode(buttons.setAlarmPin, INPUT_PULLUP);
  pinMode(buttons.colorSetPin, INPUT_PULLUP);
  pinMode(buttons.toggleAlarmOnOffPin, INPUT_PULLUP);
  pinMode(is12h24hJumperPin, INPUT);


  delay(10); // allow settle time

  //read alarm value from nvram
  alarm.alarmHour = rtc.readnvram(alarm.alarmHourAddr);
  alarm.alarmMinute = rtc.readnvram(alarm.alarmMinuteAddr);
  alarm.alarmIsOn = rtc.readnvram(alarm.alarmIsOnAddr);
  currentColorHue = rtc.readnvram(colorAddress);

  pinMode(buzzerPin, OUTPUT);
  pinMode(ALARM_LED, OUTPUT);

  analogReference(EXTERNAL);

  is24H = digitalRead(is12h24hJumperPin); // Ground is 12h, 24h is High

  startTime = micros();
  delay(100); // allow everything to settle
}


/***********************
       MAIN LOOP
************************/
void loop() {
  unsigned long diff = micros() - startTime;

  //display loop runs ever so often
  if (diff >= REFRESH_PERIOD) {
    startTime = micros();
    changeBrightness();
    DateTime now = rtc.now();

    if ( digitalRead(buttons.setAlarmPin) ) {
      // add flash here when alarm is on
      updatePixels(now.hour(), now.minute());
    }
    else { // if alarmSetPin is pressed (LOW) it will show the alarmSet time
      updatePixels(alarm.alarmHour, alarm.alarmMinute);
    }

    alarmLoop(now);

  }

  evaluateButtons();

}

/***********************
    HELPER FUNCTIONS
************************/
void alarmLoop(DateTime now) {
  if (alarm.alarmIsOn) {
    // what if our alarm is set for 23:55 and we snooze for 5 min it wont go off again, because now we are at 23*60 + 55 + 5 and the current time is 0:04
    //     we mod out calculation
    int totalClockMinutes = ((now.hour() * 60) + now.minute() ) % 1440; // 1440 is number of minutes in a day
    int totalAlarmMinutes = ((alarm.alarmHour * 60) + alarm.alarmMinute + alarm.snoozeMinutes) % 1440;
    // buzz again if set to buzzing or start buzzing when our times are equal
    if ( (alarm.isBuzzing) || ( totalClockMinutes == totalAlarmMinutes ) ) {
      toggleAlarmBuzzing(true);

      if (detectTouch()) {
        startTime = millis();
        while (detectTouch()) {
          unsigned int beenTouching = millis() - startTime;
          if (beenTouching > 1000) {
            toggleAlarmBuzzing(false); // false adds time to snooze and turns off buzzer
            Serial.println("Touched for 2s snoozing for 5 min");
            alarm.snoozeMinutes += 5;
            break; // break out of while
          } // end if
        } // end while
      }// end touched if
    }
  }
}

byte incrementRTCMinute() {
  DateTime now = rtc.now();
  if (now.minute() < 59) {
    rtc.adjust(DateTime(now.unixtime() + 60));
    return now.minute() + 1;
  }
  else {
    rtc.adjust(DateTime(now.unixtime() - 3540));
    return 0;
  }
}

byte incrementRTCHour() {
  DateTime now = rtc.now();
  uint32_t epoch = now.unixtime();
  rtc.adjust(DateTime(epoch + 3600));
  return rtc.now().hour();
}

byte incrementAlarmMinute() {
  alarm.alarmMinute = (alarm.alarmMinute + 1) % 60;
  //  Serial.print("Alarm Minute: ");  Serial.println(alarm.alarmMinute);
  rtc.writenvram(alarm.alarmMinuteAddr, alarm.alarmMinute);
  return alarm.alarmMinute;
}

byte incrementAlarmHour() {
  alarm.alarmHour = (alarm.alarmHour + 1) % 24;
  //  Serial.print("Alarm Hour: "); Serial.println(alarm.alarmHour);
  rtc.writenvram(alarm.alarmHourAddr, alarm.alarmHour);
  return alarm.alarmHour;
}

void evaluateButtons() {

  //alarmSetButton is not low, alarm default HIGH when not pressed
  if ( digitalRead(buttons.setAlarmPin) ) {
    //    Serial.println("Setting Clock");
    if (isSetMinutePressed()) {
      incrementRTCMinute();
    }

    if (isSetHourPressed()) {
      incrementRTCHour();
    }
  }
  else {
    //    Serial.println("Setting alarm");
    if (isSetMinutePressed()) {
      incrementAlarmMinute();
      setRTCRam(alarm.alarmMinuteAddr, alarm.alarmMinute);
    }

    if (isSetHourPressed()) {
      incrementAlarmHour();
      setRTCRam(alarm.alarmHourAddr, alarm.alarmHour);
    }
  }

  //TOGGLE ALARM
  if (isToggleAlarmOnOffPressed()) {
    alarm.alarmIsOn = !alarm.alarmIsOn;
    setRTCRam(alarm.alarmIsOnAddr, alarm.alarmIsOn);
    if (!alarm.alarmIsOn) {
      toggleAlarmBuzzing(false);
      alarm.snoozeMinutes = 0;
    }
  }

  if (isCycleColorPressed()) {
    //rotate through colors
    currentColorHue = currentColorHue + 5;
    setRTCRam(colorAddress, currentColorHue);
  }

}

void toggleAlarmBuzzing(boolean buzz) {
  if (buzz) {
    alarm.isBuzzing = true;
    tone(buzzerPin, 24);

    // is this ms blocking?
  } else {
    alarm.isBuzzing = false;
    noTone(buzzerPin);
  }
}

int readAmbientLight() {
  //  10 dark - 600 light - 1000 flash
  return analogRead(photoResistor);
}

boolean isSetHourPressed() {
  //all buttons are active LOW
  boolean touched = RE(digitalRead(buttons.hourSetPin), buttons.hourSetState);
  if (touched)
    Serial.println("Set Hour Pressed");

  return touched;
}

boolean isSetMinutePressed() {

  boolean touched = RE(digitalRead(buttons.minuteSetPin), buttons.minuteSetState);
  if (touched)
    Serial.println("Set minute Pressed");

  return touched;
}


boolean isSetAlarmPressed() {
  return RE(digitalRead(buttons.setAlarmPin), buttons.setAlarmState);
}


boolean isCycleColorPressed() {
  return RE(digitalRead(buttons.colorSetPin), buttons.colorSetState);
}

boolean isToggleAlarmOnOffPressed() {
  return RE(digitalRead(buttons.toggleAlarmOnOffPin), buttons.alarmOnOffState);
}

bool detectTouch() {
  // No second parameter will use 1 sample
  uint16_t value = analogTouchRead(pinAnalog, 100);
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
  //          Serial.print(touched);
  //          Serial.print("\t");

  // Print calibrated value
  //          Serial.print(value - (ref >> offset));
  //          Serial.print("\t");

  // Print raw value
  //          Serial.print(value);
  //          Serial.print("\t");

  // Print raw ref
  //          Serial.print(ref >> offset);
  //          Serial.print("\t");
  //          Serial.println(ref);

  return touched;
}

void setRTCRam(byte address, byte data) {
  rtc.writenvram(address, data);
}

void updatePixels(byte hour, byte minute) {
  // minute is always the same for either 24/12
  setPixelMinute(minute);
  // set time like normal
  setPixelHour(hour);
  setAMPMPixel(0);

  // there is a special case here, overrides previous set
  if (!is24H) {
    if (hour > 12) {
      setPixelHour(hour - 12);
      // AM/PM LED is on for PM
      setAMPMPixel(1);
    }

    if (hour == 0) {
      setPixelHour(12);
      setAMPMPixel(0);
    }
  }

  // update alarm on pixel according to alarm.alarmIsOn state
  setAlarmOnPixel();

  FastLED.show();
}

void setAlarmOnPixel() {
  setPixelState( ALARM_ON_OFF_LED , alarm.alarmIsOn); // alarmSetPixel
}

void setAMPMPixel(boolean ledState) {
  setPixelState( AM_PM_LED , ledState);
}

void changeBrightness() {
  int oldAmbientLight = readAmbientLight();
  //      Serial.println("OLD Light: ");
  //      Serial.print(oldAmbientLight);

  int newAmbientLight = map(oldAmbientLight, 0, 1024, 1, 65);
  //      Serial.println("NEW Light: ");
  //      Serial.println(newAmbientLight);


  FastLED.setBrightness(newAmbientLight);
  FastLED.show();
}

void setPixelMinute(int minute) {
  //  Serial.print("Setting Minute To: ");
  //  Serial.println(minute);
  int ones = minute % 10;
  int tens = (minute / 10) % 10;

  //set minute's tens
  for (int i = 0; i < PIXELS_IN_ROW; ++i) {
    //do a lookup of the LED number in the ledMatrix table
    setPixelState(ledMatrix[LED_MINUTE_TENS][i], morseNum[tens][i]); //minutes are pixel [10:14]
    setPixelState(ledMatrix[LED_MINUTE_ONES][i], morseNum[ones][i]); //minutes are pixel [19:15]
  }

}


void setPixelHour(int hour) {
  //  Serial.print("Setting hour To: ");
  //  Serial.println(hour);
  int ones = hour % 10;
  int tens = (hour / 10) % 10;

  //set hour's tens; [2:4] are reserved
  setPixelState(ledMatrix[LED_HOUR_TENS][0], morseNum[tens][0]); //hours are pixel [0:1]
  setPixelState(ledMatrix[LED_HOUR_TENS][1], morseNum[tens][1]); //hours are pixel [0:1]

  for (int i = 0; i < PIXELS_IN_ROW; ++i) {
    setPixelState(ledMatrix[LED_HOUR_ONES][i], morseNum[ones][i]); //hours are pixel [9:5]
  }

}


void setPixelState(int i, boolean ledState) {
  if (ledState) {
    pixels[i].setHue(currentColorHue);
  } else {
    pixels[i] = CRGB::Black; //off
  }
}

