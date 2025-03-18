from . import util_pb2 as _util_pb2
from google.protobuf import descriptor as _descriptor
from google.protobuf import message as _message
from typing import ClassVar as _ClassVar, Mapping as _Mapping, Optional as _Optional, Union as _Union

DESCRIPTOR: _descriptor.FileDescriptor

class Empty(_message.Message):
    __slots__ = ()
    def __init__(self) -> None: ...

class TaskConfig(_message.Message):
    __slots__ = ("body",)
    BODY_FIELD_NUMBER: _ClassVar[int]
    body: str
    def __init__(self, body: _Optional[str] = ...) -> None: ...

class TaskResult(_message.Message):
    __slots__ = ("success",)
    SUCCESS_FIELD_NUMBER: _ClassVar[int]
    success: bool
    def __init__(self, success: bool = ...) -> None: ...

class TrialSummary(_message.Message):
    __slots__ = ("header", "used_values", "task_config", "task_result", "behav_result")
    HEADER_FIELD_NUMBER: _ClassVar[int]
    USED_VALUES_FIELD_NUMBER: _ClassVar[int]
    TASK_CONFIG_FIELD_NUMBER: _ClassVar[int]
    TASK_RESULT_FIELD_NUMBER: _ClassVar[int]
    BEHAV_RESULT_FIELD_NUMBER: _ClassVar[int]
    header: _util_pb2.Header
    used_values: str
    task_config: str
    task_result: str
    behav_result: str
    def __init__(self, header: _Optional[_Union[_util_pb2.Header, _Mapping]] = ..., used_values: _Optional[str] = ..., task_config: _Optional[str] = ..., task_result: _Optional[str] = ..., behav_result: _Optional[str] = ...) -> None: ...

class BehavState(_message.Message):
    __slots__ = ("state",)
    STATE_FIELD_NUMBER: _ClassVar[int]
    state: str
    def __init__(self, state: _Optional[str] = ...) -> None: ...
