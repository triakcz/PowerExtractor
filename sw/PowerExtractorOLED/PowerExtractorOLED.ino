#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <avr/interrupt.h>

Adafruit_SSD1306 display(-1);

#define SELF_POWER 7
#define CHARGE_POWER 8
#define PWM_OUT 10
#define LED_FRONT 5
#define LED_TAIL 6
#define ROTATION_INPUT 2
#define SENSE_VI_CONV (5.*((330.+33)/33)/1024)
#define SENSE_VO_CONV (5.*((33.+10)/10)/1024)
#define SENSE_VI_UNIT "V"
#define SENSE_VO_UNIT "V"
#define SENSE_IO_CONV (((5/1024)/50)/0.01)
#define SENSE_II_CONV (((5./1024)/50)/0.01)
#define SENSE_IO_UNIT "A"
#define SENSE_II_UNIT "A"

#define SENSE_LIGHT 0
#define SENSE_II 2
#define SENSE_IO 6
#define SENSE_VI 1
#define SENSE_VO 7

#define ADC_MAX_SAMPLES 500

#define NUM_KEYS 4
const uint8_t KEYS[NUM_KEYS] = {9,4,12,11};

#define PWM_MAX 220

struct AdcOneValHolder {
  uint32_t value;
  uint16_t count;
};

struct AdcValuesHolder {
  uint16_t LIGHT;
  AdcOneValHolder VI;
  AdcOneValHolder VO;
  AdcOneValHolder II;
  AdcOneValHolder IO;
  boolean process;
};

struct AdcFilteredHolder {
  uint16_t light;
  uint16_t VI;
  uint16_t VO;
  uint16_t II;
  uint16_t IO;
};

volatile struct AdcValuesHolder adcvalues;
volatile struct AdcValuesHolder adcvalues2;
volatile struct AdcFilteredHolder adcfiltered;
uint16_t turnOffCounter=0xff;
uint8_t pwm=50;
volatile uint8_t pwm_front=0;

uint8_t blink;
volatile uint16_t rotations=0;

void rotation_interrupt() {
  rotations++;
  shiftAdcResult();
}

void setup() {
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);  // initialize with the I2C addr 0x3C (for the 128x64)
  display.clearDisplay();
  pinMode(13, OUTPUT);
  digitalWrite(13, HIGH);
  display.display();
  pinMode(SENSE_VI,INPUT);
  pinMode(SENSE_VO,INPUT);
  pinMode(SENSE_II,INPUT);
  pinMode(SENSE_IO,INPUT);
  pinMode(SELF_POWER,OUTPUT);
  pinMode(CHARGE_POWER,OUTPUT);
  digitalWrite(CHARGE_POWER,HIGH);
  digitalWrite(SELF_POWER,HIGH);
  pinMode(PWM_OUT,OUTPUT);
  pinMode(LED_TAIL,OUTPUT);
  pinMode(LED_FRONT,OUTPUT);
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
  ADCSRA = 0;
  ADCSRB = 0;
  ADMUX |= (1 << REFS0); //set reference voltage
  ADCSRA |= (1 << ADPS2) | (1 << ADPS0); //ADC CLK 16mHz/32/13 ... cca 38kHz
  ADCSRA |= (1 << ADATE); //free running mode (not setting any ADTS bits)
  ADCSRA |= (1 << ADIE); //enable interrupts when measurement complete
  ADCSRA |= (1 << ADEN); //enable ADC
  ADCSRA |= (1 << ADSC); //start ADC measurements
}

volatile uint16_t timercnt=0;

ISR(TIMER2_OVF_vect) {
  timercnt++;  
  blink++;
  if (blink==14 && pwm_front > 0) {
    digitalWrite(LED_TAIL,HIGH);
  } else if (blink == 16) {
    digitalWrite(LED_TAIL,LOW);
    blink=0;
  }     
}

uint8_t adcidx=SENSE_LIGHT;

