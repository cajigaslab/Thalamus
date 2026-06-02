#!/usr/bin/env python3
"""Read a Thalamus capture (``.tha``) file and plot its analog channels.

This demonstrates reading raw records directly with ``thalamus.record_reader2``
(no hydration step required) and reconstructing per-channel time series.  It works
on the file produced by ``synthetic_recording.py`` or on any real recording.

Usage::

    python examples/analyze_recording.py synthetic.tha.20260602.1
    python examples/analyze_recording.py recording.tha.20260602.1 -n wave -o plot.png
"""
import argparse
import collections
import pathlib

import numpy
import matplotlib
matplotlib.use("Agg")          # render without a display
import matplotlib.pyplot as plt

from thalamus.record_reader2 import SimpleRecordReader


def read_channels(path: pathlib.Path, node: str):
    """Return {channel_name: numpy array} and the sample interval (ns)."""
    channels = collections.defaultdict(list)
    interval_ns = None
    with SimpleRecordReader(str(path)) as reader:
        for record in reader:
            if record.WhichOneof("body") != "analog" or record.node != node:
                continue
            analog = record.analog
            for interval, span in zip(analog.sample_intervals, analog.spans):
                interval_ns = interval_ns or interval
                channels[span.name].extend(analog.data[span.begin:span.end])
    return {name: numpy.asarray(values) for name, values in channels.items()}, interval_ns


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=pathlib.Path, help="input .tha file")
    parser.add_argument("-n", "--node", default="wave", help="node name to plot")
    parser.add_argument("-o", "--output", type=pathlib.Path, default=pathlib.Path("analysis.png"))
    args = parser.parse_args()

    channels, interval_ns = read_channels(args.input, args.node)
    if not channels:
        raise SystemExit(f"No analog data for node {args.node!r} in {args.input}")

    dt = (interval_ns or 1) / 1e9
    fig, ax = plt.subplots(figsize=(8, 3))
    for name, values in channels.items():
        ax.plot(numpy.arange(len(values)) * dt, values, label=name)
        print(f"{name}: {len(values)} samples")
    ax.set_xlabel("Time (s)")
    ax.set_ylabel("Amplitude")
    ax.legend(loc="upper right")
    fig.tight_layout()
    fig.savefig(args.output, dpi=110)
    print(f"Saved {args.output}")


if __name__ == "__main__":
    main()
