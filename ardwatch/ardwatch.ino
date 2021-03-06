/*
Arduino-based watch!
*/
#include <avr/wdt.h>
#include <avr/sleep.h>
#include <avr/interrupt.h>
#include <MeetAndroid.h>
#include "Wire.h"
#include "SeeedOLED.h"
#include <Time.h>
#include "requestbuf.h"
// #include "numbers.h"
 
 void recheckConnection(uint8_t, uint8_t);
 MeetAndroid meetAndroid(recheckConnection);
 void setArdTime(byte flag, byte numOfValues);
 void setText(byte flag, byte numOfValues);
 void handleBluetoothInit();
 void handleClockTasks();
 void wakeClock();
 void connectBluetooth();
 void sleepClock();
 void setupBluetoothConnection();
 void checkBluetoothReply();
 void sendBlueToothCommand(char command[]);
 
 static const unsigned long BLANK_INTERVAL_MS = 10000;
 static const unsigned long BUZZ_INTERVAL_MS = 500;
 static const unsigned long COMM_INTERVAL_MS = 150;
 //static const time_t TEMP_INTERVAL_S = 60;
 //static const time_t BUTTON_TIME_MS = 2000;
 static unsigned long blankCounter;
 //static unsigned long buttonCounter = 0;
 static unsigned long buzzCounter;
 static unsigned long commCounter;
 //static volatile uint8_t int0_awake = 0;
 static volatile uint8_t pcint2_awake;
 
 static const uint8_t display_shdn = 6;
 static const uint8_t button = 2;
 static const uint8_t led = 8;
 static const uint8_t motor_l = 5;
 
 enum BTState { BT_INIT = 0, BT_READY, BT_INQ, BT_CONNECTING, BT_CONNECTED };
 BTState btState = BT_INIT;
 
 enum ProgState { PS_INITIALIZING, PS_WAITREPLY, PS_NOT_CONNECTED, PS_CONNECTED, PS_DISCONNECTED };
 ProgState currentState = PS_INITIALIZING;
 
 enum RespState { RS_NONE, RS_PLUS, RS_B, RS_T1, RS_S, RS_T2, RS_A, RS_T3, RS_E, RS_COL };
 RespState respState = RS_NONE;
 
#define WATCHDOG_INTERVAL (WDTO_500MS)

void setup() 
{ 
  // TODO: detect brownout and keep processor shut down with BOD disabled
  // we'll need to let it wake when charge power is applied, hook !CHG to INT1
  
  // Set the LED to indicate we're initializing
  pinMode(led, OUTPUT);
  digitalWrite(led, LOW);
  
  pinMode(motor_l, OUTPUT);
  
  Serial.begin(38400); //Set BluetoothFrame BaudRate to default baud rate 38400
  initTime();
  // temporarily disable the watchdog
  wdt_disable();
  
  // Commence system initialization
  Wire.begin();
  SeeedOled.init();
  //digitalWrite(led, HIGH);
  
//  delay(100);
//  SeeedOled.sendCommand(SeeedOLED_Display_Off_Cmd);
  //digitalWrite(led, LOW);
   
  SeeedOled.clearDisplay();
  //SeeedOled.sendCommand(SeeedOLED_Display_On_Cmd);
  SeeedOled.setNormalDisplay();
  SeeedOled.setHorizontalMode();
  //SeeedOled.setTextXY(3,4);
  //SeeedOled.sendCommand(SeeedOLED_Display_On_Cmd);
  
  //SeeedOled.putString("Init...");
  //digitalWrite(led, HIGH);
  
  setTime(0, 0, 0, 1, 1, 2012);
  meetAndroid.flush();
  meetAndroid.registerFunction(setArdTime, 't');
  meetAndroid.registerFunction(setText, 'x');
  //meetAndroid.registerFunction(sendBattery, 'b');
  //meetAndroid.registerFunction(recheckConnection, '+');

  // enable pullup on the wake button so we can read it
  pinMode(button, INPUT);
  digitalWrite(button, HIGH);
  MCUCR &= ~_BV(PUD);
  
  // Set interrupt on the button press
  cli();
  // set up pcint2 for changes to rxd and the button
  PCIFR |= _BV(PCIF2);
  PCMSK2 |= _BV(PCINT16) | _BV(PCINT18);
  sei();
  
  // Set up shutdown for display boost converter
  digitalWrite(display_shdn, LOW);
  pinMode(display_shdn, INPUT);
  
  // indicate initialization done    
  //meetAndroid.send("init done");
  wdt_enable(WATCHDOG_INTERVAL);
  //digitalWrite(led, LOW);
} 
 
void loop() 
{ 
  // walk the dog
  wdt_reset();
  
  if (currentState == PS_CONNECTED) {
    // check for updates from the outside world
    meetAndroid.receive();
  } else {
    handleBluetoothInit();
  }
  
  /* wdt_reset */;
  // handle the inside world
  handleClockTasks();
} 

