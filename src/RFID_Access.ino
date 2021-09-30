/* DESCRIPTION
  ====================
  started on 01JAN2017 - uploaded on 06.01.2017 by Dieter
  Code for garage control over RFID
  reading IDENT from xBee, retrait sending ...POR until time responds
  switch on claener by current control and separate cleaner on

  Commands to Raspi --->
  GeName  - from xBee (=Ident) [max 4 caracter including numbers]
  'POR'   - garage power on reset (Ident;por)

  'Ident;closed'   - garage reporting CLOSED-Status
  'Ident;opened'   - garage reporting OPENED-Status
  'card;nn...' - uid_2 from reader 

  Commands from Raspi
  'time'   - format time33.33.3333 33:33:33
  'open'   - Garage OPEN
  'close'  - Garage CLOSE
  'noreg'  - RFID-Chip not registed

  'setmo'  - set time for moving door
  'dison'  - display on for 60 setCursor
  'r3t...' - display text in row 3 "r3tabcde12345", max 20
  'r4t...' - display text in row 4 "r4tabcde12345", max 20

  last change: 18.09.2021 by Michael Muehl
  changed: alpha version for garage control with RFID-Tags
*/
#define Version "0.9.x" // (Test = 0.9.x ==> 1.0.0)
#define GeName "GADO"	// Gerätename für xBee

#include <Arduino.h>
#include <TaskScheduler.h>
#include <Wire.h>
#include <LCDLED_BreakOUT.h>
#include <utility/Adafruit_MCP23017.h>
#include <PN532_I2C.h>
#include <PN532.h>

// PIN Assignments
// RFID Control -------
#define RST_PIN      3  // RFID Reset
#define SS_PIN      10  // [not used]

// Garage Control (ext)
#define SW_opened    2  // position switch opened
#define SW_closed    5  // position switch closed

#define currMotor   A0  // [not used]
#define REL_open    A2  // Relais Garage open
#define REL_close   A3  // Relais Garage close

#define BUSError     8  // Bus error

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
// BUTTON_P1  2         // not used
// BUTTON_P2  1         // StopSwitch
// BACKLIGHT for LCD-Display
#define BACKLIGHToff 0x0
#define BACKLIGHTon  0x1

// DEFINES
#define porTime         5 // wait seconds for sending Ident + POR
#define MOVEGARAGE     30 // SECONDS before activation is off
#define intervalINC  3600 // 3600 * 4

// CREATE OBJECTS
Scheduler runner;
LCDLED_BreakOUT lcd = LCDLED_BreakOUT();
PN532_I2C pn532i2c(Wire);
PN532 nfc(pn532i2c);

// Callback methods prototypes
void checkXbee();        // Task connect to xBee Server
void BlinkCallback();    // Task to let LED blink - added by D. Haude 08.03.2017
void UnLoCallback();     // Task to Unlock for garage
void repeatMES();        // Task to repeat messages

void BuzzerOn();         // added by DieterH on 22.10.2017
void FlashCallback();    // Task to let LED blink - added by D. Haude 08.03.2017
void DispOFF();          // Task to switch display off after time


// Functions define for C++
void OnTimed(long);
void flash_led(int);
void ErrorOPEN();
void ErrorCLOSE();

// TASKS
Task tM(TASK_SECOND / 2, TASK_FOREVER, &checkXbee);	    // 500ms main task
Task tR(TASK_SECOND / 2, 0, &repeatMES);                // 500ms * repMES repeat messages
Task tU(TASK_SECOND,     TASK_FOREVER, &UnLoCallback);  // 1000ms
Task tB(TASK_SECOND * 5, TASK_FOREVER, &BlinkCallback); // 5000ms added M. Muehl

Task tBU(TASK_SECOND / 10, 6, &BuzzerOn);               // 100ms 6x =600ms added by DieterH on 22.10.2017
Task tBD(1, TASK_ONCE, &FlashCallback);                 // Flash Delay
Task tDF(1, TASK_ONCE, &DispOFF);                       // display off
Task tER(1, 2, &ErrorOPEN);                             // error blinking

