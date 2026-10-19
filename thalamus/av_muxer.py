"""Muxes a recorded video node (MPEG4, H264 or VP9) and audio node (AAC or
Opus) into one mp4 without re-encoding either.

The recorded streams carry no usable timestamps, so they're set from the
recording: each video frame is placed at its record time, and the audio by
sample count from where the recording says it starts. An encoded audio
record's time is that of the last sample in its packets, and encoded_count
the number of samples they hold, so that's all placing it takes (see
place_audio); recordings made before thalamus-contrib timed records that way
aren't supported.

Both tracks start at the first video frame: audio recorded before it is cut
at the packet boundary nearest to it. An mp4 could instead delay one track
with an edit list, but some players (e.g. Windows' Movies & TV) ignore edit
lists and would play the tracks out of sync. Requires PyAV (pip install av).
"""
from __future__ import annotations

import sys
import heapq
import struct
import pathlib
import argparse
import tempfile
from dataclasses import dataclass
from fractions import Fraction
from typing import Callable

from thalamus.thalamus_pb2 import AnalogResponse, Image
from thalamus.record_reader2 import RecordReader

# ADTS sampling_frequency_index values.
ADTS_SAMPLE_RATES = [96000, 88200, 64000, 48000, 44100, 32000, 24000, 22050, 16000, 12000, 11025, 8000, 7350]

OPUS_SAMPLE_RATE = 48000

# Timestamps are written in nanoseconds, as recorded; the muxer rescales them.
NANOSECONDS = Fraction(1, 1_000_000_000)

def adts_sample_rate(frame: bytes) -> int:
  return ADTS_SAMPLE_RATES[(frame[2] >> 2) & 0xF]

def aac_packet_samples(frame: bytes) -> int:
  """Samples per channel in an ADTS frame: 1024 per raw data block."""
  return 1024 * ((frame[6] & 0x03) + 1)

def split_adts(buffer: bytes) -> list[bytes]:
  """Splits a buffer of ADTS framed AAC into frames, headers included."""
  frames = []
  i = 0
  while i < len(buffer):
    if i + 7 > len(buffer) or buffer[i] != 0xFF or (buffer[i + 1] & 0xF0) != 0xF0:
      raise ValueError(f'Bad ADTS header at byte {i}')
    length = ((buffer[i + 3] & 0x03) << 11) | (buffer[i + 4] << 3) | (buffer[i + 5] >> 5)
    frames.append(buffer[i:i + length])
    i += length
  return frames

def split_opus(buffer: bytes) -> list[bytes]:
  """Splits a buffer of Opus packets, each preceded by an MPEG-TS Opus control
  header, into the packets."""
  packets = []
  i = 0
  while i < len(buffer):
    if i + 3 > len(buffer) or buffer[i] != 0x7F or (buffer[i + 1] & 0xE0) != 0xE0:
      raise ValueError(f'Bad Opus control header at byte {i}')
    flags = buffer[i + 1]
    i += 2
    size = 0
    while True:
      size += buffer[i]
      i += 1
      if buffer[i - 1] != 0xFF:
        break
    if flags & 0x10:
      i += 2 # start_trim
    if flags & 0x08:
      i += 2 # end_trim
    if flags & 0x04:
      i += 1 + buffer[i] # control extension
    packets.append(buffer[i:i + size])
    i += size
  return packets

def opus_packet_samples(packet: bytes) -> int:
  """Samples (at 48 kHz) in an Opus packet, from its TOC byte (RFC 6716 3.1)."""
  config = packet[0] >> 3
  if config < 12:
    frame = [480, 960, 1920, 2880][config & 3]
  elif config < 16:
    frame = [480, 960][config & 1]
  else:
    frame = [120, 240, 480, 960][config & 3]
  code = packet[0] & 3
  frames = 1 if code == 0 else 2 if code < 3 else packet[1] & 0x3F
  return frame * frames

