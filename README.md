This is an implementation of an AGC and a DSKY coded in C on a Raspberry Pi.
One core runs the AGC emulator, the other the DSKY, both communicating through queues.
Hardware-wise, the Pi displays its contents on a touch-screen TFT. 
An SD card reader is also present (part of the TFT card) and is used for storing the "ropes" - the AGC native apps.

An AGC rope (lunlandr and lunlandr.bin) is present and contains a version of Lunar Lander coded in AGC assembler.
For this Lunar Lander version you start with an altitude of 4000 meters, -50 m/s of vertical speed and a fuel load of 500 Kg.
The game starts when you press the ENT key and can be restarted anytime by pressing the RST key on the DSKY.
You control the rate of descent by throttling up/down the LM motor. 0 for no thrust, 9 for max thrust.
If you manage to land with a vertical speed <= -3 m/s you have what it takes. Otherwise you will be rewarded with the full complement of warning lights coming on at you.

There is provision in the hardware / software to connect a simple 4-way digital joystick to control the thrust level.
Forward increases thrust, back decreases, left cuts thrust, right takes it to max.

A circuit schematic and a PCB layout (not updated) are also present in Kicad format.

Finally, credit must go to Ron Burkey who coded the entire AGC emulator in C. I merely used it as part of this game/simulation.
