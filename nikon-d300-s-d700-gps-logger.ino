// GPS code from https://randomnerdtutorials.com/guide-to-neo-6m-gps-module-with-arduino/
// LED blinking code from https://learn.adafruit.com/multi-tasking-the-arduino-part-1/using-millis-for-timing
// EEPROM code from https://www.arduino.cc/en/Tutorial/EEPROMWrite, https://www.arduino.cc/en/Reference/EEPROMRead
// Software Serial code from https://www.arduino.cc/en/tutorial/SoftwareSerialExample

#include <EEPROM.h>
#include <NMEAGPS.h>
#include <SdFat.h>
#include <SoftwareSerial.h>
#include <SPI.h>

#define GPSPORT Serial // serial port of the connected GPS

#define BTENPIN 17 // the number of the bluetooth HC-05 module's EN pin, used for initiating AT mode to re-configure the device
#define BTRXPIN 19 // the number of the bluetooth HC-05 module's RX pin, used for receiving AT commands issued via serial communication
#define BTVCCPIN 16 // the number of the 2N2222 transistor's base pin, used to power on/off the bluetooth HC-05 module
#define BTSTATEPIN 15 // the number of the bluetooth HC-05 module's state pin, used for controlling the bluetooth state LED
#define BTNLED 8 // the number of the LED state-toggle button's pin
#define BTNBT 6 // the number of the bluetooth state-toggle button's pin
#define BTNLOG 5 // the number of the microSD card logging state-toggle button's pin
#define BTNMARKER 7 // the number of the waypoint-marker button's pin (used for marking a waypoint)
#define LEDBLUETOOTH 4 // the number of the bluetooth LED
#define LEDGPS 3 // the number of the GPS-fix LED
#define LEDLOG 2 // the number of the logging LED (indicates writes to microSD card)

SoftwareSerial BTSERIAL(20, BTRXPIN); // RX, TX on Arduino board

// define button struct
typedef struct Btn {
  Btn(byte pin); // the physical number of the button's pin
  byte pin; // the physical number of the button's pin
  byte currState = 0; // current state of button press (LOW or HIGH)
  byte lastState = 0; // prior state of button press for comparison during program execution
  boolean ignore = false; // used to help manage button presses
  boolean longPress = false; // indicates when a button was long-pressed
  unsigned long millisDown = 0; // milliseconds since a button has first been held
  unsigned long millisUp = 0; // milliseconds since a button has been released
  void checkState(); // method for checking the state of each button
};

// define led class
typedef struct Led {
  Led(byte pin); // the physical number of the LED's pin
  byte pin; // the physical number of the LED's pin
  boolean turnOff = false; // flag to set LED state to LOW (off) on next pass of checkLeds() function
  unsigned long millisLedOnStart = 0; // milliseconds since last time LED was first turned on
  byte millisLedOnMax; // max milliseconds of on-time for LED when blinking
  short int millisLedOffMax; // max milliseconds of off-time for LED when blinking
  void blink(); // method for blinking LED at defined intervals with each pass of checkLeds() function
  boolean longBlink = false; // flag to indicate that LED should remain on for an extended period of time at next blink interval
  void turnOn(); // method for immediately setting LED state to HIGH (on)
};

Btn btnLedState(BTNLED); // create a Btn object for the LED state-toggle button
Btn btnBluetooth(BTNBT); // create a Btn object for the bluetooth state-toggle button
Btn btnLogging(BTNLOG); // create a Btn object for the microSD card logging state-toggle button
Btn btnMarker(BTNMARKER); // create a Btn object for the waypoint-marker button (used for marking a waypoint)
Led ledBluetooth(LEDBLUETOOTH); // create an Led object for the bluetooth LED
Led ledGps(LEDGPS); // create an Led object for the GPS-fix LED
Led ledLog(LEDLOG); // create an Led object for the logging LED (indicates writes to microSD card)

NMEAGPS gps; // GPS object
gps_fix fix; // GPS fix structure
SdFat SD; // microSD card object
File logfile; // file to log to on microSD card

// define constants
const byte sdCSPin = 14; // the number of the microSD card adapter CS pin
const unsigned short int baud = 4800; // target baud rate of the bluetooth and GPS modules
const short int millisBtnLongPressMax = 2000; // max milliseconds before a button long-press is detected
const short int millisLoggingInterval = 2000; // interval between writes to microSD card

