#include <avr/interrupt.h>
#include <avr/io.h>

#include <LiquidCrystal.h>
LiquidCrystal lcd(8, 9, 4, 5, 6, 7);

#define NO_KEY 0
#define RIGHT 1
#define LEFT 2
#define UP 3
#define DOWN 4
#define SELECT 5

// voltage of one div of A/D (based on resistor divider) in Volts
#define VOLTAGE_DIV 0.0157356
// current of one div of A/D (based on resistor divider) in Amperes
#define CURRENT_DIV 0.0021919020

// Target voltage: 7.2V possible to get from formula TARGET_VOLTAGE=7.2/VOLTAGE_DIV
#define TARGET_VOLTAGE 457
// for 14.4V
// #define TARGET_VOLTAGE 915

/* Maximum duty cycle, 100% is 255, we have SEPIC converter, so 100% duty cycle is nonsense to use, it will saturate coils and output
 voltage will be 0.
 */
#define MAX_PWM 200

byte pwm;
byte pwmp;
byte step;
volatile int voltage;
volatile int vin;
float wh = 0;
volatile boolean whcount = false;
volatile boolean display_flag = false;
boolean whdisplay = false;
int whperiod = 0;

int int_counter;
int measure_counter;
volatile int current;
boolean dir;
long lastpower;

#define VOUT_MPPT_RUNNING 0
#define VOUT_POWER_OK 1
#define VOUT_CRITICAL 2

volatile byte vout_flag;
int vout_flag_remove;

// called with frequency of 31,5kHz
ISR(TIMER2_OVF_vect) {
  int_counter += 1;
  measure_counter += 1;
  if (measure_counter == 200) {
    //reading twice to get more precise output after switching measurement MUX
    analogRead(A4);
    vin = analogRead(A4);        
  } 
  else if ( measure_counter==400 ) {
    analogRead(A3);
    current = analogRead(A3);
  } 
  else if ( measure_counter==600 ) {
    analogRead(A5);
    voltage = analogRead(A5);
    if (voltage > TARGET_VOLTAGE) {
      pwm--;
      if (voltage > (TARGET_VOLTAGE * 1.05)) {
        vout_flag = VOUT_CRITICAL;
        vout_flag_remove = 15000;
        step = step * 2;
        if (step > 16) {
          step = 16;
        }
        pwm -= step;
      }

      dir = false;
      if (vout_flag == VOUT_MPPT_RUNNING ) {
        vout_flag = VOUT_POWER_OK;
        vout_flag_remove = 5000;
        pwmp=20;
      }
    } 
    else if (vout_flag == VOUT_POWER_OK) {
      if (voltage>TARGET_VOLTAGE*0.99) {
        vout_flag_remove = 5000;
      }
      if (pwmp==0) {
        pwm++;
        pwmp=10;
      } 
      else {
        pwmp--;
      }
    }

    analogWrite(3, pwm);
    measure_counter=0;
  }

  // 5 times in second
  if (int_counter == 6300) {
    // to sync measurement in same phase
    measure_counter=0;
    int_counter = 0;
    if (vout_flag == VOUT_MPPT_RUNNING) mppt_do();    
    whcount = true;
    display_flag = true;
  }
  if (vout_flag_remove != 0) {
    vout_flag_remove--;
  } 
  else if (vout_flag!=VOUT_MPPT_RUNNING) {
    step=1;
    dir=-1;
    vout_flag = VOUT_MPPT_RUNNING;         
  }
};

int readKey()
{
  int analog = analogRead(0);

  if (analog < 65) {
    return RIGHT;
  }  
  else if (analog < 219) {
    return UP;
  } 
  else if ( analog < 394) {
    return DOWN;
  } 
  else if (analog < 601) {
    return LEFT;
  } 
  else if (analog < 872) {
    return SELECT;
  } 
  else {
    return NO_KEY;
  }
}

