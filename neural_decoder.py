'''
Closed-loop neural decoder -> joystick cursor (proof of concept).

Pipeline (each stage is a deliberately swappable seam):

    INTAN analog stream  ->  filter  ->  features  ->  velocity model  ->  integrate?  ->  inject "Decoder"
       (listener)          (placeholder)  (RMS/win)   (linear readout)   (mode-matched)     (X, Y channels)

The Rust joystick task subscribes to the "Decoder" ANALOG node's X/Y channels exactly as it
would the hardware "Joystick" node, so no task or C++ change is needed -- point the task's
`joystick_node` config at "Decoder".

Control law: the decoder ALWAYS computes a velocity (vx, vy). A final stage matches the task's
cursor mode:
  * --emit-mode position  -> leaky-integrate velocity into a position, emit position
                             (for the task's `direct` mode; the trained default today)
  * --emit-mode velocity  -> emit raw velocity, let the task's `cumulative` mode integrate

Run standalone (the core must be up on --thalamus):
    python neural_decoder.py --loopback            # plumbing/latency test, ignores neural data
    python neural_decoder.py --emit-mode position  # real 5-channel decode

Modelled on fiducial.py / intan.py (inject / subscribe patterns) and the connection-reuse
discipline in task_context.py / rust/joystick_task/src/grpc.rs (one long-lived inject stream).
'''
import sys
import time
import math
import asyncio
import argparse
import traceback

import numpy as np
import grpc

from thalamus import thalamus_pb2
from thalamus import thalamus_pb2_grpc
from thalamus.util import IterableQueue

try:
  from scipy.signal import butter, lfilter, lfilter_zi
  HAVE_SCIPY = True
except ImportError:  # scipy is a dependency, but degrade gracefully to identity filter
  HAVE_SCIPY = False


# --------------------------------------------------------------------------------------
# Injector: one long-lived inject_analog stream, reused for the process lifetime.
# Opening a stream per pulse leaks a TCP conn + a parked server thread every call
# (see task_context.py:328-373, grpc.rs:159-203); keep exactly one open.
# --------------------------------------------------------------------------------------
class Injector:
  def __init__(self, stub, node_name):
    self.node_name = node_name
    self.queue = IterableQueue()
    # Fire, do NOT await: a client-streaming RPC only resolves when the request stream
    # ends, so awaiting here would deadlock before a single message is sent.
    self.call = stub.inject_analog(self.queue)

  async def start(self):
    # First message on the stream names the target node.
    await self.queue.put(thalamus_pb2.InjectAnalogRequest(node=self.node_name))

  async def emit(self, x, y, interval_ns):
    '''Push one (X, Y) sample. Channels are demuxed downstream by span name.'''
    signal = thalamus_pb2.AnalogResponse(
        data=[float(x), float(y)],
        spans=[thalamus_pb2.Span(begin=0, end=1, name='X'),
               thalamus_pb2.Span(begin=1, end=2, name='Y')],
        sample_intervals=[int(interval_ns), int(interval_ns)])
    await self.queue.put(thalamus_pb2.InjectAnalogRequest(signal=signal))


# --------------------------------------------------------------------------------------
# Latency instrumentation: rolling per-stage deltas, printed periodically.
# --------------------------------------------------------------------------------------
class LatencyLog:
  def __init__(self, log_every):
    self.log_every = log_every
    self.n = 0
    self.decode_us = []   # t_decode - t_recv
    self.inject_us = []   # t_inject - t_recv
    self.last_report = time.perf_counter()

  def add(self, t_recv, t_decode, t_inject):
    self.n += 1
    self.decode_us.append((t_decode - t_recv) * 1e6)
    self.inject_us.append((t_inject - t_recv) * 1e6)
    if self.n % self.log_every == 0:
      self.report()

  def report(self):
    now = time.perf_counter()
    dt = now - self.last_report
    rate = len(self.inject_us) / dt if dt > 0 else 0.0
    dec = np.array(self.decode_us)
    inj = np.array(self.inject_us)
    print('[decoder] n=%d  rate=%5.1f Hz  decode us p50/max=%6.0f/%6.0f  '
          'recv->inject us p50/max=%6.0f/%6.0f'
          % (self.n, rate, np.percentile(dec, 50), dec.max(),
             np.percentile(inj, 50), inj.max()), flush=True)
    self.decode_us.clear()
    self.inject_us.clear()
    self.last_report = now


