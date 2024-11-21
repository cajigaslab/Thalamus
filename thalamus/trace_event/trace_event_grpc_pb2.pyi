from google.protobuf.internal import containers as _containers
from google.protobuf.internal import enum_type_wrapper as _enum_type_wrapper
from google.protobuf import descriptor as _descriptor
from google.protobuf import message as _message
from typing import ClassVar as _ClassVar, Iterable as _Iterable, Mapping as _Mapping, Optional as _Optional, Union as _Union

DESCRIPTOR: _descriptor.FileDescriptor

class TraceEventArg(_message.Message):
    __slots__ = ("key", "type", "a_double", "a_int32", "a_uint32", "a_int64", "a_uint64", "a_bool", "a_string")
    class Type(int, metaclass=_enum_type_wrapper.EnumTypeWrapper):
        __slots__ = ()
        DOUBLE: _ClassVar[TraceEventArg.Type]
        INT32: _ClassVar[TraceEventArg.Type]
        UINT32: _ClassVar[TraceEventArg.Type]
        INT64: _ClassVar[TraceEventArg.Type]
        UINT64: _ClassVar[TraceEventArg.Type]
        BOOL: _ClassVar[TraceEventArg.Type]
        STRING: _ClassVar[TraceEventArg.Type]
    DOUBLE: TraceEventArg.Type
    INT32: TraceEventArg.Type
    UINT32: TraceEventArg.Type
    INT64: TraceEventArg.Type
    UINT64: TraceEventArg.Type
    BOOL: TraceEventArg.Type
    STRING: TraceEventArg.Type
    KEY_FIELD_NUMBER: _ClassVar[int]
    TYPE_FIELD_NUMBER: _ClassVar[int]
    A_DOUBLE_FIELD_NUMBER: _ClassVar[int]
    A_INT32_FIELD_NUMBER: _ClassVar[int]
    A_UINT32_FIELD_NUMBER: _ClassVar[int]
    A_INT64_FIELD_NUMBER: _ClassVar[int]
    A_UINT64_FIELD_NUMBER: _ClassVar[int]
    A_BOOL_FIELD_NUMBER: _ClassVar[int]
    A_STRING_FIELD_NUMBER: _ClassVar[int]
    key: str
    type: TraceEventArg.Type
    a_double: float
    a_int32: int
    a_uint32: int
    a_int64: int
    a_uint64: int
    a_bool: bool
    a_string: str
    def __init__(self, key: _Optional[str] = ..., type: _Optional[_Union[TraceEventArg.Type, str]] = ..., a_double: _Optional[float] = ..., a_int32: _Optional[int] = ..., a_uint32: _Optional[int] = ..., a_int64: _Optional[int] = ..., a_uint64: _Optional[int] = ..., a_bool: bool = ..., a_string: _Optional[str] = ...) -> None: ...

class TraceEventRequest(_message.Message):
    __slots__ = ("name", "cat", "ph", "ts", "pid", "tid", "id", "args")
    NAME_FIELD_NUMBER: _ClassVar[int]
    CAT_FIELD_NUMBER: _ClassVar[int]
    PH_FIELD_NUMBER: _ClassVar[int]
    TS_FIELD_NUMBER: _ClassVar[int]
    PID_FIELD_NUMBER: _ClassVar[int]
    TID_FIELD_NUMBER: _ClassVar[int]
    ID_FIELD_NUMBER: _ClassVar[int]
    ARGS_FIELD_NUMBER: _ClassVar[int]
    name: str
    cat: str
    ph: str
    ts: float
    pid: int
    tid: int
    id: int
    args: _containers.RepeatedCompositeFieldContainer[TraceEventArg]
    def __init__(self, name: _Optional[str] = ..., cat: _Optional[str] = ..., ph: _Optional[str] = ..., ts: _Optional[float] = ..., pid: _Optional[int] = ..., tid: _Optional[int] = ..., id: _Optional[int] = ..., args: _Optional[_Iterable[_Union[TraceEventArg, _Mapping]]] = ...) -> None: ...

class TraceEventReply(_message.Message):
    __slots__ = ()
    def __init__(self) -> None: ...