// VARIABLES
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
// Gate control
boolean noGATE = HIGH;      // bit no gate = HIGH
boolean gateME = LOW;       // bit gate MEssage = LOW (no message)
boolean togLED = LOW;       // bit toggle LEDs
int gateNR = 0;             // "0" = no gates, 6,7,8,9 with gate
int gatERR = 0;             // count gate ERRor > 0 = Blink)

uint32_t versiondata;       // Versiondata of PN5xx

// Variables can be set externaly: ---
// --- on timed, time before new activation
unsigned int MOVE = MOVEGARAGE; // RAM cell for before activation is off
bool toggleGarage = false;  // garage closed = false

// Serial with xBee
String inStr = "";      // a string to hold incoming data
String IDENT = "";      // Garage identifier for remote access control
String SFMes = "";      // String send for repeatMES
byte plplpl = 0;        // send +++ control AT sequenz
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
  pinMode(BUSError, OUTPUT);
  pinMode(REL_open, OUTPUT);
  pinMode(REL_close, OUTPUT);

  pinMode(SW_opened, INPUT_PULLUP);
  pinMode(SW_closed, INPUT_PULLUP);

  // Set default values 
  digitalWrite(BUSError, HIGH); // turn the LED ON (init start)
  digitalWrite(REL_open, LOW);
  digitalWrite(REL_close, LOW);

  runner.init();
  runner.addTask(tM);
  runner.addTask(tB);
  runner.addTask(tR);
  runner.addTask(tU);
  runner.addTask(tBU);
  runner.addTask(tBD);
  runner.addTask(tDF);

  runner.addTask(tER);

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
  if (IDENT.startsWith(GeName) && plplpl == 2)  // GARA
  {
    ++plplpl;
    tB.setCallback(retryPOR);
    tB.enable();
    digitalWrite(BUSError, LOW); // turn the LED off (Programm start)
  }
}

void retryPOR() {
  tDF.restartDelayed(TASK_SECOND * 30); // restart display light
  if (getTime < porTime * 5) {
    Serial.print(String(IDENT) + ";POR;V" + String(Version));
    Serial.print(";PN5"); Serial.print((versiondata>>24) & 0xFF, HEX);
    Serial.print(";V"); Serial.print((versiondata>>16) & 0xFF, DEC);
    Serial.print('.'); Serial.println((versiondata>>8) & 0xFF, DEC);
    ++getTime;
    tB.setInterval(TASK_SECOND * getTime);
    lcd.setCursor(0, 0); lcd.print(String(IDENT) + " ");
    lcd.setCursor(16, 1); lcd.print((getTime - porTime) * porTime);
  }
  else if (getTime == 255) {
    tM.setCallback(checkRFID);
    tM.enable();
    tB.disable();
    displayON();
  }

  // Only for Test!!! ---> ------------
  if (getTime == 8) {
    inStr = "time33.33.3333 33:33:33 ";
    evalSerialData();
    // inStr = "NG9";
    // evalSerialData();
    // R3TMaschine gesperrt!!!
    // R4TMaschine gesperrt!!!!
    inStr = "";
  }
  // <--- Test ------------------------
}

