/* DESCRIPTION
  ====================
  started on 01JAN2017 - uploaded on 06.01.2017 by Dieter
  Code for garage control over RFID
  reading IDENT from xBee, retrait sending ...POR until time responds
  switch on claener by current control and separate cleaner on

  Commands to Raspi --->
  xBeeName - from xBee (=Ident) [max 4 caracter including numbers]
  'POR'    - power on reset (Ident;por)
  'card;nn...'    - uid_2 from reader 
  
  'Status:'
  'Ident;stat;n'  - garage reporting Door-Status - 00, 01, 10, 11
  'Ident;opened'  - garage door opened-Status
  'Ident;closed'  - garage door closed-Status
  'Ident;openbr'  - garage motion open broken by user 
  'Ident;closebr' - garage motion close broken by user
  'Ident;open??'  - garage motion open undefined
  'Ident;close??' - garage motion close undefined

  Commands from Raspi
  'time'   - format time33.33.3333 33:33:33
  'open'   - Garage OPEN
  'close'  - Garage CLOSE
  'rsstat' - request switch status
  'noreg'  - RFID-Chip not registed

  'setmo'  - set time for moving door
  'dison'  - display on for 60 setCursor
  'r3t...' - display text in row 3 "r3tabcde12345", max 20
  'r4t...' - display text in row 4 "r4tabcde12345", max 20

  last change: 28.10.2022 by Michael Muehl
  changed: early version for garage control with RFID-Tags
*/
#define Version "1.0.x" // (Test = 1.0.x ==> 1.0.1)
#define xBeeName "GADO"	// machine name for xBee
#define checkFA      2  // event check for every (1 second / FActor)

// ---------------------
#include <Arduino.h>
#include <TaskScheduler.h>
#include <Wire.h>
#include <LCDLED_BreakOUT.h>
#include <utility/Adafruit_MCP23017.h>
#include <PN532_I2C.h>
#include <PN532.h>

// PIN Assignments
// RFID Control -------
#define RST_PIN      5  // RFID Reset
#define SS_PIN      10  // [not used]

// Garage Control (ext)
#define SW_open      2  // position switch opened
#define SW_close     3  // position switch closed

#define currMotor   A0  // [not used]
#define REL_open    A2  // Relais Garage open
#define REL_close   A3  // Relais Garage close

#define xBeError     8  // xBee and Bus error (13)

// I2C IOPort definition
byte I2CFound = 0;
byte I2CTransmissionResult = 0;
#define I2CPort   0x20  // I2C Adress MCP23017

// Pin Assignments Display (I2C LCD Port A/LED +Button Port B)
// Switched to LOW
#define FlashLED_A   0  // Flash LEDs oben
#define FlashLED_B   1  // Flash LEDs unten
#define buzzerPin    2  // Buzzer Pin
#define BUT_P1_LED   3  // not used
// Switched High - Low - High - Low
#define StopLEDrt    4  // StopLEDrt (LED + Stop-Taster)
#define StopLEDgn    5  // StopLEDgn (LED - Stop-Taster)
// switch to HIGH Value (def .h)
// BUTTON_P1         6  // not used
// BUTTON_P2         7  // StopSwitch
// BACKLIGHT for LCD-Display
#define BACKLIGHToff 0x0
#define BACKLIGHTon  0x1

// DEFINES
#define porTime         5 // [  5] wait seconds for sending Ident + POR
#define disLightOn     30 // [.30] display light on for seconds
#define MOVEGARAGE     30 // [ 30] SECONDS before activation is off
#define intervalINC  3600 // [  .] 3600 * 4

// CREATE OBJECTS
Scheduler runner;
LCDLED_BreakOUT lcd = LCDLED_BreakOUT();
PN532_I2C pn532i2c(Wire);
PN532 nfc(pn532i2c);

// Callback methods prototypes
void checkXbee();        // Task connect to xBee Server
void BlinkCallback();    // Task to let LED blink - added by D. Haude 08.03.2017
void CheckEvent();       // Task to event checker
void repeatMES();        // Task to repeat messages

void BuzzerOn();         // added by DieterH on 22.10.2017
void FlashCallback();    // Task to let LED blink - added by D. Haude 08.03.2017
void DispOFF();          // Task to switch display off after time


// Functions define for C++
void OnTimed(long);
void flash_led(int);

void MoveERROR();

