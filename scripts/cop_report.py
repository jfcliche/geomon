#!/usr/bin/env python3
"""Parse an RP5CLI meter export and report ground-source heat pump COP.

The export is the annotated CSV the HTTP endpoint will return: one row per
submeter, FM1 (Allengra ALSONIC ground loop) and PM2 (electrical), sharing a
timestamp. parse() turns that text into namespaces. Pass the response body to
parse() once the endpoint exists.

See docs/heat_pump_cop.md for the field meanings and the COP derivation.
"""

from __future__ import annotations

import argparse
from datetime import datetime, timezone
from pathlib import Path
from types import SimpleNamespace

# Meter body (TINT) is on the pipe leaving the heat pump toward the ground.
# Remote probe (TREM) is on the return. TDIF = TREM - TINT, so a positive
# TDIF means the ground warmed the fluid. Set to -1 if the probes are swapped.
GROUND_GAIN_SIGN = 1

# ACTP is negative while consumption accumulates in the export register EACE.
# Flip the sign so electrical input is positive watts. Set to +1 if a
# compressor start drives ACTP positive instead.
ELECTRICAL_SIGN = -1

# Standing delta-T and control-circuit watts are not a COP.
MIN_FLOW_LPH = 100.0
MIN_ELEC_W = 200.0

# kWh per litre per kelvin for water. Ethanol scales this by ethanol_cf().
WATER_KWH_PER_L_K = 0.001163

# Charged ethanol fraction, percent by volume. The ALSONIC GLYC register is a
# glycol model and is not this number. None leaves the heat-capacity factor unset.
ETHANOL_VOLUME_PERCENT = None

# Volumetric heat capacity relative to water at 20°C. See docs/heat_pump_cop.md.
_ETHANOL_CF_BY_VOLUME_PERCENT = (
    (0.0, 1.000),
    (12.4, 1.009),
    (24.6, 1.002),
    (30.5, 0.984),
    (36.3, 0.961),
    (41.9, 0.931),
    (47.4, 0.900),
)

FM1_FIELDS = (
    "ABUD", "ABUS", "FLOW", "FRPT", "GLYC", "POTF", "POTI",
    "POWR", "PRES", "TDIF", "TENE", "TINT", "TREM", "TWVO",
)
PM2_FIELDS = (
    "ACTP", "APPP", "CURR", "EACE", "EREE", "FREQ",
    "IACE", "IREE", "PFAC", "REAP", "TACE", "VOLT",
)

SAMPLE_PATH = Path(__file__).with_name("sample_rp5cli.csv")


def ethanol_cf(volume_percent):
    """Volumetric heat capacity of aqueous ethanol at 20°C, relative to water."""
    if volume_percent is None:
        return None
    table = _ETHANOL_CF_BY_VOLUME_PERCENT
    if volume_percent <= table[0][0]:
        return table[0][1]
    if volume_percent >= table[-1][0]:
        return table[-1][1]
    for (x0, y0), (x1, y1) in zip(table, table[1:]):
        if volume_percent <= x1:
            span = (volume_percent - x0) / (x1 - x0)
            return y0 + span * (y1 - y0)
    return table[-1][1]


def parse(text):
    """Return a namespace of paired samples, each with .fm1, .pm2 and .cop."""
    rows = _rows(text)
    paired = _pair(rows)
    samples = [sample for sample in (_sample(fm1, pm2) for fm1, pm2 in paired) if sample]
    if not samples:
        raise ValueError("export contains no FM1 or PM2 rows")
    device = samples[0].device
    return SimpleNamespace(
        device=device,
        samples=samples,
        latest=samples[-1],
        period=_period(samples),
    )


