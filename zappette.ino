/***
   Zappette Robot Alarm Clock
   12/07/2018
   Kevin Stephens

  Todo:
  - Add override off with knife switch A6 on shiftin
  - Add sounds
  - Randomize mouth on each new song
  - Rewire alarm switch to independent power
  - Stop preloading clock
  
 ***/
// Load libraries
#include <Wire.h>
#include <Arduino.h>
#include <SoftwareSerial.h>
#include <DFRobotDFPlayerMini.h>
#include <Adafruit_GFX.h>
#include <Adafruit_LEDBackpack.h>
#include <DS1307new.h>
#include <NewPing.h>
#include <millisDelay.h>
#include <DebounceInput.h>

// Define the matrixes
Adafruit_8x16matrix mouth = Adafruit_8x16matrix(); // Mouth
Adafruit_8x8matrix lEye   = Adafruit_8x8matrix(); // left eye
Adafruit_8x8matrix rEye   = Adafruit_8x8matrix(); // right eye
Adafruit_7segment clck    = Adafruit_7segment(); // Clock

// User Settings
#define snzTime 1         // amount of mins for the initial snooze
#define snzDegrade 1      // amount of mins for to degrade time by
#define snzLmt 1          // number of times to loop before degrading
#define snzUnlmt 1        // set to 1 if you want unlimited snoozing or zero for degraded snooze
#define sleepHour 20      // Hour at which system goes to sleep (meaning we close eyes and stop making sounds)
#define wakeHour 7        // Hour at which system wakes up
#define QSIZE 10          // Size of the music queue to randomize
#define EYEFRAMESPEED 75  // The speed of frames for eye animation
#define MAX_DISTANCE 200  // Maximum distance we want to ping for (in centimeters). Maximum sensor distance is rated at 400-500cm.
#define MAX_VOLUME 30     // Maximum volume. Too high causes short overload
// Set up our pins
#define volPin 0     // Analog pin 0
#define brPin 1      // Analog pin 1
#define pingTPin 2   // Ping sensor trigger pin
#define pingEPin 3   // Ping sensor echo pin
#define iLedPin 4    // Mosfet for internal LED strip
#define snzLedPin 5  // Mosfet for 9V to snooze button LED
#define BLPin 6      // Latch pin for button shiftIn (CD4021B)
#define BCPin 7      // Clock pin for button shiftIn (CD4021B)
#define BDPin 8      // Data pin for button shiftIn (CD4021B)
#define SLPin 9      // Latch pin for 7 segment shiftOut (74HC595)
#define SCPin 10     // Clock pin for 7 segment shiftOut (74HC595)
#define SDPin 11     // Data pin for for 7 segment shiftOut (74HC595)
#define mTPin 12     // TX pin for mp3 module (FN-M16P)
#define mRPin 13     // RX pin for mp3 module (FN-M16P)

// Digits for 7 segment display
const int D[10] = {63, 6, 91, 79, 102, 109, 125, 7, 127, 111};

// Sonar variables
NewPing sonar(pingTPin, pingEPin, MAX_DISTANCE); // NewPing setup of pins and maximum distance.
millisDelay sonarTimer;
millisDelay sonarAngryTimer;
int sonarLast = 0; // Last reading for sonar

// Clock  variables
uint32_t alarmSec;           // Holds the nunber of seconds since 2000 from RTC for snoozing
byte alarmOn = 0;            // Alarm status
byte lSec;                  // Last second
byte snzCnt;                // Number of times snoozed
byte snzTimeCur = snzTime;  // current length of the snooze
boolean sCol;               // Used to show the colon on the clock
boolean isPM;               // Use to show the PM dot on the clock
int  dHr;                   // Display hour;
int  dMn;                   // Display minute;
DebounceFilter alarmSwt;
int  alarmFile;             // Number for the alarm file to play
int  alarmFileL = 99;       // previous state
int  alarmHr = 7;           // Alarm hour
int  alarmMn = 0;            // Alarm minutes
DebounceFilter snzBtn;      // Snooze button
DebounceFilter hrBtn;       // Hour button
unsigned long hrBtnT;       // long press time
DebounceFilter mnBtn;       // Minute button
unsigned long mnBtnT;       // debounce time
DebounceFilter setABtn;
DebounceFilter setTBtn;

// MP3 Buttons
DebounceFilter playBtn;
DebounceFilter nextBtn;
DebounceFilter prevBtn;
DebounceFilter stopBtn;

// MP3 variables
int volVal = 0; // Volume
int m1st;         // First file for music
int mCnt;         // The count of files for music
int qS;           // Queue start
int qP = 0;       // Queue Position
int lQ[2];        // Last queue
int mQ[QSIZE];    // Music queue
bool isMusic;     // Are we playing music?
bool isStopMouth; // Are we looping a sound and shouldn't stop mouth animations?
millisDelay songTimer; // Time since we started playing song at.

// Face variables
millisDelay mouthAnimTimer;
int isMouthAnim;                 // Should we scroll
int mouthScrollPos = 15;         // Position of the scroll
int mouthCycleIndex;             // Index for the drawMouthCycle
int mouthFrameCnt;               // Number of frames in the currnt map
int mouthFrameSpeed = 100;       // Delay between frames when scrolling bitmaps
bool mouthFrameRandom = false;   // Should we pick a random frame when scrolling bitmaps
bool isMouthRepeat;              // Should we loop when animating the mouth
String mouthScrollString;        // Text to scroll
static const int *mouthFrameMap; // Pointer to array frames for the mouth
bool mouthBarGraphDir;           // Direction we are going for scrolling bar graph
int mouthBarGraphArr[16];        // Holder for scrolling bar graph data
int bitmapIndex[] = {0, 0, 0};   // Holder for scrolling bitmap indexes
int bitmapX[]     = {0, 8, 16};  // Holder for scrolling bitmap positions

