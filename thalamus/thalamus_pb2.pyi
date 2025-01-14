from . import util_pb2 as _util_pb2
from google.protobuf.internal import containers as _containers
from google.protobuf.internal import enum_type_wrapper as _enum_type_wrapper
from google.protobuf import descriptor as _descriptor
from google.protobuf import message as _message
from typing import ClassVar as _ClassVar, Iterable as _Iterable, Mapping as _Mapping, Optional as _Optional, Union as _Union

DESCRIPTOR: _descriptor.FileDescriptor

class Modalities(int, metaclass=_enum_type_wrapper.EnumTypeWrapper):
    __slots__ = ()
    AnalogModality: _ClassVar[Modalities]
    MocapModality: _ClassVar[Modalities]
    ImageModality: _ClassVar[Modalities]
    TextModality: _ClassVar[Modalities]
AnalogModality: Modalities
MocapModality: Modalities
ImageModality: Modalities
TextModality: Modalities

class Error(_message.Message):
    __slots__ = ("code", "message")
    CODE_FIELD_NUMBER: _ClassVar[int]
    MESSAGE_FIELD_NUMBER: _ClassVar[int]
    code: int
    message: str
    def __init__(self, code: _Optional[int] = ..., message: _Optional[str] = ...) -> None: ...

class StimDeclaration(_message.Message):
    __slots__ = ("data", "trigger", "id")
    DATA_FIELD_NUMBER: _ClassVar[int]
    TRIGGER_FIELD_NUMBER: _ClassVar[int]
    ID_FIELD_NUMBER: _ClassVar[int]
    data: AnalogResponse
    trigger: str
    id: int
    def __init__(self, data: _Optional[_Union[AnalogResponse, _Mapping]] = ..., trigger: _Optional[str] = ..., id: _Optional[int] = ...) -> None: ...

class StimRequest(_message.Message):
    __slots__ = ("node", "declaration", "arm", "trigger", "retrieve", "id")
    NODE_FIELD_NUMBER: _ClassVar[int]
    DECLARATION_FIELD_NUMBER: _ClassVar[int]
    ARM_FIELD_NUMBER: _ClassVar[int]
    TRIGGER_FIELD_NUMBER: _ClassVar[int]
    RETRIEVE_FIELD_NUMBER: _ClassVar[int]
    ID_FIELD_NUMBER: _ClassVar[int]
    node: NodeSelector
    declaration: StimDeclaration
    arm: int
    trigger: int
    retrieve: int
    id: int
    def __init__(self, node: _Optional[_Union[NodeSelector, _Mapping]] = ..., declaration: _Optional[_Union[StimDeclaration, _Mapping]] = ..., arm: _Optional[int] = ..., trigger: _Optional[int] = ..., retrieve: _Optional[int] = ..., id: _Optional[int] = ...) -> None: ...

class StimResponse(_message.Message):
    __slots__ = ("error", "declaration", "id")
    ERROR_FIELD_NUMBER: _ClassVar[int]
    DECLARATION_FIELD_NUMBER: _ClassVar[int]
    ID_FIELD_NUMBER: _ClassVar[int]
    error: Error
    declaration: StimDeclaration
    id: int
    def __init__(self, error: _Optional[_Union[Error, _Mapping]] = ..., declaration: _Optional[_Union[StimDeclaration, _Mapping]] = ..., id: _Optional[int] = ...) -> None: ...

class ObservableReadRequest(_message.Message):
    __slots__ = ("peer_name",)
    PEER_NAME_FIELD_NUMBER: _ClassVar[int]
    peer_name: str
    def __init__(self, peer_name: _Optional[str] = ...) -> None: ...

class ChannelId(_message.Message):
    __slots__ = ("name", "index")
    NAME_FIELD_NUMBER: _ClassVar[int]
    INDEX_FIELD_NUMBER: _ClassVar[int]
    name: str
    index: int
    def __init__(self, name: _Optional[str] = ..., index: _Optional[int] = ...) -> None: ...

