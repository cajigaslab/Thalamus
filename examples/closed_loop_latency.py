#!/usr/bin/env python3
"""Measure closed-loop latency between two pulse channels in a recording.

A core use of Thalamus is quantifying the latency of a closed loop: a trigger fires
and a downstream system responds some milliseconds later.  This example synthesizes
a recording with a ``trigger`` channel and a ``response`` channel offset by a known
delay, then recovers that delay by detecting and pairing rising edges -- the same
analysis used for the closed-loop performance figures in the Thalamus paper
(https://www.nature.com/articles/s44172-026-00646-z).

Usage::

    python examples/closed_loop_latency.py                 # synthesize + analyze
    python examples/closed_loop_latency.py recording.tha   # analyze an existing file
"""
import argparse
import collections
import pathlib
import statistics
import tempfile

from thalamus.thalamus_pb2 import StorageRecord, AnalogResponse, Span, Metadata, Pair
from thalamus.record_writer import write_record
from thalamus.record_reader2 import SimpleRecordReader

SAMPLE_RATE = 1000
INTERVAL_NS = int(1e9 / SAMPLE_RATE)


def synthesize(path: pathlib.Path, delay_samples: int = 5, n: int = 5000) -> None:
    """Write trigger + response square waves where response lags by delay_samples."""
    with path.open("wb") as stream:
        write_record(stream, StorageRecord(
            node="storage", time=0,
            metadata=Metadata(keyvalues=[Pair(key="Rec", integral=1)])))
        produced, t_ns = 0, 0
        while produced < n:
            count = min(16, n - produced)
            trig, resp = [], []
            for i in range(count):
                s = produced + i
                trig.append(5.0 if (s // 500) % 2 == 1 else 0.0)
                r = s - delay_samples
                resp.append(5.0 if (r >= 0 and (r // 500) % 2 == 1) else 0.0)
            produced += count
            t_ns += count * INTERVAL_NS
            write_record(stream, StorageRecord(
                node="daq", time=t_ns,
                analog=AnalogResponse(
                    data=trig + resp,
                    spans=[Span(begin=0, end=count, name="trigger"),
                           Span(begin=count, end=2 * count, name="response")],
                    sample_intervals=[INTERVAL_NS, INTERVAL_NS], time=t_ns)))


def load(path: pathlib.Path, node: str):
    """Return {channel: (values, sample_times_ns)} for one analog node."""
    values = collections.defaultdict(list)
    times = collections.defaultdict(list)
    with SimpleRecordReader(str(path)) as reader:
        for record in reader:
            if record.WhichOneof("body") != "analog" or record.node != node:
                continue
            analog = record.analog
            for interval, span in zip(analog.sample_intervals, analog.spans):
                seg = list(analog.data[span.begin:span.end])
                base = analog.time - (len(seg) - 1) * interval   # time of first sample
                values[span.name].extend(seg)
                times[span.name].extend(base + j * interval for j in range(len(seg)))
    return {name: (values[name], times[name]) for name in values}


def rising_edges(values, times, threshold=2.5):
    return [times[i] for i in range(1, len(values))
            if values[i - 1] < threshold <= values[i]]


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", nargs="?", type=pathlib.Path,
                        help="existing .tha file (synthesized if omitted)")
    parser.add_argument("-n", "--node", default="daq")
    parser.add_argument("--trigger", default="trigger")
    parser.add_argument("--response", default="response")
    args = parser.parse_args()

    path = args.input
    if path is None:
        path = pathlib.Path(tempfile.mkdtemp()) / "loop.tha"
        synthesize(path)
        print(f"Synthesized {path} (response lags trigger by 5 ms)")

    channels = load(path, args.node)
    trig = rising_edges(*channels[args.trigger])
    resp = rising_edges(*channels[args.response])
    latencies = [(r - t) / 1e6 for t, r in zip(trig, resp)]   # ms

    print(f"trigger edges: {len(trig)}, response edges: {len(resp)}")
    if latencies:
        print(f"latency (ms): mean={statistics.mean(latencies):.3f} "
              f"std={statistics.pstdev(latencies):.3f} "
              f"min={min(latencies):.3f} max={max(latencies):.3f}")


if __name__ == "__main__":
    main()
