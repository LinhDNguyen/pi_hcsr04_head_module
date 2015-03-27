pi_hcsr04_head_module
================

This driver create an character device which allows user configure the frequence
of Untrasonic measurement.

```
- GPIO16 - J8 36: both HC-SR04's trigger pins
- GPIO20 - J8 38:
- GPIO21 - J8 40: these two pins are echo pins of two HC-SR04 sensor.
```

Hardware setups
---------------

The dual HC-SR04 sensors will use the same vcc, gnd and trigger pin signal.
Look like bellow:
```
    +-----+                                                           +-----+
    |     | gnd                  PI GND (J8-34)                   gnd |     |
    |     |===========-----------------o-------------------===========|     |
    |  H  |                                                           |  H  |
    |  C  | echo     PI ECHO 1 (J8-38)     PI ECHO 2 (J8-40)     echo |  C  |
    |  -  |===========----o                          o-----===========|  -  |
    |  S  |                                                           |  S  |
    |  R  | trigger          PI TRIGGER PIN (J8-36)           trigger |  R  |
    |  0  |===========----------------o--------------------===========|  0  |
    |  4  |                                                           |  4  |
    |     | vcc                     PI 5V (J8-02)                 vcc |     |
    |     |===========----------------o--------------------===========|     |
    +-----+                                                           +-----+
```

Usage
-----

This module will create character device */dev/dual_hcsr04*, and you can write a
number value to set sampling frequence, read go get latest distance value of
both HC-SR04 sensor.

### Sampling frequence

You can set sampling frequence by write this number to */dev/dual_hcsr04*
```bash
echo 10 > /dev/dual_hcsr04
```
will set sampling frequence to 10 samples per second.
Maximum frequence is 50 samples per second.
If frequence is 0, the module will stop measurement.
```bash
echo 0 > /dev/dual_hcsr04
```

### Get distance value

Read file */dev/dual_hcsr04* to get latest value of both HC-SR04 sensors.
```bash
cat /dev/dual_hcsr04
```