class Spectrogram(_message.Message):
    __slots__ = ("channel", "data", "max_frequency")
    CHANNEL_FIELD_NUMBER: _ClassVar[int]
    DATA_FIELD_NUMBER: _ClassVar[int]
    MAX_FREQUENCY_FIELD_NUMBER: _ClassVar[int]
    channel: ChannelId
    data: _containers.RepeatedScalarFieldContainer[float]
    max_frequency: float
    def __init__(self, channel: _Optional[_Union[ChannelId, _Mapping]] = ..., data: _Optional[_Iterable[float]] = ..., max_frequency: _Optional[float] = ...) -> None: ...

class SpectrogramRequest(_message.Message):
    __slots__ = ("node", "channels", "window_s", "hop_s")
    NODE_FIELD_NUMBER: _ClassVar[int]
    CHANNELS_FIELD_NUMBER: _ClassVar[int]
    WINDOW_S_FIELD_NUMBER: _ClassVar[int]
    HOP_S_FIELD_NUMBER: _ClassVar[int]
    node: NodeSelector
    channels: _containers.RepeatedCompositeFieldContainer[ChannelId]
    window_s: float
    hop_s: float
    def __init__(self, node: _Optional[_Union[NodeSelector, _Mapping]] = ..., channels: _Optional[_Iterable[_Union[ChannelId, _Mapping]]] = ..., window_s: _Optional[float] = ..., hop_s: _Optional[float] = ...) -> None: ...

class SpectrogramResponse(_message.Message):
    __slots__ = ("spectrograms",)
    SPECTROGRAMS_FIELD_NUMBER: _ClassVar[int]
    spectrograms: _containers.RepeatedCompositeFieldContainer[Spectrogram]
    def __init__(self, spectrograms: _Optional[_Iterable[_Union[Spectrogram, _Mapping]]] = ...) -> None: ...

class NodeSelector(_message.Message):
    __slots__ = ("name", "type")
    NAME_FIELD_NUMBER: _ClassVar[int]
    TYPE_FIELD_NUMBER: _ClassVar[int]
    name: str
    type: str
    def __init__(self, name: _Optional[str] = ..., type: _Optional[str] = ...) -> None: ...

class NodeRequest(_message.Message):
    __slots__ = ("node", "json", "id")
    NODE_FIELD_NUMBER: _ClassVar[int]
    JSON_FIELD_NUMBER: _ClassVar[int]
    ID_FIELD_NUMBER: _ClassVar[int]
    node: str
    json: str
    id: int
    def __init__(self, node: _Optional[str] = ..., json: _Optional[str] = ..., id: _Optional[int] = ...) -> None: ...

class NodeResponse(_message.Message):
    __slots__ = ("json", "id", "status")
    class Status(int, metaclass=_enum_type_wrapper.EnumTypeWrapper):
        __slots__ = ()
        OK: _ClassVar[NodeResponse.Status]
        NOT_FOUND: _ClassVar[NodeResponse.Status]
    OK: NodeResponse.Status
    NOT_FOUND: NodeResponse.Status
    JSON_FIELD_NUMBER: _ClassVar[int]
    ID_FIELD_NUMBER: _ClassVar[int]
    STATUS_FIELD_NUMBER: _ClassVar[int]
    json: str
    id: int
    status: NodeResponse.Status
    def __init__(self, json: _Optional[str] = ..., id: _Optional[int] = ..., status: _Optional[_Union[NodeResponse.Status, str]] = ...) -> None: ...

class Text(_message.Message):
    __slots__ = ("text", "time")
    TEXT_FIELD_NUMBER: _ClassVar[int]
    TIME_FIELD_NUMBER: _ClassVar[int]
    text: str
    time: int
    def __init__(self, text: _Optional[str] = ..., time: _Optional[int] = ...) -> None: ...

