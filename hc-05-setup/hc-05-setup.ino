#include <SoftwareSerial.h>

SoftwareSerial BTSerial(10, 11); //HC-05 TX, HC-05 RX

void setup() {
  //pinMode(9, OUTPUT);  //HC-05 EN pin-- disconnected on many HC-05 breakout boards and so will not be used here
  //digitalWrite(9, HIGH);
  Serial.begin(9600);
  Serial.println("Enter AT commands:");
  BTSerial.begin(38400);  //HC-05 default speed in AT mode
}

void loop() {
  //keep reading from HC-05 and send to Arduino Serial Monitor
  if (BTSerial.available())
    Serial.write(BTSerial.read());

  //keep reading from Arduino Serial Monitor and send to HC-05
  if (Serial.available())
    //set to 4800 baud "AT+UART=4800,0,0\r\n"
    BTSerial.write(Serial.read());
}
