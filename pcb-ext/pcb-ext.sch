EESchema Schematic File Version 4
EELAYER 30 0
EELAYER END
$Descr A4 11693 8268
encoding utf-8
Sheet 1 1
Title "ESP32 IO Expansion Board"
Date "2020-11-16"
Rev "1.0"
Comp ""
Comment1 ""
Comment2 ""
Comment3 ""
Comment4 ""
$EndDescr
Text Notes 9050 850  0    50   ~ 0
Left side expansion
$Comp
L Connector_Generic:Conn_01x13 J4
U 1 1 5FBABCD6
P 10600 1600
F 0 "J4" H 10680 1642 50  0000 L CNN
F 1 "RIGHT-EXT" H 10680 1551 50  0000 L CNN
F 2 "Connector_PinHeader_2.54mm:PinHeader_1x13_P2.54mm_Vertical" H 10600 1600 50  0001 C CNN
F 3 "~" H 10600 1600 50  0001 C CNN
	1    10600 1600
	1    0    0    -1  
$EndComp
$Comp
L Connector_Generic:Conn_01x13 J2
U 1 1 5FBACE6E
P 9600 1600
F 0 "J2" H 9680 1642 50  0000 L CNN
F 1 "LEFT-EXT" H 9680 1551 50  0000 L CNN
F 2 "Connector_PinHeader_2.54mm:PinHeader_1x13_P2.54mm_Vertical" H 9600 1600 50  0001 C CNN
F 3 "~" H 9600 1600 50  0001 C CNN
	1    9600 1600
	1    0    0    -1  
$EndComp
Text GLabel 10400 2000 0    50   BiDi ~ 0
IO1
Text GLabel 10400 1900 0    50   BiDi ~ 0
IO2
Text GLabel 10400 1800 0    50   BiDi ~ 0
IO3
Text GLabel 10400 1700 0    50   BiDi ~ 0
IO4
Text GLabel 10400 1600 0    50   BiDi ~ 0
IO5
Text GLabel 10400 1500 0    50   BiDi ~ 0
IO6
Text GLabel 10400 1400 0    50   BiDi ~ 0
IO7
Text GLabel 10400 1300 0    50   BiDi ~ 0
IO8
$Comp
L power:GND #PWR08
U 1 1 5FBB3A82
P 10400 1100
F 0 "#PWR08" H 10400 850 50  0001 C CNN
F 1 "GND" V 10405 972 50  0000 R CNN
F 2 "" H 10400 1100 50  0001 C CNN
F 3 "" H 10400 1100 50  0001 C CNN
	1    10400 1100
	0    1    1    0   
$EndComp
$Comp
L power:+3V3 #PWR09
U 1 1 5FBB3A8C
P 10400 1200
F 0 "#PWR09" H 10400 1050 50  0001 C CNN
F 1 "+3V3" V 10400 1450 50  0000 C CNN
F 2 "" H 10400 1200 50  0001 C CNN
F 3 "" H 10400 1200 50  0001 C CNN
	1    10400 1200
	0    -1   -1   0   
$EndComp
Text GLabel 9400 1200 0    50   BiDi ~ 0
IO11
Text GLabel 9400 1300 0    50   BiDi ~ 0
IO12
Text GLabel 9400 1400 0    50   BiDi ~ 0
IO13
Text GLabel 9400 1500 0    50   BiDi ~ 0
IO14
$Comp
L power:GND #PWR04
U 1 1 5FBB7FC6
P 9400 2200
F 0 "#PWR04" H 9400 1950 50  0001 C CNN
F 1 "GND" V 9405 2072 50  0000 R CNN
F 2 "" H 9400 2200 50  0001 C CNN
F 3 "" H 9400 2200 50  0001 C CNN
	1    9400 2200
	0    1    1    0   
$EndComp
Text GLabel 9400 1700 0    50   BiDi ~ 0
IO16
Text GLabel 9400 1100 0    50   BiDi ~ 0
IO10
Text GLabel 9400 1600 0    50   BiDi ~ 0
IO15
Text GLabel 9400 1000 0    50   BiDi ~ 0
IO9
$Comp
L power:VCC #PWR07
U 1 1 5FBD40F3
P 10400 1000
F 0 "#PWR07" H 10400 850 50  0001 C CNN
F 1 "VCC" V 10400 1200 50  0000 C CNN
F 2 "" H 10400 1000 50  0001 C CNN
F 3 "" H 10400 1000 50  0001 C CNN
	1    10400 1000
	0    -1   -1   0   
$EndComp
Text GLabel 10400 2200 0    50   BiDi ~ 0
I2C_SCL
Text GLabel 10400 2100 0    50   BiDi ~ 0
I2C_SDA
$Comp
L power:+5V #PWR03
U 1 1 5FBE081B
P 9400 2100
F 0 "#PWR03" H 9400 1950 50  0001 C CNN
F 1 "+5V" V 9400 2300 50  0000 C CNN
F 2 "" H 9400 2100 50  0001 C CNN
F 3 "" H 9400 2100 50  0001 C CNN
	1    9400 2100
	0    -1   -1   0   
$EndComp
Text GLabel 9400 2000 0    50   BiDi ~ 0
RESET
Text GLabel 9400 1800 0    50   BiDi ~ 0
FACTORY_RESET
Text GLabel 9400 1900 0    50   BiDi ~ 0
USER
Text Notes 10000 850  0    50   ~ 0
Right side expansion
$EndSCHEMATC