millisDelay eyeAnimTimer;
bool isEyeAnim;                    // Should we animate eyes
int eyeAnimPos;                    // Position of the animation
int eyeFrameSpeed = EYEFRAMESPEED; // Temporary override for frame rate
int eyeFrameCnt;                   // Number of frames for current pointer
static const int *rEyeFrameMap;    // Pointer to array of right frames Mp
static const int *lEyeFrameMap;    // Pointer to array of left frames Mp

// Misc
bool PIRon;      // PIR sensor state
bool PIRlast;    // PIR last state
int brtn = 0;    // Brightness
bool isAsleep;   // True if it is sleep hours
bool isShutdown; // True if we are shutdown (no face no sounds)
DebounceFilter shutdownBtn;
 
// Connect to the MP3 player
SoftwareSerial mp3Ser(mTPin, mRPin); // RX, TX
DFRobotDFPlayerMini mp3;
void handleMP3msg(uint8_t type, int value);

/*
   Eye Animation frames and Mps
*/
static const int closeMap[]     = {0, 1, 2, 3};
static const int openMap[]      = {3, 2, 1, 0};
static const int blinkMap[]     = {0, 1, 2, 3, 2, 1, 0};
static const int winkOpenMap[]  = {0, 0, 0, 0, 0, 0, 0};
static const int rollUpMap[]    = {0, 4, 8, 0, 4, 8, 0, 4, 8, 0};
static const int rollSideMap[]  = {0, 6, 10, 0, 6, 10, 0, 6, 10, 0};
static const int rSpinMap[]     = {0, 4, 5, 6, 7, 8, 9, 10, 11, 4, 5, 6, 7, 8, 9, 10, 11, 4, 0};
static const int lSpinMap[]     = {0, 4, 11, 10, 9, 8, 7, 6, 5, 4, 11, 10, 9, 8, 7, 6, 5, 4, 0};
static const int searchMap[]    = {0, 4, 0, 8, 0, 10, 0, 6, 0, 4, 0, 8, 0, 10, 0, 6, 0};
static const int centerMap[]    = {0};
static const int upMap[]        = {0, 4};
static const int downMap[]      = {0, 8};
static const int rightMap[]     = {0, 6};
static const int leftMap[]      = {0, 10};
static const int upRightMap[]   = {0, 5};
static const int downRightMap[] = {0, 7};
static const int upLeftMap[]    = {0, 11};
static const int downLeftMap[]  = {0, 9};
static const int angryLeftMap[] = {0, 13};
static const int angryRightMap[] = {0, 12};

static const byte eyeFrames[][8] PROGMEM =
{
  // 0. full open
  {
    B00111100,
    B01000010,
    B10011001,
    B10111101,
    B10111101,
    B10011001,
    B01000010,
    B00111100
  },
  // 1. starting closed
  {
    B00000000,
    B00111100,
    B01000010,
    B10011001,
    B10011001,
    B01000010,
    B00111100,
    B00000000
  },
  // 2. almost closed
  {
    B00000000,
    B00000000,
    B01111110,
    B10000001,
    B10000001,
    B01111110,
    B00000000,
    B00000000
  },
  // 3. closed
  {
    B00000000,
    B00000000,
    B00000000,
    B11111111,
    B11111111,
    B00000000,
    B00000000,
    B00000000
  },
  // 4. look up
  {
    B00111100,
    B01011010,
    B10111101,
    B10111101,
    B10011001,
    B10000001,
    B01000010,
    B00111100
  },
  // 5. look upRight
  {
    B00111100,
    B01001110,
    B10011111,
    B10011111,
    B10001101,
    B10000001,
    B01000010,
    B00111100
  },
  // 6. look right
  {
    B00111100,
    B01000010,
    B10001101,
    B10011111,
    B10011111,
    B10001101,
    B01000010,
    B00111100
  },
  // 7. look downRight
  {
    B00111100,
    B01000010,
    B10000001,
    B10001101,
    B10011111,
    B10011111,
    B01001110,
    B00111100
  },
  // 8. look down
  {
    B00111100,
    B01000010,
    B10000001,
    B10011001,
    B10111101,
    B10111101,
    B01011010,
    B00111100
  },
  // 9. look downLeft
  {
    B00111100,
    B01000010,
    B10000001,
    B10110001,
    B11111001,
    B11111001,
    B01110010,
    B00111100
  },
  // 10. look left
  {
    B00111100,
    B01000010,
    B10110001,
    B11111001,
    B11111001,
    B10110001,
    B01000010,
    B00111100
  },
  // 11. look upLeft
  {
    B00111100,
    B01110010,
    B11111001,
    B11111001,
    B10110001,
    B10000001,
    B01000010,
    B00111100
  },
  // 12. Angry right
  {
    B00110000,
    B01001000,
    B10011100,
    B10111110,
    B10111101,
    B10011001,
    B01000010,
    B00111100
  },
  // 13. Angry left
  {
    B00001100,
    B00010010,
    B00111001,
    B01111101,
    B10111101,
    B10011001,
    B01000010,
    B00111100
  },
};
/*
   End eye animation
*/
/*
   Mouth animationmn
*/
static const int mouthAsleepMap[]        = {0,1};
static const int mouthAngryMap[]         = {2,3};
static const int mouthSineMap[]          = {4, 5}; // Sinewav
static const int mouthClosedMap[]        = {6,7};
static const int mouthSpeakLipsMap[]     = {6,7,8,9,10,11,12,13,10,11,8,9};
static const int mouthTongueMap[]        = {6,7,14,15,16,17,16,17,14,15,8,9,6,7};
static const int mouthSpeakDiamondMap[]  = {18,19,20,21,22,23,24,25,22,23,20,21};
static const int mouthSnoozeMap[]        = {26,27};
static const int mouthMusicMap[]         = {28,29,30,31,32};

