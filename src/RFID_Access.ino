/* DESCRIPTION
  ====================
  uploaded on 20.09.2021 by Dieter
  Code for garage control over RFID
  reading IDENT from xBee, retrait sending ...POR until time responds
  open or close garage door

  Commands to Raspi --->
  xBeeName - from xBee (=Ident) [max 4 caracter including numbers] {xBeeName + nn}
  'Ident;POR;Vx.x.x' - machine power on reset and sw version (Ident;por;Vx.x.x)
  'card;nn...'       - uid_2 from reader
  
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
  'resre'  - reset relais = all open
  'minre'  - power reset mini over WDT
  'reqst'  - request status
  'nolog'  - not loged in at entry door!
  'noreg'  - RFID-Chip not registed
  'setmo'  - set max time for moving door (xxx.x sec * checkFA)  {>minMove}
  'setdu'  - set Delay for moving door Up (xx.x sec * checkFA)   {>0}
  'setdd'  - set Delay for moving door Down (xx.x sec * checkFA) {>0}
  'dison'  - display on for 60 setCursor
  'r3t...' - display text in row 3 "r3tabcde12345", max 20
  'r4t...' - display text in row 4 "r4tabcde12345", max 20

  last change: 10.10.2024 by Michael Muehl
  changed: add very high priority for door status
*/
#define Version "1.5.1" // (Test = 1.4.9 ==> 1.5.2)
#define xBeeName "GADO"	// machine name for xBee
#define checkFA     10  // [10] event CHECK for every (1 second / FActor)
#define dostaFA     20  // [20] DOor STAtus for every (1 second / FActor)
#define minMove    100  // [100] minimal time for moving door (xx.x sec * checkFA)
#define disLightOn  15  // [15] display light on for seconds

// ---------------------
#include <Arduino.h>

#define _TASK_PRIORITY
#include <TaskScheduler.h>

#include <Wire.h>
#include <LCDLED_BreakOUT.h>
#include <utility/Adafruit_MCP23017.h>
#include <Adafruit_PN532.h>

// PIN Assignments
// RFID Control ---I2C-------
// #define SDA      A4  // Relais Garage open
// #define SCL      A5  // Relais Garage close
#define PN532_IRQ    2  // RFID IRQ
#define PN532_RESET  3  // RFID Reset [not used]

// Garage Control (ext) [sw_val]
#define SW_close     5  // position switch closed [Bit 0]
#define SW_open      7  // position switch opened [Bit 1]

#define currMotor   A0  // [not used]
#define REL_open    A2  // Relais Garage open
#define REL_close   A3  // Relais Garage close

#define xBuError     8  // xBee and Bus error (13)
#define PULS_WDT     9  // watch dog pulse

// I2C IOPort definition
byte I2CFound = 0;
byte I2CTransmissionResult = 0;
#define I2CPort   0x20  // I2C Adress MCP23017 (LCD Display +)

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
#define porTime       5   // [  5] wait seconds for sending Ident + POR
#define MOVEGARAGE   30   // [ 30] SECONDS before activation is off
#define repHour    3600uL // [3600uL] seconds per hour (unsigned long)

// CREATE OBJECTS
Scheduler r, hpr, cpr;
LCDLED_BreakOUT lcd = LCDLED_BreakOUT();
Adafruit_PN532 nfc(PN532_IRQ, PN532_RESET);

// Callback methods prototypes
void checkXbee();        // Task connect to xBee Server
void BlinkCallback();    // Task to let LED blink - added by D. Haude 08.03.2017
void CheckEvent();       // Task to event checker
void repeatMES();        // Task to repeat messages

void BuzzerOn();         // added by DieterH on 22.10.2017
void FlashCallback();    // Task to let LED blink - added by D. Haude 08.03.2017
void DisplayOFF();       // Task to switch display off after time

void MoveERROR();        // Task for Error during movement
void doorSTA();          // Task for door STAtus

// Functions define for C++
void flash_led(int);

