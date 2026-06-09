#!/usr/bin/env python3
"""Simulate a closed control loop and record it to a ``.tha`` file.

A *closed loop* is the defining Thalamus use case: a value is measured, a rule
decides on an action, and that action feeds back and changes the value.  In a live
pipeline you'd wire it as ``sensor -> ALGEBRA/TOGGLE (decide) -> output``, with the
output driving hardware that affects the sensor.

This script simulates that loop offline so you can run and inspect it with no
hardware: a process variable drifts upward; a hysteresis (bang-bang) controller
switches a ``control`` output on when it rises above an upper threshold and off when
it falls below a lower one; and the control output bends the process variable back
down -- closing the loop.  Both the process variable and the control signal are
written to a ``.tha`` capture so you can see the loop working.

Usage::

    python examples/closed_loop_demo.py -o loop.tha
    python examples/analyze_recording.py loop.tha -n loop -o loop.png   # visualize it
"""
import argparse
import pathlib

from thalamus.thalamus_pb2 import StorageRecord, AnalogResponse, Span, Text, Metadata, Pair
from thalamus.record_writer import write_record

SAMPLE_RATE = 1000
POLL_MS = 16
DURATION_S = 5
INTERVAL_NS = int(1e9 / SAMPLE_RATE)
SAMPLES_PER_POLL = SAMPLE_RATE * POLL_MS // 1000

DRIFT = 0.6 / SAMPLE_RATE     # upward drift per sample
ACTUATION = 2.2 / SAMPLE_RATE  # downward pull per sample while control is on
HIGH, LOW = 1.0, 0.0           # hysteresis band


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("-o", "--output", type=pathlib.Path, default=pathlib.Path("loop.tha"))
    args = parser.parse_args()

    pv = 0.0          # process variable (the "sensor")
    control = 0.0     # controller output (0 or 1)
    toggles = 0
    pv_lo, pv_hi = float("inf"), float("-inf")
    total = SAMPLE_RATE * DURATION_S

    with args.output.open("wb") as stream:
        write_record(stream, StorageRecord(
            node="storage", time=0,
            metadata=Metadata(keyvalues=[Pair(key="Rec", integral=1)])))

        produced, t_ns = 0, 0
        while produced < total:
            count = min(SAMPLES_PER_POLL, total - produced)
            pv_samples, ctrl_samples = [], []
            for _ in range(count):
                # --- the loop: measure -> decide -> actuate -> feed back ---
                pv += DRIFT - (ACTUATION if control else 0.0)
                new_control = 1.0 if pv > HIGH else (0.0 if pv < LOW else control)
                if new_control != control:
                    toggles += 1
                control = new_control
                pv_lo, pv_hi = min(pv_lo, pv), max(pv_hi, pv)
                pv_samples.append(pv)
                ctrl_samples.append(control)
            produced += count
            t_ns += count * INTERVAL_NS

            write_record(stream, StorageRecord(
                node="loop", time=t_ns,
                analog=AnalogResponse(
                    data=pv_samples + ctrl_samples,
                    spans=[Span(begin=0, end=count, name="process"),
                           Span(begin=count, end=2 * count, name="control")],
                    sample_intervals=[INTERVAL_NS, INTERVAL_NS], time=t_ns)))

    print(f"Wrote {args.output}")
    print(f"Controller switched {toggles} times; the loop held the process variable "
          f"in [{pv_lo:.2f}, {pv_hi:.2f}] around the [{LOW}, {HIGH}] band.")


if __name__ == "__main__":
    main()
