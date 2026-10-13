# Hardware

## Board: Waveshare ESP32-S3-POE-ETH

- ESP32-S3R8: dual-core, 8 MB octal PSRAM
- 16 MB flash (W25Q128)
- W5500 10/100 Ethernet over SPI, with optional 802.3af PoE module
- USB-C connected to the ESP32-S3 built-in USB-Serial/JTAG (flashing and logs)

### Pins used internally

| Function | GPIO |
| --- | --- |
| W5500 SCLK | 13 |
| W5500 MOSI | 11 |
| W5500 MISO | 12 |
| W5500 CS | 14 |
| W5500 INT | 10 |
| W5500 RST | 9 |
| TF card (unused) | 4-7 |

Source: [Waveshare ESP32-S3-ETH documentation](https://docs.waveshare.com/ESP32-S3-ETH/Development-Environment-Setup-Arduino)
and the [ESPHome Ethernet component](https://esphome.io/components/ethernet/).

## RS485 connection

| Signal | GPIO | Notes |
| --- | --- | --- |
| RXD (transceiver RO) | 21 | Data from the bus |
| TXD (transceiver DI) | 17 | Configured, but the firmware never writes to it |

The board has no RS485 transceiver of its own, so an external module is used.
Because the sniffer is purely passive, the transceiver must stay in receive
mode:

- **Auto-direction modules** (the common ones with only RXD/TXD/VCC/GND) switch to
  transmit only when TXD goes low. TXD idles high, so they stay in receive mode.
- **Modules with DE/RE pins** (MAX485 style): tie DE and /RE to GND so the
  driver is permanently disabled.

### Bus wiring

- Connect A to A and B to B on the existing bus. Some vendors swap the labels; if
  you get only discarded bytes at the right baud rate, swap A and B.
- Connect GND to the bus reference ground if one is available.
- **Do not add a termination resistor** on the sniffer if the bus is already
  terminated at both ends. Disable any 120 ohm jumper on the module.
- Keep the stub from the bus to the sniffer short.

## Power

Either PoE (802.3af, with the PoE module fitted) or USB-C. Connecting USB while
PoE powers the board is fine for flashing and logs.
