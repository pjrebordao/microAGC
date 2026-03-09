This is an implementation of an AGC and a DSKY coded in C on a Raspberry Pi.
One core runs the AGC emulator, the other the DSKY, both communicating through queues.
Hardware-wise, the Pi displays its contents on a touch-screen TFT. 
An SD card reader is also present (part of the TFT card) and is used for storing the "ropes" - the AGC native apps.

A circuit schematic and a PCB layout are also present in Kicad format.