// TODO: allow set using formatted string?
void setArdTime(byte flag, byte numOfValues) {
  wdt_disable();
  long newTime = meetAndroid.getLong();
  if (newTime > 0) {
    setTime(newTime);
  }
  wdt_reset();
  wdt_enable(WATCHDOG_INTERVAL);
}

// We got a text!
void setText(byte flag, byte numOfValues) {
  // wake up so the user sees the text message
  if (blankCounter == 0) {
    wakeClock();
  } 
  blankCounter = millis();
  buzzCounter = millis();
  digitalWrite(motor_l, HIGH);
  
  SeeedOled.setTextXY(1,0);
  
  int length = meetAndroid.stringLength();
  if (length > 32) {
    meetAndroid.send("trunc");
    length = 32;
  }
  int ii = length;
  
  // define an array with the appropriate size which will store the string
  int data;
  
  // tell MeetAndroid to put the string into your prepared array
  while (length-- > 0 && (data = meetAndroid.getChar()) > 0) {
    wdt_reset();
    SeeedOled.putChar(data);
  }
  while (ii++ < 32) { SeeedOled.putChar(' '); wdt_reset(); }
}

void handleClockTasks() {
  // Nothing to do until we're initialized
  if (currentState <= PS_WAITREPLY) {
    return;
  }
  
  // Extend the wake if the button is down
  if (digitalRead(button) == LOW) {
    digitalWrite(led, HIGH);
//    if (buttonCounter == 0) {
//          meetAndroid.send('b');
//      buttonCounter = millis();
//    } else {
//          meetAndroid.send('B');
//      if (buttonCounter != 0xffffffff && millis() - buttonCounter >= BUTTON_TIME_MS) {
//        currentState = PS_NOT_CONNECTED;
//        buttonCounter = 0xffffffff;
//      }
//    }
    if (blankCounter == 0) {
      wakeClock();
    } else {
      blankCounter = millis();
    }
    if (currentState == PS_DISCONNECTED || currentState == PS_NOT_CONNECTED) {
        connectBluetooth();
    }
  } else {
    //buttonCounter = 0;
    digitalWrite(led, LOW);
  }

  if (millis() - buzzCounter >= BUZZ_INTERVAL_MS) {
    digitalWrite(motor_l, LOW);
    buzzCounter = 0;
  }
  
  // If we're awake, see if it's time to sleep
  if (millis() - blankCounter >= BLANK_INTERVAL_MS && millis() - commCounter >= COMM_INTERVAL_MS) {
    sleepClock();
  }
  
  // If we're awake, display the time
  static time_t last_display;
  if (blankCounter && last_display != now()) {
    char tbuf[32];
    last_display = now();
    
    SeeedOled.setTextXY(3,0);
    snprintf(tbuf, 16, "%02d:%02d:%02d ", hour(), minute(), second());
    SeeedOled.putString(tbuf);
    
    SeeedOled.setTextXY(5,0);
    snprintf(tbuf, 16, "%02d/%02d/%04d %.3s", month(), day(), year(), dayShortStr(weekday()));
    SeeedOled.putString(tbuf);
        
    wdt_reset();
  }
  
//  if (int0_awake) {
//    int0_awake = 0;
//    meetAndroid.send("w0");
//  }
  if (pcint2_awake) {
    pcint2_awake = 0;
    //meetAndroid.send("w2");
  }
}

void sleepClock() {
  // Don't want the dog barking while we're napping
  wdt_reset();
  wdt_disable();
  
//  meetAndroid.send(">sl");
//  delay(100);
    
  SeeedOled.sendCommand(SeeedOLED_Display_Off_Cmd);
  //pinMode(display_shdn, OUTPUT);
  
  blankCounter = 0;
  buzzCounter = 0;
  commCounter = 0;
  
  cli();
  PCIFR |= _BV(PCIF2);
  PCICR |= _BV(PCIE2);
  
  // turns off everything but timer2 asynch mode
  set_sleep_mode(SLEEP_MODE_PWR_SAVE);
  sei();
  sleep_enable();
  while (pcint2_awake == 0) {
    delayMicroseconds(32);
    sleep_cpu();
  }
  sleep_disable();
  if (digitalRead(button) == HIGH) {
    // if we woke because of the serial port, come out of sleep mode
    // but don't wake the display
    commCounter = millis();
  }
  
  PCICR &= ~_BV(PCIE2);
}

void wakeClock() {
  wdt_disable();
  sei();
  //pinMode(display_shdn, INPUT);
  SeeedOled.sendCommand(SeeedOLED_Display_On_Cmd);
  blankCounter = millis();
  wdt_reset();
  wdt_enable(WATCHDOG_INTERVAL);
  wdt_reset();
}

void handleBluetoothInit()
{
  switch(currentState) {
    case PS_INITIALIZING:
      setupBluetoothConnection();
      break;
    case PS_WAITREPLY:
      checkBluetoothReply();
      break;
    case PS_NOT_CONNECTED:
      connectBluetooth();
      break;
    default:
      break;
  }
}