def start_codes(data: bytes):
  """Yields the byte following each 00 00 01 start code."""
  start = data.find(b'\x00\x00\x01')
  while start >= 0 and start + 3 < len(data):
    yield data[start + 3]
    start = data.find(b'\x00\x00\x01', start + 3)

def has_vol_header(data: bytes) -> bool:
  """Whether an MPEG4 frame carries a video object layer header (start code
  00 00 01 2x). The encoder sends one before each keyframe, and nothing before
  the first one can be decoded."""
  return any(0x20 <= code <= 0x2F for code in start_codes(data))

def has_idr(data: bytes) -> bool:
  """Whether an Annex B H264 frame has an SPS or an IDR slice, before which
  nothing can be decoded."""
  return any((code & 0x1F) in (5, 7) for code in start_codes(data))

def is_vp9_keyframe(data: bytes) -> bool:
  """Whether a VP9 frame is a keyframe (VP9 spec 6.2, uncompressed header)."""
  if not data or data[0] >> 6 != 2:
    return False
  bits = data[0]
  profile = ((bits >> 5) & 1) | (((bits >> 4) & 1) << 1)
  # Bit 7 is the MSB; after frame_marker and the profile bits come a
  # reserved bit (profile 3 only), show_existing_frame and frame_type.
  position = 4 + (profile == 3)
  show_existing_frame = (bits >> (7 - position)) & 1
  frame_type = (bits >> (6 - position)) & 1
  return not show_existing_frame and frame_type == 0

@dataclass
class VideoFormat:
  # Demuxer for the frames written back to back.
  demuxer: str
  suffix: str
  # Whether decoding can start at a frame, for images that don't say.
  starts_stream: Callable[[bytes], bool]

VIDEO_FORMATS = {
  Image.Format.MPEG4: VideoFormat('m4v', '.m4v', has_vol_header),
  Image.Format.H264: VideoFormat('h264', '.h264', has_idr),
  # VP9 frames don't delimit themselves, so they're written to an IVF file.
  Image.Format.VP9: VideoFormat('ivf', '.ivf', is_vp9_keyframe),
}

IVF_HEADER = struct.Struct('<4sHHIHHIIII')
IVF_FRAME_HEADER = struct.Struct('<IQ')

@dataclass
class AudioRecord:
  time: int
  encoded_count: int
  # Samples per channel in its packets.
  samples: int

