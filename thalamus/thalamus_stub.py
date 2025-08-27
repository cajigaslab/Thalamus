import typing
import asyncio
from . import thalamus_pb2
from . import thalamus_pb2_grpc
from .task_controller.util import create_task_with_exc_handling

import grpc.aio

class CancelableQueue:
  def __init__(self, cancel_callback: typing.Callable[[], None]):
    self.queue = asyncio.Queue()
    self.sentinel = object()
    self.cancel_sentinel = object()
    self.cancel_callback = cancel_callback

  def cancel(self):
    self.cancel_callback()
    self.queue.put(self.cancel_sentinel)

  def put(self, item):
    return self.queue.put(item)
  
  def close(self):
    return self.queue.put(self.sentinel)

  def join(self):
    return self.queue.join()

  def __aiter__(self):
    return self

  async def __anext__(self):
    try:
      item = await self.queue.get()
    except asyncio.CancelledError:
      raise StopAsyncIteration

    self.queue.task_done()
    if item is self.sentinel:
      raise StopAsyncIteration
    if item is self.cancel_sentinel:
      raise asyncio.CancelledError()
    return item
  
  async def __aenter__(self):
    return self

  async def __aexit__(self, *exc):
    await self.close()
    await self.join()
    return False

class ThalamusStub():
  def __init__(self, stub):
    self.stub = stub
    self.redirects = {}

  async def get_redirect_stub(self, location) -> thalamus_pb2_grpc.ThalamusStub:
    if location not in self.redirects:
      channel = grpc.aio.insecure_channel(location)
      await channel.channel_ready()
      self.redirects[location] = thalamus_pb2_grpc.ThalamusStub(channel)
    return self.redirects[location]
  
  async def check_redirect(self) -> thalamus_pb2_grpc.ThalamusStub:
    redirect = await self.stub.get_redirect()
    if redirect.redirect:
      return await self.get_redirect_stub(redirect.redirect)
    return self.stub

  def observable_bridge(self, request: typing.AsyncIterable[thalamus_pb2.ObservableChange]) -> typing.AsyncIterable[thalamus_pb2.ObservableChange]:
    return self.check_redirect().observable_bridge(request)

  def observable_bridge_v2(self, request: typing.AsyncIterable[thalamus_pb2.ObservableTransaction]) -> typing.AsyncIterable[thalamus_pb2.ObservableTransaction]:
    return self.check_redirect().observable_bridge_v2(request)
    
  def observable_bridge_read(self, request: thalamus_pb2.ObservableReadRequest) -> typing.AsyncIterable[thalamus_pb2.ObservableTransaction]:
    return self.check_redirect().observable_bridge_read(request)
    
  def observable_bridge_write(self, request: thalamus_pb2.ObservableTransaction) -> thalamus_pb2.Empty:
    return self.check_redirect().observable_bridge_write(request)
  
  def __stream(self, stream_func):
    stream = stream_func(self.stub)

    result = CancelableQueue(lambda: stream.cancel())

    async def func():
      nonlocal stream
      running = True
      while running:
        new_stream = False
        async for m in stream:
          if m.redirect:
            stub = await self.get_redirect_stub(m.redirect)
            stream.cancel()
            stream = stream_func(stub)
            new_stream = True
            break

          await result.put(m)
        
        if new_stream:
          continue

        running = False
        result.close()

    create_task_with_exc_handling(func())
    return result

  def analog(self, request: thalamus_pb2.AnalogRequest) -> typing.AsyncIterable[thalamus_pb2.AnalogResponse]:
    return self.__stream(lambda stub: stub.analog(request))

  def channel_info(self, request: thalamus_pb2.AnalogRequest) -> typing.AsyncIterable[thalamus_pb2.AnalogResponse]:
    return self.__stream(lambda stub: stub.channel_info(request))

  def graph(self, request: thalamus_pb2.GraphRequest) -> typing.AsyncIterable[thalamus_pb2.GraphResponse]:
    return self.__stream(lambda stub: stub.graph(request))

  def image(self, request: thalamus_pb2.ImageRequest) -> typing.AsyncIterable[thalamus_pb2.Image]:
    return self.stub.image(request)
  def xsens(self, request: thalamus_pb2.NodeSelector) -> typing.AsyncIterable[thalamus_pb2.XsensResponse]:
    return self.stub.xsens(request)
  def events(self, request: typing.AsyncIterable[thalamus_pb2.Event]) -> thalamus_pb2.Empty:
    return self.stub.events(request)
  def log(self, request: typing.AsyncIterable[thalamus_pb2.Text]) -> thalamus_pb2.Empty:
    return self.stub.log(request)
  def spectrogram(self, request: thalamus_pb2.SpectrogramRequest) -> typing.AsyncIterable[thalamus_pb2.SpectrogramResponse]:
    return self.stub.spectrogram(request)
  def get_type_name(self, request: thalamus_pb2.StringMessage) -> thalamus_pb2.StringMessage:
    return self.stub.get_type_name(request)
  def replay(self, request: thalamus_pb2.ReplayRequest) -> thalamus_pb2.Empty:
    return self.stub.replay(request)
  def notification (self, request: thalamus_pb2.Empty) -> typing.AsyncIterable[thalamus_pb2.Notification]:
    return self.stub.notification(request)
  
  async def node_request (self, request: thalamus_pb2.NodeRequest) -> thalamus_pb2.NodeResponse:
    result = await self.stub.node_request(request)
    if result.redirect:
      stub = await self.get_redirect_stub(result.redirect)
      result = stub.node_request(request)
      return result
    else:
      return result
  
  def node_request_stream (self, request: typing.AsyncIterable[thalamus_pb2.NodeRequest]) -> typing.AsyncIterable[thalamus_pb2.NodeResponse]:
    return self.__stream(lambda stub: stub.node_request_stream(request))
  
  def inject_analog(self, request: typing.AsyncIterable[thalamus_pb2.InjectAnalogRequest]) -> thalamus_pb2.Empty:
    return self.stub.inject_analog(request)
  def get_modalities(self, request: thalamus_pb2.NodeSelector) -> thalamus_pb2.ModalitiesMessage:
    return self.stub.get_modalities(request)
  def ping(self, request: typing.AsyncIterable[thalamus_pb2.Ping]) -> typing.AsyncIterable[thalamus_pb2.Pong]:
    return self.stub.ping(request)

  def text(self, request: thalamus_pb2.TextRequest) -> typing.AsyncIterable[thalamus_pb2.Text]:
    return self.__stream(lambda stub: stub.text(request))
  
  def inject_text(self, request: typing.AsyncIterable[thalamus_pb2.InjectTextRequest]) -> typing.AsyncIterable[thalamus_pb2.Pong]:
    return self.stub.inject_text(request)
  def stim(self, request: typing.AsyncIterable[thalamus_pb2.StimRequest]) -> typing.AsyncIterable[thalamus_pb2.StimResponse]:
    return self.stub.stim(request)