class StorageRecord(_message.Message):
    __slots__ = ("analog", "xsens", "event", "image", "text", "time", "node")
    ANALOG_FIELD_NUMBER: _ClassVar[int]
    XSENS_FIELD_NUMBER: _ClassVar[int]
    EVENT_FIELD_NUMBER: _ClassVar[int]
    IMAGE_FIELD_NUMBER: _ClassVar[int]
    TEXT_FIELD_NUMBER: _ClassVar[int]
    TIME_FIELD_NUMBER: _ClassVar[int]
    NODE_FIELD_NUMBER: _ClassVar[int]
    analog: AnalogResponse
    xsens: XsensResponse
    event: Event
    image: Image
    text: Text
    time: int
    node: str
    def __init__(self, analog: _Optional[_Union[AnalogResponse, _Mapping]] = ..., xsens: _Optional[_Union[XsensResponse, _Mapping]] = ..., event: _Optional[_Union[Event, _Mapping]] = ..., image: _Optional[_Union[Image, _Mapping]] = ..., text: _Optional[_Union[Text, _Mapping]] = ..., time: _Optional[int] = ..., node: _Optional[str] = ...) -> None: ...

class ImageRequest(_message.Message):
    __slots__ = ("node", "framerate")
    NODE_FIELD_NUMBER: _ClassVar[int]
    FRAMERATE_FIELD_NUMBER: _ClassVar[int]
    node: NodeSelector
    framerate: float
    def __init__(self, node: _Optional[_Union[NodeSelector, _Mapping]] = ..., framerate: _Optional[float] = ...) -> None: ...

class Image(_message.Message):
    __slots__ = ("data", "width", "height", "format", "frame_interval", "last", "bigendian")
    class Format(int, metaclass=_enum_type_wrapper.EnumTypeWrapper):
        __slots__ = ()
        Gray: _ClassVar[Image.Format]
        RGB: _ClassVar[Image.Format]
        YUYV422: _ClassVar[Image.Format]
        YUV420P: _ClassVar[Image.Format]
        YUVJ420P: _ClassVar[Image.Format]
        Gray16: _ClassVar[Image.Format]
        RGB16: _ClassVar[Image.Format]
    Gray: Image.Format
    RGB: Image.Format
    YUYV422: Image.Format
    YUV420P: Image.Format
    YUVJ420P: Image.Format
    Gray16: Image.Format
    RGB16: Image.Format
    DATA_FIELD_NUMBER: _ClassVar[int]
    WIDTH_FIELD_NUMBER: _ClassVar[int]
    HEIGHT_FIELD_NUMBER: _ClassVar[int]
    FORMAT_FIELD_NUMBER: _ClassVar[int]
    FRAME_INTERVAL_FIELD_NUMBER: _ClassVar[int]
    LAST_FIELD_NUMBER: _ClassVar[int]
    BIGENDIAN_FIELD_NUMBER: _ClassVar[int]
    data: _containers.RepeatedScalarFieldContainer[bytes]
    width: int
    height: int
    format: Image.Format
    frame_interval: int
    last: bool
    bigendian: bool
    def __init__(self, data: _Optional[_Iterable[bytes]] = ..., width: _Optional[int] = ..., height: _Optional[int] = ..., format: _Optional[_Union[Image.Format, str]] = ..., frame_interval: _Optional[int] = ..., last: bool = ..., bigendian: bool = ...) -> None: ...

class Ping(_message.Message):
    __slots__ = ("id", "payload")
    ID_FIELD_NUMBER: _ClassVar[int]
    PAYLOAD_FIELD_NUMBER: _ClassVar[int]
    id: int
    payload: bytes
    def __init__(self, id: _Optional[int] = ..., payload: _Optional[bytes] = ...) -> None: ...

class Pong(_message.Message):
    __slots__ = ("id", "payload")
    ID_FIELD_NUMBER: _ClassVar[int]
    PAYLOAD_FIELD_NUMBER: _ClassVar[int]
    id: int
    payload: bytes
    def __init__(self, id: _Optional[int] = ..., payload: _Optional[bytes] = ...) -> None: ...