static const byte mouthFrames[][8] PROGMEM =
{
  // 0. Left asleep
  {
    B00000000,
    B00000000,
    B00000000,
    B00001111,
    B00000000,
    B00000000,
    B00000000,
    B00000000
  },
  // 1. Right asleep
  {
    B00000000,
    B00000000,
    B00000000,
    B11110000,
    B00000000,
    B00000000,
    B00000000,
    B00000000
  },
  // 2. angry mouth
  {
    B10010010,
    B10010010,
    B11111111,
    B10010010,
    B10010010,
    B11111111,
    B10010010,
    B10010010
  },
  // 3. angry mouth
  {
    B01001001,
    B01001001,
    B11111111,
    B01001001,
    B01001001,
    B11111111,
    B01001001,
    B01001001
  },
  // 4. Sine wave
  {
    B00000000,
    B00111100,
    B01000010,
    B10000001,
    B00000000,
    B00000000,
    B00000000,
    B00000000
  },
  // 5. Sine wave
  {
    B00000000,
    B00000000,
    B00000000,
    B00000000,
    B10000001,
    B01000010,
    B00111100,
    B00000000
  },
  // 6. Lips
  {
    B00000000,
    B00000000,
    B11000000,
    B01111111,
    B00111111,
    B00000000,
    B00000000,
    B00000000
  },
  {
    B00000000,
    B00000000,
    B00000011,
    B11111110,
    B11111100,
    B00000000,
    B00000000,
    B00000000
  },
  {
    B00000000,
    B00000000,
    B11000000,
    B01111111,
    B00100000,
    B00011111,
    B00000000,
    B00000000
  },
  {
    B00000000,
    B00000000,
    B00000011,
    B11111110,
    B00000100,
    B11111000,
    B00000000,
    B00000000
  },
  {
    B00000000,
    B00000000,
    B11000000,
    B01111111,
    B00100000,
    B00010000,
    B00001111,
    B00000000
  },
  {
    B00000000,
    B00000000,
    B00000011,
    B11111110,
    B00000100,
    B00001000,
    B11110000,
    B00000000
  },
  {
    B00000000,
    B00000000,
    B11000000,
    B01111111,
    B00100000,
    B00010000,
    B00001111,
    B00000000
  },
  // 13. Lips last
  {
    B00000000,
    B00000000,
    B00000011,
    B11111110,
    B00000100,
    B00001000,
    B11110000,
    B00000000
  },
  // 14. Tongue out
  {
    B00000000,
    B00000000,
    B11000000,
    B01111111,
    B00100111,
    B00011111,
    B00000011,
    B00000000
  },
  {
    B00000000,
    B00000000,
    B00000011,
    B11111110,
    B11100100,
    B11111000,
    B11000000,
    B00000000
  },  
  {
    B00000000,
    B00000000,
    B11000000,
    B01111111,
    B00100111,
    B00011111,
    B00000111,
    B00000011
  },
  {
    B00000000,
    B00000000,
    B00000011,
    B11111110,
    B11100100,
    B11111000,
    B11100000,
    B11000000
  },
  // 18. Diamond
  {
    B00000000,
    B00000000,
    B00000000,
    B01111111,
    B00000000,
    B00000000,
    B00000000,
    B00000000
  }, {
    B00000000,
    B00000000,
    B00000000,
    B11111110,
    B00000000,
    B00000000,
    B00000000,
    B00000000
  }, {
    B00000000,
    B00000000,
    B00011111,
    B01111111,
    B00011111,
    B00000000,
    B00000000,
    B00000000
  }, {
    B00000000,
    B00000000,
    B11111100,
    B11111111,
    B11111100,
    B00000000,
    B00000000,
    B00000000
  }, {
    B00000000,
    B00001111,
    B00111111,
    B11111111,
    B00111111,
    B00001111,
    B00000000,
    B00000000
  }, {
    B00000000,
    B11110000,
    B11111100,
    B11111111,
    B11111100,
    B11110000,
    B00000000,
    B00000000
  }, {
    B00000011,
    B00001111,
    B00111111,
    B11111111,
    B00111111,
    B00001111,
    B00000011,
    B00000000
  },
  // 25. diamond last
  {
    B11000000,
    B11110000,
    B11111100,
    B11111111,
    B11111100,
    B11110000,
    B11000000,
    B00000000
  },
  // 26. Snooze
  {
    B01111110,
    B00000100,
    B00001000,
    B00010000,
    B00100000,
    B01111110,
    B00000000,
    B00000000
  },{
    B00000000,
    B00000000,
    B01111110,
    B00000100,
    B00001000,
    B00010000,
    B00100000,
    B01111110
  },
  //28. music
  {
    B00000010,
    B00000010,
    B00100110,
    B00101010,
    B00100100,
    B01100000,
    B10100000,
    B01000000
  },
  {
    B00000100,
    B00101010,
    B00100100,
    B00100000,
    B01100000,
    B11100000,
    B01000000,
    B00000000
  },
  {
    B00100000,
    B00100000,
    B00100100,
    B01101010,
    B11101100,
    B01001000,
    B00001000,
    B00001000
  },
  {
    B00111110,
    B00100010,
    B00111110,
    B00100010,
    B01100110,
    B11101110,
    B01000100,
    B00000000
  },
  // 30. music last
  {
    B00000000,
    B00111110,
    B00100010,
    B00100010,
    B00100010,
    B01100110,
    B11101110,
    B01000100
  },

};
/*
   End mouth animation
*/
void setup() {
  // Open a serial for debugging
  Serial.begin(115200);
  Serial.println(F("Zappette is alive!"));
  Serial.println(F("------------------"));

  // Setup random seed
  randomSeed(analogRead(2));

  // Setup the pins
  pinMode(iLedPin, OUTPUT);
  pinMode(snzLedPin, OUTPUT);
  pinMode(SLPin, OUTPUT);
  pinMode(SCPin, OUTPUT);
  pinMode(SDPin, OUTPUT);
  pinMode(BLPin, OUTPUT);
  pinMode(BCPin, OUTPUT);
  pinMode(BDPin, INPUT);

  // Setup the clock
  RTC.fillByYMD(2018, 4, 1);
  RTC.fillByHMS(6, 59, 57);
  RTC.setTime();
  RTC.startClock();
  clck.begin(0x77);

  mouth.begin(0x70);
  mouth.setTextSize(1);
  mouth.setTextWrap(false);
  mouth.setTextColor(LED_ON);
  lEye.begin(0x75);
  rEye.begin(0x71);
  rEye.setRotation(1);
  lEye.setRotation(1);

  // Initialize MP3 player
  mp3Ser.begin(9600);
  if (!mp3.begin(mp3Ser)) {  //Use softwareSerial to communicate with mp3.
    Serial.println(F("Unable to begin:"));
    while (true) {
      delay(0); // Code to compatible with ESP8266 watch dog.
    }
  }
  // Get some counts for the music randomizer. Only need to do it once per startup
  m1st = mp3.readFileCountsInFolder(1) + mp3.readFileCountsInFolder(2) + 1;
  mCnt = mp3.readFileCountsInFolder(3);
  // Set the volume immediately
  chkVolume();
  // Randomize the music folder
  randMusic();

  // Setup some millisDelay timers
  eyeAnimTimer.start(EYEFRAMESPEED);
  mouthAnimTimer.start(mouthFrameSpeed);
  sonarTimer.start(100);
  mouthScrollText(F("Zappette is alive!"),false);
}

