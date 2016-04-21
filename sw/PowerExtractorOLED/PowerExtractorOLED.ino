#define SELF_POWER 7
#define PWM_OUT 10
#define ROTATION_INPUT 2
#define SENSE_VI A1
#define SENSE_VO A7
#define SENSE_VI_CONV (5.*((330.+33)/33)/1024)
#define SENSE_VO_CONV (5.*((33.+10)/10)/1024)
#define SENSE_VI_UNIT "V"
#define SENSE_VO_UNIT "V"

#define NUM_KEYS 4
const uint8_t KEYS[NUM_KEYS] = {9,4,12,11};

#define PWM_MAX 220

#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <avr/interrupt.h>

Adafruit_SSD1306 display(-1);

volatile uint16_t rotations=0;
void rotation_interrupt() {
  rotations++;
}

void setup() {                
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);  // initialize with the I2C addr 0x3D (for the 128x64)
  display.clearDisplay();  
  display.display();
  pinMode(SENSE_VI,INPUT);
  pinMode(SENSE_VO,INPUT);
  pinMode(SELF_POWER,OUTPUT);
  digitalWrite(SELF_POWER,HIGH);
  pinMode(PWM_OUT,OUTPUT);
  digitalWrite(PWM_OUT,LOW);
  for (uint8_t i=0;i<NUM_KEYS;i++) {
    pinMode(KEYS[i], INPUT_PULLUP);
  }
  // http://playground.arduino.cc/Main/TimerPWMCheatsheet
  TCCR1B = (TCCR1B & 0b11111000) | 0x01;
  
  pinMode(ROTATION_INPUT, INPUT);
  attachInterrupt(0, rotation_interrupt, RISING);
  
  // Setup timer2 for interrupt
  TIMSK2 |= (1 << TOIE2);
  TCCR2B = TCCR2B & 0b11111000 | 0x07;
}

volatile uint16_t timercnt=0;
ISR(TIMER2_OVF_vect) {
  timercnt++; 
}

int i;

#define CHAR_X_SIZE 6
#define CHAR_Y_SIZE 7
#define KMH_SIZE 9
#define KMH_L_SIZE 3

#define BAT_X1 2*KMH_SIZE*CHAR_X_SIZE+1
#define BAT_Y1 5
#define BAT_W 18
#define BAT_H 32

void displaykmh(int value) {
  byte posx=0;
  if (value < 100) {
    posx=KMH_SIZE*CHAR_X_SIZE;
  }
  display.setCursor(posx,0);  
  display.setTextSize(KMH_SIZE);
  display.setTextColor(WHITE);
  display.println(i/10);
  display.setTextSize(KMH_L_SIZE);
  display.setCursor(2*KMH_SIZE*CHAR_X_SIZE,CHAR_Y_SIZE * (KMH_SIZE - KMH_L_SIZE));
  display.println(i%10);
}

void displayBatSymbol(byte chargeLevel) {
  if (chargeLevel > 10) {
    chargeLevel=10;
  }
  display.drawRect(BAT_X1, BAT_Y1, BAT_W, BAT_H, WHITE);
  display.fillRect(BAT_X1+BAT_W/3, BAT_Y1-(BAT_H/10), BAT_W/3, (BAT_H/10), WHITE);
  display.fillRect(BAT_X1, BAT_Y1+((BAT_H*(10-chargeLevel))/10), BAT_W, BAT_H-((BAT_H*(10-chargeLevel))/10), WHITE);
}

#define str(x) #x
#define dispADVal(TYPE) {\
  display.print(str(TYPE) " = ");\
  display.print(analogRead(SENSE_##TYPE) * SENSE_##TYPE##_CONV );\
  display.print(SENSE_##TYPE##_UNIT);\
}

byte keyState=0xff;

byte handleKbd() {
  for (uint8_t i=0;i<NUM_KEYS;i++) {
    if (!digitalRead(KEYS[i])) {
      if (keyState & 1<<i ) {
        keyState &= ~(1<<i);
        return i+1;
      }
    } else {
      keyState |= 1<<i;
    }
  }
  return 0;
}

void turnOff() {
  digitalWrite(SELF_POWER,LOW);  
}

uint16_t turnOffCounter=0x5ff;
uint8_t pwm=0;
void loop() {
  display.setTextColor(WHITE);
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0,0);
  dispADVal(VO);
  display.setCursor(0,10);
  dispADVal(VI);
  display.setCursor(0,20);
  switch (handleKbd()) {
    case 1:
      if (pwm+10<PWM_MAX) pwm+=10;
      break;
    case 2:
      if (pwm>10) pwm-=10;
      break;
    case 3:
      if (pwm<PWM_MAX) pwm++;
      break;
    case 4:
      if (pwm>0) pwm--;
      break;
  }
  display.print("PWM: ");
  display.println(pwm);
  display.print("turnOffCounter:");
  display.println(turnOffCounter);
  display.print("rotations:");
  display.println(rotations);
  display.print("timercnt:");
  display.println(timercnt);
  display.display();  
  analogWrite(PWM_OUT,pwm);
  display.clearDisplay();
  if (turnOffCounter>0) {
    turnOffCounter--;
  } else {
    turnOff();
  }
}
