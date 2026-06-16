#include <EEPROM.h>

// =======================
// Driver Pins (PORT Manipulation)
// =======================
#define STEP_BIT 1   // PORTB1 = Pin 9
#define DIR_BIT  0   // PORTB0 = Pin 8

#define START_SENSOR_PIN 7
#define STOP_SENSOR_PIN  6

// =======================
// Machine Parameters
// =======================
long maxStepsPerCycle = 1500;  // Safety limit
int extraStepsAfterStop = 80;  // Dispense margin (Adjustable via HMI)
volatile int pulseDelay = 350; // Motor speed

// Timer & Motor Variables
volatile long stepsCount = 0;
volatile bool motorRunning = false;

// State Machine
enum MachineState { IDLE, MOVING, WAIT_RELEASE };
MachineState state = IDLE;
long lastStopStep = 0;
bool stopSensorTriggered = false;

// =======================
// Timer Setup (Non-blocking Stepper)
// =======================
void setupTimer1() {
  cli();
  TCCR1A = 0;
  TCCR1B = 0;
  TCCR1B |= (1 << WGM12); // CTC Mode
  TCCR1B |= (1 << CS10);  // Prescaler = 1 (16MHz)
  OCR1B = 32 - 1;         // Fixed pulse width
  TIMSK1 = 0;
  sei();
}

void startMotor() {
  stepsCount = 0;
  motorRunning = true;
  TCNT1 = 0;
  OCR1A = (pulseDelay * 16) - 1; 
  TIMSK1 |= (1 << OCIE1A);
}

void stopMotor() {
  motorRunning = false;
  TIMSK1 = 0;
  PORTB &= ~(1 << STEP_BIT);
}

ISR(TIMER1_COMPA_vect) {
  if (!motorRunning) return;
  PORTB |= (1 << STEP_BIT);
  stepsCount++;
  TIMSK1 |= (1 << OCIE1B);
}

ISR(TIMER1_COMPB_vect) {
  PORTB &= ~(1 << STEP_BIT);
  TIMSK1 &= ~(1 << OCIE1B);
}

// =======================
// HMI Command Parser
// =======================
void processHMIData() {
  if (Serial.available()) {
    String rxString = Serial.readStringUntil('\n');
    rxString.trim(); // Clean up hidden characters
    
    if (rxString.length() == 0) return;

    if (rxString == "CMD:START") {
      Serial.println("HMI: Operator pressed START");
      if (state == IDLE) {
        stopSensorTriggered = false;
        startMotor();
        state = MOVING;
      }
    } 
    else if (rxString == "CMD:STOP") {
      Serial.println("HMI: Operator pressed STOP (E-STOP)");
      stopMotor();
      state = IDLE;
    }
    else if (rxString.startsWith("SET:MARGIN,")) {
      // Extract the number after the comma
      String valString = rxString.substring(11);
      extraStepsAfterStop = valString.toInt();
      
      Serial.print("HMI: Dispense Margin updated to -> ");
      Serial.println(extraStepsAfterStop);
      
      // Save to EEPROM Address 0 so it survives reboots
      EEPROM.put(0, extraStepsAfterStop); 
    }
  }
}

// =======================
// Setup
// =======================
void setup() {
  DDRB |= (1 << STEP_BIT);
  DDRB |= (1 << DIR_BIT);
  PORTB |= (1 << DIR_BIT); // Fixed direction

  pinMode(START_SENSOR_PIN, INPUT);
  pinMode(STOP_SENSOR_PIN, INPUT);

  // Hardware Serial handles BOTH HMI communication and debugging now
  Serial.begin(9600); 
  
  // Load saved margin from EEPROM
  EEPROM.get(0, extraStepsAfterStop);
  if (extraStepsAfterStop < 0 || extraStepsAfterStop > 500) {
    extraStepsAfterStop = 80; // Default fallback if EEPROM is uninitialized
  }

  setupTimer1();
  Serial.println("Machine Initialized. Waiting for HMI...");
}

// =======================
// Main Loop
// =======================
void loop() {
  // 1. Always check the display for new operator commands
  processHMIData();

  // 2. Run the Machine Logic
  switch (state) {
    case IDLE:
      // Physical start sensor check
      if (digitalRead(START_SENSOR_PIN) == HIGH && digitalRead(STOP_SENSOR_PIN) == LOW) {
        stopSensorTriggered = false;
        startMotor();
        state = MOVING;
        Serial.println("Cycle Started (Sensor Triggered)");
      }
      break;

    case MOVING: {
      cli(); long currentStep = stepsCount; sei();

      // Look for the gap sensor
      if (!stopSensorTriggered && digitalRead(STOP_SENSOR_PIN) == HIGH) {
        stopSensorTriggered = true;
        cli(); lastStopStep = stepsCount; sei();
        Serial.print("Gap Detected at step: ");
        Serial.println(lastStopStep);
      }

      // Calculate if we have finished the extra dispense steps
      bool extraDone = stopSensorTriggered && ((currentStep - lastStopStep) >= extraStepsAfterStop);
      bool overLimit = (currentStep >= maxStepsPerCycle);

      if (extraDone || overLimit) {
        stopMotor();
        state = WAIT_RELEASE;
        if (overLimit) Serial.println("ERROR: Max steps reached! Gap not found.");
        else Serial.println("Cycle Complete.");
      }
      break;
    }

    case WAIT_RELEASE:
      // Wait for the bottle to clear the sensor before resetting
      if (digitalRead(START_SENSOR_PIN) == LOW) {
        state = IDLE;
      }
      break;
  }
}