void loop() {
  readButtons();
  writeAlarmNumber();
  chkSnzLed();
  chkButtons();
  chkShutdown();
  chkVolume();
  chkBright();

  RTC.getTime();
  if ( RTC.second != lSec )
  {
    lSec = RTC.second; // Not the last second so we can update time every second
    if (!isEyeAnim && RTC.second % 10 == 0 && !isAsleep && !alarmOn && !isShutdown)
    {
      eyeRandomAct(); // Animate the eyes every 30 seconds
    }
    printFullTime();
    printTime();
    chkSleep();
  }

  chkAlarm();
  chkAlarmSwitch();
  chkMP3msg();
  if (!isAsleep && !alarmOn && !isShutdown) {
    chkPIR();
    chkSonar();
  }
  chkMouthAnim();
  chkEyeAnim();
}

void chkShutdown()
{
  if (shutdownBtn.stateChanged())
  { 
    if (shutdownBtn.state())
    {
      isShutdown = true;
    } else {
      isShutdown = false;
      eyeOpen();
      stopMouth();
    }
  }
}

void chkSleep()
{
  if ((RTC.hour >= sleepHour || RTC.hour < wakeHour)&& !isAsleep)
  {
    isAsleep = true;
    mouthAsleep();
    eyeClose();
    digitalWrite(iLedPin, false);
  }
  else if (RTC.hour >= wakeHour && RTC.hour < sleepHour && isAsleep)
  {
    isAsleep = false;
    eyeOpen();
    mouthScrollText(F("Robot is online!"),false);
    digitalWrite(iLedPin, true);
  }
}
void chkSonar()
{
  if (sonarTimer.isFinished())
  {
    int distance = sonar.ping_in();
    if (!isEyeAnim && distance > 0) {
      if (distance <= 6) {
        if (sonarLast != 1) {
          if (!sonarAngryTimer.isRunning()) sonarAngryTimer.start(3000);
          eyeCrossDown();
        }
        sonarLast = 1;
      } else if (distance <= 12) {
        if (sonarLast != 2) {
          if (!sonarAngryTimer.isRunning()) sonarAngryTimer.start(3000);
          // Play sound
          eyeCross();
        }
        sonarLast = 2;
      } else {
        if (sonarLast != 0) {
          eyeCenter();
          isStopMouth = true;
          if (!isMouthAnim) mouthClosed();
        }
        sonarLast = 0;
        sonarAngryTimer.stop();
      }
    }
    sonarTimer.restart();
  }
  if (sonarAngryTimer.isFinished() && (sonarLast == 1 || sonarLast == 2)) {
    //Play sound?
    eyeAngry();
    mouthAngry();
    playSound(3,false);
  }
}
void chkBright()
{
  int tmp = map(constrain(analogRead(brPin), 0, 350), 0, 350, 0, 15);
  if (tmp > 0 && tmp > brtn + 1 || brtn + 1 > tmp) {
    brtn = tmp;
    // If brightness has changed then update everything
    rEye.setBrightness(brtn);
    lEye.setBrightness(brtn);
    mouth.setBrightness(brtn);
  }
  if (brtn < 0) brtn = 0; // For some reason it occassionally goes negative.
}

void chkPIR() {
  if (PIRon != PIRlast) {
    PIRlast = PIRon;
    if (PIRon) eyeSearch();
  }
}

void chkMP3msg()
{
  if (mp3.available()) {
    handleMP3msg(mp3.readType(), mp3.read()); //Print the detail message from DFPlayer to handle different errors and states.
  }
}

void handleMP3msg(uint8_t type, int value) {
  switch (type) {
    case DFPlayerPlayFinished:
      // Keep playing songs when the last one has ended if we are playing music.
      if (isMusic) {
        playMusic(false);
      } else if (isStopMouth) {
        stopMouth();
      }
      break;
    default:
      Serial.print(F("MP3 - Type: "));
      Serial.print(type);
      Serial.print(F(" Value: "));
      Serial.println(value);
      break;
  }
}

void chkVolume()
{
  int val = analogRead(0) / MAX_VOLUME;    // read the value from the sensor
  if (val > MAX_VOLUME) {
    val = MAX_VOLUME;
  }
  if (val > volVal + 1 || val < volVal - 1) { // Make sure the difference is significant
    mp3.volume(val);
    volVal = val;
  }
}

void handlePlayBtn()
{
  // Only play music if we aren't already playing
  if (!isMusic) {
    playMusic(false);
    mouthScrollMusic();
  } else {
    if (mp3.readState() == 514) {
      mp3.start();
    } else {
      mp3.pause();
    }
  }
}

void stopMusic()
{
  isMusic = false;
  isStopMouth = true;
  stopMouth();
  // Loop trying to stop any previous before playing next
  do {
    mp3.stop();
  } while (mp3.readState() != 512);
}