// define global variables
boolean sdCardReady = false; // used to indicate if microSD card is ready
boolean sdLogReady = false; // used to indicate if a logfile on microSD has been created and is available for writing
unsigned long millisLoggingStart = 0; // milliseconds since last time access to the microSD card was initiated
char logfileName[] = "yymmdd_log_##.txt"; // dynamic track logging filename on microSD card (based on unique date & number-sequence)

// initial state parameters
boolean setBluetoothOn = false; // bluetooth is either OFF or ON (default OFF)
boolean setLedOn = true; // LEDs are either ON or OFF (default ON)
boolean setLoggingOn = false; // logging is either OFF or ON (default OFF)

void setup() {  
  //setDefaults();  // erase EEPROM and set to defaults
 
  // read EEPROM values into memory
  readEeprom();

  // intialize the LEDs
  ledBluetooth.millisLedOnMax = 100;
  ledBluetooth.millisLedOffMax = 1000;
  ledGps.millisLedOnMax = 100;
  ledGps.millisLedOffMax = 1000;
  ledLog.millisLedOnMax = 100;

  pinMode(BTENPIN, OUTPUT);
  digitalWrite(BTENPIN, LOW); // bluetooth HC-05 module's EN pin is only used when re-configuring the device, and is thus set to LOW
  pinMode(BTVCCPIN, OUTPUT);
  digitalWrite(BTVCCPIN, LOW); // start with bluetooth HC-05 module off unless user settings (read later from EEPROM) indicate otherwise
  pinMode(BTRXPIN, INPUT); // set Arduino's TX pin (bluetooth HC-05 module's RX pin) to input mode so as to not interfere with GPS
                           // NMEA sentences being sent to the HC-05 module's same RX pin and the same 'line' to Arduino's serial RX--
                           // perhaps one day I'll learn enough to figure out the issue and why input mode works

  // should such settings have been lost, reset the bluetooth HC-05 module's baud rate, pin and role to what's required
  // of slave operations by holding the waypoint-marker button when powering on-- may require a few attempts
  if (digitalRead(btnMarker.pin) == LOW) {
    ledBluetooth.turnOn(); // immediately turn on the bluetooth LED to indicate attempted reset of bluetooth HC-05 module
    resetBluetooth(baud);
  }
  
  // initialize the microSD card
  sdInit();

  GPSPORT.begin(9600); // open serial connection with GPS at default baud rate of 9600
  
  setGpsBaudRate(baud); // set the GPS baud rate to 4800 with each startup
  byte compatMode[] = { 0xB5, 0x62, 0x06, 0x17, 0x04, 0x00, 0x00, 0x23, 0x00, 0x03, 0x47, 0x55, 0xE3, 0xC6 };
  sendUBX(compatMode, sizeof(compatMode) / sizeof(byte)); // set the GPS compat flag via a u-blox UBX-CFG-NMEA command to enable
                 // compatibility mode and present co-ordinates in 4 decimal places rather than 5, required for Nikon cameras
}

void loop() {
  checkBtns(); // check state of buttons
  checkLeds(); // take action on LEDs (blink, turn off, etc.)
  checkSerialData(); // check incoming serial data and pass to gps object
  checkBluetooth(); // take actions against bluetooth HC-05 module
  checkLogger(); // take logging actions
}

void checkBtns() {
  btnLedState.checkState();
  btnBluetooth.checkState();
  btnLogging.checkState();
  btnMarker.checkState();

  // toggle LED state
  if (btnLedState.longPress) { // the LED state-toggle button was long-pressed
    setLedOn = ! setLedOn; // toggle LED state and write to EEPROM
    writeEeprom();
  }
  
  // add a waypoint-marker
  if (btnMarker.longPress) { // the waypoint-marker button was long-pressed
    if (sdCardReady) { // attempt to mark a waypoint only if microSD card is accessible
      if (fix.valid.location) {
        ledLog.longBlink = true; // indicate that logging LED should remain on for a brief period of time to visually indicate a
                                 // waypoint was written to file
        sdWriteMarker(); // write a waypoint to file      
      }
    }
  }

  // toggle bluetooth state
  if (btnBluetooth.longPress) { // the bluetooth state-toggle button was long-pressed
    ledBluetooth.longBlink = true; // indicate that bluetooth LED should remain on for a brief period of time to visually indicate
                                   // change of state
    setBluetoothOn = ! setBluetoothOn; // toggle bluetooth state and write to EEPROM
    writeEeprom();
  }

  // toggle logging state
  if (btnLogging.longPress) { // the microSD card logging state-toggle button was long-pressed
    if (sdCardReady) { // attempt to change logging state only if microSD card is accessible
      ledLog.longBlink = true; // indicate that logging LED should remain on for a brief period of time to visually indicate change of
                               // state
      setLoggingOn = ! setLoggingOn; // toggle logging state and write to EEPROM
      writeEeprom();
    }
  }
}

