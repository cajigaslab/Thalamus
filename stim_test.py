import json
import asyncio
import grpc.aio

from thalamus import thalamus_pb2
from thalamus import thalamus_pb2_grpc

async def main():
  async with grpc.aio.insecure_channel('localhost:50050') as channel:
    await channel.channel_ready()

    stub = thalamus_pb2_grpc.ThalamusStub(channel)

    request = thalamus_pb2.NodeRequest(
      node="Node 1",
      json=json.dumps({
        'type': 'setup',
        "amp_uA": 100,
        "pw_us": 200,
        "freq_hz": 200,
        "ipd_ms": 0,
        "num_pulses": 1,
        "stim_dur_s": .5,
        "is_biphasic": True,
        "polarity": 1,
        "dis_dur_s": .5
      })
    )
    response = await stub.node_request(request)
    print(response)

asyncio.run(main())