#include <EEPROM.h>

// Pins
#define STEP_BIT 1   // Pin 9
#define DIR_BIT  0   // Pin 8
#define START_SENSOR_PIN 7
#define STOP_SENSOR_PIN  2 

// EEPROM Addresses
#define ADDR_MARGIN 0  
#define ADDR_SPEED  4  
#define ADDR_LIMIT  8  
#define ADDR_DELAY  12 
#define ADDR_COUNT  16 

// Global Variables
long maxStepsPerCycle = 1500;
int extraStepsAfterStop = 80;
volatile int pulseDelay = 350;
int startDelayMs = 0;
long batchCount = 0;

volatile long stepsCount = 0;
volatile bool motorRunning = false;
volatile long lastStopStep = 0;
volatile bool stopSensorTriggered = false;

enum SystemState { STATE_OFF, STATE_AUTO_WAIT_BOTTLE, STATE_AUTO_WAIT_DELAY, 
                   STATE_AUTO_DISPENSING, STATE_AUTO_WAIT_BOTTLE_CLEAR, STATE_JOG_DISPENSING };
SystemState state = STATE_OFF;
SystemState lastState = STATE_OFF;
unsigned long delayStartTime = 0;

// Interrupt
void gapSensorISR() {
  if ((state == STATE_AUTO_DISPENSING || state == STATE_JOG_DISPENSING) && stepsCount > 300) {
    if (!stopSensorTriggered) { lastStopStep = stepsCount; stopSensorTriggered = true; }
  }
}

// HMI Updates
void updateHMI(String msg) {
  Serial.print("page0.t0.txt=\"" + msg + "\"");
  Serial.write(0xFF); Serial.write(0xFF); Serial.write(0xFF);
}

void updateBatchCount() {
  Serial.print("page0.t3.val=" + String(batchCount));
  Serial.write(0xFF); Serial.write(0xFF); Serial.write(0xFF);
}

// Timer Logic
void setupTimer1() {
  cli(); TCCR1A = 0; TCCR1B = 0;
  TCCR1B |= (1 << WGM12); TCCR1B |= (1 << CS10);
  OCR1B = 32 - 1; TIMSK1 = 0; sei();
}

void startMotor() {
  stepsCount = 0; motorRunning = true; TCNT1 = 0;
  OCR1A = (pulseDelay * 16) - 1; TIMSK1 |= (1 << OCIE1A);
}

void stopMotor() {
  motorRunning = false; TIMSK1 = 0;
  PORTB &= ~(1 << STEP_BIT);
}

ISR(TIMER1_COMPA_vect) {
  if (!motorRunning) return;
  PORTB |= (1 << STEP_BIT); stepsCount++;
  TIMSK1 |= (1 << OCIE1B);
}

ISR(TIMER1_COMPB_vect) {
  PORTB &= ~(1 << STEP_BIT); TIMSK1 &= ~(1 << OCIE1B);
}

// Main logic
void processHMIData() {
  if (!Serial.available()) return;
  String rx = Serial.readStringUntil('\n'); rx.trim();
  if (rx.length() == 0) return;

  if (rx == "CMD:START") { if (state == STATE_OFF) state = STATE_AUTO_WAIT_BOTTLE; }
  else if (rx == "CMD:STOP") { stopMotor(); state = STATE_OFF; }
  else if (rx == "CMD:JOG") { if (state == STATE_OFF) { stopSensorTriggered = false; startMotor(); state = STATE_JOG_DISPENSING; } }
  else if (rx == "CMD:RESET_COUNT") { batchCount = 0; updateBatchCount(); EEPROM.put(ADDR_COUNT, batchCount); }
  
  // Dirty Check Saves
  else if (rx.startsWith("SET:MARGIN,")) {
    int v = rx.substring(11).toInt();
    if(v != extraStepsAfterStop) { extraStepsAfterStop = v; EEPROM.put(ADDR_MARGIN, extraStepsAfterStop); }
  }
  else if (rx.startsWith("SET:SPEED,")) {
    int v = rx.substring(10).toInt();
    if(v != pulseDelay) { pulseDelay = v; EEPROM.put(ADDR_SPEED, pulseDelay); }
  }
  else if (rx.startsWith("SET:DELAY,")) {
    int v = rx.substring(10).toInt();
    if(v != startDelayMs) { startDelayMs = v; EEPROM.put(ADDR_DELAY, startDelayMs); }
  }
}

void setup() {
  DDRB |= (1 << STEP_BIT) | (1 << DIR_BIT); PORTB |= (1 << DIR_BIT);
  pinMode(START_SENSOR_PIN, INPUT); pinMode(STOP_SENSOR_PIN, INPUT);
  attachInterrupt(digitalPinToInterrupt(STOP_SENSOR_PIN), gapSensorISR, RISING);
  Serial.begin(9600);
  EEPROM.get(ADDR_MARGIN, extraStepsAfterStop);
  EEPROM.get(ADDR_SPEED, pulseDelay);
  EEPROM.get(ADDR_DELAY, startDelayMs);
  EEPROM.get(ADDR_COUNT, batchCount);
  setupTimer1();
}

void loop() {
  processHMIData();
  
  // State Change HMI update
  if (state != lastState) {
    if (state == STATE_OFF) updateHMI("IDLE");
    else if (state == STATE_AUTO_WAIT_BOTTLE) updateHMI("ARMED");
    lastState = state;
  }

  switch (state) {
    case STATE_AUTO_WAIT_BOTTLE:
      if (digitalRead(START_SENSOR_PIN) == HIGH) {
        if (startDelayMs > 0) { delayStartTime = millis(); state = STATE_AUTO_WAIT_DELAY; }
        else { stopSensorTriggered = false; startMotor(); state = STATE_AUTO_DISPENSING; }
      }
      break;

    case STATE_AUTO_WAIT_DELAY:
      if (millis() - delayStartTime >= startDelayMs) {
        stopSensorTriggered = false; startMotor(); state = STATE_AUTO_DISPENSING;
      }
      break;

    case STATE_AUTO_DISPENSING:
    case STATE_JOG_DISPENSING:
      if (stopSensorTriggered && (stepsCount - lastStopStep) >= extraStepsAfterStop) {
        stopMotor();
        if (state == STATE_AUTO_DISPENSING) { batchCount++; updateBatchCount(); EEPROM.put(ADDR_COUNT, batchCount); state = STATE_AUTO_WAIT_BOTTLE_CLEAR; }
        else state = STATE_OFF;
      }
      if (stepsCount >= maxStepsPerCycle) { stopMotor(); state = STATE_OFF; }
      break;

    case STATE_AUTO_WAIT_BOTTLE_CLEAR:
      if (digitalRead(START_SENSOR_PIN) == LOW) state = STATE_AUTO_WAIT_BOTTLE;
      break;
  }
}