class RemoteNodeMessage(_message.Message):
    __slots__ = ("request", "data", "ping", "pong")
    REQUEST_FIELD_NUMBER: _ClassVar[int]
    DATA_FIELD_NUMBER: _ClassVar[int]
    PING_FIELD_NUMBER: _ClassVar[int]
    PONG_FIELD_NUMBER: _ClassVar[int]
    request: AnalogRequest
    data: AnalogResponse
    ping: Ping
    pong: Pong
    def __init__(self, request: _Optional[_Union[AnalogRequest, _Mapping]] = ..., data: _Optional[_Union[AnalogResponse, _Mapping]] = ..., ping: _Optional[_Union[Ping, _Mapping]] = ..., pong: _Optional[_Union[Pong, _Mapping]] = ...) -> None: ...

class ReplayRequest(_message.Message):
    __slots__ = ("filename", "nodes")
    FILENAME_FIELD_NUMBER: _ClassVar[int]
    NODES_FIELD_NUMBER: _ClassVar[int]
    filename: str
    nodes: _containers.RepeatedScalarFieldContainer[str]
    def __init__(self, filename: _Optional[str] = ..., nodes: _Optional[_Iterable[str]] = ...) -> None: ...

class XsensSegment(_message.Message):
    __slots__ = ("id", "x", "y", "z", "q0", "q1", "q2", "q3", "frame", "time", "actor")
    ID_FIELD_NUMBER: _ClassVar[int]
    X_FIELD_NUMBER: _ClassVar[int]
    Y_FIELD_NUMBER: _ClassVar[int]
    Z_FIELD_NUMBER: _ClassVar[int]
    Q0_FIELD_NUMBER: _ClassVar[int]
    Q1_FIELD_NUMBER: _ClassVar[int]
    Q2_FIELD_NUMBER: _ClassVar[int]
    Q3_FIELD_NUMBER: _ClassVar[int]
    FRAME_FIELD_NUMBER: _ClassVar[int]
    TIME_FIELD_NUMBER: _ClassVar[int]
    ACTOR_FIELD_NUMBER: _ClassVar[int]
    id: int
    x: float
    y: float
    z: float
    q0: float
    q1: float
    q2: float
    q3: float
    frame: int
    time: int
    actor: int
    def __init__(self, id: _Optional[int] = ..., x: _Optional[float] = ..., y: _Optional[float] = ..., z: _Optional[float] = ..., q0: _Optional[float] = ..., q1: _Optional[float] = ..., q2: _Optional[float] = ..., q3: _Optional[float] = ..., frame: _Optional[int] = ..., time: _Optional[int] = ..., actor: _Optional[int] = ...) -> None: ...

class XsensResponse(_message.Message):
    __slots__ = ("segments", "pose_name")
    SEGMENTS_FIELD_NUMBER: _ClassVar[int]
    POSE_NAME_FIELD_NUMBER: _ClassVar[int]
    segments: _containers.RepeatedCompositeFieldContainer[XsensSegment]
    pose_name: str
    def __init__(self, segments: _Optional[_Iterable[_Union[XsensSegment, _Mapping]]] = ..., pose_name: _Optional[str] = ...) -> None: ...

class StringMessage(_message.Message):
    __slots__ = ("value",)
    VALUE_FIELD_NUMBER: _ClassVar[int]
    value: str
    def __init__(self, value: _Optional[str] = ...) -> None: ...

class ModalitiesMessage(_message.Message):
    __slots__ = ("values",)
    VALUES_FIELD_NUMBER: _ClassVar[int]
    values: _containers.RepeatedScalarFieldContainer[Modalities]
    def __init__(self, values: _Optional[_Iterable[_Union[Modalities, str]]] = ...) -> None: ...

class StringListMessage(_message.Message):
    __slots__ = ("value",)
    VALUE_FIELD_NUMBER: _ClassVar[int]
    value: _containers.RepeatedScalarFieldContainer[str]
    def __init__(self, value: _Optional[_Iterable[str]] = ...) -> None: ...

class EvalResponse(_message.Message):
    __slots__ = ("value", "id")
    VALUE_FIELD_NUMBER: _ClassVar[int]
    ID_FIELD_NUMBER: _ClassVar[int]
    value: str
    id: int
    def __init__(self, value: _Optional[str] = ..., id: _Optional[int] = ...) -> None: ...

