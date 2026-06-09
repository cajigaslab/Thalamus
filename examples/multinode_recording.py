#!/usr/bin/env python3
"""Record several nodes into one capture file, then export each separately.

A real Thalamus experiment records many nodes into a single ``.tha`` file -- for
example an eye tracker, an EMG sensor, and a trigger channel all at once.  Each
node's data is tagged with its node name, so analysis tools select a node by name.

This example writes a recording with two analog nodes -- ``eye`` (channels ``x`` and
``y``) and ``emg`` (channel ``ch0``) -- and prints the per-node export commands.

Usage::

    python examples/multinode_recording.py -o session.tha

Then export each node independently::

    python -m thalamus.dataframe -n eye -i session.tha -f csv -o eye.csv
    python -m thalamus.dataframe -n emg -i session.tha -f csv -o emg.csv
"""
import argparse
import math
import pathlib

from thalamus.thalamus_pb2 import StorageRecord, AnalogResponse, Span, Metadata, Pair
from thalamus.record_writer import write_record

SAMPLE_RATE = 1000
POLL_MS = 16
DURATION_S = 5
INTERVAL_NS = int(1e9 / SAMPLE_RATE)
SAMPLES_PER_POLL = SAMPLE_RATE * POLL_MS // 1000


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("-o", "--output", type=pathlib.Path, default=pathlib.Path("session.tha"))
    args = parser.parse_args()

    total = SAMPLE_RATE * DURATION_S
    with args.output.open("wb") as stream:
        write_record(stream, StorageRecord(
            node="storage", time=0,
            metadata=Metadata(keyvalues=[Pair(key="Rec", integral=1)])))

        produced, t_ns = 0, 0
        while produced < total:
            count = min(SAMPLES_PER_POLL, total - produced)
            t_ns += count * INTERVAL_NS

            eye_x, eye_y, emg = [], [], []
            for i in range(count):
                t = (produced + i) / SAMPLE_RATE
                eye_x.append(math.sin(2 * math.pi * 0.5 * t))
                eye_y.append(math.cos(2 * math.pi * 0.5 * t))
                emg.append(abs(math.sin(2 * math.pi * 3.0 * t)))
            produced += count

            write_record(stream, StorageRecord(
                node="eye", time=t_ns,
                analog=AnalogResponse(
                    data=eye_x + eye_y,
                    spans=[Span(begin=0, end=count, name="x"),
                           Span(begin=count, end=2 * count, name="y")],
                    sample_intervals=[INTERVAL_NS, INTERVAL_NS], time=t_ns)))
            write_record(stream, StorageRecord(
                node="emg", time=t_ns,
                analog=AnalogResponse(
                    data=emg,
                    spans=[Span(begin=0, end=count, name="ch0")],
                    sample_intervals=[INTERVAL_NS], time=t_ns)))

    print(f"Wrote {args.output} with nodes: eye (x, y), emg (ch0)")


if __name__ == "__main__":
    main()