void setup()
{
  // Enable interrupt from timer2
  TIMSK2 |= (1 << TOIE2);
  // set up the LCD's number of columns and rows:
  TCCR2B = TCCR2B & 0b11111000 | 0b00000001;
  pwm = 40;
  voltage = 0;
  dir = true;
  step = 1;
  lastpower = 0;
  createChars();
  lcd.begin(16, 2);
  pinMode(2,OUTPUT);
  digitalWrite(2,HIGH);
}

void createChars()
{
  byte newChar[8];

  for (byte c = 0; c < 8; c++) {
    for (byte i = 0; i < 8; i++) {
      if (dir) {
        if (i > c) {
          newChar[7 - i] = B10001;
        } 
        else {
          newChar[7 - i] = B11111;
        }
      }
    }
    lcd.createChar(c, newChar);
  }
}

void mppt_do()
{
  /* try to find maximum power point, current is increased, beacause when current is low it is hard to precisely measure it so it is
   directed by voltage
   */
  long power=voltage*(current+10);
  if (lastpower > power) {
    if (step > 2) {
      step /= 2;
    } 
    else {
      if (step > 1) {
        step = 1;
      }
    }
    dir = !dir;
  } 
  else {
    step++;
    if (step > 10) {
      step = 10;
    }
  }

  if (dir) {
    if (pwm + step < MAX_PWM) pwm += step;
    else {
      pwm = MAX_PWM;
      dir = false;
    }
  } 
  else {
    if (pwm - step > 0) pwm -= step;
    else {
      pwm = 0;
      dir = true;
    }
  }
  lastpower = power;
  analogWrite(3, pwm);
}

void loop()
{
  if (whcount) addWh();
  if (display_flag) updateDisplay();
}

void updateDisplay() {
  display_flag=false;
  //line 1
  lcd.setCursor(0, 0);
  switch (vout_flag) {
  case VOUT_POWER_OK:
    lcd.print("**");
    break;
  case VOUT_CRITICAL:
    lcd.print("!!");
    break;
  case VOUT_MPPT_RUNNING:
    // step direction
    if (dir) {
      lcd.print("^");
    } 
    else {
      lcd.print("v");
    }
    // step bargraph
    byte stepWrite = step - 1;
    if (stepWrite > 7) stepWrite = 7;
    lcd.write(stepWrite);  
  }
  lcd.write(byte(0x7e));
  // pwm bargraph
  lcd.write(byte(((pwm - 1) * 8) / 200));
  lcd.setCursor(8, 0);
  if (whdisplay) {
    lcd.print("Vi=");
    lcd.print(((5. * vin / 1024.) / 470.) * (4700. + 470.));
    lcd.print("V");
  } 
  else {
    lcd.print("Io=");
    lcd.print( current * CURRENT_DIV );
    lcd.print("A");
  }
  lcd.print(" ");
  lcd.setCursor(0, 1);
  whperiod++;
  if (whperiod > 30) {
    whdisplay = !whdisplay;
    whperiod = 0;
  }
  if (whdisplay) {
    int wprint = wh * 1000;
    lcd.print(wprint / 10000);
    wprint %= 10000;
    lcd.print(wprint / 1000);
    wprint %= 1000;
    lcd.print(".");
    lcd.print(wprint / 100);
    wprint %= 100;
    lcd.print(wprint / 10);
    wprint %= 10;
    lcd.print(wprint);
    lcd.print("Wh  ");
  } 
  else {
    lcd.print((current * CURRENT_DIV) * (voltage * VOLTAGE_DIV));
    lcd.print("W   ");
  }
  lcd.setCursor(8, 1);
  lcd.print("Vo=");
  lcd.print(voltage * VOLTAGE_DIV);
}

void addWh()
{  
  whcount = false;
  wh += (((current * CURRENT_DIV) * (voltage * VOLTAGE_DIV)) / (5.*3600));
}


