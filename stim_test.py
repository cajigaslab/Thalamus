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
        "amp_uA": 1,
        "pw_us": 2,
        "freq_hz": 3,
        "ipd_ms": 4,
        "num_pulses": 5,
        "stim_dur_s": 6,
        "is_biphasic": True,
        "polarity": 7,
        "dis_dur_s": 8
      })
    )
    response = await stub.node_request(request)
    print(response)

asyncio.run(main())