// TASKS
Task tM(TASK_SECOND / 2, TASK_FOREVER, &checkXbee);	    // 500ms main task
Task tR(TASK_SECOND / 2, 0, &repeatMES);                // 500ms * repMES repeat messages
Task tU(TASK_SECOND / checkFA, TASK_FOREVER, &CheckEvent);  // 1000ms / checkFA ctor
Task tB(TASK_SECOND * 5, TASK_FOREVER, &BlinkCallback); // 5000ms added M. Muehl

Task tBU(TASK_SECOND / 10, 6, &BuzzerOn);               // 100ms 6x =600ms added by DieterH on 22.10.2017
Task tBD(1, TASK_ONCE, &FlashCallback);                 // Flash Delay
Task tDF(1, TASK_ONCE, &DispOFF);                       // display off
Task tMV(TASK_SECOND / 4, TASK_FOREVER, &MoveERROR);    // 250ms for Garage move display

// VARIABLES
uint8_t success;                          // RFID
uint8_t uid[] = { 0, 0, 0, 0, 0, 0, 0 };  // Buffer to store the returned UID
uint8_t uidLength;                        // Length of the UID (4 or 7 bytes depending on ISO14443A card type)

unsigned long val;
unsigned int timer = 0;
bool onTime = false;
int minutes = 0;
bool toggle = false;
unsigned long code;
byte atqa[2];
byte atqaLen = sizeof(atqa);
byte intervalRFID = 0;      // 0 = off; from 1 sec to 6 sec after Displayoff
// Cleaner Control
bool displayIsON = false;   // if display is switched on = true
bool isCleaner  = false;    // is cleaner under control (installed)
byte steps4push = 0;        // steps for push button action
unsigned int pushCount = 0; // counter how long push button in action

uint32_t versiondata;       // Versiondata of PN5xx

// Variables can be set externaly: ---
// --- on timed, time before new activation
unsigned int MOVE = MOVEGARAGE  * checkFA; // RAM cell for before activation is off
bool togGarage = false;  // bit toggle garage closed = false
bool togLED = LOW;       // bit toggle LEDs on / off
bool togMOVE = false;

// Serial with xBee
String inStr = "";      // a string to hold incoming data
String IDENT = "";      // identifier for remote access control
String SFMes = "";      // String send for repeatMES
byte co_ok = 0;         // count ok after +++ control AT sequenz
byte getTime = porTime;

// ======>  SET UP AREA <=====
void setup()
{
  //init Serial port
  Serial.begin(57600);  // Serial
  inStr.reserve(40);    // reserve for instr serial input
  IDENT.reserve(5);     // reserve for IDENT serial output

  // initialize:
  Wire.begin();         // I2C
   
  // IO MODES
  pinMode(xBeError, OUTPUT);

  pinMode(REL_open, OUTPUT);
  pinMode(REL_close, OUTPUT);

  pinMode(SW_open, INPUT_PULLUP);
  pinMode(SW_close, INPUT_PULLUP);

  // Set default values
  digitalWrite(xBeError, HIGH); // turn the LED ON (init start)

  digitalWrite(REL_open, HIGH);
  digitalWrite(REL_close, HIGH);

  runner.init();
  runner.addTask(tM);
  runner.addTask(tB);
  runner.addTask(tU);
  runner.addTask(tMV);
  runner.addTask(tBU);
  runner.addTask(tBD);
  runner.addTask(tDF);

  // I2C _ Ports definition only for test if I2C is avilable
  Wire.beginTransmission(I2CPort);
  I2CTransmissionResult = Wire.endTransmission();
  if (I2CTransmissionResult == 0)
  {
    I2CFound++;
  }
  // I2C Bus mit slave vorhanden
  Wire.beginTransmission(I2CPort + 4);
  I2CTransmissionResult = Wire.endTransmission();
  if (I2CTransmissionResult == 0)
  {
    I2CFound++;
  }
  if (I2CFound == 2)
  {
    lcd.begin(20, 4);        // initialize the LCD

    nfc.begin();
    versiondata = nfc.getFirmwareVersion();
    nfc.SAMConfig();
 
    lcd.clear();
    lcd.pinLEDs(buzzerPin, LOW);
    lcd.pinLEDs(BUT_P1_LED, LOW);

    but_led(1);
    flash_led(1);
    dispRFID();
    tM.enable();         // xBee check
    Serial.print("+++"); //Starting the request of IDENT
  }
  else 
  {
    tB.enable();  // enable Task Error blinking
    tB.setInterval(TASK_SECOND);
  }
}
// Setup End -----------------------------------

