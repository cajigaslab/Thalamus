#!/usr/bin/env python3
"""Record behavioral event markers (text records) in a Thalamus capture file.

Many experiments interleave a continuous analog signal with discrete *event
markers* -- short text labels such as ``trial_start`` or ``reward``.  Thalamus
stores these as ``text`` records in the same ``.tha`` file as the analog data, so
they share a common timeline.

This example writes a recording containing five event markers on an ``events`` node
plus a continuous ``reward`` analog channel on a ``daq`` node.  Afterwards you can
export the markers with::

    python -m thalamus.dataframe -n events -t text -i events.tha -f csv

Usage::

    python examples/event_markers.py -o events.tha
"""
import argparse
import math
import pathlib

from thalamus.thalamus_pb2 import StorageRecord, AnalogResponse, Span, Text, Metadata, Pair
from thalamus.record_writer import write_record

MARKERS = ["trial_start", "cue_on", "cue_off", "reward", "trial_end"]
SAMPLE_RATE = 1000
INTERVAL_NS = int(1e9 / SAMPLE_RATE)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("-o", "--output", type=pathlib.Path, default=pathlib.Path("events.tha"))
    args = parser.parse_args()

    with args.output.open("wb") as stream:
        write_record(stream, StorageRecord(
            node="task", time=0,
            metadata=Metadata(keyvalues=[Pair(key="Rec", integral=1)])))

        # One marker every 200 ms, with a continuous analog channel in between.
        for i, label in enumerate(MARKERS):
            marker_ns = (i + 1) * 200_000_000

            # Emit ~200 ms of analog data leading up to this marker.
            data = [math.sin(2 * math.pi * 5.0 * (marker_ns - INTERVAL_NS * j) / 1e9)
                    for j in range(200)][::-1]
            write_record(stream, StorageRecord(
                node="daq", time=marker_ns,
                analog=AnalogResponse(
                    data=data,
                    spans=[Span(begin=0, end=len(data), name="reward")],
                    sample_intervals=[INTERVAL_NS], time=marker_ns)))

            # Emit the event marker itself.
            write_record(stream, StorageRecord(
                node="events", time=marker_ns,
                text=Text(text=label, time=marker_ns)))

    print(f"Wrote {args.output} with {len(MARKERS)} event markers")


if __name__ == "__main__":
    main()
