import json
import asyncio
import grpc.aio

from thalamus import thalamus_pb2
from thalamus import thalamus_pb2_grpc

async def main():
  async with grpc.aio.insecure_channel('localhost:50050') as channel:
    await channel.channel_ready()

    stub = thalamus_pb2_grpc.ThalamusStub(channel)

    request = thalamus_pb2.AnalogRequest(
      #node=thalamus_pb2.NodeSelector(type='WAVE')
      node=thalamus_pb2.NodeSelector(name='Node 5')
    )
    response = stub.analog(request)
    async for m in response:
      print(m)

asyncio.run(main())