void Btn::checkState() {
  // reset button state so as to not continually change settings with each loop if button is held too long
  longPress = false;

  currState = digitalRead(pin); // get current state of button to determine if it's been pressed
  if (currState == LOW) { // button is currently DOWN (pressed/held); reset millisDown timer
    millisDown = millis();
    if (((millisDown - millisUp) >= millisBtnLongPressMax) && (! ignore)) { // take action on long-presses
      longPress = true; // indicate that the button was long-pressed
      ignore = true; // indicate that the button is still held to avoid acting on another long-press with each loop
    }
  } else {
    millisUp = millis();
    ignore = false;
  }
    
  lastState = currState; // save state of button press for comparison with each loop
}

void checkSerialData() {
  // wait for all serial data to be consumed from buffer before passing it to the gps object
  while (gps.available(GPSPORT)) {
    fix = gps.read();
  }
}

void checkBluetooth() {
  digitalWrite(BTVCCPIN, setBluetoothOn ? HIGH : LOW); // enable or disable HC-05 module, based on user settings
}

void checkLogger() {
  if (setLoggingOn) {
    if (sdCardReady) {
      if (! sdLogReady) {
        if (fix.valid.date) {
          // create a new date & number-sequenced logfile on microSD card each time the logging state is enabled/re-enabled
          sdCreateLog();
        }
      } else if (millis() - millisLoggingStart >= millisLoggingInterval) {
        // log to microSD card at a specified interval if it's been successfully initialized and a logfile is ready for writing
        sdWriteFile(logfile, logfileName);
      }
    } else {
      setLoggingOn = false; // disable logging as microSD card is not available
    }
  } else {
    sdLogReady = false; // logging state is disabled, so indicate that no logfile is available for writing
  }
}

void checkLeds() { 
  if (setLedOn) { // LEDs are enabled
    // GPS-fix LED
    // blink continuously until a GPS-fix is detected, then remain lit
    if (fix.valid.location) {
      digitalWrite(ledGps.pin, HIGH);
    } else {
      ledGps.blink();
    }
  
    // Bluetooth state LED
    // blink if bluetooth is on until pairing is established, then remain lit
    if ((setBluetoothOn) || (ledBluetooth.longBlink)) { // bluetooth is enabled or longBlink flag was set on bluetooth LED,
                                                        // indicating recent change to state (enabled/disabled)
      if (digitalRead(BTSTATEPIN) == HIGH) { // HC-05 module's state pin indicates a device is paired, so turn on bluetooth LED
        digitalWrite(ledBluetooth.pin, HIGH);
      } else {
        ledBluetooth.blink(); // bluetooth is enabled but no device is yet paired, so blink
      }  
    } else {
      digitalWrite(ledBluetooth.pin, LOW); // bluetooth is disabled, so turn off LED
    }

    // Logging state LED
    if (! sdCardReady) {
      digitalWrite(ledLog.pin, HIGH); // stay on if microSD card is not yet ready, to indicate there's a problem...
    } else if (ledLog.longBlink) {
      ledLog.blink(); // ...else, remain on for a brief period of time if either the waypoint-marker or logging buttons were
                      // long-pressed to add a marker or change logging state respectively, or turn off LED (below) if we've manually
                      // flagged to do so via turnOff boolean
    } else if ((digitalRead(ledLog.pin) == HIGH) && (ledLog.turnOff) && (millis() - ledLog.millisLedOnStart >= ledLog.millisLedOnMax)) {
      ledLog.millisLedOnStart = millis(); // remember the time
      digitalWrite(ledLog.pin, LOW); // update the actual LED
      ledLog.turnOff = false;
    }
  } else {
    ledBluetooth.longBlink = false; // longBlinks are only reset to false on next call of an LED's blink routine, which doesn't occur
                                    // with LEDs off- so set longBlinks to false here should long-presses have occured while LEDs, to
                                    // avoid having them longBlink when LEDs are next turned on
    ledGps.longBlink = false;
    ledLog.longBlink = false;
    digitalWrite(ledBluetooth.pin, LOW);
    digitalWrite(ledGps.pin, LOW);
    digitalWrite(ledLog.pin, LOW);
  }
}

