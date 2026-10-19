# Ground-source heat pump COP

`scripts/cop_report.py` reads the annotated CSV from the RP5CLI meter export
and computes COP. The saved sample is `scripts/sample_rp5cli.csv`, one minute
of FM1 and PM2 rows from 2026-10-13 19:16:58Z to 19:35:46Z.

The file is an Influx-style annotated CSV. Lines starting with `#` are
metadata. The header row and every data row are tab-separated and begin with
an empty column. Each data row is one submeter at one timestamp. FM1 and PM2
that share `time` are one sample.

`deviceName` in the sample is `RP5CLI`.

## Bus

Geomon sniffs the RS485 link. The UART is RX GPIO17 and TX GPIO21, and driver enable is GPIO16. The heat pump master is the only writer of registers. Geomon transmits a read only after that master has been silent for 5 seconds, and it gives the bus back within 30 seconds. The master sweeps about once a minute.

| Setting | Value |
| --- | --- |
| Baud | 19200 |
| Data bits | 8 |
| Parity | none |
| Stop bits | 1 |

The master reads input registers only (function 0x04). No writes were seen. A full sweep takes about four seconds, one register group every 200 ms, and the sweep repeats about once a minute.

| Slave | Meter | CSV name |
| --- | --- | --- |
| 1 | Allengra ALSONIC on the ground loop | FM1 |
| 3 | Electrical meter at the heat pump | PM2 |

FM1 multi-register values are big-endian integers (high word first). PM2 values are IEEE 754 floats, also big-endian. A capture at 2026-10-15 22:00 local (America/Toronto), about 55 hours after the CSV sample, is listed beside each field below. Slave 1's powered-time counter had advanced 197403 seconds (54.8 hours), which lines up with that gap.

## FM1, Allengra ALSONIC ground loop

