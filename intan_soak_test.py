'''
INTAN soak test: subscribe to the live analog stream and measure stability over time --
no decoder, no injection, read-only. Establishes a baseline (packet cadence, drift,
throughput) at the current channel count so later channel-count increases (Phase 2 of the
closed-loop BCI roadmap) have something concrete to compare against.

What it watches for, per packet:
  * inter-arrival gaps far above the expected packet interval (backlog / stall)
  * cumulative sample-time drift vs wall-clock time (samples silently dropped or queued)
  * inconsistent per-channel sample counts within a packet (malformed/partial packet)
  * non-finite values in the channel data

Run (core must already be up and INTAN streaming):
    python intan_soak_test.py                          # 5ch default, runs until Ctrl-C
    python intan_soak_test.py --duration 1800           # 30-minute soak
    python intan_soak_test.py --channels A-001,...,A-244  # future full-scale run

Modelled on neural_decoder.py's subscribe pattern (stub.analog / span demux) but strips out
everything downstream of the listener -- this measures the INTAN->core->client leg only.
'''
import csv
import sys
import time
import asyncio
import argparse
import datetime
import traceback

import numpy as np
import grpc

from thalamus import thalamus_pb2
from thalamus import thalamus_pb2_grpc


class Rolling:
  '''Accumulates values for one reporting window, then clears.'''
  def __init__(self):
    self.vals = []

  def add(self, v):
    self.vals.append(v)

  def percentiles(self):
    a = np.array(self.vals)
    return np.percentile(a, 50), np.percentile(a, 90), np.percentile(a, 99), a.max()

  def clear(self):
    self.vals = []


