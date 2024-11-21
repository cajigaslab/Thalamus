from google.protobuf import descriptor as _descriptor
from google.protobuf import message as _message
from typing import ClassVar as _ClassVar, Optional as _Optional

DESCRIPTOR: _descriptor.FileDescriptor

class Empty(_message.Message):
    __slots__ = ()
    def __init__(self) -> None: ...

class Header(_message.Message):
    __slots__ = ("stamp",)
    STAMP_FIELD_NUMBER: _ClassVar[int]
    stamp: int
    def __init__(self, stamp: _Optional[int] = ...) -> None: ...
