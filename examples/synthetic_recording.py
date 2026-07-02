#!/usr/bin/env python3
"""Generate a synthetic Thalamus capture (``.tha``) file with no hardware.

This writes a recording that looks just like one produced by a STORAGE2 node:
a leading metadata record followed by a series of analog records.  Two channels
are generated -- a 2 Hz sine wave and a 1 Hz square pulse -- so you can exercise
the rest of the Thalamus tooling (``record_reader2``, ``dataframe``, ``hydrate``)
without any acquisition hardware attached.

Usage::

    python examples/synthetic_recording.py            # writes synthetic.tha.YYYYMMDD.1
    python examples/synthetic_recording.py -o demo.tha # custom output path
"""
import math
import argparse
import datetime
import pathlib

from thalamus.thalamus_pb2 import StorageRecord, AnalogResponse, Span, Metadata, Pair
from thalamus.record_writer import write_record

SAMPLE_RATE = 1000   # Hz
POLL_MS = 16         # how many milliseconds of data each record carries
DURATION_S = 5       # length of the recording
SINE_HZ = 2.0        # frequency of the sine channel
PULSE_HZ = 1.0       # frequency of the square pulse channel
INTERVAL_NS = int(1e9 / SAMPLE_RATE)
SAMPLES_PER_POLL = SAMPLE_RATE * POLL_MS // 1000


def default_output() -> pathlib.Path:
    stamp = datetime.date.today().strftime("%Y%m%d")
    return pathlib.Path(f"synthetic.tha.{stamp}.1")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("-o", "--output", type=pathlib.Path, default=default_output(),
                        help="output .tha file (default: synthetic.tha.<date>.1)")
    parser.add_argument("-d", "--duration", type=float, default=DURATION_S,
                        help="recording duration in seconds")
    args = parser.parse_args()

    total_samples = int(SAMPLE_RATE * args.duration)

    with args.output.open("wb") as stream:
        # First record: metadata announcing the recording number, as STORAGE2 does.
        write_record(stream, StorageRecord(
            node="storage", time=0,
            metadata=Metadata(keyvalues=[Pair(key="Rec", integral=1)]),
        ))

        produced = 0
        t_ns = 0
        while produced < total_samples:
            count = min(SAMPLES_PER_POLL, total_samples - produced)
            sine, pulse = [], []
            for i in range(count):
                t = (produced + i) / SAMPLE_RATE
                sine.append(math.sin(2 * math.pi * SINE_HZ * t))
                pulse.append(5.0 if int(t * PULSE_HZ * 2) % 2 == 0 else 0.0)
            produced += count
            t_ns += count * INTERVAL_NS

            # Channels are concatenated in `data`; each `Span` maps a slice to a channel.
            write_record(stream, StorageRecord(
                node="wave", time=t_ns,
                analog=AnalogResponse(
                    data=sine + pulse,
                    spans=[Span(begin=0, end=count, name="sine"),
                           Span(begin=count, end=2 * count, name="pulse")],
                    sample_intervals=[INTERVAL_NS, INTERVAL_NS],
                    time=t_ns,
                ),
            ))

    print(f"Wrote {args.output} ({args.output.stat().st_size} bytes, "
          f"{total_samples} samples/channel)")


if __name__ == "__main__":
    main()
