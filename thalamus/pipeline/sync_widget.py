import collections
import functools
import asyncio
import random
import bisect
import typing
import bisect
import grpc
import pdb

from ..qt import *
from ..config import *
from ..task_controller.util import create_task_with_exc_handling
from .. import thalamus_pb2
from .. import thalamus_pb2_grpc

from ..observable_item_models import FlatObservableCollectionModel, TreeObservableCollectionModel, TreeObservableCollectionDelegate
from ..channels_item_model import ChannelsItemModel

class NameTracker:
  def __init__(self, nodes, namer: typing.Callable[[typing.Any], str], callback: typing.Callable[[str, typing.Optional[str]], None], depth = 1):
    self.nodes = nodes
    self.namer = namer
    self.callback = callback
    self.names = {}

    self.nodes.add_observer(functools.partial(self.__on_change, depth))
    self.nodes.recap(functools.partial(self.__on_change, depth))

  def __update(self):
    missing = dict(self.names)
    for node in self.nodes:
      if id(node) in missing:
        del missing[id(node)]
      new_name = self.namer(node)
      if id(node) in self.names:
        old_name = self.names[id(node)]
        if old_name != new_name:
          self.callback(old_name, new_name)
      self.names[id(node)] = new_name

    for k, v in missing.items():
      self.callback(v, None)

  def __on_change(self, remaining_levels: int, action: ObservableCollection.Action, key: typing.Any, value: typing.Any):
    if action == ObservableCollection.Action.SET:
      if isinstance(value, ObservableCollection):
        if remaining_levels:
          value.add_observer(functools.partial(self.__on_change, remaining_levels-1))
    self.__update()

class Delegate(TreeObservableCollectionDelegate):
  def __init__(self, nodes: ObservableList, stub: thalamus_pb2_grpc.ThalamusStub, model: TreeObservableCollectionModel):
    super().__init__(model)
    self.stub = stub
    self.node_model = FlatObservableCollectionModel(nodes, lambda n: n['name'])
    self.channel_models: typing.Dict[str, ChannelsItemModel] = {}

  def createEditor(self, parent: QWidget, option: QStyleOptionViewItem, index: QModelIndex) -> QWidget:
    print('createEditor')
    item, key = self.model.get_location(index)
    if key == 'Channel 1':
      node = item['Node 1']
      if node not in self.channel_models:
        self.channel_models[node] = ChannelsItemModel(self.stub, node)
    elif key == 'Channel 2':
      node = item['Node 2']
      if node not in self.channel_models:
        self.channel_models[node] = ChannelsItemModel(self.stub, node)
    elif key == 'Node 1':
      combo = QComboBox(parent)
      combo.setModel(self.node_model)
      return combo
    elif key == 'Node 2':
      combo = QComboBox(parent)
      combo.setModel(self.node_model)
      return combo
    else:
      return super().createEditor(parent, option, index)

    model = self.channel_models[node]
    combo = QComboBox(parent)
    combo.setModel(model)
    return combo

class SyncWidget(QWidget):
  def __init__(self, config: ObservableDict, stub: thalamus_pb2_grpc.ThalamusStub):
    super().__init__()
    self.config = config
    self.stub = stub
    assert self.config.parent is not None

    if 'Pairs' not in self.config:
      self.config['Pairs'] = []
    self.pairs = None
    self.nodes = self.config.parent

    def on_rename(old_name, new_name):
      if self.pairs is None:
        return
      for pair in self.pairs:
        if pair['Node 1'] == old_name:
          pair['Node 1'] = new_name if new_name is not None else ''
        if pair['Node 2'] == old_name:
          pair['Node 2'] = new_name if new_name is not None else ''

    self.name_tracker = NameTracker(self.nodes, lambda n: n['name'], on_rename)

    layout = QVBoxLayout()
    add_button = QPushButton('Add')
    self.qlist = QTreeView()
    remove_button = QPushButton('Remove')

    layout.addWidget(self.qlist, 1)
    layout.addWidget(add_button)
    layout.addWidget(remove_button)

    self.setLayout(layout)

    def on_add():
      #new_node = combo.currentData()
      if self.pairs is None:
        return
      self.pairs.append({
        'Node 1': '',
        'Channel 1': '',
        'Node 2': '',
        'Channel 2': '',
        'Window (s)': .5,
        'Threshold': .5,
      })
    add_button.clicked.connect(on_add)

    def on_remove():
      if self.pairs is None:
        return
      rows = sorted(set(i.row() for i in self.qlist.selectedIndexes()), reverse=True)
      for row in rows:
        del self.pairs[row]
    remove_button.clicked.connect(on_remove)

    self.config.add_observer(self.__on_change, lambda: isdeleted(self), True)

  def __prepare(self, pairs: ObservableList):
    self.pairs = pairs
    model = TreeObservableCollectionModel(pairs, key_column='#', columns=['Node 1', 'Channel 1', 'Node 2', 'Channel 2', 'Window (s)', 'Threshold'],
                                          show_extra_values=False,
                                          is_editable = lambda o, k: True)
    delegate = Delegate(self.nodes, self.stub, model)
    self.qlist.setModel(model)
    self.qlist.setItemDelegate(delegate)

  def __on_change(self, action, key, value):
    if key == 'Pairs':
      self.__prepare(value)

