#!/usr/bin/env python3
"""Record an unsigned 64-bit (``ulong``) analog channel and read it back.

Thalamus analog streams can carry 64-bit **unsigned** integers (``ulong_data`` /
``is_ulong_data``) in addition to floating-point and signed-integer samples -- useful
for monotonically increasing counters, hardware timestamps, or event tallies that do
not fit in a signed integer.

This example writes a ``.tha`` capture whose ``counter`` node emits a uint64 sample
counter, then reads it back and shows that ``record_reader2`` and ``dataframe`` both
handle the ulong path.  No hardware required.

Usage::

    python examples/ulong_counter.py -o counter.tha
    python -m thalamus.dataframe -n counter -i counter.tha -f csv -o counter.csv
"""
import argparse
import pathlib

from thalamus.thalamus_pb2 import StorageRecord, AnalogResponse, Span, Metadata, Pair
from thalamus.record_writer import write_record

SAMPLE_RATE = 1000
POLL_MS = 16
DURATION_S = 2
INTERVAL_NS = int(1e9 / SAMPLE_RATE)
SAMPLES_PER_POLL = SAMPLE_RATE * POLL_MS // 1000


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("-o", "--output", type=pathlib.Path, default=pathlib.Path("counter.tha"))
    args = parser.parse_args()

    total = SAMPLE_RATE * DURATION_S
    with args.output.open("wb") as stream:
        write_record(stream, StorageRecord(
            node="storage", time=0,
            metadata=Metadata(keyvalues=[Pair(key="Rec", integral=1)])))

        produced, t_ns = 0, 0
        while produced < total:
            count = min(SAMPLES_PER_POLL, total - produced)
            samples = [produced + i for i in range(count)]   # monotonically increasing uint64
            produced += count
            t_ns += count * INTERVAL_NS
            write_record(stream, StorageRecord(
                node="counter", time=t_ns,
                analog=AnalogResponse(
                    ulong_data=samples,
                    is_ulong_data=True,
                    spans=[Span(begin=0, end=count, name="samples")],
                    sample_intervals=[INTERVAL_NS],
                    time=t_ns)))

    print(f"Wrote {args.output} ({total} uint64 samples)")


if __name__ == "__main__":
    main()