void shiftAdcResult() {
  cli();
  adcvalues2.VI.value=adcvalues.VI.value;
  adcvalues2.VI.count=adcvalues.VI.count;
  adcvalues2.VO.value=adcvalues.VO.value;
  adcvalues2.VO.count=adcvalues.VO.count;
  adcvalues2.II.value=adcvalues.II.value;
  adcvalues2.II.count=adcvalues.II.count;
  adcvalues2.IO.value=adcvalues.IO.value;
  adcvalues2.IO.count=adcvalues.IO.count;
  adcvalues2.LIGHT=adcvalues.LIGHT;
  adcvalues2.process=true;
  adcvalues.VI.value=0;
  adcvalues.VI.count=0;
  adcvalues.II.value=0;
  adcvalues.II.count=0;
  adcvalues.VO.value=0;
  adcvalues.VO.count=0;
  adcvalues.IO.value=0;
  adcvalues.IO.count=0;
  adcvalues.LIGHT=0;
  adcvalues.process=false;
  sei();
}

ISR(ADC_vect) {
  uint8_t low = ADCL;
  uint8_t high = ADCH;
  uint16_t val = (high << 8) | low;
  ADMUX&=~(0x07);
  switch (adcidx) {
    case SENSE_LIGHT:
      ADMUX|=SENSE_II;
      adcvalues.LIGHT=val;
      if (adcvalues.VO.count > ADC_MAX_SAMPLES || adcvalues.process) {
        shiftAdcResult();
      }
      break;
    case SENSE_II:
      ADMUX|=SENSE_IO;
      adcvalues.II.value+=val;
      adcvalues.II.count++;
      if (adcvalues.VO.count > ADC_MAX_SAMPLES || adcvalues.process) {
        shiftAdcResult();
      }
      break;
    case SENSE_IO:
      ADMUX|=SENSE_VI;
      adcvalues.IO.value+=val;
      adcvalues.IO.count++;
      if (adcvalues.VO.count > ADC_MAX_SAMPLES || adcvalues.process) {
        shiftAdcResult();
      }
      break;
    case SENSE_VI:
      ADMUX|=SENSE_VO;
      adcvalues.VI.value+=val;
      adcvalues.VI.count++;
      if (adcvalues.VI.count > ADC_MAX_SAMPLES || adcvalues.process) {
        shiftAdcResult();
      }
      break;
    case SENSE_VO:
      ADMUX|=SENSE_LIGHT;
      adcvalues.VO.value+=val;
      adcvalues.VO.count++;
      if (adcvalues.VO.count > ADC_MAX_SAMPLES || adcvalues.process) {
        shiftAdcResult();
      }
      break;
  }
  adcidx=(ADMUX&0x07);
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
  display.print(str(TYPE) "= ");\
  display.print(adcfiltered.TYPE * SENSE_##TYPE##_CONV );\
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

void loop() {
  boolean blink;
  if (blink) {
    digitalWrite(13, HIGH);
  } else { 
    digitalWrite(13,LOW);
  }
  blink = !blink;
  if (adcvalues2.process) {
    adcfiltered.VI=adcvalues2.VI.value/adcvalues2.VI.count;
    adcfiltered.VO=adcvalues2.VO.value/adcvalues2.VO.count;
    adcfiltered.II=adcvalues2.II.value/adcvalues2.II.count;
    adcfiltered.IO=adcvalues2.IO.value/adcvalues2.IO.count;
    adcvalues2.process=false;
  }
  display.clearDisplay();
  display.setTextColor(WHITE);
  display.setTextSize(2);
  display.setCursor(0,0);
//  display.print(adcvalues.VO);
  dispADVal(VO);
  display.println();
//  display.print(adcvalues.VI);
  dispADVal(VI);
  display.println();
//  display.print(adcvalues.IO);
  dispADVal(IO);
  display.println();
//  display.print(adcvalues.II);
  dispADVal(II);
  display.println();
  display.display();  
  switch (handleKbd()) {
    case 1:    
      pwm_front+=32;
      break;
    case 2:
      if (pwm_front>0) pwm_front-=32;
      break;
    case 3:
      pwm+=10;
      break;
    case 4:
      pwm-=10;
      break;
  }
  analogWrite(PWM_OUT,pwm);
  analogWrite(LED_FRONT,pwm_front);
  
  if (pwm_front > 0) {
    turnOffCounter=0xff;
  }
  if (turnOffCounter>0) {
    turnOffCounter--;
    digitalWrite(SELF_POWER,HIGH);
  } else {
    digitalWrite(SELF_POWER,LOW);    
  }
}
