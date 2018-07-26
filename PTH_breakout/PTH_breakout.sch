EESchema Schematic File Version 4
EELAYER 26 0
EELAYER END
$Descr A4 11693 8268
encoding utf-8
Sheet 1 1
Title "PTH Protoboard"
Date ""
Rev ""
Comp ""
Comment1 "#badgelife"
Comment2 ""
Comment3 ""
Comment4 ""
$EndDescr
$Comp
L Connector:Conn_02x02_Counter_Clockwise J1
U 1 1 5B539592
P 3200 2700
F 0 "J1" H 3250 2917 50  0000 C CNN
F 1 "Conn_02x02_Counter_Clockwise" H 3250 2826 50  0000 C CNN
F 2 "Badgelife:ShittyAddon_PTH_only_SAO" H 3200 2700 50  0001 C CNN
F 3 "~" H 3200 2700 50  0001 C CNN
	1    3200 2700
	1    0    0    -1  
$EndComp
$Comp
L power:VCC #PWR0101
U 1 1 5B5395E9
P 2600 2650
F 0 "#PWR0101" H 2600 2500 50  0001 C CNN
F 1 "VCC" H 2617 2823 50  0000 C CNN
F 2 "" H 2600 2650 50  0001 C CNN
F 3 "" H 2600 2650 50  0001 C CNN
	1    2600 2650
	1    0    0    -1  
$EndComp
$Comp
L power:GND #PWR0102
U 1 1 5B53962E
P 2600 2850
F 0 "#PWR0102" H 2600 2600 50  0001 C CNN
F 1 "GND" H 2605 2677 50  0000 C CNN
F 2 "" H 2600 2850 50  0001 C CNN
F 3 "" H 2600 2850 50  0001 C CNN
	1    2600 2850
	1    0    0    -1  
$EndComp
Wire Wire Line
	3000 2700 2600 2700
Wire Wire Line
	2600 2700 2600 2650
Wire Wire Line
	3000 2800 2600 2800
Wire Wire Line
	2600 2800 2600 2850
Text GLabel 3500 2700 2    50   Input ~ 0
SDA
Text GLabel 3500 2800 2    50   Input ~ 0
SCL
$Comp
L Connector:Conn_01x08 J2
U 1 1 5B539698
P 5100 2700
F 0 "J2" H 5179 2692 50  0000 L CNN
F 1 "GND" H 5179 2601 50  0000 L CNN
F 2 "Pin_Headers:Pin_Header_Straight_1x08_Pitch2.54mm" H 5100 2700 50  0001 C CNN
F 3 "~" H 5100 2700 50  0001 C CNN
	1    5100 2700
	1    0    0    -1  
$EndComp
$Comp
L Connector:Conn_01x08 J3
U 1 1 5B5396F6
P 6200 2700
F 0 "J3" H 6280 2692 50  0000 L CNN
F 1 "VCC" H 6280 2601 50  0000 L CNN
F 2 "Pin_Headers:Pin_Header_Straight_1x08_Pitch2.54mm" H 6200 2700 50  0001 C CNN
F 3 "~" H 6200 2700 50  0001 C CNN
	1    6200 2700
	1    0    0    -1  
$EndComp
$Comp
L power:GND #PWR0103
U 1 1 5B53972F
P 4800 3200
F 0 "#PWR0103" H 4800 2950 50  0001 C CNN
F 1 "GND" H 4805 3027 50  0000 C CNN
F 2 "" H 4800 3200 50  0001 C CNN
F 3 "" H 4800 3200 50  0001 C CNN
	1    4800 3200
	1    0    0    -1  
$EndComp
$Comp
L power:VCC #PWR0104
U 1 1 5B539740
P 5900 2300
F 0 "#PWR0104" H 5900 2150 50  0001 C CNN
F 1 "VCC" H 5917 2473 50  0000 C CNN
F 2 "" H 5900 2300 50  0001 C CNN
F 3 "" H 5900 2300 50  0001 C CNN
	1    5900 2300
	1    0    0    -1  
$EndComp
Wire Wire Line
	5900 2300 5900 2400
Wire Wire Line
	5900 3100 6000 3100
Wire Wire Line
	6000 3000 5900 3000
Connection ~ 5900 3000
Wire Wire Line
	5900 3000 5900 3100
Wire Wire Line
	6000 2900 5900 2900
Connection ~ 5900 2900
Wire Wire Line
	5900 2900 5900 3000
Wire Wire Line
	6000 2800 5900 2800
