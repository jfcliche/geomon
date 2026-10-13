# geomon

ESPHome firmware for a **Waveshare ESP32-S3-POE-ETH** that passively listens to a
Modbus RTU bus on RS485 and publishes the values it sees to Home Assistant over
wired Ethernet (W5500, optionally powered by PoE). Wi-Fi is not used.

The device never transmits on the bus. It reconstructs request/response pairs
exchanged between the existing Modbus master and its slaves, and exposes the
registers you choose as Home Assistant sensors.

## Quick start

Requirements: [uv](https://docs.astral.sh/uv/) and `make`.

```bash
make setup        # create .venv with ESPHome, create secrets.yaml with a random API key
make compile      # build the firmware
make syncserial   # first flash, over the USB-C port
make synceth      # later updates, over Ethernet (OTA)
make logs         # stream logs over the network
```

Edit `ota_password` in `secrets.yaml` before the first flash. Run `make` with no
arguments for the full list of targets.

The device appears in Home Assistant as **Geomon** (ESPHome integration,
auto-discovered). Use the `api_encryption_key` from `secrets.yaml` when adding it.

## Configuration

| What | Where |
| --- | --- |
| Bus speed, parity, stop bits, RS485 pins | `substitutions` in [`geomon.yaml`](geomon.yaml) |
| Registers published to Home Assistant | [`common/modbus_sniffer.yaml`](common/modbus_sniffer.yaml) |
| Ethernet (DHCP or static IP) | [`common/board_esp32s3_poe_eth.yaml`](common/board_esp32s3_poe_eth.yaml) |
| API, OTA, logger, diagnostics | [`common/base.yaml`](common/base.yaml) |

The bus defaults to 115200 baud 8N1, which is a guess. If the `Modbus Frames`
counter stays at 0 while `Modbus Discarded Bytes` keeps climbing, the speed or
parity is wrong.

## Repository layout

```
geomon.yaml              main device configuration
common/                  configuration packages included by geomon.yaml
components/modbus_sniffer/  custom ESPHome component (passive Modbus RTU decoder)
tests/                   build test exercising every sensor value type
docs/                    hardware, sniffer and development notes
pyproject.toml, uv.lock  pinned ESPHome version, managed by uv
Makefile                 build / flash / log shortcuts
```

## Documentation

- [docs/hardware.md](docs/hardware.md): board, pinout and RS485 wiring
- [docs/modbus_sniffer.md](docs/modbus_sniffer.md): how the sniffer works and how to add registers
- [docs/development.md](docs/development.md): toolchain, flashing and troubleshooting