void playMusic(bool prev)
{
  isMusic = true;
  // Loop trying to stop any previous before playing next
  do {
    mp3.stop();
  } while (mp3.readState() != 512);
  if (prev) {
    // If it is less than 3 seconds since song started then play previous song
    if (!songTimer.isFinished()) {
      if (qP > 0) {
        qP -= 1; // Move back in the queue
      } else {
        // Music got randomized so we can't play from a queue position so force last known
        mp3.play(lQ[1]);
        songTimer.start(3000);
        return;
      }
    }
    // Otherwise don't move the queue and play the same song
  } else {
    // Not previous so play next in the queue
    qP += 1;
  }
  if (qP > QSIZE)
  {
    randMusic();
    qP = 0;
  }
  // If we are playing a song we last played pick up another song
  if (mQ[qP] == lQ[0] && !prev) {
    playMusic(false);
  }
  else {
    // Found a song so play
    mp3.play(mQ[qP]);
    lQ[1] = lQ[0];
    lQ[0] = mQ[qP];
  }
  songTimer.start(3000);
}

void playSound(int sndFile, bool isMouth) {
  if (!isMusic) {
    mp3.playFolder(2, sndFile);
    if (isMouth) {
      isStopMouth = true;
      mouthSpeakRandom();
    } else {
      isStopMouth = false;
    }
  }
}

void randMusic() {
  /*
    Pick a random point in the folder to begin in case there are more than 10
    Continue on from the last song picked in the last group if already defined
  */
  if (!qS)
    qS = random(m1st, m1st + mCnt - 1);

  // Pick 10 songs from the queueStart position
  for (int i = 0; i < QSIZE; i++) {
    mQ[i] = qS;
    qS += 1;
    if (qS >= m1st + mCnt)
      qS = m1st;
  }
  randomize(mQ, 10);
}

void chkButtons()
{
  if (setABtn.stateChanged() && setABtn.state()) {
    playSound(1,true);
  }
  if (setTBtn.stateChanged() && setTBtn.state()) {
    playSound(2,true);
  }
  
  // Only allow time changes if the setBtn is pressed AND held
  if ( setTBtn.state() || setABtn.state() )
  {
    // If hrBtn is pressed and state changed with enough delay for debounce or held down with enough delay then increase
    if ( hrBtn.state() && ( hrBtn.stateChanged() || (millis() - hrBtnT) > 300))
    {
      hrBtnT = millis();
      setTBtn.state() ? incClock('h') : incAlarm('h');
    }

    // If mnBtn is pressed and state changed with enough delay for debounce or held down with enough delay then increase
    if ( mnBtn.state() && ( mnBtn.stateChanged() || (millis() - mnBtnT) > 300))
    {
      mnBtnT = millis();
      setTBtn.state() ? incClock('m') : incAlarm('m');
    }
  }

  if (isPressed(playBtn)) handlePlayBtn();
  if (isPressed(nextBtn)) playMusic(false);
  if (isPressed(prevBtn)) playMusic(true);
  if (isPressed(stopBtn)) stopMusic();
  if (isPressed(snzBtn))  snooze();
  if (snzBtn.stateChanged() && !snzBtn.state() && !alarmOn) {
    stopMusic();
    stopMouth();
  }
}

bool isPressed(DebounceFilter btn) {
  return btn.stateChanged() && btn.state();
}

byte readButtons() {
  alarmFile = 0;

  // Flip the latch
  digitalWrite(BLPin, 1);
  delayMicroseconds(20);
  digitalWrite(BLPin, 0);

  int i;
  for (i = 0; i <= 15; i++)
  {
    byte x = 0;
    digitalWrite(BCPin, 0);
    delayMicroseconds(2);
    x = digitalRead(BDPin);

    switch (i) {
      case 0:
      case 1:
      case 2:
      case 3:
        /// Shift the 1st 4 bits onto alarmFile for the file number
        alarmFile |= x << i;
        break;
      case 4:
        alarmSwt.addSample(x);
        break;
      case 5:
        snzBtn.addSample(x);
        break;
      case 6:
        shutdownBtn.addSample(x);
        break;
      case 7:
        setABtn.addSample(x);
        break;
      case 8:
        setTBtn.addSample(x);
        break;
      case 9:
        hrBtn.addSample(x);
        break;
      case 10:
        mnBtn.addSample(x);
        break;
      case 11:
        playBtn.addSample(x);
        break;
      case 12:
        nextBtn.addSample(x);
        break;
      case 13:
        prevBtn.addSample(x);
        break;
      case 14:
        stopBtn.addSample(x);
        break;
      case 15:
        PIRon = x;
        break;
      default:
        break;
    }

    digitalWrite(BCPin, 1);
  }
}

void writeAlarmNumber()
{
  // Only update on changes to save some cycles
  bool alarmChanged = alarmSwt.stateChanged(); // Need to set to a variable because checking in the if causes segfault
  if (alarmFile != alarmFileL || alarmChanged) {
    alarmFileL = alarmFile;

    int d1 = alarmFile / 10;
    int d2 = alarmFile % 10;
    digitalWrite(SLPin, LOW);
    int dO1 = D[d1];
    int dO2 = D[d2];
    if (alarmSwt.state()) {
      dO2 += 128; // Set the most signficant bit to turn off the alarm stepper
    }
    shiftOut(SDPin, SCPin, MSBFIRST, dO2); // digitTwo
    shiftOut(SDPin, SCPin, MSBFIRST, dO1); // digitOne
    digitalWrite(SLPin, HIGH);
  }
}

void chkSnzLed()
{
  digitalWrite(snzLedPin, alarmOn == 1);
}

void incClock(char field)
{
  RTC.getTime();
  if ( field == 'h' )
  {
    RTC.hour += 1;
    if ( RTC.hour >= 24 ) RTC.hour = 0;
  }
  else if ( field == 'm' )
  {
    RTC.minute += 1;
    if ( RTC.minute > 59 ) RTC.minute = 0;
  }

  RTC.stopClock();
  RTC.fillByHMS(RTC.hour, RTC.minute, 0);
  RTC.setTime();
  RTC.startClock();

  // trigger clock refresh
  lSec = 99;
}

