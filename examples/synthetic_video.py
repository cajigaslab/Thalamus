#!/usr/bin/env python3
"""Record a synthetic grayscale video stream into a Thalamus capture file.

Thalamus stores camera data as ``image`` records.  Each record carries the raw
frame bytes plus its width, height, pixel format, and frame interval.  This
example writes a short ``Gray`` (8-bit) clip of a vertical bar sweeping across the
frame -- no camera required -- and then extracts one frame to a PNG so you can see
how to decode image records with ``thalamus.record_reader2``.

Usage::

    python examples/synthetic_video.py -o video.tha --frame 15 --png frame.png
"""
import argparse
import pathlib

import numpy

from thalamus.thalamus_pb2 import StorageRecord, Image
from thalamus.record_writer import write_record
from thalamus.record_reader2 import SimpleRecordReader

WIDTH, HEIGHT, FRAMES = 64, 48, 30
FRAME_INTERVAL_NS = 33_000_000   # ~30 fps


def write_video(path: pathlib.Path) -> None:
    with path.open("wb") as stream:
        for frame in range(FRAMES):
            buf = numpy.zeros((HEIGHT, WIDTH), dtype=numpy.uint8)
            buf[:, int(frame / FRAMES * (WIDTH - 1))] = 255   # sweeping bar
            write_record(stream, StorageRecord(
                node="cam", time=frame * FRAME_INTERVAL_NS,
                image=Image(data=[buf.tobytes()], width=WIDTH, height=HEIGHT,
                            format=Image.Format.Gray, frame_interval=FRAME_INTERVAL_NS)))


def extract_frame(path: pathlib.Path, index: int) -> numpy.ndarray:
    with SimpleRecordReader(str(path)) as reader:
        frames = [r for r in reader if r.WhichOneof("body") == "image"]
    image = frames[index].image
    return numpy.frombuffer(image.data[0], dtype=numpy.uint8).reshape(image.height, image.width)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("-o", "--output", type=pathlib.Path, default=pathlib.Path("video.tha"))
    parser.add_argument("--frame", type=int, default=15, help="frame index to extract")
    parser.add_argument("--png", type=pathlib.Path, default=pathlib.Path("frame.png"))
    args = parser.parse_args()

    write_video(args.output)
    print(f"Wrote {args.output} ({FRAMES} frames, {WIDTH}x{HEIGHT}, Gray)")

    arr = extract_frame(args.output, args.frame)
    try:
        from PIL import Image as PImage
        PImage.fromarray(arr).save(args.png)
        print(f"Saved frame {args.frame} to {args.png}")
    except ImportError:
        print(f"Frame {args.frame}: shape={arr.shape}, bar at column {int(arr.argmax() % arr.shape[1])} "
              "(install Pillow to save a PNG)")


if __name__ == "__main__":
    main()