void Led::blink() {
  if ((digitalRead(pin) == HIGH) && (millis() - millisLedOnStart >= (longBlink ? millisLedOnMax * 20 : millisLedOnMax))) {
    millisLedOnStart = millis(); // remember the time
    digitalWrite(pin, LOW); // update the actual LED
    longBlink = false; // exceeded time LED is to remain on, so set longBlink flag to false regardless of it's prior state
  } else if (digitalRead(pin) == LOW) {
    if ((millis() - millisLedOnStart >= millisLedOffMax) || (longBlink)) { // set LED to high if we've exceeded time LED
                                                                           // is to remain off, OR longBlink was flagged
      millisLedOnStart = millis(); // remember the time
      digitalWrite(pin, HIGH); // update the actual LED
    }
  }
}

void Led::turnOn() {
  // function to immediately turn on LED
  if ((setLedOn) && ! (longBlink)) { // turn on LED only if user has enabled them AND a long-press has not already occurred
                                     // to result in a longBlink, in order to avoid timing issues when blinking it on and off
                                     // from the checkLeds routine- otherwise the logging LED will remain lit longer than
                                     // expected during logging state change 
    digitalWrite(pin, HIGH); // update the actual LED
    millisLedOnStart = millis(); // remember the time
  }
}

void readEeprom() {
  // first check if the used EEPROM address space includes any bytes with values of 255, which is outside the
  // range of our stored values and is indicative of a default Arduino EEPROM state
  for (char eepromAddr = 0; eepromAddr <= 2; eepromAddr++) {
    if (EEPROM.read(eepromAddr) == 255) { // not within range, so set parameters to usable defaults and write to EEPROM
      setDefaults();
      break;
    }
  }

  setBluetoothOn = EEPROM.read(0); // read index of bluetooth state setting from EEPROM
  setLedOn = EEPROM.read(1); // read index of LED state setting from EEPROM
  setLoggingOn = EEPROM.read(2); // read index of logging state setting from EEPROM
}

void writeEeprom() {
  EEPROM.update(0, setBluetoothOn); // write index of bluetooth state setting to EEPROM only if changed
  EEPROM.update(1, setLedOn); // write index of LED state setting to EEPROM only if changed
  EEPROM.update(2, setLoggingOn); // write index of logging state setting to EEPROM only if changed
}

void setDefaults() {
  setBluetoothOn = false;
  setLedOn = true;
  setLoggingOn = false;

  writeEeprom();
}

void resetBluetooth(unsigned short int baud) {
  digitalWrite(BTVCCPIN, LOW); // turn off bluetooth HC-05 module if it's not already off
  delay(100);
  digitalWrite(BTENPIN, HIGH); // set bluetooth HC-05 module to AT mode by first bringing up EN pin before applying power (VCC)
  delay(100);
  digitalWrite(BTVCCPIN, HIGH);
  pinMode(BTRXPIN, OUTPUT); // set Arduino's TX pin (bluetooth HC-05 module's RX pin) to output mode to enable serial communication
  delay(250);

  BTSERIAL.begin(38400); // open serial connection with bluetooth HC-05 module
  delay(250);
  
  BTSERIAL.write("AT\r\n"); // oddly, after toggling BTRXPIN from input to output, an initial AT command needs to first be sent to 'wake
                            // up' the bluetooth HC-05 module, after which subsequent commands will be accepted
  delay(1000); // shorter delays resulted in subsequent AT commands not being acknowledged
  switch(baud) { // send to the bluetooth HC-05 module the required AT command for changing its baud rate
    case 4800:
      BTSERIAL.write("AT+UART=4800,0,0\r\n");
      break;

    case 9600:
      BTSERIAL.write("AT+UART=9600,0,0\r\n");
      break;

    default:
      break;
  }
  delay(1000);

  BTSERIAL.write("AT+PSWD=\"0000\"\r\n"); // reset the bluetooth HC-05 module's pin
  delay(1000);

  BTSERIAL.write("AT+NAME=Nikon-D300(S)-D700-GPS-Logger\r\n"); //set name
  delay(1000);

  BTSERIAL.write("AT+ROLE=0\r\n"); // reset the bluetooth HC-05 module's role to slave
  delay(1000);

  BTSERIAL.end(); // close serial connection with bluetooth HC-05 module
  digitalWrite(BTENPIN, LOW);
  digitalWrite(BTVCCPIN, LOW); // cut power to bluetooth HC-05 module
  delay(100);
  pinMode(BTRXPIN, INPUT); // set Arduino's TX pin (bluetooth HC-05 module's RX pin) to input mode so as to not interfere with GPS
                           // NMEA sentences being sent to the HC-05 module's same RX pin and the same 'line' to Arduino's serial RX--
                           // perhaps such interference is due to additional voltage leaking over from Arduino when in output mode, thus
                           // malforming the GPS feed? Perhaps one day I'll learn enough to figure out the issue and why input mode works
  digitalWrite(BTVCCPIN, setBluetoothOn ? HIGH : LOW); // power up bluetooth HC-05 module if needed, based on user settings
  delay(100);
}