// TASKS
Task tM(TASK_SECOND / 2, TASK_FOREVER, &checkXbee, &r);	         // 500ms main task
Task tR(TASK_SECOND / 2, 0, &repeatMES, &r);                     // 500ms * repMES repeat messages
Task tU(TASK_SECOND / checkFA, TASK_FOREVER, &CheckEvent, &hpr); // 1000ms / checkFActor
Task tB(TASK_SECOND * 5, TASK_FOREVER, &BlinkCallback, &r);      // 5000ms blinking

Task tBU(TASK_SECOND / 10, 6, &BuzzerOn, &r);                    // 100ms 6x =600ms buzzer added by DieterH on 22.10.2017
Task tBD(1, TASK_ONCE, &FlashCallback, &r);                      // Flash Delay
Task tDF(1, TASK_ONCE, &DisplayOFF, &r);                         // display off

Task tMV(TASK_SECOND / 4, TASK_FOREVER, &MoveERROR, &r);         // 250ms for Garage move display
Task tDS(TASK_SECOND / dostaFA, TASK_FOREVER, &doorSTA, &cpr);   // 1000ms / dostaFActor

// VARIABLES
// external watch dog
byte wdTimeL = 10; // value * (Task tRFR(TASK_SECOND / 2)
byte wdTimeH = 10; // value * (Task tRFR(TASK_SECOND / 2)
byte wdCount = 0;  // counter for watch dog

uint8_t success;                          // RFID
uint8_t uid[] = { 0, 0, 0, 0, 0, 0, 0 };  // Buffer to store the returned UID
uint8_t uidLength;                        // Length of the UID (4 or 7 bytes depending on ISO14443A card type)

bool onError = false;
bool rfidCheck = false;
bool oneTime = false;
bool toggle = false;
unsigned long val;
unsigned int timer = 0;
unsigned long code;
int minutes = 0;
byte atqa[2];
byte atqaLen = sizeof(atqa);
byte intervalRFID = 0;      // 0 = off; from 1 sec to 6 sec after Displayoff
bool displayIsON = false;   // if display is switched on = true

byte sw_val = 0;            // garage switch value (open / closed)
byte sw_last = 255;         // garage switch value last time

unsigned int pushCount = 0; // counter how long push button in action

uint32_t versiondata;       // Versiondata of PN5xx

// Variables can be set externaly: ---
// --- on timed, time before new activation
unsigned int MOVE = MOVEGARAGE * checkFA; // RAM cell for time, door is moving 
unsigned int MODEUP = checkFA;            // RAM cell MOving DElay after sw UP
unsigned int MODEDO = checkFA;            // RAM cell MOving DElay after sw DOwn

byte movDoor = 0;       // byte move door [>0]
bool togLED = LOW;      // bit toggle LEDs on / off
bool togMOVE = false;
unsigned long staCount = 0uL; // counter to send status ervery hour

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
  pinMode(xBuError, OUTPUT);
  pinMode(PULS_WDT, OUTPUT);

  pinMode(REL_open, OUTPUT);
  pinMode(REL_close, OUTPUT);

  pinMode(SW_open, INPUT_PULLUP);
  pinMode(SW_close, INPUT_PULLUP);

  // Set default values
  digitalWrite(xBuError, HIGH); // turn the LED ON
  digitalWrite(PULS_WDT, LOW);  // turn wdtoogle off

  digitalWrite(REL_open, HIGH);
  digitalWrite(REL_close, HIGH);

  r.setHighPriorityScheduler(&hpr);
  hpr.setHighPriorityScheduler(&cpr);
  cpr.startNow();
  hpr.startNow();
  r.startNow();

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
  if (I2CFound >= 1)   //[1] min display available
  {
    lcd.begin(20, 4);  // initialize the LCD

    nfc.begin();
    nfc.SAMConfig();   // Configure board to read RFID tags
    versiondata = nfc.getFirmwareVersion();
 
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
    digitalWrite(xBuError, LOW); // turn the LED off (Programm start)
  }
}

