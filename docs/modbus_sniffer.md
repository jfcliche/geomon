# Modbus sniffer component

`components/modbus_sniffer` is a custom ESPHome external component that decodes
Modbus RTU traffic it receives on a UART, without ever transmitting.

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

If no new bytes arrive for `idle_timeout` (50 ms by default), any incomplete
leftovers are discarded.

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
  log_frames: true          # hex dump of each frame at DEBUG level
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

A sensor only updates when the master actually reads or writes that register.
If the master polls quickly, add a `throttle` filter so Home Assistant isn't
flooded.

## Mapping an unknown device

1. Keep `log_frames: true` and run `make logs`.
2. Note which slaves, function codes and register ranges the master polls.
3. Compare the values with what the device displays, then add sensors.
4. Set `log_frames: false` once you are done; logging every frame is costly at
   high poll rates.