class EvalRequest(_message.Message):
    __slots__ = ("code", "id")
    CODE_FIELD_NUMBER: _ClassVar[int]
    ID_FIELD_NUMBER: _ClassVar[int]
    code: str
    id: int
    def __init__(self, code: _Optional[str] = ..., id: _Optional[int] = ...) -> None: ...

class AnalogRequest(_message.Message):
    __slots__ = ("node", "channels", "channel_names")
    NODE_FIELD_NUMBER: _ClassVar[int]
    CHANNELS_FIELD_NUMBER: _ClassVar[int]
    CHANNEL_NAMES_FIELD_NUMBER: _ClassVar[int]
    node: NodeSelector
    channels: _containers.RepeatedScalarFieldContainer[int]
    channel_names: _containers.RepeatedScalarFieldContainer[str]
    def __init__(self, node: _Optional[_Union[NodeSelector, _Mapping]] = ..., channels: _Optional[_Iterable[int]] = ..., channel_names: _Optional[_Iterable[str]] = ...) -> None: ...

class InjectAnalogRequest(_message.Message):
    __slots__ = ("node", "signal")
    NODE_FIELD_NUMBER: _ClassVar[int]
    SIGNAL_FIELD_NUMBER: _ClassVar[int]
    node: str
    signal: AnalogResponse
    def __init__(self, node: _Optional[str] = ..., signal: _Optional[_Union[AnalogResponse, _Mapping]] = ...) -> None: ...

class InjectTextRequest(_message.Message):
    __slots__ = ("node", "text")
    NODE_FIELD_NUMBER: _ClassVar[int]
    TEXT_FIELD_NUMBER: _ClassVar[int]
    node: str
    text: Text
    def __init__(self, node: _Optional[str] = ..., text: _Optional[_Union[Text, _Mapping]] = ...) -> None: ...

class AnalogResponse(_message.Message):
    __slots__ = ("data", "spans", "sample_intervals", "channels_changed")
    DATA_FIELD_NUMBER: _ClassVar[int]
    SPANS_FIELD_NUMBER: _ClassVar[int]
    SAMPLE_INTERVALS_FIELD_NUMBER: _ClassVar[int]
    CHANNELS_CHANGED_FIELD_NUMBER: _ClassVar[int]
    data: _containers.RepeatedScalarFieldContainer[float]
    spans: _containers.RepeatedCompositeFieldContainer[Span]
    sample_intervals: _containers.RepeatedScalarFieldContainer[int]
    channels_changed: bool
    def __init__(self, data: _Optional[_Iterable[float]] = ..., spans: _Optional[_Iterable[_Union[Span, _Mapping]]] = ..., sample_intervals: _Optional[_Iterable[int]] = ..., channels_changed: bool = ...) -> None: ...

class GraphRequest(_message.Message):
    __slots__ = ("node", "channels", "bin_ns", "channel_names")
    NODE_FIELD_NUMBER: _ClassVar[int]
    CHANNELS_FIELD_NUMBER: _ClassVar[int]
    BIN_NS_FIELD_NUMBER: _ClassVar[int]
    CHANNEL_NAMES_FIELD_NUMBER: _ClassVar[int]
    node: NodeSelector
    channels: _containers.RepeatedScalarFieldContainer[int]
    bin_ns: int
    channel_names: _containers.RepeatedScalarFieldContainer[str]
    def __init__(self, node: _Optional[_Union[NodeSelector, _Mapping]] = ..., channels: _Optional[_Iterable[int]] = ..., bin_ns: _Optional[int] = ..., channel_names: _Optional[_Iterable[str]] = ...) -> None: ...

class Span(_message.Message):
    __slots__ = ("begin", "end", "name")
    BEGIN_FIELD_NUMBER: _ClassVar[int]
    END_FIELD_NUMBER: _ClassVar[int]
    NAME_FIELD_NUMBER: _ClassVar[int]
    begin: int
    end: int
    name: str
    def __init__(self, begin: _Optional[int] = ..., end: _Optional[int] = ..., name: _Optional[str] = ...) -> None: ...

