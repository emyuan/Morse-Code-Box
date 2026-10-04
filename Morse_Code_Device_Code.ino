//This code is meant to be uploaded into each ESP

#include "AdafruitIO_WiFi.h"
#include "ESP8266WiFi.h"
#include "user_interface.h"
/// ==============================
// WIFI: This will be different for each ESP based on which WIFI they connect to
// ==============================
#define WIFI_SSID    "----"
#define WIFI_PASS     "----"

// ==============================
// ADAFRUIT IO: This is also different for each ESP
// in Adafruit IO each one has a feed both associated with an username, or they can be separate usernames
// ==============================
#define IO_USERNAME   "----"
#define IO_KEY        "----""

AdafruitIO_WiFi io(IO_USERNAME, IO_KEY, WIFI_SSID, WIFI_PASS);
AdafruitIO_Feed *sendFeed = io.feed("esp-signal");
AdafruitIO_Feed *receiveFeed = io.feed("esp-signal2");

// ==================================================
// PINS
// ==================================================

const int touchSensor = 4;  // D2 / GPIO4
const int blueLED     = 12; // D6 / GPIO12
const int greenLED    = 13; // D7 / GPIO13
const int buzzer      = 0;  // D3 / GPIO0



// ==================================================
// SIGNAL SETTINGS
// ==================================================

const unsigned long SHORT_MAX = 265;
const unsigned long LONG_SIGNAL_MIN = 270;

const unsigned long SHORT_PLAY_TIME = 150;
const unsigned long LONG_PLAY_TIME = 500;

const unsigned long SIGNAL_PAUSE = 500;

const unsigned long SEQUENCE_WAIT = 600;

const unsigned long LONG_PRESS_TIME = 2300;


// ==================================================
// SLEEP SETTINGS
// After every 2 mins, the device will enter a sleep mode
// The touch sensor however will continue to check for any activity and awaken when touched
// ==================================================

const unsigned long SLEEP_TIMEOUT = 120000;  // 2 minutes

unsigned long lastActivityTime = 0;


// ==================================================
// QUEUE SETTINGS
// Each sequence of signals will form a que to prevent new signals from interupting any old ones
// They will then be displayed in a uniform way with a set short and long duration to make it more readable
// ==================================================

const int MAX_SIGNALS = 20;
const int MAX_SEQUENCES = 5;


// ==================================================
// VARIABLES
// ==================================================

bool buzzerEnabled = true;

bool touching = false;
unsigned long touchStartTime = 0;


// ==================================================
// OUTGOING SEQUENCE
// ==================================================

char outgoingSequence[MAX_SIGNALS + 1];
int outgoingCount = 0;

unsigned long lastTouchTime = 0;
bool sequenceWaitingToSend = false;


// ==================================================
// RECEIVED SEQUENCE QUEUE
// ==================================================

char receivedSequences[MAX_SEQUENCES][MAX_SIGNALS + 1];
int receivedCounts[MAX_SEQUENCES];

int sequenceHead = 0;
int sequenceTail = 0;
int sequenceCount = 0;


// ==================================================
// CURRENTLY PLAYING SEQUENCE
// ==================================================

bool playingSequence = false;

int currentSequenceSlot = -1;
int currentSignalIndex = 0;
int currentSequenceLength = 0;

unsigned long currentSignalDuration = 0;
unsigned long signalStartTime = 0;

bool inSignalPause = false;
unsigned long pauseStartTime = 0;


// ==================================================
// WAKE-UP FLASH
// Everytime the device wakes up from sleep it flashes both lights three times to signify it's awake
// ==================================================

void wakeFlash() {

  for (int i = 0; i < 3; i++) {

    digitalWrite(blueLED, HIGH);
    digitalWrite(greenLED, HIGH);

    delay(150);

    digitalWrite(blueLED, LOW);
    digitalWrite(greenLED, LOW);

    delay(150);
  }
}


// ==================================================
// GO TO SLEEP
// ==================================================