# --------------------------------------------------------------------------------------
# Decoder core: filter -> features -> velocity model -> mode-matched integration.
# Stateful across packets. Neural-path only; loopback bypasses this entirely.
# --------------------------------------------------------------------------------------
class Decoder:
  def __init__(self, args, n_channels, fs):
    self.args = args
    self.n_channels = n_channels
    self.fs = fs
    self.window_samples = max(1, int(args.window_ms / 1000.0 * fs))

    # Stage 1 seam -- filter. Persistent IIR state (zi) per channel avoids per-packet
    # edge transients. Identity passthrough if --band is unset or scipy is missing.
    self.b = self.a = None
    self.zi = None
    if args.band and HAVE_SCIPY:
      low, high = args.band
      self.b, self.a = butter(4, [low, high], btype='band', fs=fs)
      base = lfilter_zi(self.b, self.a)
      self.zi = [base.copy() for _ in range(n_channels)]

    # Per-channel rolling window for feature extraction.
    self.buf = [np.zeros(0) for _ in range(n_channels)]

    # Stage 3 seam -- velocity model: v = W @ features.
    # Default legible demo mapping (5ch): A-001-A-002 -> vx, A-003-A-004 -> vy. The
    # opponent-channel structure centers the output, so a steadily-louder channel gives
    # a *sustained* velocity. A real decoder would z-score/whiten features with fitted
    # stats here -- that normalization belongs in this seam, not a temporal baseline
    # (which would wash out constant activity to zero).
    self.W = self._default_readout(n_channels)

    # Stage 4 seam -- position integrator state (only used in --emit-mode position).
    self.pos = np.zeros(2)
    self.last_t = None

  @staticmethod
  def _default_readout(n):
    W = np.zeros((2, n))
    if n >= 1: W[0, 0] = 1.0    # A-001 -> +x
    if n >= 2: W[0, 1] = -1.0   # A-002 -> -x
    if n >= 3: W[1, 2] = 1.0    # A-003 -> +y
    if n >= 4: W[1, 3] = -1.0   # A-004 -> -y
    return W

  def _filter(self, ch, samples):
    if self.b is None:
      return samples
    out, self.zi[ch] = lfilter(self.b, self.a, samples, zi=self.zi[ch] * samples[0])
    return out

  def features(self):
    '''Per-channel RMS over the rolling window -> feature vector.'''
    f = np.zeros(self.n_channels)
    for ch in range(self.n_channels):
      buf = self.buf[ch]
      if buf.size:
        f[ch] = math.sqrt(float(np.mean(buf * buf)))
    return f

  def step(self, packet, now):
    '''packet: list of np arrays (per channel, new samples). Returns (x, y) to emit.'''
    # Stage 1+2: filter each channel's new samples, push into the rolling window.
    for ch in range(self.n_channels):
      new = self._filter(ch, packet[ch])
      buf = np.concatenate([self.buf[ch], new])
      if buf.size > self.window_samples:
        buf = buf[-self.window_samples:]
      self.buf[ch] = buf

    f = self.features()

    # Stage 3: velocity readout.
    v = (self.W @ f) * self.args.gain         # shape (2,)

    # Stage 4: mode-matched output.
    if self.args.emit_mode == 'velocity':
      out = np.clip(v, -1.0, 1.0)
    else:  # position: leaky integrate real elapsed time, clamp to task range
      dt = 0.0 if self.last_t is None else min(now - self.last_t, 0.05)
      self.last_t = now
      self.pos = self.args.leak * self.pos + v * dt
      out = np.clip(self.pos, -1.0, 1.0)
    return float(out[0]), float(out[1])


# --------------------------------------------------------------------------------------
# Run loops
# --------------------------------------------------------------------------------------
async def run_loopback(args, injector):
  '''Ignore neural data; emit a slow circle as position. Proves plumbing + latency.'''
  print('[decoder] LOOPBACK: emitting %g Hz circle amp=%g into "%s"'
        % (args.loopback_freq, args.loopback_amp, args.target), flush=True)
  period = 0.010                       # 100 Hz emit
  interval_ns = int(period * 1e9)
  lat = LatencyLog(args.log_every)
  t0 = time.perf_counter()
  while True:
    t_recv = time.perf_counter()
    phase = 2 * math.pi * args.loopback_freq * (t_recv - t0)
    x = args.loopback_amp * math.cos(phase)
    y = args.loopback_amp * math.sin(phase)
    t_decode = time.perf_counter()
    await injector.emit(x, y, interval_ns)
    lat.add(t_recv, t_decode, time.perf_counter())
    await asyncio.sleep(period)


