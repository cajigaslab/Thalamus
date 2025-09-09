'''
Python implementation of "native thalamus".  Currently does as little as possible, just allows the task controller to run.
'''
import asyncio
import typing

import grpc
from .. import thalamus_pb2
from .. import thalamus_pb2_grpc

class PipelineServicer(thalamus_pb2_grpc.ThalamusServicer):
  def __init__(self):
    super().__init__()

  def get_type_name(self, request: thalamus_pb2.StringMessage, context: grpc.ServicerContext):
    return thalamus_pb2.StringMessage(value=request.value)

  async def log(self, stream: typing.AsyncIterable[thalamus_pb2.Text], context: grpc.ServicerContext):
    async for text in stream:
      print('Pipeline', text.text)

  async def analog(self, request: thalamus_pb2.AnalogRequest, context: grpc.ServicerContext):
    print('Pipeline', request)
    while True:
      await asyncio.sleep(1)

  async def inject_analog(self, stream: typing.AsyncIterable[thalamus_pb2.Text], context: grpc.ServicerContext):
    async for text in stream:
      print(text)
      pass