void goToSleep() {

  Serial.println();
  Serial.println("==============================");
  Serial.println("2 MINUTES INACTIVE");
  Serial.println("Going to sleep...");
  Serial.println("==============================");

  // Make sure outputs are off
  digitalWrite(blueLED, LOW);
  digitalWrite(greenLED, LOW);

  noTone(buzzer);

  // Disconnect from Adafruit IO / Wi-Fi
  // Disconnect Wi-Fi before sleeping
WiFi.disconnect();
WiFi.mode(WIFI_OFF);

  delay(100);

  Serial.println("Waiting for touch on GPIO4...");

  // --------------------------------------------------
  // Configure GPIO4 as wake source
  // TTP223B is HIGH when touched
  // --------------------------------------------------

  pinMode(touchSensor, INPUT);

  // Start light sleep
  wifi_set_sleep_type(LIGHT_SLEEP_T);

  // Wake when GPIO4 goes HIGH
  gpio_pin_wakeup_enable(
    GPIO_ID_PIN(touchSensor),
    GPIO_PIN_INTR_HILEVEL
  );

  wifi_fpm_set_sleep_type(LIGHT_SLEEP_T);

  wifi_fpm_open();

  wifi_fpm_do_sleep(0xFFFFFFF);

  delay(10);

  // --------------------------------------------------
  // ESP has now woken up
  // --------------------------------------------------

  wifi_fpm_close();

  gpio_pin_wakeup_disable();

  WiFi.mode(WIFI_STA);

  Serial.println();
  Serial.println("==============================");
  Serial.println("WOKE UP!");
  Serial.println("==============================");

  wakeFlash();

  lastActivityTime = millis();

  // --------------------------------------------------
  // Reconnect to Adafruit IO
  // --------------------------------------------------

  Serial.println("Reconnecting to Adafruit IO...");

  io.connect();

  while (io.status() < AIO_CONNECTED) {

    Serial.print(".");
    delay(500);
  }

  Serial.println();
  Serial.println("Reconnected to Adafruit IO!");
  Serial.println("Ready!");

  // Touch may still be held when we wake.
  // Wait until released before allowing another touch.
  while (digitalRead(touchSensor)) {
    delay(10);
  }

  touching = false;

  lastActivityTime = millis();
}


// ==================================================
// SEND OUTGOING SEQUENCE
// ==================================================

void sendCurrentSequence() {

  if (outgoingCount == 0) {
    return;
  }

  outgoingSequence[outgoingCount] = '\0';

  Serial.print("Sending sequence: ");
  Serial.println(outgoingSequence);

  String message = String(outgoingSequence);

  sendFeed->save(message);

  outgoingCount = 0;
  sequenceWaitingToSend = false;

  lastActivityTime = millis();
}


// ==================================================
// HANDLE RECEIVED MESSAGE
// ==================================================

void handleMessage(AdafruitIO_Data *data) {

  String message = data->toString();

  Serial.print("Received sequence: ");
  Serial.println(message);


  if (sequenceCount >= MAX_SEQUENCES) {

    Serial.println("WARNING: Sequence queue full!");
    return;
  }


  int validCount = 0;

  for (int i = 0; i < message.length(); i++) {

    char symbol = message.charAt(i);

    if ((symbol == 'S' || symbol == 'L') &&
        validCount < MAX_SIGNALS) {

      receivedSequences[sequenceTail][validCount] = symbol;
      validCount++;
    }
  }


  if (validCount == 0) {

    Serial.println("Invalid sequence.");
    return;
  }


  receivedSequences[sequenceTail][validCount] = '\0';
  receivedCounts[sequenceTail] = validCount;


  Serial.print("Added sequence: ");
  Serial.println(receivedSequences[sequenceTail]);


  sequenceTail++;

  if (sequenceTail >= MAX_SEQUENCES) {
    sequenceTail = 0;
  }

  sequenceCount++;


  Serial.print("Sequences waiting: ");
  Serial.println(sequenceCount);

  lastActivityTime = millis();
}


// ==================================================
// START NEXT SEQUENCE
// ==================================================

void startNextSequence() {

  if (playingSequence || sequenceCount == 0) {
    return;
  }


  currentSequenceSlot = sequenceHead;

  currentSequenceLength =
    receivedCounts[currentSequenceSlot];

  currentSignalIndex = 0;


  sequenceHead++;

  if (sequenceHead >= MAX_SEQUENCES) {
    sequenceHead = 0;
  }

  sequenceCount--;


  playingSequence = true;
  inSignalPause = false;


  if (receivedSequences[currentSequenceSlot][0] == 'S') {
    currentSignalDuration = SHORT_PLAY_TIME;
  }
  else {
    currentSignalDuration = LONG_PLAY_TIME;
  }


  signalStartTime = millis();


  digitalWrite(blueLED, HIGH);

  if (buzzerEnabled) {
    tone(buzzer, 1000);
  }


  Serial.print("Starting sequence: ");
  Serial.println(receivedSequences[currentSequenceSlot]);

  lastActivityTime = millis();
}


// ==================================================
// UPDATE RECEIVING
// ==================================================

void updateReceiving() {

  if (!playingSequence) {
    return;
  }


  // ------------------------------------------
  // CURRENT SIGNAL
  // ------------------------------------------

  if (!inSignalPause) {

    if (millis() - signalStartTime >= currentSignalDuration) {

      digitalWrite(blueLED, LOW);

      if (buzzerEnabled) {
        noTone(buzzer);
      }


      Serial.print("Finished signal ");
      Serial.print(currentSignalIndex + 1);
      Serial.print("/");
      Serial.println(currentSequenceLength);


      currentSignalIndex++;


      if (currentSignalIndex < currentSequenceLength) {

        inSignalPause = true;
        pauseStartTime = millis();

      }

      else {

        playingSequence = false;
        currentSequenceSlot = -1;

        Serial.println("Finished entire sequence.");

        lastActivityTime = millis();
      }
    }
  }


  // ------------------------------------------
  // PAUSE BETWEEN SIGNALS
  // ------------------------------------------

  else {

    if (millis() - pauseStartTime >= SIGNAL_PAUSE) {

      inSignalPause = false;


      char symbol =
        receivedSequences[currentSequenceSlot][currentSignalIndex];


      if (symbol == 'S') {
        currentSignalDuration = SHORT_PLAY_TIME;
      }

      else {
        currentSignalDuration = LONG_PLAY_TIME;
      }


      signalStartTime = millis();


      digitalWrite(blueLED, HIGH);

      if (buzzerEnabled) {
        tone(buzzer, 1000);
      }


      Serial.print("Starting signal ");
      Serial.print(currentSignalIndex + 1);
      Serial.print("/");
      Serial.println(currentSequenceLength);
    }
  }
}