class GraphResponse(_message.Message):
    __slots__ = ("bins", "spans", "channels_changed")
    BINS_FIELD_NUMBER: _ClassVar[int]
    SPANS_FIELD_NUMBER: _ClassVar[int]
    CHANNELS_CHANGED_FIELD_NUMBER: _ClassVar[int]
    bins: _containers.RepeatedScalarFieldContainer[float]
    spans: _containers.RepeatedCompositeFieldContainer[Span]
    channels_changed: bool
    def __init__(self, bins: _Optional[_Iterable[float]] = ..., spans: _Optional[_Iterable[_Union[Span, _Mapping]]] = ..., channels_changed: bool = ...) -> None: ...

class Event(_message.Message):
    __slots__ = ("payload", "time")
    PAYLOAD_FIELD_NUMBER: _ClassVar[int]
    TIME_FIELD_NUMBER: _ClassVar[int]
    payload: bytes
    time: int
    def __init__(self, payload: _Optional[bytes] = ..., time: _Optional[int] = ...) -> None: ...

class ObservableChange(_message.Message):
    __slots__ = ("address", "value", "action", "id", "acknowledged")
    class Action(int, metaclass=_enum_type_wrapper.EnumTypeWrapper):
        __slots__ = ()
        Set: _ClassVar[ObservableChange.Action]
        Delete: _ClassVar[ObservableChange.Action]
    Set: ObservableChange.Action
    Delete: ObservableChange.Action
    ADDRESS_FIELD_NUMBER: _ClassVar[int]
    VALUE_FIELD_NUMBER: _ClassVar[int]
    ACTION_FIELD_NUMBER: _ClassVar[int]
    ID_FIELD_NUMBER: _ClassVar[int]
    ACKNOWLEDGED_FIELD_NUMBER: _ClassVar[int]
    address: str
    value: str
    action: ObservableChange.Action
    id: int
    acknowledged: int
    def __init__(self, address: _Optional[str] = ..., value: _Optional[str] = ..., action: _Optional[_Union[ObservableChange.Action, str]] = ..., id: _Optional[int] = ..., acknowledged: _Optional[int] = ...) -> None: ...

class ObservableTransaction(_message.Message):
    __slots__ = ("changes", "id", "acknowledged", "redirection", "peer_name")
    CHANGES_FIELD_NUMBER: _ClassVar[int]
    ID_FIELD_NUMBER: _ClassVar[int]
    ACKNOWLEDGED_FIELD_NUMBER: _ClassVar[int]
    REDIRECTION_FIELD_NUMBER: _ClassVar[int]
    PEER_NAME_FIELD_NUMBER: _ClassVar[int]
    changes: _containers.RepeatedCompositeFieldContainer[ObservableChange]
    id: int
    acknowledged: int
    redirection: str
    peer_name: str
    def __init__(self, changes: _Optional[_Iterable[_Union[ObservableChange, _Mapping]]] = ..., id: _Optional[int] = ..., acknowledged: _Optional[int] = ..., redirection: _Optional[str] = ..., peer_name: _Optional[str] = ...) -> None: ...

class Notification(_message.Message):
    __slots__ = ("type", "title", "message")
    class Type(int, metaclass=_enum_type_wrapper.EnumTypeWrapper):
        __slots__ = ()
        Error: _ClassVar[Notification.Type]
        Warning: _ClassVar[Notification.Type]
        Info: _ClassVar[Notification.Type]
    Error: Notification.Type
    Warning: Notification.Type
    Info: Notification.Type
    TYPE_FIELD_NUMBER: _ClassVar[int]
    TITLE_FIELD_NUMBER: _ClassVar[int]
    MESSAGE_FIELD_NUMBER: _ClassVar[int]
    type: Notification.Type
    title: str
    message: str
    def __init__(self, type: _Optional[_Union[Notification.Type, str]] = ..., title: _Optional[str] = ..., message: _Optional[str] = ...) -> None: ...
