// =======================
// Driver Pins (PORT)
// =======================
// STEP -> Pin 9  (PORTB1)
// DIR  -> Pin 8  (PORTB0)

#define STEP_BIT 1   // PORTB1 = Pin 9
#define DIR_BIT  0   // PORTB0 = Pin 8

// =======================
// Sensors
// =======================
#define START_SENSOR_PIN 7
#define STOP_SENSOR_PIN  6

// =======================
// Motion Settings
// =======================
const long stepsPerLabel = 720;      // عدد خطوات الليبل المحسوب
const long maxStepsPerCycle = 1000;   // حد الأمان الأقصى
const int extraStepsAfterStop = 80;  // خطوات التعويض بعد الحساس

volatile int pulseDelay = 350;        // سرعة التشغيل (ميكروثانية)

// =======================
// Pulse width settings (بدون delay)
// =======================
#define F_CPU 16000000UL
#define PULSE_WIDTH_US  2
#define PULSE_TICKS     ( (PULSE_WIDTH_US * (F_CPU / 1000000UL)) )  // عدد دورات الساعة = 32

// =======================
// Timer & Motor Variables
// =======================
volatile long stepsCount = 0;
volatile bool motorRunning = false;

// =======================
// Timer1 Setup (CTC, بدون delay)
// =======================
void setupTimer1() {
  cli();
  TCCR1A = 0;
  TCCR1B = 0;
  // CTC Mode, TOP = OCR1A
  TCCR1B |= (1 << WGM12);
  // Prescaler = 1 (16MHz)
  TCCR1B |= (1 << CS10);
  // عرض النبضة الثابت عبر OCR1B
  OCR1B = PULSE_TICKS - 1;
  TIMSK1 = 0;   // سنفعل المقاطعات عند الحاجة
  sei();
}

// =======================
// Start/Stop Motor
// =======================
void startMotor() {
  stepsCount = 0;
  motorRunning = true;
  TCNT1 = 0;
  OCR1A = (pulseDelay * (F_CPU / 1000000UL)) - 1;  // الفترة بين الخطوات
  TIMSK1 |= (1 << OCIE1A);                         // فعّل مقاطعة A فقط
}

void stopMotor() {
  motorRunning = false;
  TIMSK1 = 0;                          // أوقف جميع مقاطعات المؤقت
  PORTB &= ~(1 << STEP_BIT);           // تأكيد LOW على STEP
}

// =======================
// Timer Interrupts (توليد النبضة بدون delay)
// =======================
ISR(TIMER1_COMPA_vect) {
  if (!motorRunning) return;

  PORTB |= (1 << STEP_BIT);            // ارفع STEP
  stepsCount++;                        // زيادة العداد

  TIMSK1 |= (1 << OCIE1B);            // فعّل مقاطعة B لإنهاء النبضة
}

ISR(TIMER1_COMPB_vect) {
  PORTB &= ~(1 << STEP_BIT);           // أنزل STEP
  TIMSK1 &= ~(1 << OCIE1B);           // عطّل مقاطعة B حتى النبضة التالية
}

// =======================
// Setup
// =======================
void setup() {
  DDRB |= (1 << STEP_BIT);
  DDRB |= (1 << DIR_BIT);
  PORTB |= (1 << DIR_BIT);            // اتجاه ثابت

  pinMode(START_SENSOR_PIN, INPUT);
  pinMode(STOP_SENSOR_PIN, INPUT);

  setupTimer1();
  delay(100);
}

// =======================
// Main Loop (غير حاجزة تماماً - لا يوجد delay)
// =======================
void loop() {
  static enum { IDLE, WAIT_START, MOVING, WAIT_RELEASE } state = IDLE;
  static unsigned long lastTime = 0;
  static long lastStopStep = 0;
  static bool stopSensorTriggered = false;

  switch (state) {
    case IDLE:
      if (digitalRead(START_SENSOR_PIN) == HIGH) {
        // تأكد أن STOP غير مفعل
        if (digitalRead(STOP_SENSOR_PIN) == HIGH) {
          // انتظر حتى يتحرر STOP (بدون delay)
          state = WAIT_START;  // سنعود إذا كان STOP عالي
        } else {
          // بدء دورة جديدة
          stopSensorTriggered = false;
          startMotor();
          state = MOVING;
        }
      }
      break;

    case WAIT_START:
      // ننتظر تحرير STOP قبل البدء (تأخير 5ms غير حاجز)
      if (digitalRead(STOP_SENSOR_PIN) == LOW) {
        // تحرر STOP، ابدأ
        stopSensorTriggered = false;
        startMotor();
        state = MOVING;
      } else {
        // لم يتحرر، انتظر 5ms ثم تحقق مجدداً
        if (millis() - lastTime >= 5) {
          lastTime = millis();
          // لا شيء، فقط توقف هنا بدون حجب
        }
      }
      break;

    case MOVING: {
      // قراءة آمنة للعداد (Atomic read)
      cli();
      long currentStep = stepsCount;
      sei();

      // تحقق من مستشعر التوقف
      if (!stopSensorTriggered && digitalRead(STOP_SENSOR_PIN) == HIGH) {
        stopSensorTriggered = true;
        cli();
        lastStopStep = stepsCount;
        sei();
      }

      // شروط التوقف
      bool extraDone = stopSensorTriggered && ((currentStep - lastStopStep) >= extraStepsAfterStop);
      bool overLimit = (currentStep >= maxStepsPerCycle);   // استخدام maxStepsPerCycle كحد أمان

     if (extraDone || overLimit) {
     //if (overLimit) {
        stopMotor();
        state = WAIT_RELEASE;
      }
      break;
    }

    case WAIT_RELEASE:
      // انتظر تحرير زر البدء (بدون delay)
      if (digitalRead(START_SENSOR_PIN) == LOW) {
        state = IDLE;
      } else {
        // حجب بسيط بـ 10ms (غير مؤثر)
        if (millis() - lastTime >= 10) {
          lastTime = millis();
        }
      }
      break;
  }
}