// ==================================================
// SETUP
// ==================================================

void setup() {

  Serial.begin(115200);
  delay(1000);


  Serial.println();
  Serial.println("==============================");
  Serial.println("ESP #2");
  Serial.println("==============================");


  pinMode(touchSensor, INPUT);
  pinMode(blueLED, OUTPUT);
  pinMode(greenLED, OUTPUT);
  pinMode(buzzer, OUTPUT);


  digitalWrite(blueLED, LOW);
  digitalWrite(greenLED, LOW);

  noTone(buzzer);


  // Listen to ESP #1
  receiveFeed->onMessage(handleMessage);


  Serial.println("Connecting to Adafruit IO...");


  io.connect();


  while (io.status() < AIO_CONNECTED) {

    Serial.print(".");
    delay(500);
  }


  Serial.println();
  Serial.println("Connected to Adafruit IO!");
  Serial.println("Ready!");


  // Start inactivity timer
  lastActivityTime = millis();
}


// ==================================================
// LOOP
// ==================================================

void loop() {

  io.run();


  // ==================================================
  // SEND AFTER SEQUENCE WAIT
  // ==================================================

  if (sequenceWaitingToSend) {

    if (millis() - lastTouchTime >= SEQUENCE_WAIT) {

      sendCurrentSequence();
    }
  }


  // ==================================================
  // RECEIVE
  // ==================================================

  updateReceiving();

  startNextSequence();


  // ==================================================
  // TOUCH INPUT
  // ==================================================

  bool currentTouch = digitalRead(touchSensor);


  // ------------------------------------------
  // FINGER JUST TOUCHED
  // ------------------------------------------

  if (currentTouch && !touching) {

    touching = true;

    touchStartTime = millis();

    lastActivityTime = millis();

    Serial.println("Touch started");


    digitalWrite(greenLED, HIGH);

    if (buzzerEnabled) {
      tone(buzzer, 1000);
    }
  }


  // ------------------------------------------
  // FINGER RELEASED
  // ------------------------------------------

  if (!currentTouch && touching) {

    touching = false;


    unsigned long duration =
      millis() - touchStartTime;


    Serial.print("Touch duration: ");
    Serial.print(duration);
    Serial.println(" ms");


    lastActivityTime = millis();


    digitalWrite(greenLED, LOW);

    if (buzzerEnabled) {
      noTone(buzzer);
    }


    // ==================================================
    // LONG PRESS = BUZZER TOGGLE
    // ==================================================

    if (duration >= LONG_PRESS_TIME) {

      buzzerEnabled = !buzzerEnabled;


      Serial.print("Buzzer ");
      Serial.println(
        buzzerEnabled ? "ENABLED" : "DISABLED"
      );


      if (buzzerEnabled) {

        tone(buzzer, 1000);
        delay(200);
        noTone(buzzer);
      }
    }


    // ==================================================
    // SHORT / LONG SIGNAL
    // ==================================================

    else {

      char symbol = '\0';


      if (duration <= SHORT_MAX) {

        symbol = 'S';

        Serial.println("Classified as SHORT");
      }

      else if (duration >= LONG_SIGNAL_MIN) {

        symbol = 'L';

        Serial.println("Classified as LONG");
      }

      else {

        Serial.println("Touch in gray zone - ignored");
      }


      if (symbol != '\0') {

        if (outgoingCount < MAX_SIGNALS) {

          outgoingSequence[outgoingCount] = symbol;

          outgoingCount++;

          outgoingSequence[outgoingCount] = '\0';

          lastTouchTime = millis();

          sequenceWaitingToSend = true;


          Serial.print("Added ");
          Serial.print(symbol);
          Serial.print(" | Sequence: ");
          Serial.println(outgoingSequence);

        }

        else {

          Serial.println("WARNING: Sequence full!");
        }
      }
    }
  }


  // ==================================================
  // 2-MINUTE INACTIVITY SLEEP
  // ==================================================

  if (!touching &&
      !playingSequence &&
      !sequenceWaitingToSend &&
      outgoingCount == 0 &&
      sequenceCount == 0 &&
      millis() - lastActivityTime >= SLEEP_TIMEOUT) {

    goToSleep();
  }
}