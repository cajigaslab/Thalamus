"""Muxes a recorded MPEG4 video node and AAC audio node into one mp4 without
re-encoding either.

The video frames and the ADTS framed AAC are extracted to temporary files and
stream copied by ffmpeg. Raw MPEG4 has no timestamps, so the video gets a
constant frame rate: the average over the recording, which keeps both ends
aligned with the record times. The audio is offset from the video by the
difference between their first record times.
"""
from __future__ import annotations

import sys
import pathlib
import argparse
import fractions
import tempfile
import subprocess

from thalamus.thalamus_pb2 import AnalogResponse, Image
from thalamus.record_reader2 import RecordReader

# ADTS sampling_frequency_index values.
ADTS_SAMPLE_RATES = [96000, 88200, 64000, 48000, 44100, 32000, 24000, 22050, 16000, 12000, 11025, 8000, 7350]

# Samples of silence FFmpeg's AAC encoder puts before the first input sample.
# ADTS has no field for it, so ffmpeg can't trim it and the audio is shifted
# earlier by this much instead.
AAC_PRIMING_SAMPLES = 1024

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
  parser.add_argument('--ffmpeg', default='ffmpeg', help='ffmpeg executable')
  args = parser.parse_args()

  with tempfile.TemporaryDirectory() as tmp:
    video_path = pathlib.Path(tmp) / 'video.m4v'
    audio_path = pathlib.Path(tmp) / 'audio.aac'
    video_node, audio_node = args.video_node, args.audio_node
    video_times: list[int] = []
    skipped_frames = 0
    frame_interval = 0
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
          # A recording can start mid stream. ffmpeg drops frames before the
          # first stream header, which would shift every later frame's
          # timestamp, so the video starts at that header instead.
          data = b''.join(image.data)
          if not video_times and not has_vol_header(data):
            skipped_frames += 1
            continue
          video.write(data)
          video_times.append(record.time)
          frame_interval = frame_interval or image.frame_interval
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

    if len(video_times) > 1:
      duration = fractions.Fraction(video_times[-1] - video_times[0], 1_000_000_000)
      fps = (fractions.Fraction(len(video_times) - 1) / duration).limit_denominator(1_000_000)
    else:
      fps = fractions.Fraction(1_000_000_000, frame_interval or 66_666_667).limit_denominator(1_000_000)

    # Where the decoded audio's first sample (priming included) falls
    # relative to the first video frame.
    audio_start = audio_first_time - AAC_PRIMING_SAMPLES * 1_000_000_000 // audio_rate
    offset = (audio_start - video_times[0]) / 1e9

    video_input = ['-f', 'm4v', '-r', f'{fps.numerator}/{fps.denominator}', '-i', str(video_path)]
    audio_input = ['-f', 'aac', '-i', str(audio_path)]
    # Only delay the stream that starts later; a negative offset would make
    # the other one start before zero.
    if offset >= 0:
      audio_input = ['-itsoffset', f'{offset:.6f}'] + audio_input
    else:
      video_input = ['-itsoffset', f'{-offset:.6f}'] + video_input

    command = [args.ffmpeg, '-y', '-hide_banner', '-loglevel', 'warning'] \
      + video_input + audio_input \
      + ['-map', '0:v:0', '-map', '1:a:0', '-c', 'copy', '-movflags', '+faststart', args.output]

    print(f'video: {video_node}, {len(video_times)} frames at {float(fps):.4f} fps'
          + (f', skipped {skipped_frames} before the first keyframe' if skipped_frames else ''))
    print(f'audio: {audio_node}, {audio_samples} samples at {audio_rate} Hz ({audio_samples / audio_rate:.2f}s)')
    print(f'audio starts {offset * 1000:+.1f} ms from video')
    subprocess.run(command, check=True)
    print(f'wrote {args.output}')

if __name__ == '__main__':
  main()