void incAlarm(char field)
{
  if (field == 'h')
  {
    alarmHr += 1;
    if (alarmHr >= 24) alarmHr = 0;
  }

  else if (field == 'm')
  {
    alarmMn += 1;
    if (alarmMn > 59) alarmMn = 0;
  }
  // trigger clock refresh
  lSec = 99;
}

void chkAlarmSwitch()
{
  if (!alarmSwt.state())
  {
    if (alarmOn > 0) {
      stopMusic();
      stopMouth();
      eyeOpen();
      mouthScrollText(F("Good morning Veronica! I love you!"),false);
    }
    // Reset all the variables if the switch is off
    alarmOn = 0;
    snzCnt = 0;
    snzTimeCur = snzTime;
  }
}

void chkAlarm()
{
  if ( alarmSwt.state() && (
         ( alarmOn == 0 && alarmHr == RTC.hour && alarmMn == RTC.minute ) || // Start the alarm when the hour:min matches
         ( alarmOn == 2 && alarmSec > 0 && alarmSec < RTC.time2000 ) // or if we are snoozing and snooze time has passed
       ) )
  {    
    alarmOn = 1;
    mouthScrollText(F("Alarm! Wake Up! Alarm!"),true);
    eyeOpen();
    playAlarm();
  }
}

void playAlarm() {
  if (alarmFile == 0) {
    playMusic(false);
  } else {
    isStopMouth = false;
    mp3.loop(alarmFile);
  }  
}

void snooze()
{
  if (alarmOn > 0)
  {
    alarmOn = 2; // Note that we are snoozing
    snzCnt++;
    if (snzTimeCur > 0)
    {
      stopMusic();
      mouthSnooze();
      eyeClose();
      
      // Set the time in seconds after which snooze is over
      alarmSec = RTC.time2000 + ( snzTimeCur * 60 );

      // Speed up the snooze time
      if ( snzLmt && snzCnt > snzLmt && ! snzUnlmt )
      {
        snzTimeCur -= snzDegrade;
      }
    }
  } else {
    printTime();
    playAlarm();
    if (!isMusic) {
      mouthSpeakRandom();
    } else {
      mouthScrollMusic();
    }
  }
}

void printTime()
{
  clck.clear();

  if (snzBtn.state() || setABtn.state())
  {
    dHr = alarmHr;
    dMn  = alarmMn;
    sCol = false;
  } else {
    dHr = RTC.hour;
    dMn  = RTC.minute;
  }

  sCol = 1 - sCol;
  clck.drawColon(sCol);

  if (dHr > 11)
  {
    isPM = true;
    dHr -= 12;
  } else {
    isPM = false;
  }
  if (dHr == 0) dHr = 12;

  if (int(dHr / 10) > 0)
    clck.writeDigitNum(0, int(dHr / 10), 0);
  clck.writeDigitNum(1, (dHr % 10), 0);

  clck.writeDigitNum(3, int(dMn / 10), 0);
  clck.writeDigitNum(4, (dMn % 10), isPM);

  clck.setBrightness(brtn);
  clck.writeDisplay();
}

// Method to swap items in an array
void swap (int *a, int *b)
{
  int temp = *a;
  *a = *b;
  *b = temp;
}

// Method to randomize an array
void randomize (int arr[], int n)
{
  for (int i = n - 1; i > 0; i--)
  {
    unsigned int j = random(0, n);
    swap(&arr[i], &arr[j]);
  }
}

/*
    Mouth animation methods
*/
void mouthSpeakRandom() {
  int r = random(1,6);
  switch(r) {
    case 1:
      mouthSpeakLips();
      break;
    case 2:
      mouthSpeakDiamond();
      break;
    case 3:
      mouthSineWave();
      break;
    case 4:
      mouthBarGraph();
      break;
    case 5:
      mouthScrollBarGraph();
      break;
  }
}

void mouthClear() {
  mouth.clear();
  mouth.writeDisplay();
}
void mouthAsleep() {
  startMouthCycle();
  mouthFrameMap = mouthAsleepMap;
  mouthFrameCnt = sizeof(mouthAsleepMap) / 2;
}

void mouthAngry() {
  startMouthCycle();
  mouthFrameMap = mouthAngryMap;
  mouthFrameCnt = sizeof(mouthAngryMap) / 2;
}

void mouthClosed()
{
  startMouthCycle();
  mouthFrameMap = mouthClosedMap;
  mouthFrameCnt = sizeof(mouthClosedMap) / 2;
}

void mouthSpeakLips() {
  startMouthCycle();
  mouthFrameMap = mouthSpeakLipsMap;
  mouthFrameCnt = sizeof(mouthSpeakLipsMap) / 2;
  isMouthRepeat = true;
}

void mouthSpeakDiamond() {
  startMouthCycle();
  mouthFrameMap = mouthSpeakDiamondMap;
  mouthFrameCnt = sizeof(mouthSpeakDiamondMap) / 2;
  isMouthRepeat = true;
}

void mouthTongueOut() {
  startMouthCycle();
  mouthFrameMap = mouthTongueMap;
  mouthFrameCnt = sizeof(mouthTongueMap) / 2;
  mouthFrameSpeed = 100;
}

void mouthSnooze() {
  mouthFrameRandom = true;
  mouthFrameMap = mouthSnoozeMap;
  mouthFrameCnt = sizeof(mouthSnoozeMap) / 2;
  mouthFrameSpeed = 150;
  resetBitmapX();
  startBitmapScroll();
}

void mouthSineWave() {
  mouthFrameMap = mouthSineMap;
  mouthFrameCnt = sizeof(mouthSineMap) / 2;
  mouthFrameSpeed = 70;
  resetBitmapX();
  startBitmapScroll();
}

void mouthScrollBarGraph() {
  isMouthAnim = 3;
  mouthFrameSpeed = 75;
}

void mouthBarGraph() {
  isMouthAnim = 4;
  mouthFrameSpeed = 150;
}

