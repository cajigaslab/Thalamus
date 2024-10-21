import typing

from . import thalamus_pb2
from . import util_pb2

class ThalamusStub:
  def channel_info(self, request: thalamus_pb2.AnalogRequest) -> typing.AsyncIterable[thalamus_pb2.AnalogResponse]: ...
  def analog(self, request: thalamus_pb2.AnalogRequest) -> typing.AsyncIterable[thalamus_pb2.AnalogResponse]: ...
  def image(self, request: thalamus_pb2.ImageRequest) -> typing.AsyncIterable[thalamus_pb2.Image]: ...
  def xsens(self, request: thalamus_pb2.NodeSelector) -> typing.AsyncIterable[thalamus_pb2.XsensResponse]: ...
  async def events(self, request: typing.AsyncIterable[thalamus_pb2.Event]) -> util_pb2.Empty: ...
  async def log(self, request: typing.AsyncIterable[thalamus_pb2.Text]) -> util_pb2.Empty: ...
  def observable_bridge(self, request: typing.AsyncIterable[thalamus_pb2.ObservableChange]) -> typing.AsyncIterable[thalamus_pb2.ObservableChange]: ...
  def observable_bridge_v2(self, request: typing.AsyncIterable[thalamus_pb2.ObservableTransaction]) -> typing.AsyncIterable[thalamus_pb2.ObservableTransaction]: ...
  def observable_bridge_read(self, request: thalamus_pb2.ObservableReadRequest) -> typing.AsyncIterable[thalamus_pb2.ObservableTransaction]: ...
  async def observable_bridge_write(self, request: thalamus_pb2.ObservableTransaction) -> util_pb2.Empty: ...
  def graph(self, request: thalamus_pb2.GraphRequest) -> typing.AsyncIterable[thalamus_pb2.GraphResponse]: ...
  def spectrogram(self, request: thalamus_pb2.SpectrogramRequest) -> typing.AsyncIterable[thalamus_pb2.SpectrogramResponse]: ...
  async def get_type_name(self, request: thalamus_pb2.StringMessage) -> thalamus_pb2.StringMessage: ...
  async def replay(self, request: thalamus_pb2.ReplayRequest) -> util_pb2.Empty: ...
  def notification (self, request: util_pb2.Empty) -> typing.AsyncIterable[thalamus_pb2.Notification]: ...
  async def node_request (self, request: thalamus_pb2.NodeRequest) -> thalamus_pb2.NodeResponse: ...
  def node_request_stream (self, request: typing.AsyncIterable[thalamus_pb2.NodeRequest]) -> typing.AsyncIterable[thalamus_pb2.NodeResponse]: ...
  async def inject_analog(self, request: typing.AsyncIterable[thalamus_pb2.InjectAnalogRequest]) -> util_pb2.Empty: ...
  async def get_modalities(self, request: thalamus_pb2.NodeSelector) -> thalamus_pb2.ModalitiesMessage: ...
  def ping(self, request: typing.AsyncIterable[thalamus_pb2.Ping]) -> typing.AsyncIterable[thalamus_pb2.Pong]: ...
  def inject_text(self, request: typing.AsyncIterable[thalamus_pb2.InjectTextRequest]) -> typing.AsyncIterable[thalamus_pb2.Pong]: ...