void retryPOR()
{
  tDF.restartDelayed(TASK_SECOND * disLightOn); // restart display light
  if (getTime < porTime * 5)
  {
    Serial.print(String(IDENT) + ";POR;V" + String(Version));
    if (versiondata)
    {
      Serial.print(";PN5"); Serial.print((versiondata>>24) & 0xFF, HEX);
      Serial.print(";V"); Serial.print((versiondata>>16) & 0xFF, DEC);
      Serial.print('.'); Serial.println((versiondata>>8) & 0xFF, DEC);
    }
    else
    {
      Serial.println(";PN53?;V?.?");
      lcd.setCursor(0, 3); lcd.print(F("No RFID: PN53? V?.? "));
    }
    ++getTime;
    tB.setInterval(TASK_SECOND * getTime);
    lcd.setCursor(0, 0); lcd.print(String(IDENT) + " ");
    lcd.setCursor(16, 1); lcd.print((getTime - porTime) * porTime);
  }
  else if (getTime == 255)
  {
    tM.setCallback(checkRFID);
    tM.enable();
    tB.disable();
    displayON();
    rfidCheck = true;
    tDS.enable(); // start task for sending status
  }
}

void checkRFID()  // wait until rfid token is recognized
{ // 500ms Tick
  // check RFID -----------------------
  if (!digitalRead(PN532_IRQ)) 
  {
    success = nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength);  // read RFID
    // Serial.println("nfc;" + String(success));
    if (success && rfidCheck == true)
    {
      lcd.clear();
      flash_led(4);
      code = 0;
      for (byte i = 0; i < uidLength; i++)
      {
        code = ((code + uid[i]) * 10);
      }
      Serial.println("card;" + String(code));
      lcd.setCursor(0, 0); lcd.print("Card# "); lcd.print(code);
      displayON();
    }
  }
}

