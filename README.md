# geomon

ESPHome firmware for a **Waveshare ESP32-S3-POE-ETH** that listens to a Modbus
RTU bus on RS485 and publishes the values to Home Assistant over wired Ethernet
(W5500, optionally powered by PoE). Wi-Fi is not used.

Most sensors are passive: the firmware reconstructs request/response pairs
between the existing Modbus master and its slaves, and never answers those
frames. Sensors marked `active: true` are read by geomon itself, starting 5 s
after the other master goes silent and stopping within 30 s so that master has
the bus back before its next sweep (about once a minute). The driver-enable
pin is raised only while those bytes are leaving.

The heat pump bus is 19200 baud, 8 data bits, no parity, 1 stop bit. Slave 1
is the Allengra flow meter. Slave 3 is the electrical meter.

## Quick start

Requirements: [uv](https://docs.astral.sh/uv/) and `make`.

```bash
make setup        # create .venv with ESPHome, create secrets.yaml with a random API key
make compile      # build the firmware
make syncserial   # first flash, over the USB-C port
make upload       # later updates, over Ethernet (OTA). HOST=name-or-ip
make logs         # stream logs over the network. HOST=name-or-ip
```

Edit `ota_password` in `secrets.yaml` before the first flash. Run `make` with no
arguments for the full list of targets.

The device appears in Home Assistant as **Geomon** (ESPHome integration).
mDNS (`geomon.local`) does not cross subnets, so from another subnet use the
IP address and forward TCP 6053 (API and logs), TCP 3232 (OTA) and, if you
want the web page, TCP 80.

## Configuration

| What | Where |
| --- | --- |
| Bus speed, parity, stop bits, RS485 pins | `substitutions` in [`geomon.yaml`](geomon.yaml) |
| Registers published to Home Assistant | [`common/modbus_sniffer.yaml`](common/modbus_sniffer.yaml) |
| Ethernet (DHCP or static IP) | [`common/board_esp32s3_poe_eth.yaml`](common/board_esp32s3_poe_eth.yaml) |
| API, OTA, logger, diagnostics | [`common/base.yaml`](common/base.yaml) |

`rs485_txen_pin: "0"` leaves the driver disabled (receive only). `GPIO16` is
the DE pin of the current module. If `Modbus Frames` stays at 0 while
`Modbus Discarded Bytes` climbs, the speed, parity, or A/B polarity is wrong.

## Repository layout

```
geomon.yaml                 main device configuration
common/                     configuration packages included by geomon.yaml
components/modbus_sniffer/  custom ESPHome component
tests/                      build test exercising every sensor value type
docs/                       Sphinx site (hardware, sniffer, COP, API)
pyproject.toml, uv.lock     pinned ESPHome version, managed by uv
Makefile                    build / flash / log shortcuts
```

## Documentation

The same pages are published to GitHub Pages from `main`.

- [docs/hardware.md](docs/hardware.md): board, pinout and RS485 wiring
- [docs/modbus_sniffer.md](docs/modbus_sniffer.md): framing, active reads, and how to add registers
- [docs/heat_pump_cop.md](docs/heat_pump_cop.md): meter registers, units, and COP
- [docs/development.md](docs/development.md): toolchain, flashing and troubleshooting