// TASK (Functions) ----------------------------
void checkXbee()
{
  if (IDENT.startsWith(xBeeName) && co_ok == 2)
  {
    ++co_ok;
    tB.setCallback(retryPOR);
    tB.enable();
    digitalWrite(xBeError, LOW); // turn the LED off (Programm start)
  }
}

void retryPOR() {
  tDF.restartDelayed(TASK_SECOND * disLightOn); // restart display light
  if (getTime < porTime * 5)
  {
    tB.setInterval(TASK_SECOND * getTime);
    ++getTime;
    Serial.print(String(IDENT) + ";POR;V" + String(Version));
    Serial.print(";PN5"); Serial.print((versiondata>>24) & 0xFF, HEX);
    Serial.print(";V"); Serial.print((versiondata>>16) & 0xFF, DEC);
    Serial.print('.'); Serial.println((versiondata>>8) & 0xFF, DEC);
    lcd.setCursor(0, 0); lcd.print(String(IDENT) + " ");
    lcd.setCursor(16, 1); lcd.print((getTime - porTime) * porTime);
  }
  else if (getTime == 255)
  {
    tM.setCallback(checkRFID);
    tM.enable();
    tB.disable();
    displayON();
  }
}

void checkRFID()
{   // 500ms Tick
  success = nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength);
  if (success)
  {
    flash_led(4);
    code = 0;
    for (byte i = 0; i < uidLength; i++) {
      code = ((code + uid[i]) * 10);
    }
    Serial.println("card;" + String(code));
    // Display changes
    lcd.setCursor(5, 0); lcd.print("               ");
    lcd.setCursor(0, 0); lcd.print("Card# "); lcd.print(code);
    displayON();
  }

}

void CheckEvent()
{
  uint8_t buttons = lcd.readButtons();
  if ((buttons & BUTTON_P2) ==2)
  {
        digitalWrite(REL_open, HIGH);
        but_led(2);
        lcd.setCursor(0, 2); lcd.print("Stop occurs!!!      ");
        if (togGarage)
        {
          Serial.println(String(IDENT) + ";openbr");
        }
        else
        {
          Serial.println(String(IDENT) + ";closebr");
        }
        togGarage = !togGarage;

  }
  else if (timer > 0 && digitalRead(SW_open) && digitalRead(SW_close))
  {
    timer -= 1;
    if (timer % checkFA == 0)
    {
      char tbs[8];
      sprintf(tbs, "% 4d", timer / checkFA);
      lcd.setCursor(16, 2); lcd.print(tbs);
    }
  }
  else
  {
    tMV.setInterval(TASK_SECOND / 4);
    tMV.setCallback(MoveERROR);
    if (togGarage)  // garage opened =1 or closed =0
    {
      if (timer > 0 && !digitalRead(SW_open))
      {
        lcd.setCursor(0, 2); lcd.print("Action finished     ");
        lcd.setCursor(0, 3); lcd.print("Garage open    ");
        Serial.println(String(IDENT) + ";opened");
      }
      else if (timer == 0 && !digitalRead(SW_open))
      { // fertig open
        digitalWrite(REL_open, HIGH);
        but_led(1);
        flash_led(1);
        tMV.disable();
        tM.enable();
      }
      else
      {
        but_led(1);
        lcd.setCursor(0, 2); lcd.print("Error occurs? Time 0");
        lcd.setCursor(0, 3); lcd.print("Garage open??  ");
        Serial.println(String(IDENT) + ";open??");
      }
    }
    else
    {
      if (timer > 0 && !digitalRead(SW_close))
      {
        lcd.setCursor(0, 2); lcd.print("Action finished     ");
        lcd.setCursor(0, 3); lcd.print("Garage closed  ");
        Serial.println(String(IDENT) + ";closed");
      }
      else if (timer == 0 && !digitalRead(SW_close))
      { // Fertig closed
        digitalWrite(REL_close, HIGH);
        but_led(1);
        flash_led(1);
        tMV.disable();
        tM.enable();
      }
      else
      {
        but_led(1);
        lcd.setCursor(0, 2); lcd.print("Error occurs? Time 0");
        lcd.setCursor(0, 3); lcd.print("Garage closed??");
        Serial.println(String(IDENT) + ";close??");
      }
    }
    tU.disable();
    tDF.restartDelayed(TASK_SECOND * disLightOn);
  }
}