void setGpsBaudRate(unsigned short int baud) {
  for (int i = 0; i < 2; i++) {
    delay(100);

    switch(baud) { // send to the u-blox NEO-6M GPS the required NMEA sentence for changing its baud rate
      case 4800:
        GPSPORT.write("$PUBX,41,1,0007,0003,4800,0*13\r\n");
        break;

      case 9600:
        GPSPORT.write("$PUBX,41,1,0007,0003,9600,0*10\r\n");
        break;

      default:
        break;
    }

    delay(100);
    GPSPORT.end();
    delay(100);
    GPSPORT.begin(9600); // we attempt to set the baud rate twice- initially with the connected serial port already open should the GPS
                         // currently be active and accepting of commands, and again with the connected serial port re-opened at the GPS'
                         // default baud rate of 9600 should it have forgotten any previous configuration changes
  }

  delay(100);
  GPSPORT.end();
  delay(100);
  GPSPORT.begin(baud); // re-open the serial port at our desired baud rate
}

void sendUBX(byte *command, byte len) {
  // compat mode command courtesy of: https://www.navilock.com/service/fragen/gruppe_59_uCenter/beitrag/101_ublox-Center-Compatibility-Mode-since-ublox-6.html
  // backup mode command courtesy of: https://forum.arduino.cc/index.php?topic=497410.0
  //                                  https://ukhas.org.uk/guides:ublox_psm
  // checksum calculation courtesy of: https://gist.github.com/tomazas/3ab51f91cdc418f5704d

  for (byte i = 0; i < len; i++) {
    GPSPORT.write(command[i]);
  }
}

void sdInit() {
  // see if the microSD card is present and can be initialized
  if (SD.begin(sdCSPin)) {
    sdCardReady = true; // indicate we're ready to begin logging
  }
}

void sdCreateLog() {
  // pick a new date & number-sequenced filename with each power cycle
  sprintf(logfileName, "%02d%02d%02d", fix.dateTime.year, fix.dateTime.month, fix.dateTime.date); // get current date from GPS
  strcat(logfileName, "_log_##.txt"); // append _##.txt to filename*/

  for (uint8_t i = 0; i < 100; i++) {
    logfileName[11] = '0' + i/10;
    logfileName[12] = '0' + i%10;
    if (! SD.exists(logfileName)) {
      // use this one!
      break;
    }
  }

  logfile = SD.open(logfileName, FILE_WRITE);
  if (logfile) {
    ledLog.turnOn(); // turn on logging LED immediately
    //logfile.println( F("Latitude,Longitude,Speed (km/h),Heading (degrees),Altitude (floating-point meters),Date and Time (UTC)") );
    logfile.println("Latitude,Longitude,Speed (km/h),Heading (degrees),Altitude (floating-point meters),Date and Time (UTC)");
    logfile.flush(); // close file after each write as only a single file can be open at a time
    ledLog.turnOff = true; // indicate that logging LED should be turned off at next pass of checkLeds() function
    
    sdLogReady = true; // indicate that a new logfile is available for writing
  } else {
    sdCardReady = false; // there was a problem creating a new logfile, so set microSD card ready state to false to visually report a problem
    setLoggingOn = false; // disable logging as microSD card is not available
  }
}