void mouthScrollMusic() {
  mouthFrameRandom = true;
  mouthFrameMap = mouthMusicMap;
  mouthFrameCnt = sizeof(mouthMusicMap) / 2;
  mouthFrameSpeed = 50;
  resetBitmapX();
  startBitmapScroll();
}

void mouthScrollText(String text, bool isRepeat) {
  isMouthAnim = 1;
  isMouthRepeat = isRepeat;
  mouthScrollPos = 15;
  mouthFrameSpeed = 75;
  mouthScrollString = text;
}

void stopMouth() {
  isMouthAnim = false;
  if (isAsleep)
  {
    mouthAsleep();
  } else {
    mouthClosed();
  }
}

void resetBitmapX()
{
  bitmapX[0] = 0;
  bitmapX[1] = 8;
  bitmapX[2] = 16;
}

void startBitmapScroll()
{
  isMouthAnim = 2;
  for (int i = 0; i < 3; i++) {
    int index = i;
    if (i >= mouthFrameCnt) index = 0;
    bitmapIndex[i] = index;
  }
}

void startMouthCycle()
{
  isMouthAnim = 5;
  isMouthRepeat = false;
  mouthCycleIndex = 0;
  mouthFrameSpeed = 100;
}
/*
  Method to scroll text if needed
*/
/*
  Loops if isMouthAnim has value.
  isMouthAnim can be:
    1. to scroll text
    2. to scroll bitmap
    3. to scroll a random bar graph
    4. to write a static random bar graph
    5. to cycle through static bitmaps
  Expects GLOBALS:

*/
void chkMouthAnim()
{
  if (isMouthAnim && !isShutdown) {
    if (mouthAnimTimer.isFinished())
    {
      mouth.clear();
      mouth.setRotation(1);
      switch (isMouthAnim) {
        case 1:
          drawMouthScrollText();
          break;
        case 2:
          drawMouthScrollBitmap();
          break;
        case 3:
          drawMouthScrollBarGraph();
          break;
        case 4:
          drawMouthBarGraph();
          break;
        case 5:
          drawMouthCycle();
          break;
        default:
          break;
      }
      mouth.writeDisplay();
      mouthAnimTimer.start(mouthFrameSpeed);
    }
  }
  else if (isShutdown)
  {
    mouthClear();
  }
}

void drawMouthScrollBitmap()
{
  for (int i = 0; i < 3; i++) {
    mouth.drawBitmap(bitmapX[i], 0, mouthFrames[mouthFrameMap[bitmapIndex[i]]], 8, 8, LED_ON);
    bitmapX[i] -= 1;
  }
  // First frame scrolled off so push on the end
  if (bitmapX[0] < -8) {
    resetBitmapX();
    for (int j = 0; j < 2; j++) {
      bitmapIndex[j] = bitmapIndex[j + 1]; // Shift them down
    }
    // Pick a new last frame
    if (mouthFrameRandom) {
      bitmapIndex[2] = random(0, mouthFrameCnt);
    } else {
      bitmapIndex[2] += 1;
      if (bitmapIndex[2] >= mouthFrameCnt) bitmapIndex[2] = 0;
    }
  }
}

void drawMouthScrollText()
{
  int len = 0 - mouthScrollString.length() * 5;
  mouthScrollPos -= 1;
  if (mouthScrollPos < len - 32) {
    if (isMouthRepeat) {
      mouthScrollPos = 15;
    } else {
      stopMouth();
    }
  }
  mouth.setCursor(mouthScrollPos, 0);
  mouth.print(mouthScrollString);
}

void drawMouthScrollBarGraph() {
  mouth.setRotation(3);
  // Shift all the numbers left
  for (int i = 0; i < 16; i++) {
    if (i == 15) {
      // Make sure it is not more than two more or less than the previous
      if (mouthBarGraphArr[i - 1] >= 7) mouthBarGraphDir = false;
      if (mouthBarGraphArr[i - 1] <= -1)  mouthBarGraphDir = true;
      if (mouthBarGraphDir) {
        mouthBarGraphArr[i] = mouthBarGraphArr[i - 1] + random(0, 2);
      } else {
        mouthBarGraphArr[i] = mouthBarGraphArr[i - 1] - random(0, 2);
      }
    } else {
      mouthBarGraphArr[i] = mouthBarGraphArr[i + 1];
    }
    mouth.drawLine(i, mouthBarGraphArr[i], i, -1, LED_ON);
  }
}

void drawMouthBarGraph() {
  mouth.setRotation(3);
  for (int i = 0; i < 16; i += 2) {
    int r = random(-1, 8);
    mouth.drawLine(i, r, i, 0, LED_ON);
    mouth.drawLine(i + 1, r, i + 1, 0, LED_ON);
  }
}

void drawMouthCycle() {
  mouth.drawBitmap(0, 0, mouthFrames[mouthFrameMap[mouthCycleIndex]], 8, 8, LED_ON);
  mouth.drawBitmap(8, 0, mouthFrames[mouthFrameMap[mouthCycleIndex + 1]], 8, 8, LED_ON);
  mouthCycleIndex += 2;
  if (mouthCycleIndex >= mouthFrameCnt) {
    if (isMouthRepeat) {
      mouthCycleIndex = 0;
    } else {
      isMouthAnim = 0;
    }
  }
}
/*
   End Mouth Animation methods
*/

