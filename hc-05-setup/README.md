# Configuring the HC-05 Bluetooth module for use with the Nikon D300(s)/D700 GPS Logger
Proceed as follows to configure the the HC-05 Bluetooth module:
1. Upload [hc-05-setup.ino](./hc-05-setup.ino) to Arduino of choice
2. Connect HC-05 to Arduino as follows:
	- HC-05 TX <-> Arduino pin 10
	- HC-05 RX <-> Arduino pin 11
	- HC-05 5v <-> Arduino 5v
	- HC-05 GND <-> Arduino ground
3. Hold HC-05 button to enter AT mode upon applying power to Arduino. HC-05 LED should blink slowly
4. Open serial monitor at 9600 baud rate with both NL & CR selected, and type AT. HC-05 should return OK
5. Enter the following commands in sequence:
	1. AT+ORGL
	2. AT+UART=4800,0,0
	3. AT+PSWD="0000"
	4. AT+ROLE=0
6. Disconnect and install in the Nikon D300(s)/D700 GPS Logger

Included here are additional documents that may prove as useful reference:
- [Arduino with HC-05 (ZS-040) Bluetooth.pdf](./Arduino with HC-05 (ZS-040) Bluetooth.pdf)
- [HC-05_ATCommandSet.pdf]](./HC-05_ATCommandSet.pdf)