FM1 is the [ALSONIC brass DN15–DN50](https://allengra.eu/products/alsonic-brass-dn15-dn50-flow-sensor)
ultrasonic meter. Register addresses and units are from the
[Modbus specification](https://allengra.eu/products/documents/product_alsonic_brass_dn15_dn50_flow_sensor_docs/ALSONIC-Brass-DN15-DN50-Flowmeter-MODBUS-Specification.pdf)
(v1.31). The export already applies the register scale, so `TINT` is degrees,
not the raw 0.01°C integer.

`TINT` is the probe in the meter body. `TREM` is the remote probe on the cable.
In this installation the body is on the pipe leaving the heat pump toward the
ground, and the remote probe is on the return. `TDIF = TREM − TINT`.

Slave 1. Scales below are the v1.31 register table (Modbus specification, 2026-07-17). The “Flow = value × 1” line later in that document is the calibration look-up table at registers 1670–1679, not the live flow register. Registers 329–331 select the unit of the float copies at addresses 1000–1008; they do not rescale registers 0–14.

Temperatures, glycol, and freeze point are signed 16-bit, 0.01 per count. Pressure is signed 16-bit, 0.001 bar per count. `FLOW` is signed 32-bit, 0.1 L/h per count (the FAQ works the example: raw `0x1770` = 6000 = 600.0 L/h, and raw `0x719` = 181.7 L/h). `POWR` is signed 32-bit, 0.01 kW per count. The totals at 500–508 are signed 32-bit: volume and the two timers are 1 count per litre or second, and `TENE` is 0.01 kWh per count.

| Field | Register | Meaning | Unit | Sample at 19:16:58Z | Bus at 22:00 |
| --- | --- | --- | --- | --- | --- |
| `FLOW` | 0 | Volumetric flow. Positive is the normal direction through the body. | L/h | 0 | 18.0 |
| `TINT` | 2 | Temperature in the meter body | °C | 18.19 | 17.55 |
| `TREM` | 3 | Remote-probe temperature | °C | 15.94 | 12.82, then 12.89 |
| `TDIF` | 4 | `TREM − TINT` | °C | −2.24 | −4.79, then −4.65 |
| `PRES` | 5 | Pressure | bar | 1.842 | 1.812 |
| `POWR` | 6 | Thermal power computed by the meter | kW | 0 | −0.09, then −0.08 |
| `ABUD` | 10 | Air in the pipe: 0 none, 1 detected | | 0 | 0 |
| `ABUS` | 11 | Air amount: 0 none, 1 small, 2 medium, 3 high, 4 pipe full of air | | 0 | 0 |
| `GLYC` | 12 | Concentration from the meter's glycol model | % | 34.02 | 34.42, then 34.43 |
| `FRPT` | 14 | Freeze point from that same glycol model | °C | −17.64 | −18.03, then −18.05 |
| `TWVO` | 500 | Volume totalizer | L | 1,485,365 | 1,492,370 |
| `POTI` | 502 | Time the meter has been powered | s | 17,082,864 | 17,280,267 |
| `POTF` | 504 | Time powered while flow is non-zero | s | 6,054,826 | 6,101,007 |
| `TENE` | 506 | Heating energy total. Increases only while `POWR` is positive. | kWh | 2214.73 | 2225.36 |

Cooling energy is Allengra register 508, 0.01 kWh per count. It is not in this export. `TENE` stays still during cooling. The heat pump master does not read it, so geomon reads it actively, along with the other documented registers from 0 to 1008 that the master skips.

| Field | Register | Type | Meaning | Unit |
| --- | --- | --- | --- | --- |
| Bubble amount | 15 | input S16 | Air in the pipe, quantified | % ×0.01 |
| Sound-speed temperature | 17 | input S16 | Fast temperature from speed of sound. Water only, to 60°C | °C ×0.01 |
| Firmware version | 277 | input S32 | Meter firmware | |
| Cooling energy | 508 | input U32 | Adds while `POWR` is negative | kWh ×0.01 |
| Flow, temperatures, pressure (alt) | 1000–1008 | input float | Same quantities in the unit selected by registers 329–331 | |
| Device ID | 320 | holding U16 | Modbus address | |
| Baud | 321 | holding U32 | | bps |
| Parity | 324 | holding U16 | 0 none, 1 even, 2 odd | |
| Stop bit | 325 | holding U16 | 0 = 1 stop bit, 1 = 2 | |
| Glycol config | 327 | holding U16 | 0 off, 1 ethylene, 2 propylene | |
| Energy config | 328 | holding U16 | 0 off, 1–5 see the Allengra manual | |
| Flow unit | 329 | holding U16 | 0 L/h through 6 gal/min | |
| Temperature unit | 330 | holding U16 | 0 °C, 1 °F | |
| Pressure unit | 331 | holding U16 | 0 mbar, 1 bar, 2 psi | |
| Calibration enable | 338 | holding U16 | 0 off, 1 third-party table on | |

Reserved addresses and the unimplemented warning at address 9 are not published. The calibration tables at 1540 and above are not read.

`POWR` is the meter's own flow × temperature-difference result. COP below
uses `FLOW`, `TDIF`, and the ethanol factor instead, and keeps `POWR` so the
two can be compared on a real run. `POWR / (FLOW × |TDIF| × 0.001163)` near 1
means the meter is still using water. Near the ethanol factor below, it is
already corrected.

Register 327 selects ethylene or propylene glycol. It has no ethanol setting,
so `GLYC` and `FRPT` are not the ethanol fraction or its freeze point. The
loop fluid is ethanol. Set `ETHANOL_VOLUME_PERCENT` in `scripts/cop_report.py`
to the charged mixture, percent by volume. The meter was built to be
calibrated for one liquid; flow accuracy on ethanol depends on that
calibration.

## PM2, electrical input

PM2 is the heat pump's electrical meter. These names are fixed by the values
in the sample: `241 V × 0.102 A ≈ 24 VA`, and `√(12² + 21²) ≈ 24 VA`.

Slave 3. Each value is one big-endian float over two input registers. At 22:00, √(12² + 22²) = 25 VA and −12 / 25 = −0.48, which matches `APPP` and `PFAC` on the wire. `TACE` equals `EACE` because `IACE` is still 0. Register 356 is not polled.

| Field | Register | Meaning | Unit | Sample at 19:16:58Z | Bus at 22:00 |
| --- | --- | --- | --- | --- | --- |
| `VOLT` | 2 | Voltage | V | 240.94 | 245.02, then 244.47 |
| `FREQ` | 4 | Line frequency | Hz | 59.97 | 60.08, then 60.01 |
| `CURR` | 6 | Current | A | 0.102 | 0.105, then 0.104 |
| `ACTP` | 8 | Active power, signed | W | −12 | −12 |
| `APPP` | 10 | Apparent power | VA | 24 | 25 |
| `REAP` | 12 | Reactive power | var | −20 | −22, then −21 |
| `PFAC` | 14 | Power factor, sign follows `ACTP` | | −0.51 | −0.486, then −0.490 |
| `IACE` | 352 | Import active energy | kWh | 0 | 0 |
| `IREE` | 354 | Import reactive energy | kvarh | 48.6 | 48.6 |
| `EACE` | 358 | Export active energy | kWh | 3149.1 | 3169.0 |
| `EREE` | 360 | Export reactive energy | kvarh | 1308.5 | 1319.2 |
| `TACE` | 362 | Total active energy, `EACE + IACE` | kWh | 3149.1 | 3169.0 |

`ACTP` is negative and the accumulated consumption sits in `EACE`, with
`IACE` at 0. The current sensor is reversed relative to the meter's import
register. Electrical input for COP is `−ACTP` watts. Confirm on the next
compressor start: input should move to a large negative `ACTP`. If it goes
positive, drop the minus.

Twelve watts at a power factor near −0.5 is the control circuit. The
compressor is off.

## Columns with no values

`AETY`, `BRAT`, `DVID`, `ENMC`, `FVER`, `GLDC`, `HVER`, `MBID`, `MCOD`,
`NPAS`, `PARI`, `REFC`, `REFV`, `SBIT`, `SNUM`, `SVER`, and `TCOD` are in the
header and blank in this export. COP does not use them.

## COP

Pair FM1 and PM2 on `time`. Heat gained by the fluid from the ground, in kW:

```text
Q = GROUND_GAIN_SIGN × FLOW_L/h × TDIF_K × 0.001163 × CF
```

`0.001163` is the volumetric heat capacity of water, in kWh per litre per
kelvin. `CF` scales it for ethanol at 20°C. It is interpolated from this
table, built from aqueous-ethanol specific heat and density at 20°C
([ProcessConvert](https://www.processconvert.com/substances/ethanol) specific
heats, density converted to volume percent with pure ethanol at 789.3 kg/m³):

| Ethanol, vol% | CF |
| --- | --- |
| 0 | 1.000 |
| 12 | 1.009 |
| 25 | 1.002 |
| 30 | 0.985 |
| 36 | 0.961 |
| 42 | 0.931 |
| 47 | 0.900 |

From 0 to about 25 vol% the factor stays within 1% of water, because the
higher specific heat offsets the lower density. `GLYC` is not an input to
this table. `GROUND_GAIN_SIGN` is +1 for the probe placement above and −1 if
the body and the remote probe are swapped. Positive `Q` means the ground
heated the fluid.

Electrical input is `P = −ACTP / 1000` kilowatts.

The script computes COP only when the loop is moving and the compressor is
drawing power: `abs(FLOW) ≥ 100` L/h and `P ≥ 200` W. A standing temperature
difference at zero flow is not heat transfer.

The Home Assistant sensor uses a simpler gate. It publishes only while
`CURR > 1` A, which is the heat pump running rather than the control circuit
(about 0.1 A at idle). It uses `CF = 1` until the charged ethanol fraction is
known (`ethanol_cf` in `geomon.yaml`). `GLYC` is not that fraction. The sensor
is unknown while the pump is off, while `P` is below 50 W, or while `Q` is 0.

```text
Heating (Q > 0):   COP = (Q + P) / P
Cooling (Q < 0):   COP = (−Q − P) / P
```

Heating COP is heat delivered indoors divided by electricity. The ground loop
measures the heat taken from the ground, so the indoor heat is that quantity
plus the electrical input. Cooling COP is heat removed indoors. The loop
measures heat dumped into the ground, so the indoor heat is that quantity
minus the electrical input.

The same ratio over a whole run, from the counters, is less noisy than one
minute. It follows the meter's own thermal total, so it does not apply the
ethanol factor above. It only covers heating, because cooling kWh is not in
the export:

```text
COP_heating = (ΔTENE + ΔEACE) / ΔEACE
```

Use it when both counters increased. For cooling, sum `Q` and `P` from the
one-minute samples.

## This sample

Every paired minute in `scripts/sample_rp5cli.csv` is idle. Flow is 0 L/h,
`POWR` is 0, the volume totalizer stays at 1,485,365 L, and `POTF` does not
advance. Electrical input is 12 W. The body is about 2°C warmer than the
remote probe, and that gap shrinks from 2.24 K to 1.59 K as the stopped loop
equalizes. COP is not computed. `TENE` stays 2214.73 kWh and `EACE` stays
3149.1 kWh, so the window ratio is not available either.