/*
   Eye animation methods
*/
void eyeClear() {
  rEye.clear();
  lEye.clear();
  rEye.writeDisplay();
  lEye.writeDisplay();
}
void eyeClose()
{
  rEyeFrameMap = closeMap;
  lEyeFrameMap = closeMap;
  eyeFrameCnt = sizeof(closeMap) / 2; // Using sizeof is fine because it should always be ints
  eyeStartAnim();
}
void eyeOpen()
{
  rEyeFrameMap = openMap;
  lEyeFrameMap = openMap;
  eyeFrameCnt = sizeof(openMap) / 2;
  eyeStartAnim();
}
void eyeBlink()
{
  rEyeFrameMap = blinkMap;
  lEyeFrameMap = blinkMap;
  eyeFrameCnt = sizeof(blinkMap) / 2;
  eyeStartAnim();
}
void eyeWink()
{
  rEyeFrameMap = blinkMap;
  lEyeFrameMap = winkOpenMap;
  eyeFrameCnt = sizeof(blinkMap) / 2;
  eyeFrameSpeed = 25;
  eyeStartAnim();
}
void eyeCenter() {
  rEyeFrameMap = centerMap;
  lEyeFrameMap = centerMap;
  eyeFrameCnt = sizeof(centerMap) / 2;
  eyeStartAnim();
}
void eyeUp()
{
  rEyeFrameMap = upMap;
  lEyeFrameMap = upMap;
  eyeFrameCnt = sizeof(upMap) / 2;
  eyeStartAnim();
}
void eyeDown()
{
  rEyeFrameMap = downMap;
  lEyeFrameMap = downMap;
  eyeFrameCnt = sizeof(downMap) / 2;
  eyeStartAnim();
}
void eyeLeft()
{
  rEyeFrameMap = leftMap;
  lEyeFrameMap = leftMap;
  eyeFrameCnt = sizeof(leftMap) / 2;
  eyeStartAnim();
}
void eyeRight()
{
  rEyeFrameMap = rightMap;
  lEyeFrameMap = rightMap;
  eyeFrameCnt = sizeof(rightMap) / 2;
  eyeStartAnim();
}
void eyeCross()
{
  rEyeFrameMap = rightMap;
  lEyeFrameMap = leftMap;
  eyeFrameCnt = sizeof(rightMap) / 2;
  eyeStartAnim();
}
void eyeCrossUp()
{
  rEyeFrameMap = upRightMap;
  lEyeFrameMap = upLeftMap;
  eyeFrameCnt = sizeof(upRightMap) / 2;
  eyeStartAnim();
}
void eyeCrossDown()
{
  rEyeFrameMap = downRightMap;
  lEyeFrameMap = downLeftMap;
  eyeFrameCnt = sizeof(downRightMap) / 2;
  eyeStartAnim();
}
void eyeRollUp()
{
  rEyeFrameMap = rollUpMap;
  lEyeFrameMap = rollUpMap;
  eyeFrameCnt = sizeof(rollUpMap) / 2;
  eyeFrameSpeed = 100;
  eyeStartAnim();
}
void eyeRollSide()
{
  rEyeFrameMap = rollSideMap;
  lEyeFrameMap = rollSideMap;
  eyeFrameCnt = sizeof(rollSideMap) / 2;
  eyeFrameSpeed = 100;
  eyeStartAnim();
}
void eyeSpin()
{
  rEyeFrameMap = rSpinMap;
  lEyeFrameMap = lSpinMap;
  eyeFrameSpeed = 25;
  eyeFrameCnt = sizeof(rSpinMap) / 2;
  eyeStartAnim();
}
void eyeSearch()
{
  rEyeFrameMap = searchMap;
  lEyeFrameMap = searchMap;
  eyeFrameCnt = sizeof(searchMap) / 2;
  eyeStartAnim();
}
void eyeAngry()
{
  rEyeFrameMap = angryRightMap;
  lEyeFrameMap = angryLeftMap;
  eyeFrameCnt = sizeof(angryRightMap) / 2;
  eyeStartAnim();
}
void eyeStartAnim()
{
  isEyeAnim = true;
  eyeAnimPos = 0;
}
void eyeRandomAct()
{
  int second = RTC.second;
  int minute = RTC.minute;
  if (!isEyeAnim) {
    if (minute % 5 == 0 && second == 0) { // On 5 minute mark do a random animation
      int action = random(1, 5);
      switch (action) {
        case 1:
          eyeRollUp();
          break;
        case 2:
          eyeRollSide();
          break;
        case 3:
          eyeSpin();
          break;
        case 4:
          eyeWink();
          break;
        default:
          eyeWink();
          break;
      }
      if (!isMouthAnim) mouthTongueOut();
    } else {
      eyeBlink();
    }
    sonarLast = 0;// Reset the sonar distance to redraw
  }
}
/*
  Loops if isEyeAnim is true.
  Expects GLOBALS:
  eyeAnimPos = point to start the frame maps (should always be 0 set by calling eyeStartAnim)
  eyeFrameCnt = the number of frames in the map
  rEyeFrameMap = pointer to array of the frames to animate for right eye (Done as two arrays to allow independent eyes)
  lEyeFrameMap = pointer to array of the frames to animate for left eye
  eyeFrameSpeed = optional to change the frame rate
*/
void chkEyeAnim() {
  if (isEyeAnim && !isShutdown) {
    if ( eyeAnimTimer.isFinished() )
    {
      rEye.clear();
      lEye.clear();
      rEye.drawBitmap(0, 0, eyeFrames[rEyeFrameMap[eyeAnimPos]], 8, 8, LED_ON);
      lEye.drawBitmap(0, 0, eyeFrames[lEyeFrameMap[eyeAnimPos]], 8, 8, LED_ON);
      rEye.writeDisplay();
      lEye.writeDisplay();
      eyeAnimPos += 1;
      if (eyeAnimPos >= eyeFrameCnt) {
        isEyeAnim = false;
        eyeFrameSpeed = EYEFRAMESPEED; // Reset to the default
      }
      eyeAnimTimer.start(eyeFrameSpeed);
    }
  }
  else if (isShutdown)
  {
    eyeClear();
  }
}

/*
  Used for debugging only
*/
void printFullTime()
{
  if (RTC.hour < 10)                    // correct hour if necessary
    Serial.print("0");
  Serial.print(RTC.hour, DEC);
  Serial.print(":");
  if (RTC.minute < 10)                  // correct minute if necessary
    Serial.print("0");
  Serial.print(RTC.minute, DEC);
  Serial.print(":");
  if (RTC.second < 10)                  // correct second if necessary
    Serial.print("0");
  Serial.print(RTC.second, DEC);
  Serial.println();
}
