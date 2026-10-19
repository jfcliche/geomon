# Development

## Toolchain

ESPHome is a Python package pinned in `pyproject.toml` / `uv.lock`, and every
command runs through `uv run`, so nothing is installed globally. The first
compile downloads the ESP-IDF toolchain (several hundred MB, a few minutes).
Build output goes to `.esphome/` (ignored by git).

| Target | Action |
| --- | --- |
| `make setup` | `uv sync`, and create `secrets.yaml` with a random API key if missing |
| `make update` | upgrade ESPHome (`uv lock --upgrade && uv sync`) |
| `make config` | validate `geomon.yaml` |
| `make compile` | build the firmware |
| `make syncserial` | build and flash over USB (`PORT=...`) |
| `make upload` | build and flash over Ethernet OTA (`HOST=...`) |
| `make logs` / `make logsserial` | stream logs over the network / USB |
| `make test` | compile `tests/register_sensors.yaml`, which covers every sensor value type |
| `make docs` | build the Sphinx site into `docs/_build/html` |
| `make clean` | remove build artifacts |

## Flashing

### First time: USB

`PORT` defaults to the stable by-id path of the development board:

```
/dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_A4:CB:8F:F8:9A:C8-if00
```

For another board, use `make syncserial PORT=/dev/ttyACM0` (or its own
by-id path). Close minicom or any other program holding the port first.

If the upload cannot connect, hold **BOOT**, press **RESET**, release **BOOT**,
then retry.

### Afterwards: Ethernet

```bash
make upload                      # uses geomon.local (mDNS, same subnet only)
make upload HOST=10.0.7.104      # or the IP address directly
```

From another subnet, forward TCP 3232 for OTA and TCP 6053 for logs and the
Home Assistant API. TCP 80 is the web page only. `geomon.local` will not
resolve across subnets.

## Logs

- `make logs` connects through the native API over Ethernet.
- `make logsserial` (or `minicom -D <PORT>`) reads the USB-Serial/JTAG port.
  The baud setting does not matter on this virtual USB port.

## Secrets

`secrets.yaml` is ignored by git. `secrets.example.yaml` is the template.
`tests/secrets.yaml` contains throwaway values for build tests only.

## Troubleshooting

- **No frames, many discarded bytes**: wrong baud rate or parity, or A/B
  swapped.
- **No frames, no discarded bytes**: nothing is being received. Check the RX
  wiring (GPIO17) and that the transceiver is powered. DE is GPIO16 and must
  not be left floating.
- **Many unmatched responses**: the sniffer started mid-conversation (this is
  normal for a moment) or an active read overlapped the other master. The
  active window is supposed to abort when that happens.
- **Active reads repeat every few seconds**: a discarded turnaround byte used
  to abort the window and re-arm it. A matched reply is no longer a collision.
- **`Active reads need rs485_txen_pin`**: DE is not configured, so probes
  never reach the bus. Set the pin or tie DE to ground and drop the active
  sensors.
- **Ethernet does not come up**: check the cable and the link LEDs, then
  `make logsserial`.

## Documentation site

Sphinx (Read the Docs theme) builds `docs/` and GitHub Actions publishes the
HTML to GitHub Pages on every push to `main`. Locally:

```bash
make docs
```

`docs/pyproject.toml` is a separate uv project. `make docs` installs Sphinx,
the Read the Docs theme, Hawkmoth, and the libclang 14 wheel, then builds the
HTML. Narrative pages are the Markdown files in `docs/`.
