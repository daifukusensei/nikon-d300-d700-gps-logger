# Configuring the HC-05 Bluetooth module for use with the Nikon D300(s)/D700 GPS Logger
Proceed as follows to configure the the HC-05 Bluetooth module:
1. Upload [hc-05-setup.ino](./hc-05-setup.ino) to Arduino of choice
2. Connect HC-05 to Arduino as follows:
	- HC-05 TX <-> Arduino pin 10
	- HC-05 RX <-> Arduino pin 11
	- HC-05 5v <-> Arduino 5v
	- HC-05 GND <-> Arduino ground
3. Hold HC-05 button to enter AT mode upon applying power to Arduino. HC-05 LED should blink slowly
4. Open Arduino IDE's serial monitor at 9600 baud rate with both NL & CR selected, and type AT. HC-05 should return OK
5. Enter the following commands in sequence, to which the HC-05 should always respond OK:
	1. AT+UART=4800,0,0
	2. AT+PSWD="0000"
	3. AT+ROLE=0
6. Disconnect and install in the Nikon D300(s)/D700 GPS Logger

Please find additional documents that may prove as useful reference:
- [Arduino_with_HC-05_(ZS-040)_Bluetooth.pdf](./Arduino_with_HC-05_(ZS-040)_Bluetooth.pdf)
- [HC-05_ATCommandSet.pdf](./HC-05_ATCommandSet.pdf)