def report(data):
    """Text summary of the measurements that go into COP."""
    start = _clock(data.samples[0].time)
    end = _clock(data.samples[-1].time)
    lines = [
        f"{data.device}   {len(data.samples)} paired samples   {start} – {end}",
        "",
        _sample_block(data.latest),
        "",
        _period_line(data.period),
        "",
        f"{'time':<10} {'P_W':>7} {'L/h':>7} {'body':>7} {'remote':>7} {'dT':>7} {'COP':>7}",
    ]
    for sample in data.samples:
        fm1, pm2, cop = sample.fm1, sample.pm2, sample.cop
        cop_text = "—" if cop.value is None else f"{cop.value:.2f}"
        lines.append(
            f"{_clock(sample.time):<10} "
            f"{_num(cop.p_w, 0):>7} {_num(_get(fm1, 'FLOW'), 0):>7} "
            f"{_num(_get(fm1, 'TINT'), 2):>7} {_num(_get(fm1, 'TREM'), 2):>7} "
            f"{_num(_get(fm1, 'TDIF'), 2):>7} {cop_text:>7}"
        )
    return "\n".join(lines)


def _sample(fm1_row, pm2_row):
    row = fm1_row or pm2_row
    if row is None:
        return None
    fm1 = _meter(fm1_row, FM1_FIELDS)
    pm2 = _meter(pm2_row, PM2_FIELDS)
    return SimpleNamespace(
        device=row["deviceName"],
        time=_parse_time(row["time"]),
        fm1=fm1,
        pm2=pm2,
        cop=_cop(fm1, pm2),
    )


def _meter(row, fields):
    values = {name: None for name in fields}
    if row is not None:
        for name in fields:
            values[name] = row.get(name)
    return SimpleNamespace(**values)


def _cop(fm1, pm2):
    flow = fm1.FLOW
    delta_t = fm1.TDIF
    cf = ethanol_cf(ETHANOL_VOLUME_PERCENT)
    p_w = None if pm2.ACTP is None else ELECTRICAL_SIGN * pm2.ACTP
    q_kw = None
    if flow is not None and delta_t is not None and cf is not None:
        q_kw = GROUND_GAIN_SIGN * flow * delta_t * WATER_KWH_PER_L_K * cf
        if abs(q_kw) < 1e-9:
            q_kw = 0.0

    mode = "off"
    value = None
    reason = _idle_reason(flow, p_w)
    if reason is None and cf is None:
        reason = "ethanol volume percent is not set"
    elif reason is None:
        p_kw = p_w / 1000.0
        if q_kw > 0:
            mode = "heating"
            value = (q_kw + p_kw) / p_kw
        elif q_kw < 0:
            mode = "cooling"
            value = (-q_kw - p_kw) / p_kw
        else:
            reason = "ground loop delta-T is 0"
    return SimpleNamespace(
        mode=mode,
        value=value,
        q_ground_kw=q_kw,
        q_meter_kw=fm1.POWR,
        p_w=p_w,
        ethanol_volume_percent=ETHANOL_VOLUME_PERCENT,
        fluid_cf=cf,
        reason=reason,
    )


def _idle_reason(flow, p_w):
    flow_ok = flow is not None and abs(flow) >= MIN_FLOW_LPH
    power_ok = p_w is not None and p_w >= MIN_ELEC_W
    if flow_ok and power_ok:
        return None
    missing = []
    if flow is None or p_w is None:
        if flow is None:
            missing.append("flow meter")
        if p_w is None:
            missing.append("power meter")
        return "missing " + " and ".join(missing)
    if not flow_ok and not power_ok:
        return "loop is stopped and compressor is off"
    if not flow_ok:
        return "loop is stopped"
    return "compressor is off"


def _period(samples):
    first, last = samples[0], samples[-1]
    heat = _delta(first.fm1.TENE, last.fm1.TENE)
    electric = _delta(first.pm2.EACE, last.pm2.EACE)
    cop = None
    if heat is None or electric is None:
        reason = "heating or electric energy total is missing"
    elif heat <= 0 or electric <= 0:
        reason = "heating and electric energy totals did not both increase"
    else:
        cop = (heat + electric) / electric
        reason = None
    return SimpleNamespace(heat_kwh=heat, electric_kwh=electric, value=cop, reason=reason)


def _delta(start, end):
    if start is None or end is None:
        return None
    return end - start


