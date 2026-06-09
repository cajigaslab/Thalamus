#!/usr/bin/env python3
"""Print a summary of any Thalamus capture (``.tha``) file.

A handy first step when you receive a recording: see which nodes it contains, what
kind of data each produced, the analog channel names, the recording duration, and
the metadata stored at the start.  Works on any ``.tha`` file, including the ones
produced by the other examples.

Usage::

    python examples/capture_summary.py session.tha
"""
import argparse
import collections
import pathlib

from thalamus.record_reader2 import SimpleRecordReader


def summarize(path: pathlib.Path) -> None:
    body_counts = collections.Counter()
    node_bodies = collections.defaultdict(collections.Counter)
    node_channels = collections.defaultdict(set)
    metadata = []
    t_min = t_max = None

    with SimpleRecordReader(str(path)) as reader:
        for record in reader:
            body = record.WhichOneof("body")
            body_counts[body] += 1
            node_bodies[record.node][body] += 1
            if record.time:
                t_min = record.time if t_min is None else min(t_min, record.time)
                t_max = record.time if t_max is None else max(t_max, record.time)
            if body == "analog":
                for span in record.analog.spans:
                    node_channels[record.node].add(span.name)
            elif body == "metadata":
                metadata = [(kv.key, getattr(kv, kv.WhichOneof("value")))
                            for kv in record.metadata.keyvalues if kv.WhichOneof("value")]

    print(f"File: {path}")
    print(f"Records: {sum(body_counts.values())}  {dict(body_counts)}")
    if t_min is not None:
        print(f"Duration: {(t_max - t_min) / 1e9:.3f} s")
    if metadata:
        print(f"Metadata: {metadata}")
    print("Nodes:")
    for node in sorted(node_bodies):
        bodies = dict(node_bodies[node])
        channels = sorted(node_channels[node])
        extra = f"  channels={channels}" if channels else ""
        print(f"  {node}: {bodies}{extra}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=pathlib.Path, help="input .tha file")
    summarize(parser.parse_args().input)


if __name__ == "__main__":
    main()
