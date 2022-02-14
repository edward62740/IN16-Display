# IN-16 Display

This is an Internet connected nixie tube display (4 x IN-16s) for displaying information (e.g CO2 levels, air quality...). It is designed to be used with [Wireless Mesh Network System](https://github.com/edward62740/Wireless-Mesh-Network-System) or other similar system with a database (InfluxDB, Firebase) to fetch and display information.

This project allows for seamless integration into existing smart home networks for displaying data which users may not want to access using an app/web application (i.e CO2 levels at a glance).

The provided software displays CO2 data on the device, and hosts a webserver to allow for adjustment of input source if necessary (i.e sensor 1/2).


![alt text](https://github.com/edward62740/IN16-Display/blob/master/Hardware/in16display.jpeg "IN-16 Display")


The hardware consists of an ESP32 with GPIO expander, HV power supply, darlington arrays, fan and 4x in-16 tubes.
