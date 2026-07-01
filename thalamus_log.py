import json
import asyncio
import grpc.aio

import numpy

from thalamus import thalamus_pb2
from thalamus import thalamus_pb2_grpc
from thalamus.iterable_queue import IterableQueue

async def main():
  async with grpc.aio.insecure_channel('localhost:50051') as python_channel, grpc.aio.insecure_channel('localhost:50050') as cpp_channel:
    await python_channel.channel_ready()
    await cpp_channel.channel_ready()

    stub = thalamus_pb2_grpc.ThalamusStub(cpp_channel)

    response = stub.logout(thalamus_pb2.Empty())
    async for m in response:
      print(m)

    #response = stub.observable_bridge_v2(IterableQueue())
    #async for m in response:
    #  print(m)

asyncio.run(main())