void CheckEvent()
{
  uint8_t buttons = lcd.readButtons();
  if ((buttons & BUTTON_P2) ==2)
  { // stop movement
    digitalWrite(REL_open, HIGH);
    digitalWrite(REL_close, HIGH);
    onError = true;
    lcd.setCursor(0, 2); lcd.print(F("Stop occurs!!!      "));
    lcd.setCursor(0, 3); lcd.print(F("Door moved "));
    if (movDoor == 1)
    {
      lcd.print(F("up???    "));
      Serial.println(String(IDENT) + ";openbr");
    }
    else if  (movDoor == 2)
    {
      lcd.print(F("down?    "));
      Serial.println(String(IDENT) + ";closebr");
    }
    sw_last = 255;  // send status
  }

  if (timer > 0)
  {
    --timer;
    if (timer == 0)
    { // stop movement
      digitalWrite(REL_open, HIGH);
      digitalWrite(REL_close, HIGH);
      flash_led(1);
      tU.disable();
      tMV.disable();
      but_led(1); // only red possible
      sw_last = 255;  // send status
    }
    if (timer % checkFA == 0)
    {
      char tbs[8];
      sprintf(tbs, "% 3d", timer / checkFA);
      lcd.setCursor(17, 2); lcd.print(tbs);
    }
    tDF.restartDelayed(TASK_SECOND * disLightOn);
  }

  if (timer ==  0)
  {
    if ((movDoor == 11) || (movDoor == 22))
    {
      lcd.setCursor(0, 2); lcd.print(F("Action finished     "));
      lcd.setCursor(0, 3); lcd.print(F("Garage is "));
      if (movDoor == 11)      // garage open
      {
        lcd.print(F("open      "));
        Serial.println(String(IDENT) + ";opened");
      }
      else if (movDoor == 22) // garage close
      {
        lcd.print(F("closed    "));
        Serial.println(String(IDENT) + ";closed");
      }
   }
    else
    {
      lcd.setCursor(0, 2); lcd.print(F("Error occurs? Time 0"));
      lcd.setCursor(0, 3); lcd.print(F("Garage "));
      if (movDoor == 1)
      {
        lcd.print(F("open??       "));
        Serial.println(String(IDENT) + ";open??");
      }
      else if (movDoor == 2)
      {
        lcd.print(F("closed??     "));
        Serial.println(String(IDENT) + ";close??");
      }
      onError = true;
    }
    movDoor = 0;
  }

  if (onError)
  {
    onError = false;
    but_led(1);
    tU.disable();
    tMV.setInterval(TASK_SECOND / 4);
    tMV.setCallback(MoveERROR);
    tMV.enable();
    sw_last = 255;  // send status
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
  digitalWrite(xBuError, !digitalRead(xBuError));
  digitalWrite(PULS_WDT, !digitalRead(PULS_WDT));
}

void FlashCallback()
{
  flash_led(1);
}

void doorSTA()
{
  // signal for watch dog timer -------
  if (wdCount == wdTimeL) 
  {
    // Serial.println(String(IDENT) + ";count;" + String(wdCount));
    digitalWrite(PULS_WDT, HIGH);
  }
  if (wdCount == (wdTimeL + wdTimeH) && digitalRead(PULS_WDT))
  {
    // Serial.println(String(IDENT) + ";count;" + String(wdCount));
    digitalWrite(PULS_WDT, LOW);
    wdCount = 0;
  }
  // door status check
  bitWrite(sw_val, 0, digitalRead(SW_open));
  bitWrite(sw_val, 1, digitalRead(SW_close));
  if (sw_val != sw_last)
  {
    if (sw_val == 1 && movDoor == 1) 
    {
      timer = MODEUP; // delay open
      movDoor = 11;
    }
    if (sw_val == 2 && movDoor ==2)
    {
      timer = MODEDO; // delay close
      movDoor = 22;
    }
    sw_last = sw_val;
    staCount = 0;
  }
  if (staCount == 0)
  {
    staCount = repHour * dostaFA;
    nfc.startPassiveTargetIDDetection(PN532_MIFARE_ISO14443A); //  start RFID for next reading
    Serial.println(String(IDENT) + ";stat;" + String(sw_val));
  }
  --staCount;
  ++wdCount;
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

void DisplayOFF()
{
  displayIsON = false;
  tU.disable();
  tMV.disable();
  lcd.setBacklight(BACKLIGHToff);
  lcd.clear();
  but_led(1);
  flash_led(1);
  sw_last = 255;  // send status
  rfidCheck = true;
  nfc.startPassiveTargetIDDetection(PN532_MIFARE_ISO14443A); //  start RFID for next reading
}
// END OF TASKS ---------------------------------

// FUNCTIONS ------------------------------------
int getNum(String strNum) // Check if realy numbers
{
  strNum.trim();
  for (byte i = 0; i < strNum.length(); i++)
  {
    if (!isDigit(strNum[i])) 
    {
      Serial.println(String(IDENT) + ";?;" + inStr + ";Num?;" + strNum);
      lcd.setCursor(0, 2); lcd.print(F("no mumber           "));
      lcd.setCursor(0, 3); lcd.print(F("logout and reset    "));
      return 0;
    }
  }
  return strNum.toInt();
}

void noact()
{ // no action
  digitalWrite(REL_open, HIGH);
  digitalWrite(REL_close, HIGH);
  rfidCheck = true;
  BadSound();
  but_led(1);
  flash_led(1);
  tDF.restartDelayed(TASK_SECOND * disLightOn);
}

void Opened(void)
{ // Open garage
  movDoor = 1;
  digitalWrite(REL_open, LOW);
  digitalWrite(REL_close, HIGH);
  Serial.println(String(IDENT) + ";open");
  char tbs[8];
  sprintf(tbs, "% 3d", MOVE / checkFA);
  lcd.setCursor(0, 2); lcd.print("Garage open in  :"); lcd.print(tbs);
  lcd.setCursor(0, 3); lcd.print(F("Open Garage         "));
  tMV.setCallback(MoveOPEN);
  granted();
}

void Closed(void)
{ // Close garage
  movDoor = 2;
  digitalWrite(REL_close, LOW);
  digitalWrite(REL_open, HIGH);
  Serial.println(String(IDENT) + ";close");
  char tbs[8];
  sprintf(tbs, "% 3d", MOVE / checkFA);
  lcd.setCursor(0, 2); lcd.print("Garage closed in:"); lcd.print(tbs);
  lcd.setCursor(0, 3); lcd.print(F("Close Garage        "));
  tMV.setCallback(MoveCLOSE);
  granted();
}

void granted()
{ // Tag registered
  rfidCheck = false;
  but_led(2);
  flash_led(1);
  GoodSound();
  timer = MOVE;
  tMV.setInterval(TASK_SECOND / 4);
  tMV.enable();
  tU.enable();
}

void but_led(int var)
{ // button led switching
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
{ // flash leds switching
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

void BuzzerOff()  // switch buzzer off
{
  lcd.pinLEDs(buzzerPin, LOW);
  tBU.setCallback(&BuzzerOn);
}

void BuzzerOn()   // switch buzzer on
{
  lcd.pinLEDs(buzzerPin, HIGH);
  tBU.setCallback(&BuzzerOff);
}

void BadSound(void) // generate bad sound
{   // added by DieterH on 22.10.2017
  tBU.setInterval(100);
  tBU.setIterations(6); // I think it must be Beeps * 2?
  tBU.setCallback(&BuzzerOn);
  tBU.enable();
}

void GoodSound(void) // generate good sound
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

void displayON()  // switch display on
{
  displayIsON = true;
  intervalRFID = 0;
  lcd.setBacklight(BACKLIGHTon);
  tDF.restartDelayed(TASK_SECOND * disLightOn);
}
// End Funktions --------------------------------

// Funktions Serial Input ------- ---------------
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
      Serial.println("ATCN");
    }
    else
    {
      lcd.setCursor(0, 2);
      lcd.print("?:" + inStr + ";not for:" + xBeeName); 
    }
  }
  else if (inStr.startsWith("TIME"))
  {
    inStr.concat("                   ");     // add blanks to string
    lcd.setCursor(0, 1); lcd.print(inStr.substring(4,24));
    tB.setInterval(TASK_SECOND / 2);
    getTime = 255;
  }
  else if (inStr.startsWith("REQST") && inStr.length() ==5)
  {
    Serial.println(String(IDENT) + ";stat;" + String(sw_val));
  }
  else if (inStr.startsWith("NOLOG") && inStr.length() ==5)
  {
    lcd.setCursor(0, 2); lcd.print(F("Not logged in -     "));
    lcd.setCursor(0, 3); lcd.print(F("Login at Entry Door!"));
    noact();
  }
  else if (inStr.startsWith("NOREG") && inStr.length() ==5)
  {
    lcd.setCursor(0, 2); lcd.print(F("Tag not registered  "));
    lcd.setCursor(0, 3); lcd.print(F("===> No access! <==="));
    noact();
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
  else if (inStr.startsWith("RESRE") && inStr.length() ==5)
  {
    digitalWrite(REL_open, HIGH);
    digitalWrite(REL_close, HIGH);
  }
  else if (inStr.startsWith("MINRE") && inStr.length() ==5)
  { // no Task for 1 sec WDT goes aktice
    delay(1000);
  }
  else if (inStr.startsWith("SETMO") && inStr.length() <9)
  { // set time during door is moving [sec * checkFA]
    val = getNum(inStr.substring(5));
    if (val > minMove) MOVE = val;
  }
  else if (inStr.startsWith("SETDU") && inStr.length() <8)
  { // set delay time for door moving up [sec * checkFA]
    val = getNum(inStr.substring(5));
    if (val > 0) MODEUP = val;
  }
  else if (inStr.startsWith("SETDD") && inStr.length() <8)
  { // set delay time for door moving down [sec * checkFA]
    val = getNum(inStr.substring(5));
    if (val > 0) MODEDO = val;
  }
  else if (inStr.startsWith("DISON"))
  { // Switch display on for disLightOn secs
    displayON();
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
    Serial.println(String(IDENT) + ";cmd?;" + inStr);
    lcd.setCursor(0, 2); lcd.print(F("Unknown command     "));
    lcd.setCursor(0, 3); lcd.print(F("logout and reset    "));
    noact();
  }
  inStr = "";
}
// End Funktions Serial Input -------------------

// PROGRAM LOOP AREA ----------------------------
void loop()
{
  r.execute();
  if (Serial.available() > 0)
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
}