void checkBluetoothReply() {
  byte b = 0;
  while (Serial.available()) {
    wdt_reset();
    b = tolower(Serial.read());
    //SeeedOled.putChar(b);
    if (b == '+') {
      respState = RS_PLUS;
      continue;
    }
    
    //    RS_NONE, RS_PLUS, RS_B, RS_T1, RS_S, RS_T2, RS_A, RS_COL
    switch(respState) {
      default:
        break;
      case RS_PLUS:
        if (b == 'b') respState = RS_B;
        else { respState = RS_NONE;}
        break;
     case RS_B:
        if (b == 't') respState = RS_T1;
        else { respState = RS_NONE; }
        break;
     case RS_T1:
        if (b == 's') respState = RS_S;
        else { respState = RS_NONE; }
        break;
     case RS_S:
        if (b == 't') respState = RS_T2;
        else { respState = RS_NONE; }
        break;
     case RS_T2:
        if (b == 'a') respState = RS_A;
        else { respState = RS_NONE; }
        break;
     case RS_A:
        if (b == 't') respState = RS_T3;
        else { respState = RS_NONE; }
        break;
     case RS_T3:
        if (b == 'e') respState = RS_E;
        else { respState = RS_NONE; }
        break;
     case RS_E:
        if (b == ':') respState = RS_COL;
        else { respState = RS_NONE; }
        break;
     case RS_COL:
        if (b < '0' || b > '9') respState = RS_NONE;
        break;
    }
    // do we have the full reply?
    if (respState == RS_COL) {
      int newBtState = b - '0';
      switch (newBtState) {
        case BT_INIT:
          if (btState == BT_INQ || btState == BT_CONNECTING) {
            currentState = PS_DISCONNECTED;
          } else {  
            // still need to wait for the ready
            currentState = PS_WAITREPLY;
          }
          break;
        case BT_READY:
          if (btState == BT_INQ || btState == BT_CONNECTING) {
            currentState = PS_DISCONNECTED;
          } else { 
            currentState = PS_INITIALIZING;
          }
          delay(30);
          break;
        case BT_INQ:
          SeeedOled.putChar('Q');
          currentState = PS_WAITREPLY;
          break;
        case BT_CONNECTING:
          SeeedOled.putChar('N');
          currentState = PS_WAITREPLY;
          break;
        case BT_CONNECTED:
          SeeedOled.putChar('C');
          currentState = PS_CONNECTED;
          blankCounter = millis();
          SeeedOled.setTextXY(0,0);
          SeeedOled.putString("          ");
          break;
      }
      btState = (BTState)newBtState;
    }
  }
}

static int btStep;
void setupBluetoothConnection()
{
  SeeedOled.putNumber(btStep);
  switch(btStep) {
    case 0:sendBlueToothCommand((char *)"+STWMOD=0");break;
    case 1:sendBlueToothCommand((char *)"+STNA=ArdWatch_00003");break;
    case 2:sendBlueToothCommand((char *)"+STAUTO=0");break;
    case 3:sendBlueToothCommand((char *)"+STOAUT=1");break;
    case 4:sendBlueToothCommand((char *)"+STPIN=0000");break;
    default:break;
  }
  if (btStep < 5) {
    ++btStep;
    respState = RS_NONE;
    currentState = PS_WAITREPLY;
  } else {
    currentState = PS_NOT_CONNECTED;
  }
}

void connectBluetooth()
{
    sendBlueToothCommand((char *)"+INQ=1");
    currentState = PS_WAITREPLY;
}
 
 
//Send the command to Bluetooth Frame
void sendBlueToothCommand(char command[])
{
    Serial.print("\r\n");
    Serial.print(command);
    Serial.print("\r\n"); 
}

// Interrupt handlers
ISR(PCINT2_vect) {
  pcint2_awake = 1;
//  if (!digitalRead(button)) {
//    int0_awake = 1;
//  }
}

ISR(BADISR_vect) {
}

#if 0
void sendBattery(byte flag, byte numOfValues) {
  long temp = readVcc();
  meetAndroid.send(temp);
}

long readVcc() {
  long result;
  
  // Enable the ADC
  PRR &= ~_BV(0);
  ADCSRA |= _BV(ADEN);
  
  // Read 1.1V reference against AVcc
  ADMUX = _BV(REFS0) | _BV(MUX3) | _BV(MUX2) | _BV(MUX1);
  delay(2); // Wait for Vref to settle
  ADCSRA |= _BV(ADSC); // Convert
  while (bit_is_set(ADCSRA,ADSC)) wdt_reset();
  result = ADCL;
  result |= ADCH<<8;
  result = 1126400L / result; // Back-calculate AVcc in mV
  
  // disable the ADC
  ADCSRA &= ~_BV(ADEN);
  PRR |= _BV(0);
  return result;
}
#endif
void recheckConnection(uint8_t c, uint8_t l) {
  if (c != '+' || l > 14) return;
  char statBuf[14];
  meetAndroid.getString(statBuf);
  if (!strncasecmp(statBuf, "btstate:", 8)) {
    btState = (BTState)(statBuf[8]-'0');
    if (currentState == PS_CONNECTED && btState < BT_CONNECTED) {
      currentState = PS_DISCONNECTED;
    }
  }
}