// Task repeatMES: ------------------------
void repeatMES()
{
  // --repeat messages from machines
  Serial.println(String(SFMes));
}

void BlinkCallback()
{
  // --Blink if BUS Error
  digitalWrite(xBeError, !digitalRead(xBeError));
}

void FlashCallback()
{
  flash_led(1);
}

void MoveERROR()
{
    togMOVE = !togMOVE;
    if (togMOVE)
    { 
      flash_led(4);
    }
    else
    {
      flash_led(1);
    }
}

void MoveOPEN()
{
  if (togLED) {
    tMV.setInterval(TASK_SECOND / 4);
    flash_led(3);
    togLED = !togLED;
  } else {
    tMV.setInterval(TASK_SECOND / 2);
    flash_led(2);
    togLED = !togLED;
  }
}

void MoveCLOSE()
{
  if (togLED) {
    tMV.setInterval(TASK_SECOND / 4);
    flash_led(2);
    togLED = !togLED;
  } else {
    tMV.setInterval(TASK_SECOND / 2);
    flash_led(3);
    togLED = !togLED;
  }
}

void DispOFF()
{
  displayIsON = false;
  tMV.disable();
  tM.enable();
  lcd.setBacklight(BACKLIGHToff);
  lcd.clear();
  but_led(1);
  flash_led(1);
}
// END OF TASKS ---------------------------------

// FUNCTIONS ------------------------------------
void noreg() {
  digitalWrite(REL_open, HIGH);
  digitalWrite(REL_close, HIGH);
  lcd.setCursor(0, 2); lcd.print("Tag not registered");
  lcd.setCursor(0, 3); lcd.print("===> No access! <===");
  tM.enable();
  BadSound();
  but_led(1);
  flash_led(1);
  tDF.restartDelayed(TASK_SECOND * disLightOn);
}

void SwStatus(void)
{    // switch status)
  byte sw_val =0b00000000;
  bitWrite(sw_val, 0, digitalRead(SW_open));
  bitWrite(sw_val, 1, digitalRead(SW_close));
  Serial.println(String(IDENT) + ";stat;" + String(sw_val));
}

void Opened(void)
{    // Open garage)
  Serial.println(String(IDENT) + ";open");
  lcd.setCursor(0, 2); lcd.print("Garage open in  : ");
  lcd.setCursor(0, 3); lcd.print("Open Garage");
  digitalWrite(REL_open, LOW);
  tMV.setCallback(MoveOPEN);
  togGarage = true;
  granted();
}

void Closed(void)
{    // Close garage)
  Serial.println(String(IDENT) + ";close");
  lcd.setCursor(0, 2); lcd.print("Garage closed in: ");
  lcd.setCursor(0, 3); lcd.print("Close Garage");
  digitalWrite(REL_close, LOW);
  tMV.setCallback(MoveCLOSE);
  togGarage = false;
  granted();
}

// Tag registered
void granted()
{
  tM.disable();
  tDF.disable();
  but_led(2);
  flash_led(1);
  GoodSound();
  timer =  MOVE;
  tMV.setInterval(TASK_SECOND / 4);
  tMV.enable();
  tU.enable();
}

void but_led(int var)
{
  switch (var)
  {
  case 1: // LED rt & gn off
    lcd.pinLEDs(StopLEDrt, HIGH);
    lcd.pinLEDs(StopLEDgn, HIGH);
    break;
  case 2: // RED LED on
    lcd.pinLEDs(StopLEDrt, LOW);
    lcd.pinLEDs(StopLEDgn, HIGH);
    break;
  case 3: // GREEN LED on
    lcd.pinLEDs(StopLEDrt, HIGH);
    lcd.pinLEDs(StopLEDgn, LOW);
    break;
  }
}

void flash_led(int var)
{
  switch (var)
  {
    case 1:   // LEDs off
      lcd.pinLEDs(FlashLED_A, LOW);
      lcd.pinLEDs(FlashLED_B, LOW);
      break;
    case 2:
      lcd.pinLEDs(FlashLED_A, HIGH);
      lcd.pinLEDs(FlashLED_B, LOW);
      break;
    case 3:
      lcd.pinLEDs(FlashLED_A, LOW);
      lcd.pinLEDs(FlashLED_B, HIGH);
      break;
    case 4:
      lcd.pinLEDs(FlashLED_A, HIGH);
      lcd.pinLEDs(FlashLED_B, HIGH);
      break;
  }
}