async def run_neural(args, stub, injector):
  '''Subscribe to the source node, decode each packet, inject (X, Y).'''
  channels = args.channels
  request = thalamus_pb2.AnalogRequest(
      node=thalamus_pb2.NodeSelector(name=args.source),
      channel_names=channels)
  stream = stub.analog(request)
  print('[decoder] subscribing to "%s" channels %s' % (args.source, channels), flush=True)

  decoder = None
  lat = LatencyLog(args.log_every)
  packet_i = 0
  try:
    async for message in stream:
      t_recv = time.perf_counter()

      # Demux requested channels by span name (data[begin:end] per channel).
      by_name = {s.name: np.array(message.data[s.begin:s.end]) for s in message.spans}
      packet = [by_name.get(name, np.zeros(0)) for name in channels]
      if all(p.size == 0 for p in packet):
        continue

      # Lazily build the decoder once we know sample rate + channel count.
      if decoder is None:
        iv = message.sample_intervals[0] if message.sample_intervals else 0
        fs = 1e9 / iv if iv else 30000.0
        decoder = Decoder(args, len(channels), fs)
        print('[decoder] fs=%.0f Hz  window=%d samples  emit_mode=%s  band=%s'
              % (fs, decoder.window_samples, args.emit_mode,
                 args.band if args.band else 'identity'), flush=True)

      packet_i += 1
      if packet_i % args.decimate != 0:
        continue  # decimate decode rate if requested (silent-cap avoided: rate logged below)

      x, y = decoder.step(packet, t_recv)
      t_decode = time.perf_counter()
      await injector.emit(x, y, int(1e9 * args.window_ms / 1000.0 / max(1, decoder.window_samples)))
      lat.add(t_recv, t_decode, time.perf_counter())
  finally:
    stream.cancel()


async def run_forever(args):
  '''Connect and run; reconnect on core restart / connection loss, mirroring the Rust
  executor's connect_lazy resilience. Lets the orchestrated decoder survive stack
  restarts without a manual relaunch. channel_ready() also blocks (rather than crashing)
  until the core first comes up, so the decoder can be started before the core.'''
  backoff = 1.0
  while True:
    channel = grpc.aio.insecure_channel(args.thalamus)
    try:
      await channel.channel_ready()
      stub = thalamus_pb2_grpc.ThalamusStub(channel)
      injector = Injector(stub, args.target)
      await injector.start()
      print('[decoder] connected to %s; injecting into "%s" (X, Y)'
            % (args.thalamus, args.target), flush=True)
      backoff = 1.0
      if args.loopback:
        await run_loopback(args, injector)
      else:
        await run_neural(args, stub, injector)
      print('[decoder] stream ended cleanly; reconnecting', flush=True)
    except grpc.aio.AioRpcError as e:
      print('[decoder] connection lost (%s); reconnecting in %.0fs' % (e.code(), backoff),
            flush=True)
      await asyncio.sleep(backoff)
      backoff = min(backoff * 2, 10.0)
    except Exception:
      traceback.print_exc()
      await asyncio.sleep(backoff)
      backoff = min(backoff * 2, 10.0)
    finally:
      try:
        await channel.close()
      except Exception:
        pass


async def main():
  parser = argparse.ArgumentParser(description='Closed-loop neural decoder -> joystick cursor')
  parser.add_argument('--thalamus', default='localhost:50050', help='core gRPC address')
  parser.add_argument('--source', default='INTAN', help='analog node to decode from')
  parser.add_argument('--target', default='Decoder', help='ANALOG node to inject X/Y into')
  parser.add_argument('--channels', default='A-001,A-002,A-003,A-004,A-005',
                      help='comma-separated source channel names')
  parser.add_argument('--window-ms', type=float, default=200.0, help='feature window (ms)')
  parser.add_argument('--emit-mode', choices=['position', 'velocity'], default='position',
                      help='position: decoder integrates (task direct mode); '
                           'velocity: task integrates (task cumulative mode)')
  parser.add_argument('--gain', type=float, default=1.0, help='velocity gain')
  parser.add_argument('--leak', type=float, default=0.98,
                      help='position-integrator leak per tick (<1 recenters, prevents drift)')
  parser.add_argument('--band', default='', help='bandpass "low,high" Hz (empty = identity)')
  parser.add_argument('--decimate', type=int, default=1, help='decode every Nth packet')
  parser.add_argument('--log-every', type=int, default=200, help='latency report interval (ticks)')
  parser.add_argument('--loopback', action='store_true',
                      help='ignore neural data; emit a programmatic circle (plumbing test)')
  parser.add_argument('--loopback-freq', type=float, default=0.1, help='loopback circle Hz')
  parser.add_argument('--loopback-amp', type=float, default=0.6, help='loopback amplitude [0..1]')
  args = parser.parse_args()

  args.channels = [c.strip() for c in args.channels.split(',') if c.strip()]
  args.band = tuple(float(x) for x in args.band.split(',')) if args.band else None
  if args.band and not HAVE_SCIPY:
    print('[decoder] WARNING: scipy missing, --band ignored (identity filter)', flush=True)
    args.band = None

  await run_forever(args)


if __name__ == '__main__':
  try:
    asyncio.get_event_loop().run_until_complete(main())
  except KeyboardInterrupt:
    pass
  except Exception:
    traceback.print_exc()
    sys.exit(1)
