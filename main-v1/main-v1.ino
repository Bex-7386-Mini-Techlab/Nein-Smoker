#include <MQ135.h>

// -----------------------------
// Nein-Smoker
// ESP8266 Generic Module
// -----------------------------

#define PIN_MQ135   A0
#define PIN_BUZZER  13   // D7
#define PIN_WARNLED 14   // D5
#define PIN_DEADLED 15   // D8
#define PIN_SAFELED 12   // D6

MQ135 mq135_sensor(PIN_MQ135);

// Fixed environmental values for correction
// Better: replace with real DHT readings later
float temperature = 21.0;
float humidity    = 81.0;

// Thresholds requested by you
const float SAFE_LIMIT    = 350000.0;
const float WARNING_LIMIT = 550000.0;

// Timing
const unsigned long READ_INTERVAL_MS   = 500;
const unsigned long WARN_BLINK_MS      = 700;  // slow blink/beep
const unsigned long DEAD_BLINK_MS      = 150;  // rapid blink/beep
const unsigned long BUZZER_BEEP_MS     = 200;

unsigned long lastReadTime   = 0;
unsigned long lastBlinkTime  = 0;
unsigned long lastBeepTime   = 0;

bool blinkState = false;
bool beepState  = false;

float correctedPPM = 0.0;

enum SmokeState {
  SAFE_STATE,
  WARNING_STATE,
  DEATH_STATE
};

SmokeState currentState = SAFE_STATE;

void allOff() {
  digitalWrite(PIN_SAFELED, LOW);
  digitalWrite(PIN_WARNLED, LOW);
  digitalWrite(PIN_DEADLED, LOW);
  digitalWrite(PIN_BUZZER, LOW);
}

void setup() {
  Serial.begin(115200);
  delay(200);

  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_WARNLED, OUTPUT);
  pinMode(PIN_DEADLED, OUTPUT);
  pinMode(PIN_SAFELED, OUTPUT);

  allOff();
  digitalWrite(PIN_SAFELED, HIGH);   // start safe

  Serial.println();
  Serial.println("=== Nein-Smoker Booting ===");
}

void loop() {
  unsigned long now = millis();

  // Read sensor periodically
  if (now - lastReadTime >= READ_INTERVAL_MS) {
    lastReadTime = now;

    correctedPPM = mq135_sensor.getCorrectedPPM(temperature, humidity);

    if (correctedPPM < SAFE_LIMIT) {
      currentState = SAFE_STATE;
    } else if (correctedPPM <= WARNING_LIMIT) {
      currentState = WARNING_STATE;
    } else {
      currentState = DEATH_STATE;
    }

    Serial.print("Corrected PPM: ");
    Serial.print(correctedPPM);
    Serial.print(" | State: ");

    if (currentState == SAFE_STATE) Serial.println("SAFE");
    else if (currentState == WARNING_STATE) Serial.println("WARNING");
    else Serial.println("DEATH");
  }

  // State machine outputs
  switch (currentState) {
    case SAFE_STATE:
      digitalWrite(PIN_SAFELED, HIGH);
      digitalWrite(PIN_WARNLED, LOW);
      digitalWrite(PIN_DEADLED, LOW);
      digitalWrite(PIN_BUZZER, LOW);
      blinkState = false;
      beepState = false;
      break;

    case WARNING_STATE:
      digitalWrite(PIN_SAFELED, LOW);
      digitalWrite(PIN_DEADLED, LOW);

      if (now - lastBlinkTime >= WARN_BLINK_MS) {
        lastBlinkTime = now;
        blinkState = !blinkState;
        digitalWrite(PIN_WARNLED, blinkState ? HIGH : LOW);
      }

      if (now - lastBeepTime >= BUZZER_BEEP_MS) {
        lastBeepTime = now;
        beepState = !beepState;
        digitalWrite(PIN_BUZZER, beepState ? HIGH : LOW);
      }
      break;

    case DEATH_STATE:
      digitalWrite(PIN_SAFELED, LOW);
      digitalWrite(PIN_WARNLED, LOW);

      if (now - lastBlinkTime >= DEAD_BLINK_MS) {
        lastBlinkTime = now;
        blinkState = !blinkState;
        digitalWrite(PIN_DEADLED, blinkState ? HIGH : LOW);
      }

      if (now - lastBeepTime >= DEAD_BLINK_MS) {
        lastBeepTime = now;
        beepState = !beepState;
        digitalWrite(PIN_BUZZER, beepState ? HIGH : LOW);
      }
      break;
  }
}