def _sample_block(sample):
    fm1, pm2, cop = sample.fm1, sample.pm2, sample.cop
    if cop.value is None:
        head = f"Latest {_clock(sample.time)}   {cop.mode}   COP not computed ({cop.reason})"
    else:
        head = f"Latest {_clock(sample.time)}   {cop.mode}   COP {cop.value:.2f}"
    lines = [
        head,
        "  Electric     "
        f"{_num(cop.p_w, 0)} W in (ACTP {_num(pm2.ACTP, 0)} W)   "
        f"{_num(pm2.VOLT, 1)} V   {_num(pm2.CURR, 3)} A   "
        f"PF {_num(pm2.PFAC, 2)}   {_num(pm2.FREQ, 2)} Hz",
        "  Ground loop  "
        f"{_num(fm1.FLOW, 0)} L/h   body {_num(fm1.TINT, 2)}°C   "
        f"remote {_num(fm1.TREM, 2)}°C   ΔT {_num(fm1.TDIF, 2)} K   "
        f"{_num(fm1.PRES, 3)} bar",
        _fluid_line(sample),
        "  Heat         "
        f"calculated {_num(cop.q_ground_kw, 3)} kW   "
        f"meter {_num(cop.q_meter_kw, 3)} kW",
        "  Totals       "
        f"heat {_num(fm1.TENE, 2)} kWh   electric {_num(pm2.EACE, 1)} kWh   "
        f"volume {_num(fm1.TWVO, 0)} L",
    ]
    return "\n".join(lines)


def _fluid_line(sample):
    cop, fm1 = sample.cop, sample.fm1
    if cop.ethanol_volume_percent is None:
        charged = "ethanol volume percent not set (ETHANOL_VOLUME_PERCENT)"
        factor = "factor —"
    else:
        charged = f"ethanol {cop.ethanol_volume_percent:g} vol%"
        factor = f"factor {cop.fluid_cf:.3f}"
    return (
        f"  Fluid        {charged}   {factor}\n"
        f"               meter GLYC {_num(fm1.GLYC, 1)}%   "
        f"freeze {_num(fm1.FRPT, 2)}°C, from the meter's glycol model"
    )


def _period_line(period):
    if period.value is None:
        moved = (
            f"heat {_signed(period.heat_kwh)} kWh, "
            f"electric {_signed(period.electric_kwh)} kWh"
        )
        return f"Window COP not computed ({period.reason}; {moved})"
    return (
        f"Window COP {period.value:.2f}   "
        f"heat {period.heat_kwh:.2f} kWh   electric {period.electric_kwh:.2f} kWh"
    )


def _rows(text):
    header = None
    rows = []
    for line in text.splitlines():
        if not line or line.startswith("#"):
            continue
        parts = line.split("\t")
        if header is None:
            if "subMeter" not in parts:
                continue
            header = parts
            continue
        if len(parts) == len(header) + 1 and parts[0] == "":
            parts = parts[1:]
        if len(parts) != len(header):
            raise ValueError(
                f"row has {len(parts)} columns, header has {len(header)}"
            )
        row = {}
        for name, raw in zip(header, parts):
            if not name or raw == "":
                continue
            row[name] = _number(raw)
        if "subMeter" in row and "time" in row:
            rows.append(row)
    if header is None:
        raise ValueError("export has no column header")
    return rows


def _pair(rows):
    order = []
    by_time = {}
    for row in rows:
        stamp = row["time"]
        if stamp not in by_time:
            by_time[stamp] = {}
            order.append(stamp)
        by_time[stamp][row["subMeter"]] = row
    return [(by_time[stamp].get("FM1"), by_time[stamp].get("PM2")) for stamp in order]


def _number(raw):
    try:
        return float(raw)
    except ValueError:
        return raw


def _parse_time(value):
    if isinstance(value, datetime):
        return value
    return datetime.fromisoformat(value.replace("Z", "+00:00")).astimezone(timezone.utc)


def _clock(value):
    return value.strftime("%H:%M:%S")


def _get(meter, name):
    return getattr(meter, name, None)


def _num(value, digits):
    if value is None:
        return "—"
    if digits == 0:
        return f"{value:.0f}"
    return f"{value:.{digits}f}"


def _signed(value):
    if value is None:
        return "—"
    return f"{value:+.2f}"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "path",
        nargs="?",
        type=Path,
        default=SAMPLE_PATH,
        help="annotated CSV export (default: the saved RP5CLI sample)",
    )
    args = parser.parse_args()
    print(report(parse(args.path.read_text())))


if __name__ == "__main__":
    main()
