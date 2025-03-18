from . import util_pb2 as _util_pb2
from google.protobuf.internal import enum_type_wrapper as _enum_type_wrapper
from google.protobuf import descriptor as _descriptor
from google.protobuf import message as _message
from typing import ClassVar as _ClassVar, Mapping as _Mapping, Optional as _Optional, Union as _Union

DESCRIPTOR: _descriptor.FileDescriptor

class Empty(_message.Message):
    __slots__ = ()
    def __init__(self) -> None: ...

class OculomaticRequest(_message.Message):
    __slots__ = ()
    def __init__(self) -> None: ...

class ImagesRequest(_message.Message):
    __slots__ = ("topic",)
    TOPIC_FIELD_NUMBER: _ClassVar[int]
    topic: str
    def __init__(self, topic: _Optional[str] = ...) -> None: ...

class Image(_message.Message):
    __slots__ = ("header", "encoding", "width", "height", "data")
    class Encoding(int, metaclass=_enum_type_wrapper.EnumTypeWrapper):
        __slots__ = ()
        NONE: _ClassVar[Image.Encoding]
        PNG: _ClassVar[Image.Encoding]
        JPEG: _ClassVar[Image.Encoding]
    NONE: Image.Encoding
    PNG: Image.Encoding
    JPEG: Image.Encoding
    HEADER_FIELD_NUMBER: _ClassVar[int]
    ENCODING_FIELD_NUMBER: _ClassVar[int]
    WIDTH_FIELD_NUMBER: _ClassVar[int]
    HEIGHT_FIELD_NUMBER: _ClassVar[int]
    DATA_FIELD_NUMBER: _ClassVar[int]
    header: _util_pb2.Header
    encoding: Image.Encoding
    width: int
    height: int
    data: bytes
    def __init__(self, header: _Optional[_Union[_util_pb2.Header, _Mapping]] = ..., encoding: _Optional[_Union[Image.Encoding, str]] = ..., width: _Optional[int] = ..., height: _Optional[int] = ..., data: _Optional[bytes] = ...) -> None: ...

class Gaze(_message.Message):
    __slots__ = ("header", "x", "y", "og_height", "og_width", "i", "diameter")
    HEADER_FIELD_NUMBER: _ClassVar[int]
    X_FIELD_NUMBER: _ClassVar[int]
    Y_FIELD_NUMBER: _ClassVar[int]
    OG_HEIGHT_FIELD_NUMBER: _ClassVar[int]
    OG_WIDTH_FIELD_NUMBER: _ClassVar[int]
    I_FIELD_NUMBER: _ClassVar[int]
    DIAMETER_FIELD_NUMBER: _ClassVar[int]
    header: _util_pb2.Header
    x: float
    y: float
    og_height: int
    og_width: int
    i: int
    diameter: int
    def __init__(self, header: _Optional[_Union[_util_pb2.Header, _Mapping]] = ..., x: _Optional[float] = ..., y: _Optional[float] = ..., og_height: _Optional[int] = ..., og_width: _Optional[int] = ..., i: _Optional[int] = ..., diameter: _Optional[int] = ...) -> None: ...