async def run_soak(args, stub):
  channels = args.channels
  n_channels = len(channels)
  request = thalamus_pb2.AnalogRequest(
      node=thalamus_pb2.NodeSelector(name=args.source),
      channel_names=channels)
  stream = stub.analog(request)
  print('[soak] subscribing to "%s" channels %s' % (args.source, channels), flush=True)

  csv_writer = None
  csv_file = None
  if args.csv:
    csv_file = open(args.csv, 'w', newline='')
    csv_writer = csv.writer(csv_file)
    csv_writer.writerow(['packet_i', 't_recv_s', 'dt_ms', 'n_samples', 'fs_hz',
                          'cum_samples', 'expected_cum_samples', 'drift_ms', 'anomaly'])
    print('[soak] logging per-packet CSV to %s' % args.csv, flush=True)

  fs = None
  expected_interval_s = None
  packet_i = 0
  cum_samples = 0
  t_start = None
  last_t_recv = None

  window = Rolling()
  window_start = time.perf_counter()

  all_dts = []
  total_anomalies = 0
  total_nan_packets = 0
  total_bad_shape = 0
  total_bytes = 0

  stop_at = time.perf_counter() + args.duration if args.duration else None

  def report_window():
    if not window.vals:
      return
    p50, p90, p99, mx = window.percentiles()
    dt = time.perf_counter() - window_start
    rate = len(window.vals) / dt if dt > 0 else 0.0
    drift_ms = ((cum_samples - (time.perf_counter() - t_start) * fs) / fs) * 1000 if fs else 0.0
    print('[soak] n=%d  rate=%5.1f Hz  gap-ms p50/p90/p99/max=%.2f/%.2f/%.2f/%.2f  '
          'drift=%+.1fms  anomalies=%d  nan_packets=%d  bad_shape=%d'
          % (packet_i, rate, p50 * 1000, p90 * 1000, p99 * 1000, mx * 1000,
             drift_ms, total_anomalies, total_nan_packets, total_bad_shape), flush=True)

  try:
    async for message in stream:
      t_recv = time.perf_counter()
      if stop_at and t_recv >= stop_at:
        print('[soak] duration elapsed, stopping', flush=True)
        break

      by_name = {s.name: np.array(message.data[s.begin:s.end]) for s in message.spans}
      packet = [by_name.get(name, np.zeros(0)) for name in channels]
      sizes = {p.size for p in packet}
      bad_shape = len(sizes) != 1 or (sizes and 0 in sizes)
      n_samples = max(sizes) if sizes else 0
      if bad_shape:
        total_bad_shape += 1

      if fs is None and n_samples:
        iv = message.sample_intervals[0] if message.sample_intervals else 0
        fs = 1e9 / iv if iv else 0.0
        expected_interval_s = n_samples / fs if fs else None
        t_start = t_recv
        last_t_recv = t_recv
        print('[soak] fs=%.1f Hz  samples/packet=%d  expected packet interval=%.2fms'
              % (fs, n_samples, (expected_interval_s or 0) * 1000), flush=True)

      dt = (t_recv - last_t_recv) if packet_i > 0 else 0.0
      last_t_recv = t_recv
      if packet_i > 0:
        window.add(dt)
        all_dts.append(dt)

      cum_samples += n_samples
      expected_cum_samples = (t_recv - t_start) * fs if (fs and t_start is not None) else 0.0
      drift_ms = ((cum_samples - expected_cum_samples) / fs) * 1000 if fs else 0.0

      any_nan = any(p.size and not np.all(np.isfinite(p)) for p in packet)
      if any_nan:
        total_nan_packets += 1

      is_anomaly = bad_shape or any_nan or (
          expected_interval_s is not None and packet_i > 0 and dt > 3 * expected_interval_s)
      if is_anomaly:
        total_anomalies += 1

      total_bytes += message.ByteSize()

      if csv_writer:
        csv_writer.writerow([packet_i, '%.6f' % t_recv, '%.3f' % (dt * 1000), n_samples,
                              '%.1f' % (fs or 0), cum_samples, '%.1f' % expected_cum_samples,
                              '%.3f' % drift_ms, int(is_anomaly)])

      packet_i += 1
      if packet_i % args.log_every == 0:
        report_window()
        window.clear()
        window_start = time.perf_counter()

  finally:
    stream.cancel()
    if csv_file:
      csv_file.close()

  # ---- final summary ----
  total_dt = (last_t_recv - t_start) if (t_start is not None and last_t_recv is not None) else 0.0
  print('\n[soak] ==== summary ====')
  print('[soak] duration=%.1fs  packets=%d  channels=%d  fs=%.1fHz'
        % (total_dt, packet_i, n_channels, fs or 0.0))
  if all_dts:
    a = np.array(all_dts)
    print('[soak] inter-packet gap ms: p50=%.2f p90=%.2f p99=%.2f max=%.2f  (expected ~%.2f)'
          % (np.percentile(a, 50) * 1000, np.percentile(a, 90) * 1000,
             np.percentile(a, 99) * 1000, a.max() * 1000,
             (expected_interval_s or 0) * 1000))
  final_drift_ms = ((cum_samples - total_dt * fs) / fs) * 1000 if fs else 0.0
  print('[soak] cumulative sample-time drift vs wall clock: %+.1f ms  '
        '(growing drift = queue building up / samples silently dropped)' % final_drift_ms)
  print('[soak] anomalous packets=%d (%.3f%%)  nan_packets=%d  bad_shape_packets=%d'
        % (total_anomalies, 100.0 * total_anomalies / max(1, packet_i),
           total_nan_packets, total_bad_shape))
  print('[soak] throughput: %.1f KB/s  (%d bytes total)'
        % (total_bytes / max(total_dt, 1e-9) / 1024.0, total_bytes))
  if args.csv:
    print('[soak] per-packet detail: %s' % args.csv)
  ok = total_anomalies == 0 and total_bad_shape == 0 and total_nan_packets == 0
  print('[soak] result: %s' % ('CLEAN' if ok else 'ANOMALIES DETECTED -- inspect the CSV'))
  return ok


async def main():
  parser = argparse.ArgumentParser(description='INTAN stream soak test (read-only)')
  parser.add_argument('--thalamus', default='localhost:50050', help='core gRPC address')
  parser.add_argument('--source', default='INTAN', help='analog node to subscribe to')
  parser.add_argument('--channels', default='A-001,A-002,A-003,A-004,A-005',
                       help='comma-separated source channel names')
  parser.add_argument('--duration', type=float, default=0.0,
                       help='seconds to run (0 = until Ctrl-C)')
  parser.add_argument('--log-every', type=int, default=200, help='report interval (packets)')
  parser.add_argument('--csv', default=None,
                       help='per-packet CSV path (default: timestamped file); pass "" to disable')
  args = parser.parse_args()

  args.channels = [c.strip() for c in args.channels.split(',') if c.strip()]
  if args.csv is None:
    stamp = datetime.datetime.now().strftime('%Y%m%d_%H%M%S')
    args.csv = 'intan_soak_%s.csv' % stamp
  elif args.csv == '':
    args.csv = None

  channel = grpc.aio.insecure_channel(args.thalamus)
  try:
    await channel.channel_ready()
    stub = thalamus_pb2_grpc.ThalamusStub(channel)
    ok = await run_soak(args, stub)
  finally:
    await channel.close()
  sys.exit(0 if ok else 1)


if __name__ == '__main__':
  try:
    asyncio.get_event_loop().run_until_complete(main())
  except KeyboardInterrupt:
    pass
  except Exception:
    traceback.print_exc()
    sys.exit(1)