def place_audio(records: list[AudioRecord], rate: int) -> float | None:
  """When the first recorded packet's first decoded sample falls. Each
  record's time is that of the last sample in its packets, so each record
  with packets says where the decoded stream is at its time. Returns None for
  recordings whose records don't count the samples in their packets: those
  made before thalamus-contrib timed encoded records by their packets, when a
  record had the time of the input that completed it and counted the samples
  pushed."""
  if any(record.encoded_count != record.samples for record in records):
    return None
  sample_ns = 1_000_000_000 / rate
  offsets = []
  decoded = 0
  for record in records:
    if not record.samples:
      continue
    decoded += record.samples
    offsets.append(record.time - (decoded - 1) * sample_ns)
  if not offsets:
    return None
  # Record times jitter; the median is robust to it.
  offsets.sort()
  return offsets[len(offsets) // 2]

def main():
  parser = argparse.ArgumentParser(description='Mux a recorded video node (MPEG4, H264 or VP9) and audio node (AAC or Opus) into an mp4')
  parser.add_argument('-i', '--input', required=True, help='Input recording')
  parser.add_argument('-o', '--output', default='output.mp4', help='Output mp4')
  parser.add_argument('-v', '--video-node', help='Video node (default: the first node with images)')
  parser.add_argument('-a', '--audio-node', help='Audio node (default: the first node with AAC or Opus data)')
  args = parser.parse_args()

  try:
    import av
  except ImportError:
    sys.exit('av_muxer needs PyAV: pip install av')

  with tempfile.TemporaryDirectory() as tmp:
    video_node, audio_node = args.video_node, args.audio_node
    video_format = None
    video_format_id = Image.Format.Gray
    video_path = None
    video = None
    video_times: list[int] = []
    width = height = 0
    skipped_frames = 0
    audio_encoding = None
    audio_channels = 0
    audio_rate = None
    audio_samples = 0
    audio_records: list[AudioRecord] = []
    audio_packets_data: list[bytes] = []

    try:
      with RecordReader(args.input, decode_video=False) as reader:
        for record in reader:
          body = record.WhichOneof('body')
          if body == 'image':
            if video_node is None:
              video_node = record.node
            if record.node != video_node:
              continue
            image = record.image
            if video_format is None:
              if image.format not in VIDEO_FORMATS:
                sys.exit(f'{video_node} has {Image.Format.Name(image.format)} images, not MPEG4, H264 or VP9')
              video_format = VIDEO_FORMATS[image.format]
              video_format_id = image.format
              video_path = pathlib.Path(tmp) / f'video{video_format.suffix}'
              video = open(video_path, 'wb')
              if video_format.demuxer == 'ivf':
                video.write(bytes(IVF_HEADER.size))
            elif image.format != video_format_id:
              sys.exit(f'{video_node} changed from {Image.Format.Name(video_format_id)} to {Image.Format.Name(image.format)}')
            # A recording can start mid stream, and nothing before the first
            # keyframe can be decoded. Recordings from before images said
            # whether they're keyframes are checked by parsing the frame.
            data = b''.join(image.data)
            keyframe = image.is_keyframe if image.HasField('is_keyframe') else video_format.starts_stream(data)
            if not video_times and not keyframe:
              skipped_frames += 1
              continue
            assert video is not None
            if video_format.demuxer == 'ivf':
              video.write(IVF_FRAME_HEADER.pack(len(data), len(video_times)))
            video.write(data)
            video_times.append(record.time)
            width, height = image.width, image.height
          elif body == 'analog':
            analog = record.analog
            if analog.encoding not in (AnalogResponse.Encoding.AAC, AnalogResponse.Encoding.Opus):
              continue
            if audio_node is None:
              audio_node = record.node
            if record.node != audio_node:
              continue
            if audio_encoding is None:
              audio_encoding = analog.encoding
              # Its stats channels have no sample interval, the encoded
              # channels do.
              audio_channels = sum(1 for interval in analog.sample_intervals if interval)
            elif analog.encoding != audio_encoding:
              sys.exit(f'{audio_node} changed audio encoding')
            if audio_encoding == AnalogResponse.Encoding.AAC:
              packets = split_adts(analog.buffer)
              if audio_rate is None and packets:
                audio_rate = adts_sample_rate(packets[0])
            else:
              packets = split_opus(analog.buffer)
              audio_rate = OPUS_SAMPLE_RATE
            audio_samples += analog.encoded_count
            if audio_encoding == AnalogResponse.Encoding.AAC:
              samples = sum(aac_packet_samples(p) for p in packets)
            else:
              samples = sum(opus_packet_samples(p) for p in packets)
            audio_records.append(AudioRecord(record.time, analog.encoded_count, samples))
            audio_packets_data.extend(packets)
    finally:
      if video is not None:
        if video_format.demuxer == 'ivf':
          video.seek(0)
          video.write(IVF_HEADER.pack(b'DKIF', 0, IVF_HEADER.size, 0x30395056, width, height, 30, 1, len(video_times), 0))
        video.close()

    if not video_times:
      sys.exit(f'No video found{f" for {video_node}" if video_node else ""}')
    if not audio_packets_data or audio_rate is None:
      sys.exit(f'No AAC or Opus audio found{f" for {audio_node}" if audio_node else ""}')
    assert video_format is not None and video_path is not None and audio_encoding is not None
    audio_start = place_audio(audio_records, audio_rate)
    if audio_start is None:
      sys.exit(f'{audio_node}: its records don\'t count the samples in their packets, so it was recorded'
               ' before thalamus-contrib timed encoded audio by its packets, which isn\'t supported')

    with av.open(str(video_path), format=video_format.demuxer) as video_in, \
         av.open(args.output, 'w', format='mp4', options={'movflags': '+faststart'}) as out:
      video_out = out.add_stream_from_template(video_in.streams.video[0])
      if audio_encoding == AnalogResponse.Encoding.AAC:
        audio_path = pathlib.Path(tmp) / 'audio.aac'
        audio_path.write_bytes(b''.join(audio_packets_data))
        audio_in = av.open(str(audio_path), format='aac')
        audio_out = out.add_stream_from_template(audio_in.streams.audio[0])
        packet_samples = [aac_packet_samples(p) for p in audio_packets_data]
        # ADTS has no field for AAC's priming, so the decoder plays it.
        skipped = 0
      else:
        audio_in = None
        # Opus packets have no container to copy the stream from. The encoder
        # is only opened to write the stream's Opus header (with libopus's
        # pre-skip, as in thalamus-contrib) and never encodes anything.
        audio_out = out.add_stream('libopus', rate=OPUS_SAMPLE_RATE, layout='mono' if audio_channels == 1 else 'stereo')
        out.start_encoding()
        # The decoder drops the Opus header's pre-skip from the start of the
        # track.
        skipped = struct.unpack_from('<H', audio_out.codec_context.extradata or bytes(12), 10)[0]
        packet_samples = [opus_packet_samples(p) for p in audio_packets_data]

      # Cut the audio at the packet boundary nearest the first video frame,
      # counting the samples the decoder drops from the start of the track.
      origin = video_times[0]
      sample_ns = 1_000_000_000 / audio_rate
      first_packet = 0
      samples_before = 0
      def audible_start(samples: int) -> float:
        return audio_start + (samples + skipped) * sample_ns
      while first_packet + 1 < len(packet_samples) and \
            abs(audible_start(samples_before + packet_samples[first_packet]) - origin) < abs(audible_start(samples_before) - origin):
        samples_before += packet_samples[first_packet]
        first_packet += 1
      audio_error = (audible_start(samples_before) - origin) / 1e6
      if audio_error > 1000 * packet_samples[0] / audio_rate:
        print(f'warning: audio starts {audio_error:.1f} ms after the video, and both tracks start together')

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
        if audio_in is not None:
          packets = (p for p in audio_in.demux(audio_in.streams.audio[0]) if p.size)
        else:
          packets = (av.Packet(p) for p in audio_packets_data)
        samples = 0
        for index, (packet, count) in enumerate(zip(packets, packet_samples)):
          if index < first_packet:
            continue
          ts = samples * 1_000_000_000 // audio_rate
          packet.time_base = NANOSECONDS
          packet.pts = packet.dts = ts
          packet.duration = count * 1_000_000_000 // audio_rate
          packet.stream = audio_out
          yield ts, 1, packet
          samples += count

      try:
        # Interleaved by time, so the muxer doesn't buffer one stream while
        # waiting for the other.
        for _, _, packet in heapq.merge(video_packets(), audio_packets(), key=lambda p: (p[0], p[1])):
          out.mux(packet)
      finally:
        if audio_in is not None:
          audio_in.close()

    duration = (video_times[-1] - video_times[0]) / 1e9
    print(f'video: {video_node}, {Image.Format.Name(video_format_id)}, {len(video_times)} frames over {duration:.2f}s'
          + (f', skipped {skipped_frames} before the first keyframe' if skipped_frames else ''))
    print(f'audio: {audio_node}, {AnalogResponse.Encoding.Name(audio_encoding)}, {audio_samples} samples at {audio_rate} Hz ({audio_samples / audio_rate:.2f}s)')
    print(f'audio recorded from {(audible_start(0) - origin) / 1e6:+.1f} ms relative to the video, cut {first_packet} packets'
          f' before it, starts {audio_error:+.1f} ms from its exact position')
    print(f'wrote {args.output}')

if __name__ == '__main__':
  main()