void checkRFID()
{   // 500ms Tick
  uint8_t success;
  uint8_t uid[] = { 0, 0, 0, 0, 0, 0, 0 };  // Buffer to store the returned UID
  uint8_t uidLength;                        // Length of the UID (4 or 7 bytes depending on ISO14443A card type)
  success = nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength);

  if (success)
  {
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

  // Only for Test!!! ---> ------------
  if (getTime == 255)
  {
    if (code == 2394380)
    {
      if (!toggleGarage)
      {
        inStr = "time33.33.3333 33:33:13 ";
        evalSerialData();
        inStr = "open";
        evalSerialData();
        code = 0;
      }
      else
      {
        inStr = "time33.33.3333 33:33:23 ";
        evalSerialData();
        inStr = "close";
        evalSerialData();
        code = 0;
      }
    }
    else if (code == 1915760)
    {
     if (!toggleGarage)
      {
        inStr = "time33.33.3333 33:33:13 ";
        evalSerialData();
        inStr = "open";
        evalSerialData();
        code = 0;
      }
      else
      {
        inStr = "time33.33.3333 33:33:23 ";
        evalSerialData();
        inStr = "close";
        evalSerialData();
        code = 0;
      }
    }
    else if (code == 1748790)
    {
     if (!toggleGarage)
      {
        inStr = "time33.33.3333 33:33:13 ";
        evalSerialData();
        inStr = "open";
        evalSerialData();
        code = 0;
      }
      else
      {
        inStr = "time33.33.3333 33:33:23 ";
        evalSerialData();
        inStr = "close";
        evalSerialData();
        code = 0;
      }
    }
    else if (code == 2024370)
    {
     if (!toggleGarage)
      {
        inStr = "time33.33.3333 33:33:13 ";
        evalSerialData();
        inStr = "open";
        evalSerialData();
        code = 0;
      }
      else
      {
        inStr = "time33.33.3333 33:33:23 ";
        evalSerialData();
        inStr = "close";
        evalSerialData();
        code = 0;
      }
    }
    else if (code == 1206910)
    {
     if (!toggleGarage)
      {
        inStr = "time33.33.3333 33:33:13 ";
        evalSerialData();
        inStr = "open";
        evalSerialData();
        code = 0;
      }
      else
      {
        inStr = "time33.33.3333 33:33:23 ";
        evalSerialData();
        inStr = "close";
        evalSerialData();
        code = 0;
      }
    }
    else if (code >= 99999)
    {
      inStr = "time33.33.3333 33:33:33 ";
      evalSerialData();
      inStr = "noreg";
      evalSerialData();
      code =0;
    }
  }
  // <--- Test ------------------------
}

void UnLoCallback() {   // 500ms Tick
  if (timer > 0 && digitalRead(SW_opened) && digitalRead(SW_closed))
  {
    toggle = !toggle;
    if (toggle)
    { // toggle GREEN Button LED
      but_led(1);
      flash_led(1);
    }
    else
    {
      but_led(3);
      flash_led(4);
    }
    timer -= 1;
    char tbs[8];
    sprintf(tbs, "% 4d", timer);
    lcd.setCursor(16, 2); lcd.print(tbs);
  }
  else
  {
    if (toggleGarage)
    {
      digitalWrite(REL_open, LOW);
      if (timer > 0 && !digitalRead(SW_opened))
      {
        lcd.setCursor(0, 2); lcd.print("Action finished     ");
        lcd.setCursor(0, 3); lcd.print("Garage opened");
        Serial.println(String(IDENT) + ";opened");
      }
      else
      {
        lcd.setCursor(0, 2); lcd.print("Error occurs? Time 0");
        lcd.setCursor(0, 3); lcd.print("Garage opened?");
        Serial.println(String(IDENT) + ";opened?");
      }
    }
    else
    {
      digitalWrite(REL_close, LOW);
      if (timer > 0 && !digitalRead(SW_closed))
      {
        lcd.setCursor(0, 2); lcd.print("Action finished     ");
        lcd.setCursor(0, 3); lcd.print("Garage closed");
        Serial.println(String(IDENT) + ";closed");
      }
      else
      {
        lcd.setCursor(0, 2); lcd.print("Error occurs? Time 0");
        lcd.setCursor(0, 3); lcd.print("Garage closed?");
        Serial.println(String(IDENT) + ";closed?");
      }
    }
    tU.disable();
    tM.enable();
    tDF.restartDelayed(TASK_SECOND * 30);
  }
}

// Task repeatMES: ------------------------
void repeatMES() {
  // --repeat messages from machines
  Serial.println(String(SFMes));
}

void BlinkCallback() {
  // --Blink if BUS Error
  digitalWrite(BUSError, !digitalRead(BUSError));
}

void FlashCallback() {
  flash_led(1);
}

void ErrorOPEN() {
  if (gatERR > 0) {
    if (togLED) {
      tER.restartDelayed(250);
      flash_led(3);
      togLED = !togLED;
    } else {
      tER.restartDelayed(750);
      flash_led(2);
      togLED = !togLED;
    }
  }
}

void ErrorCLOSE() {
  if (gatERR > 0) {
    if (togLED) {
      tER.restartDelayed(250);
      flash_led(2);
      togLED = !togLED;
    } else {
      tER.restartDelayed(750);
      flash_led(3);
      togLED = !togLED;
    }
  }
}

