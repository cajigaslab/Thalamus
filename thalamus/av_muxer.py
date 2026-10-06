"""Muxes a recorded MPEG4 video node and AAC audio node into one mp4 without
re-encoding either.

Raw MPEG4 and ADTS framed AAC carry no usable timestamps, so they're set from
the recording: each video frame is placed at its record time, and the audio,
whose record times follow its sample count closely, is placed by sample count
from its first record time. Both are relative to whichever stream starts
first. Requires PyAV (pip install av).
"""
from __future__ import annotations

import sys
import heapq
import pathlib
import argparse
import tempfile
from fractions import Fraction

from thalamus.thalamus_pb2 import AnalogResponse, Image
from thalamus.record_reader2 import RecordReader

# ADTS sampling_frequency_index values.
ADTS_SAMPLE_RATES = [96000, 88200, 64000, 48000, 44100, 32000, 24000, 22050, 16000, 12000, 11025, 8000, 7350]

# Samples of silence FFmpeg's AAC encoder puts before the first input sample.
# ADTS has no field for it, so it isn't trimmed and the audio is shifted
# earlier by this much instead.
AAC_PRIMING_SAMPLES = 1024

# Samples per channel in an AAC frame.
AAC_FRAME_SAMPLES = 1024

# Timestamps are written in nanoseconds, as recorded; the muxer rescales them.
NANOSECONDS = Fraction(1, 1_000_000_000)

def adts_sample_rate(frame: bytes) -> int:
  return ADTS_SAMPLE_RATES[(frame[2] >> 2) & 0xF]

def has_vol_header(data: bytes) -> bool:
  """Whether an MPEG4 frame carries a video object layer header (start code
  00 00 01 2x). The encoder sends one before each keyframe, and nothing before
  the first one can be decoded."""
  start = data.find(b'\x00\x00\x01')
  while start >= 0 and start + 3 < len(data):
    if 0x20 <= data[start + 3] <= 0x2F:
      return True
    start = data.find(b'\x00\x00\x01', start + 3)
  return False

def main():
  parser = argparse.ArgumentParser(description='Mux a recorded MPEG4 video node and AAC audio node into an mp4')
  parser.add_argument('-i', '--input', required=True, help='Input recording')
  parser.add_argument('-o', '--output', default='output.mp4', help='Output mp4')
  parser.add_argument('-v', '--video-node', help='Video node (default: the first node with images)')
  parser.add_argument('-a', '--audio-node', help='Audio node (default: the first node with AAC data)')
  args = parser.parse_args()

  try:
    import av
  except ImportError:
    sys.exit('av_muxer needs PyAV: pip install av')

  with tempfile.TemporaryDirectory() as tmp:
    video_path = pathlib.Path(tmp) / 'video.m4v'
    audio_path = pathlib.Path(tmp) / 'audio.aac'
    video_node, audio_node = args.video_node, args.audio_node
    video_times: list[int] = []
    skipped_frames = 0
    audio_first_time = None
    audio_rate = None
    audio_samples = 0

    with RecordReader(args.input, decode_video=False) as reader, \
         open(video_path, 'wb') as video, open(audio_path, 'wb') as audio:
      for record in reader:
        body = record.WhichOneof('body')
        if body == 'image':
          if video_node is None:
            video_node = record.node
          if record.node != video_node:
            continue
          image = record.image
          if image.format != Image.Format.MPEG4:
            sys.exit(f'{video_node} has {Image.Format.Name(image.format)} images, not MPEG4')
          # A recording can start mid stream, and nothing before the first
          # stream header can be decoded.
          data = b''.join(image.data)
          if not video_times and not has_vol_header(data):
            skipped_frames += 1
            continue
          video.write(data)
          video_times.append(record.time)
        elif body == 'analog':
          analog = record.analog
          if analog.encoding != AnalogResponse.Encoding.AAC:
            continue
          if audio_node is None:
            audio_node = record.node
          if record.node != audio_node:
            continue
          if audio_first_time is None and analog.encoded_count:
            # A record's time is that of its last sample. Its stats channels
            # have no sample interval, the encoded channels do.
            interval = max(analog.sample_intervals, default=0)
            audio_first_time = record.time - (analog.encoded_count - 1) * interval
          if audio_rate is None and len(analog.buffer) >= 7:
            audio_rate = adts_sample_rate(analog.buffer)
          audio_samples += analog.encoded_count
          audio.write(analog.buffer)

    if not video_times:
      sys.exit(f'No MPEG4 video found{f" for {video_node}" if video_node else ""}')
    if audio_first_time is None or audio_rate is None:
      sys.exit(f'No AAC audio found{f" for {audio_node}" if audio_node else ""}')

    # Where the decoded audio's first sample (priming included) falls.
    audio_start = audio_first_time - AAC_PRIMING_SAMPLES * 1_000_000_000 // audio_rate
    origin = min(video_times[0], audio_start)

    with av.open(str(video_path), format='m4v') as video_in, \
         av.open(str(audio_path), format='aac') as audio_in, \
         av.open(args.output, 'w', format='mp4', options={'movflags': '+faststart'}) as out:
      video_out = out.add_stream_from_template(video_in.streams.video[0])
      audio_out = out.add_stream_from_template(audio_in.streams.audio[0])

      def video_packets():
        # Each record holds one frame, so the parser's packets line up with
        # video_times. There are no B-frames, so dts equals pts.
        frames = 0
        previous = None
        for packet in video_in.demux(video_in.streams.video[0]):
          if packet.size == 0:
            continue
          if frames >= len(video_times):
            sys.exit(f'{video_node}: the stream has more frames than the recording has records')
          ts = video_times[frames] - origin
          # Timestamps must increase even if two records share a time.
          if previous is not None and ts <= previous:
            ts = previous + 1
          previous = ts
          packet.time_base = NANOSECONDS
          packet.pts = packet.dts = ts
          packet.stream = video_out
          yield ts, 0, packet
          frames += 1
        if frames != len(video_times):
          sys.exit(f'{video_node}: found {frames} frames in the stream but {len(video_times)} records')

      def audio_packets():
        audio_offset = audio_start - origin
        samples = 0
        for packet in audio_in.demux(audio_in.streams.audio[0]):
          if packet.size == 0:
            continue
          ts = audio_offset + samples * 1_000_000_000 // audio_rate
          packet.time_base = NANOSECONDS
          packet.pts = packet.dts = ts
          packet.duration = AAC_FRAME_SAMPLES * 1_000_000_000 // audio_rate
          packet.stream = audio_out
          yield ts, 1, packet
          samples += AAC_FRAME_SAMPLES

      # Interleaved by time, so the muxer doesn't buffer one stream while
      # waiting for the other.
      for _, _, packet in heapq.merge(video_packets(), audio_packets(), key=lambda p: (p[0], p[1])):
        out.mux(packet)

    duration = (video_times[-1] - video_times[0]) / 1e9
    print(f'video: {video_node}, {len(video_times)} frames over {duration:.2f}s'
          + (f', skipped {skipped_frames} before the first keyframe' if skipped_frames else ''))
    print(f'audio: {audio_node}, {audio_samples} samples at {audio_rate} Hz ({audio_samples / audio_rate:.2f}s)')
    print(f'audio starts {(audio_start - video_times[0]) / 1e6:+.1f} ms from video')
    print(f'wrote {args.output}')

if __name__ == '__main__':
  main()
