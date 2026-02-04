import asyncio
import traceback
import grpc
import time

from thalamus import thalamus_pb2
from thalamus import thalamus_pb2_grpc
from thalamus.util import IterableQueue

async def main():
  try:
    channel = grpc.aio.insecure_channel(f'localhost:50050')
    await channel.channel_ready()
    stub = thalamus_pb2_grpc.ThalamusStub(channel)
    analog_queue = IterableQueue()
    events_call = stub.inject_analog(analog_queue)
    await analog_queue.put(thalamus_pb2.InjectAnalogRequest(node="Fiducial"))
    elapsed = 0

    while True:
        if elapsed < 3:
          await asyncio.sleep(3 - elapsed)
        start = time.perf_counter()
        print('Fiducial')
        await analog_queue.put(thalamus_pb2.InjectAnalogRequest(signal=thalamus_pb2.AnalogResponse(
            data=[5, 0],
            spans=[thalamus_pb2.Span(begin=0,end=2)],
            sample_intervals=[100000000])))
        stop = time.perf_counter()
        elapsed = stop - start
  except:
    traceback.print_exc()
    raise


if __name__ == '__main__':
    asyncio.get_event_loop().run_until_complete(main())