void DispOFF() {
  displayIsON = false;
  lcd.setBacklight(BACKLIGHToff);
  lcd.clear();
  but_led(1);
  flash_led(1);
}

// END OF TASKS ---------------------------------

// FUNCTIONS ------------------------------------
void noreg() {
  digitalWrite(REL_open, LOW);
  digitalWrite(REL_close, LOW);
  lcd.setCursor(0, 2); lcd.print("Tag not registered");
  lcd.setCursor(0, 3); lcd.print("===> No access! <===");
  tM.enable();
  gateME = LOW;
  BadSound();
  but_led(1);
  flash_led(1);
  tDF.restartDelayed(TASK_SECOND * 30);

}

void Opened(void)
{    // Open garage)
  Serial.println(String(IDENT) + ";open");
  lcd.setCursor(0, 2); lcd.print("Garage open in  : ");
  lcd.setCursor(0, 3); lcd.print("Open Garage");
  digitalWrite(REL_open, HIGH);
  // delay(200);
  // digitalWrite(REL_open, LOW);
  toggleGarage = true;
  granted();
}

void Closed(void)
{    // Close garage)
  Serial.println(String(IDENT) + ";close");
  lcd.setCursor(0, 2); lcd.print("Garage closed in: ");
  lcd.setCursor(0, 3); lcd.print("Close Garage");
  digitalWrite(REL_close, HIGH);
  // delay(200);
  // digitalWrite(REL_close, LOW);
  toggleGarage = false;
  granted();
}

// Tag registered
void granted()
{
  tM.disable();
  tDF.disable();
  tU.enable();
  but_led(3);
  flash_led(1);
  GoodSound();
  timer = MOVE;
}

void but_led(int var)
{
  switch (var)
  {
    case 1:   // LEDs off
      lcd.pinLEDs(StopLEDrt, HIGH);
      lcd.pinLEDs(StopLEDgn, HIGH);
      break;
    case 2:   // RED LED on
      lcd.pinLEDs(StopLEDrt, LOW);
      lcd.pinLEDs(StopLEDgn, HIGH);
      break;
    case 3:   // GREEN LED on
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
  tB.disable();
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
    if (plplpl == 0)
    {
      ++plplpl;
      Serial.println("ATNI");
    }
    else
    {
      ++plplpl;
    }
  }

  if (inStr.startsWith(GeName) && inStr.length() == 4) // GARA
  {
    Serial.println("ATCN");
    IDENT = inStr;
  }

  if (inStr.startsWith("TIME"))
  {
    inStr.concat("                   ");     // add blanks to string
    lcd.setCursor(0, 1); lcd.print(inStr.substring(4,24));
    tB.setInterval(TASK_SECOND / 2);
    getTime = 255;
  }

  if (inStr.startsWith("NOREG") && inStr.length() ==5)
  {
    noreg();  // changed by D. Haude on 18.10.2017
  }

  if (inStr.startsWith("OPEN") && inStr.length() ==4)
  {
    Opened();
  }

  if (inStr.startsWith("CLOSE") && inStr.length() ==5)
  {
    Closed();
  }

  if (inStr.startsWith("SETMO") && inStr.length() <9)
  { // set time before ClosE garage
    MOVE = inStr.substring(5).toInt();
  }

  if (inStr.startsWith("DISON"))
  { // Switch display on for 60 sec
    displayON();
    tDF.restartDelayed(TASK_SECOND * 60);
  }

  if (inStr.substring(0, 3) == "R3T" && inStr.length() >3)
  {  // print to LCD row 3
    inStr.concat("                   ");     // add blanks to string
    lcd.setCursor(0,2);
    lcd.print(inStr.substring(3,23)); // cut string lenght to 20 char
  }

  if (inStr.substring(0, 3) == "R4T" && inStr.length() >3)
  {  // print to LCD row 4
    inStr.concat("                   ");     // add blanks to string
    lcd.setCursor(0,3);
    lcd.print(inStr.substring(3,23));   // cut string lenght to 20 char  changed by MM 10.01.2018
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
