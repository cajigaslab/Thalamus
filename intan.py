import asyncio
import traceback
import grpc
import time

from thalamus import thalamus_pb2
from thalamus import thalamus_pb2_grpc
from thalamus.util import IterableQueue

from matplotlib import pyplot

async def main():
  try:
    channel = grpc.aio.insecure_channel(f'localhost:50050')
    await channel.channel_ready()
    stub = thalamus_pb2_grpc.ThalamusStub(channel)
    analog_queue = IterableQueue()
    request = thalamus_pb2.AnalogRequest(node=thalamus_pb2.NodeSelector(type='INTAN'))
    stream = stub.analog(request)
    #await analog_queue.put(thalamus_pb2.InjectAnalogRequest(node="Fiducial"))
    #elapsed = 0

    start = time.time()
    data = []
    async for message in stream:
       #print(message)
       for span in message.spans:
          if span.name == 'A-000':
             data.extend(message.data[span.begin:span.end])
             break
       if len(data) > 200_000:
          break
       
    stream.cancel()
       
    pyplot.plot(data)
    pyplot.show()
    
  except:
    traceback.print_exc()
    raise


if __name__ == '__main__':
    asyncio.get_event_loop().run_until_complete(main())
