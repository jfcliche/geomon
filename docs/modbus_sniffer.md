# Modbus sniffer component

`components/modbus_sniffer` is a custom ESPHome external component. It decodes
Modbus RTU traffic on a UART and can also read registers the other master
skips, during a short quiet window.

## How frames are found

Modbus RTU separates frames with a silent gap of 3.5 character times (about
0.3 ms at 115200 baud). The ESPHome main loop runs roughly every 16 ms and
the UART driver buffers bytes in between, so that gap cannot be observed.

Instead, the sniffer delimits frames by their structure:

1. Look at the slave address and function code at the front of the buffer.
2. Work out the possible frame lengths for that function code, for both a
   request and a response. For example, function 0x03 is either an 8-byte
   request or a `5 + byte_count` response.
3. Try the most likely candidate first (a response if the last request came
   from the same slave with the same function), and accept the first length
   whose CRC16 is valid.
4. If no candidate matches, drop one byte (counted in `discarded_bytes`) and
   try again. This resynchronises after noise or a start in the middle of a
   frame.

If no new bytes arrive for `idle_timeout` (50 ms by default), the burst is
parsed. Bytes that still do not form a frame are discarded.

## Request/response pairing

The last request seen is remembered (slave, function, start register, count).
When the matching response arrives, its data words are mapped back to absolute
register addresses and handed to the configured sensors.

| Function | Decoded from | Register type |
| --- | --- | --- |
| 0x03 Read Holding Registers | response | holding |
| 0x04 Read Input Registers | response | input |
| 0x06 Write Single Register | request | holding |
| 0x10 Write Multiple Registers | request | holding |
| 0x17 Read/Write Multiple Registers | both | holding |

Coil and discrete-input functions (0x01, 0x02, 0x05, 0x0F) are framed and
counted, but their values are not published.

## Configuration

```yaml
modbus_sniffer:
  id: sniffer
  uart_id: rs485
  flow_control_pin: GPIO16  # DE. Omit, or pass 0, to stay receive-only
  active_delay: 5s          # silence after the other master before we read
  active_window: 30s        # longest we hold the bus after that
  log_frames: true          # hex dump of each idle burst, plus decoded frames
  idle_timeout: 50ms
  update_interval: 30s      # how often the counters below are published
  frames:                   # all optional
    name: Modbus Frames
  discarded_bytes:
    name: Modbus Discarded Bytes
  unmatched_responses:
    name: Modbus Unmatched Responses
  exceptions:
    name: Modbus Exceptions
```

`flow_control_pin` selects ESP-IDF RS485 half-duplex. The pin goes high only
while a byte is shifted out. Releasing the driver often leaves a single `0xFF`
on RX ahead of the slave's reply. That byte is discarded and is not treated as
another master when the reply itself matches the request we sent.

## Active reads

Sensors with `active: true` are collected at startup and merged into the
fewest reads that still cover them. Two sensors on the same slave and register
type are one request when at most one unused register sits between them.
Holding registers use function 0x03. Input registers use function 0x04.

The window opens once the bus has been silent for `active_delay`. It closes
when every range has been read, when `active_window` expires, or when a frame
that is not our own reply arrives. Closing because the other master resumed
waits for another full silence before trying again. A matched reply is kept
even if the turnaround `0xFF` was discarded in front of it.

### Register sensors

```yaml
sensor:
  - platform: modbus_sniffer
    name: Supply Temperature
    slave: 1                 # 1-247
    register_type: holding   # holding (default) or input
    address: 0x0010          # protocol address (0-based), not the 4xxxx number
    value_type: S16          # default U16
    unit_of_measurement: "°C"
    filters:
      - multiply: 0.1
      - throttle: 10s
```

Value types:

| Type | Registers | Meaning |
| --- | --- | --- |
| `U16` / `S16` | 1 | unsigned / signed 16-bit |
| `U32` / `S32` | 2 | 32-bit, high word first |
| `U32_R` / `S32_R` | 2 | 32-bit, low word first |
| `FLOAT32` | 2 | IEEE 754 float, high word first |
| `FLOAT32_R` | 2 | IEEE 754 float, low word first |

A 32-bit value is published only when both of its registers appear in the
same frame.

A sensor without `active: true` updates only when the other master reads or
writes that register. `active: true` makes geomon read it itself. Those reads
start `active_delay` (5 s) after the other master has gone silent, and they
stop after `active_window` (30 s), which leaves the bus free before a master
that polls once a minute. Adjacent active registers on the same slave are one
request. A reply from the other master aborts the rest of the window.

If the master polls quickly, add a `throttle` filter so Home Assistant isn't
flooded.

## Mapping an unknown device

1. Keep `log_frames: true` and run `make logs`.
2. Note which slaves, function codes and register ranges the master polls.
3. Compare the values with what the device displays, then add sensors.
4. Set `log_frames: false` once you are done; logging every frame is costly at
   high poll rates.
