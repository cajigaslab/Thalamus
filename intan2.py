import asyncio
import traceback
import grpc
import time
import json

from thalamus import thalamus_pb2
from thalamus import thalamus_pb2_grpc
from thalamus.util import IterableQueue
from prompt_toolkit import PromptSession
from prompt_toolkit.patch_stdout import patch_stdout

async def main():
  try:
    session = PromptSession()
    channel = grpc.aio.insecure_channel(f'localhost:50050')
    await channel.channel_ready()
    stub = thalamus_pb2_grpc.ThalamusStub(channel)
    analog_queue = IterableQueue()
    events_call = stub.node_request_stream(analog_queue)
    await analog_queue.put(thalamus_pb2.NodeRequest(node="intan2"))
    elapsed = 0

    while True:
        with patch_stdout():
          command = await session.prompt_async("> ")
        await analog_queue.put(thalamus_pb2.NodeRequest(node="intan2", json=json.dumps(command)))
  except:
    traceback.print_exc()
    raise


if __name__ == '__main__':
    asyncio.run(main())