Connection ~ 5900 2800
Wire Wire Line
	5900 2800 5900 2900
Wire Wire Line
	6000 2700 5900 2700
Connection ~ 5900 2700
Wire Wire Line
	5900 2700 5900 2800
Wire Wire Line
	6000 2600 5900 2600
Connection ~ 5900 2600
Wire Wire Line
	5900 2600 5900 2700
Wire Wire Line
	6000 2500 5900 2500
Connection ~ 5900 2500
Wire Wire Line
	5900 2500 5900 2600
Wire Wire Line
	6000 2400 5900 2400
Connection ~ 5900 2400
Wire Wire Line
	5900 2400 5900 2500
Wire Wire Line
	4800 2400 4900 2400
Wire Wire Line
	4900 2500 4800 2500
Connection ~ 4800 2500
Wire Wire Line
	4800 2500 4800 2400
Wire Wire Line
	4900 2600 4800 2600
Connection ~ 4800 2600
Wire Wire Line
	4800 2600 4800 2500
Wire Wire Line
	4900 2700 4800 2700
Wire Wire Line
	4800 2600 4800 2700
Connection ~ 4800 2700
Wire Wire Line
	4800 2700 4800 2800
Wire Wire Line
	4900 2800 4800 2800
Connection ~ 4800 2800
Wire Wire Line
	4800 2800 4800 2900
Wire Wire Line
	4900 2900 4800 2900
Connection ~ 4800 2900
Wire Wire Line
	4800 2900 4800 3000
Wire Wire Line
	4900 3000 4800 3000
Connection ~ 4800 3000
Wire Wire Line
	4800 3000 4800 3100
Wire Wire Line
	4900 3100 4800 3100
Connection ~ 4800 3100
Wire Wire Line
	4800 3100 4800 3200
$Comp
L Connector:Conn_01x06 J4
U 1 1 5B53B79C
P 5100 3800
F 0 "J4" H 5179 3792 50  0000 L CNN
F 1 "SDA" H 5179 3701 50  0000 L CNN
F 2 "Pin_Headers:Pin_Header_Straight_1x06_Pitch2.54mm" H 5100 3800 50  0001 C CNN
F 3 "~" H 5100 3800 50  0001 C CNN
	1    5100 3800
	1    0    0    -1  
$EndComp
$Comp
L Connector:Conn_01x06 J5
U 1 1 5B53B7CC
P 6200 3800
F 0 "J5" H 6279 3792 50  0000 L CNN
F 1 "SCL" H 6279 3701 50  0000 L CNN
F 2 "Pin_Headers:Pin_Header_Straight_1x06_Pitch2.54mm" H 6200 3800 50  0001 C CNN
F 3 "~" H 6200 3800 50  0001 C CNN
	1    6200 3800
	1    0    0    -1  
$EndComp
Wire Wire Line
	4900 3600 4800 3600
Wire Wire Line
	4800 3600 4800 3700
Wire Wire Line
	4800 4100 4900 4100
Wire Wire Line
	4900 4000 4800 4000
Connection ~ 4800 4000
Wire Wire Line
	4800 4000 4800 4100
Wire Wire Line
	4900 3900 4800 3900
Connection ~ 4800 3900
Wire Wire Line
	4800 3900 4800 4000
Wire Wire Line
	4900 3800 4800 3800
Connection ~ 4800 3800
Wire Wire Line
	4800 3800 4800 3900
Wire Wire Line
	4900 3700 4800 3700
Connection ~ 4800 3700
Wire Wire Line
	4800 3700 4800 3800
Wire Wire Line
	6000 3600 5900 3600
Wire Wire Line
	5900 3600 5900 3700
Wire Wire Line
	5900 4100 6000 4100
Wire Wire Line
	6000 3700 5900 3700
Connection ~ 5900 3700
Wire Wire Line
	5900 3700 5900 3800
Wire Wire Line
	6000 3800 5900 3800
Connection ~ 5900 3800
Wire Wire Line
	5900 3800 5900 3900
Wire Wire Line
	6000 3900 5900 3900
Connection ~ 5900 3900
Wire Wire Line
	5900 3900 5900 4000
Wire Wire Line
	6000 4000 5900 4000
Connection ~ 5900 4000
Wire Wire Line
	5900 4000 5900 4100
Text GLabel 4800 3850 0    50   Input ~ 0
SDA
Text GLabel 5900 3850 0    50   Input ~ 0
SCL
$EndSCHEMATC
