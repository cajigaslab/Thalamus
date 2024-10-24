import pdb
import grpc
import typing
import bisect
import asyncio
import functools
import traceback

from .config import ObservableCollection, ObservableDict, ObservableList
from .qt import *

from .task_controller.util import create_task_with_exc_handling
from . import thalamus_pb2
from . import thalamus_pb2_grpc

class ChannelsItemModel(QAbstractItemModel):
  def __init__(self, stub: thalamus_pb2_grpc.ThalamusStub, node: typing.Optional[str] = None):
    super().__init__()
    self.task: typing.Optional[asyncio.Task] = None
    self.stub = stub
    self.channels = []
    if node:
      self.set_node(node)
    self.destroyed.connect(lambda o: self.stop())

  def stop(self):
    if self.task is not None:
      self.task.cancel()

  def set_node(self, node: str):
    if self.task is not None:
      self.task.cancel()
    self.task = create_task_with_exc_handling(self.__channel_info(node))

  async def __channel_info(self, name):
    selector = thalamus_pb2.NodeSelector(name=name)
    stream = self.stub.channel_info(thalamus_pb2.AnalogRequest(node=selector))
    try:
      async for message in stream:
        new_channels = sorted([s.name for s in message.spans])
        i, j = 0, 0
        while i < len(self.channels) and j < len(new_channels):
          if self.channels[i] < new_channels[j]:
            self.beginRemoveRows(QModelIndex(), i, i)
            del self.channels[i]
            self.endRemoveRows()
          elif self.channels[i] > new_channels[j]:
            self.beginInsertRows(QModelIndex(), i, i)
            self.channels.insert(i, new_channels[j])
            self.endInsertRows()
            i, j = i+1, j+1
          else:
            i, j = i+1, j+1

        
        if i < len(self.channels):
          self.beginRemoveRows(QModelIndex(), i, len(self.channels)-1)
          del self.channels[i:]
          self.endRemoveRows()
        elif j < len(new_channels):
          self.beginInsertRows(QModelIndex(), i, len(self.channels)-1)
          self.channels.extend(new_channels[j:])
          self.endInsertRows()
    except asyncio.CancelledError:
      pass
    except grpc.aio.AioRpcError as e:
      if e.code() != grpc.StatusCode.CANCELLED:
        raise

  def data(self, index: QModelIndex, role: int) -> typing.Any:
    print('data', index.row(), index.column(), role)
    if role == Qt.ItemDataRole.DisplayRole:
      return self.channels[index.row()]
    elif role == Qt.ItemDataRole.EditRole:
      return self.channels[index.row()]
    else:
      return None

  def setData(self, index: QModelIndex, value: typing.Any, role: int = Qt.ItemDataRole.EditRole) -> bool:
    #print('setData', index, value, role)
    if role == Qt.ItemDataRole.DisplayRole:
      self.channels[index.row()] = value
      return True
    elif role == Qt.ItemDataRole.EditRole:
      self.channels[index.row()] = value
      return True
    else:
      return False

  def index(self, row: int, column: int, parent: QModelIndex) -> QModelIndex:
    return self.createIndex(row, column, parent)
  
  def parent(self, index: QModelIndex) -> QModelIndex:
    return QModelIndex()

  def rowCount(self, parent: QModelIndex) -> int:
    return len(self.channels)

  def columnCount(self, _: QModelIndex) -> int:
    return 1
