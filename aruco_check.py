import json
import asyncio
import grpc.aio

import numpy

from thalamus import thalamus_pb2
from thalamus import thalamus_pb2_grpc

async def main():
  async with grpc.aio.insecure_channel('localhost:50050') as channel:
    await channel.channel_ready()

    stub = thalamus_pb2_grpc.ThalamusStub(channel)

    async def stream(node):
      request = thalamus_pb2.NodeSelector(name=node)
      response = stub.xsens(request)
      async for m in response:
        if len(m.segments) < 2:
          print(node, 'XXX')
          continue

        #print(node, m)
        a = numpy.array([m.segments[0].x, m.segments[0].y, m.segments[0].z])
        b = numpy.array([m.segments[1].x, m.segments[1].y, m.segments[1].z])
        print(node, numpy.linalg.norm(a - b))

    await asyncio.gather(stream('Aruco Left'), stream('Aruco Top'))

asyncio.run(main())
