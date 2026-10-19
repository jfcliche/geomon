geomon
======

ESPHome firmware for a Waveshare ESP32-S3-POE-ETH. It listens to a Modbus RTU
bus on RS485 and publishes the values to Home Assistant over wired Ethernet.

Sensors without ``active`` follow the heat pump master and are never answered.
Sensors marked ``active: true`` are read by this device after that master has
been silent for 5 seconds, and the bus is released within 30 seconds. The
driver-enable pin is high only while those bytes are leaving.

The heat pump bus is 19200 baud, 8 data bits, no parity, one stop bit.

.. toctree::
   :maxdepth: 2
   :caption: Contents

   hardware
   modbus_sniffer
   heat_pump_cop
   development
   api