void BuzzerOff()
{
  lcd.pinLEDs(buzzerPin, LOW);
  tBU.setCallback(&BuzzerOn);
}

void BuzzerOn()
{
  lcd.pinLEDs(buzzerPin, HIGH);
  tBU.setCallback(&BuzzerOff);
}

void BadSound(void)
{   // added by DieterH on 22.10.2017
  tBU.setInterval(100);
  tBU.setIterations(6); // I think it must be Beeps * 2?
  tBU.setCallback(&BuzzerOn);
  tBU.enable();
}

void GoodSound(void)
{
  lcd.pinLEDs(buzzerPin, HIGH);
  tBD.setCallback(&BuzzerOff);  // changed by DieterH on 18.10.2017
  tBD.restartDelayed(200);      // changed by DieterH on 18.10.2017
}

//  RFID ------------------------------
void dispRFID(void)
{
  lcd.print("Sys  V" + String(Version).substring(0,3) + " starts at:");
  lcd.setCursor(0, 1); lcd.print("Wait Sync xBee:");
}

void displayON()
{
  displayIsON = true;
  lcd.setBacklight(BACKLIGHTon);
  tM.enable();
  intervalRFID = 0;
}
// End Funktions --------------------------------

// Funktions Serial Input (Event) ---------------
void evalSerialData()
{
  inStr.toUpperCase();
  if (inStr.startsWith("OK"))
  {
    if (co_ok == 0)
    {
      Serial.println("ATNI");
      ++co_ok;
    }
    else
    {
      ++co_ok;
    }
  }
  else if (co_ok ==1  && inStr.length() == 4)
  {
    if (inStr.startsWith(xBeeName))
    {
      IDENT = inStr;
    }
    else
    {
      lcd.setCursor(0, 0); lcd.print(inStr);
    }
    Serial.println("ATCN");
  }
  else if (inStr.startsWith("TIME"))
  {
    inStr.concat("                   ");     // add blanks to string
    lcd.setCursor(0, 1); lcd.print(inStr.substring(4,24));
    tB.setInterval(TASK_SECOND / 2);
    getTime = 255;
  }
  else if (inStr.startsWith("NOREG") && inStr.length() ==5)
  {
    noreg();  // changed by D. Haude on 18.10.2017
  }
  else if (inStr.startsWith("RSSTAT") && inStr.length() ==6)
  {
    SwStatus();
  }
  else if (inStr.startsWith("OPEN") && inStr.length() ==4)
  {
    but_led(3);
    Opened();
  }
  else if (inStr.startsWith("CLOSE") && inStr.length() ==5)
  {
    but_led(3);
    Closed();
  }
  else if (inStr.startsWith("SETMO") && inStr.length() <9)
  { // set time before ClosE garage
    MOVE = inStr.substring(5).toInt() * checkFA;
  }
  else if (inStr.startsWith("DISON"))
  { // Switch display on for disLightOn secs
    displayON();
    tDF.restartDelayed(TASK_SECOND * disLightOn);
  }
  else if (inStr.substring(0, 3) == "R3T" && inStr.length() >3)
  {  // print to LCD row 3
    inStr.concat("                    ");     // add blanks to string
    lcd.setCursor(0,2);
    lcd.print(inStr.substring(3,23)); // cut string lenght to 20 char
  }
  else if (inStr.substring(0, 3) == "R4T" && inStr.length() >3)
  {  // print to LCD row 4
    inStr.concat("                    ");     // add blanks to string
    lcd.setCursor(0,3);
    lcd.print(inStr.substring(3,23));   // cut string lenght to 20 char  changed by MM 10.01.2018
  }
  else
  {
    Serial.println(String(IDENT) + ";?;" + inStr);
    inStr.concat("                    ");    // add blanks to string
    lcd.setCursor(0,2);
    lcd.print("?:" + inStr.substring(0,18)); // cut string lenght to 20 char
  }
  inStr = "";
}

/* SerialEvent occurs whenever a new data comes in the
  hardware serial RX.  This routine is run between each
  time loop() runs, so using delay inside loop can delay
  response.  Multiple bytes of data may be available.
*/
void serialEvent()
 {
  char inChar = (char)Serial.read();
  if (inChar == '\x0d')
  {
    evalSerialData();
    inStr = "";
  }
  else if (inChar != '\x0a')
  {
    inStr += inChar;
  }
}
// End Funktions Serial Input -------------------

// PROGRAM LOOP AREA ----------------------------
void loop() {
  runner.execute();
}