void sdWriteMarker() {
  File waypointfile;
  char waypointfileName[] = "waypoints.txt";
  
  if (! SD.exists(waypointfileName)) { // create waypoints.txt file if it doesn't already exist
    waypointfile = SD.open(waypointfileName, FILE_WRITE);
    if (waypointfile) {
      ledLog.turnOn(); // turn on logging LED immediately
      //waypointfile.println( F("Latitude,Longitude,Speed (km/h),Heading (degrees),Altitude (floating-point meters),Date and Time (UTC)") );
      waypointfile.println("Latitude,Longitude,Speed (km/h),Heading (degrees),Altitude (floating-point meters),Date and Time (UTC)");
      waypointfile.flush(); // close file after each write as only a single file can be open at a time
      ledLog.turnOff = true; // indicate that logging LED should be turned off at next pass of checkLeds() function
    } else {
      sdCardReady = false; // there was a problem creating a new waypoints file, so set microSD card ready state to false to visually report a problem
      setLoggingOn = false; // disable logging as microSD card is not available
    }
  }

  sdWriteFile(waypointfile, waypointfileName);
}

void sdWriteFile(File file, const char *filename) {
  if (fix.valid.location) {
    file = SD.open(filename, FILE_WRITE);
    if (file) {
      ledLog.turnOn(); // turn on logging LED immediately
      printL( file, fix.latitudeL() ); // write current latitude to file
      file.print( ',' );
      
      printL( file, fix.longitudeL() ); // write current longitude to file
      file.print(',');
      
      printL( file, fix.speed_kph() ); // write current speed in km/h to file
      file.print(',');
  
      printL( file, fix.heading() ); // write current heading in degrees to file
      file.print(',');
  
      printL( file, fix.altitude() ); // write current altitude in meters to file
      file.print(',');
  
      if (fix.dateTime.hours < 10) // write current date & time in UTC format to file
        file.print( '0' );
      file.print(fix.dateTime.hours);
      file.print( ':' );
      if (fix.dateTime.minutes < 10)
        file.print( '0' );
      file.print(fix.dateTime.minutes);
      file.print( ':' );
      if (fix.dateTime.seconds < 10)
        file.print( '0' );
      file.print(fix.dateTime.seconds);
      file.print( '.' );
      if (fix.dateTime_cs < 10)
        file.print( '0' ); // leading zero for .05, for example
      file.print(fix.dateTime_cs);
  
      file.println();
      
      file.flush(); // close file
      ledLog.turnOff = true; // indicate that logging LED should be turned off at next pass of checkLeds() function
      millisLoggingStart = millis(); // remember the time
    } else {
      sdCardReady = false; // there was a problem writing to file, so set microSD card ready state to false to visually report a problem
      setLoggingOn = false; // disable logging as microSD card is not available
    }
  }
}

void printL(Print & outs, int32_t degE7) {
  // extract and print negative sign
  if (degE7 < 0) {
    degE7 = -degE7;
    outs.print( '-' );
  }

  // whole degrees
  int32_t deg = degE7 / 10000000L;
  outs.print( deg );
  outs.print( '.' );

  // get fractional degrees
  degE7 -= deg*10000000L;

  // print leading zeroes, if needed
  if (degE7 < 10L)
    //outs.print( F("000000") );
    outs.print("000000");
  else if (degE7 < 100L)
    //outs.print( F("00000") );
    outs.print("00000");
  else if (degE7 < 1000L)
    //outs.print( F("0000") );
    outs.print("0000");
  else if (degE7 < 10000L)
    //outs.print( F("000") );
    outs.print("000");
  else if (degE7 < 100000L)
    //outs.print( F("00") );
    outs.print("00");
  else if (degE7 < 1000000L)
    //outs.print( F("0") );
    outs.print("0");
  
  // print fractional degrees
  outs.print( degE7 );
}

// description of the Btn struct constructor
Btn::Btn(byte p) {
  pin = p;
  pinMode(pin, INPUT_PULLUP); // set pin as input
}

// description of the Led struct constructor
Led::Led(byte p) {
  pin = p;
  pinMode(pin, OUTPUT); // set pin as output
  digitalWrite(pin, LOW);
}
