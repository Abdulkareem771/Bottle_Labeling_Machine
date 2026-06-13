#define STEP_PIN 9
#define DIR_PIN 8

#define START_SENSOR 7
#define STOP_SENSOR 6

int preDispenseSteps = 80;
int maxSteps = 1000;

void setup()
{
  pinMode(STEP_PIN,OUTPUT);
  pinMode(DIR_PIN,OUTPUT);

pinMode(START_SENSOR, INPUT);
pinMode(STOP_SENSOR, INPUT);

    Serial.begin(9600);

  delay(1000);

  Serial.println("STARTED");

  digitalWrite(DIR_PIN,HIGH);
}

void makeStep()
{
  digitalWrite(STEP_PIN,HIGH);
  delayMicroseconds(100);

  digitalWrite(STEP_PIN,LOW);
  delayMicroseconds(100);

  delay(1);
}

void loop()
{

  if(digitalRead(START_SENSOR))
  {
      Serial.println("Bottle Detected");

      bool gapFound = false;
      int gapStep = 0;

      for(int step=0; step<maxSteps; step++)
      {
          makeStep();

          if(!gapFound &&
             digitalRead(STOP_SENSOR))
          {
              gapFound = true;
              gapStep = step;

              Serial.print("Gap Found At ");
              Serial.println(gapStep);
          }

          if(gapFound &&
             step >= gapStep + preDispenseSteps)
          {
              Serial.println("Cycle Complete");
              break;
          }
      }

      while(digitalRead(START_SENSOR));
  }
}