import collections
import functools
import random
import bisect
import typing
import bisect
import pdb

from ..qt import *
from ..config import *
from ..task_controller.util import create_task_with_exc_handling
from .. import thalamus_pb2

from ..observable_item_models import FlatObservableCollectionModel, TreeObservableCollectionModel

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

class SyncWidget(QWidget):
  def __init__(self, config: ObservableDict, stub):
    super().__init__()
    self.config = config
    self.stub = stub
    assert self.config.parent is not None

    if 'Sources' not in self.config:
      self.config['Sources'] = {}
    sources = self.config['Sources']
    nodes = self.config.parent

    def on_rename(old_name, new_name):
      if old_name in sources:
        node = sources[old_name]
        del sources[old_name]
        if new_name is not None:
          sources[new_name] = node

    self.name_tracker = NameTracker(nodes, lambda n: n['name'], on_rename)

    layout = QVBoxLayout()
    combo = QComboBox()
    combo_model = FlatObservableCollectionModel(nodes, lambda c: c['name'])
    combo.setModel(combo_model)
    add_button = QPushButton('Add')
    qlist = QTreeView()
    model = TreeObservableCollectionModel(sources, key_column='Name', columns=['Channel', 'Is Sync'],
                                          show_extra_values=False,
                                          is_editable = lambda o, k: k == 'Is Sync')
    qlist.setModel(model)
    #qlist.setItemDelegate(Delegate())
    remove_button = QPushButton('Remove')

    layout.addWidget(combo)
    layout.addWidget(add_button)
    layout.addWidget(qlist, 1)
    layout.addWidget(remove_button)

    self.setLayout(layout)

    def on_add():
      #new_node = combo.currentData()
      name = combo.currentText()
      for node in nodes:
        if node['name'] == name:
          sources[name] = [{'Channel': str(random.random()), 'Is Sync': False}, {'Channel': str(random.random()), 'Is Sync': False}]
      #if new_name in sources:
      #  return
      #sources[new_name] = []
    add_button.clicked.connect(on_add)

    def on_remove():
      for item in qlist.selectedIndexes():
        if item.parent().isValid():
          item = item.parent()
        del sources[item.data()]
    remove_button.clicked.connect(on_remove)

    self.info_tasks = {}

  async def __channel_info(self, name, channels):
    self.clear()
    selected_node = self.config['selected_node']

    selector = thalamus_pb2.NodeSelector(name=name)
    stream = self.stub.channel_info(thalamus_pb2.AnalogRequest(node=selector))
    try:
      async for message in stream:
        new_channels = sorted([s.name for s in message.spans])
        i, j = 0, 0
        while i < len(channels) and j < len(new_channels):
          if channels[i]['Name'] < new_channels[j]:
            del channels[i]
          elif channels[i]['Name'] > new_channels[j]:
            channels.insert(i, {'Name': new_channels[j], 'Is Sync': False})
            i, j = i+1, j+1
          else:
            i, j = i+1, j+1

        
        if i < len(channels):
          for i2 in range(len(channels), i-1, -1):
            del channels[i2]
        elif j < len(new_channels):
          for j2 in range(j, len(new_channels)):
            del channels[i2]




        selected_channel = self.config[self.selected_channel_key]
        self.clear()
        self.addItems()
        self.setCurrentText(selected_channel)
    except asyncio.CancelledError:
      pass
    except grpc.aio.AioRpcError as e:
      if e.code() != grpc.StatusCode.CANCELLED:
        raise


  def __on_change(self, action, key, value):
    if action == ObservableCollection.Action.SET:
      self.info_tasks[key] = create_task_with_exc_handling(self.__channel_info_task())
