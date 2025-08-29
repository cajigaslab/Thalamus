from ..qt import *
from .. import thalamus_pb2
from .. import thalamus_pb2_grpc
from ..observable_item_models import FlatObservableCollectionModel, TreeObservableCollectionModel, TreeObservableCollectionDelegate, get_selected_rows
from ..config import ObservableDict
from ..task_controller.util import create_task_with_exc_handling
import datetime
import asyncio
import time
import bisect
import pathlib
import typing
import json

def next_id(components):
  ids = set(c['ID'] for c in components)
  result = 1
  while result in ids:
    result += 1
  return result

class DelsysDelegate(QItemDelegate):
  def __init__(self, node_name: typing.Callable[[], str], model: TreeObservableCollectionModel, stub: thalamus_pb2_grpc.ThalamusStub):
    super().__init__()
    self.node_name = node_name
    self.model = model
    self.stub = stub

  def createEditor(self, parent: QWidget, option: QStyleOptionViewItem, index: QModelIndex) -> QWidget:
    #print('createEditor')
    item, key = self.model.get_location(index)
    if key == 'ID':
      return QSpinBox(parent)
    elif key == 'Sample Mode':
      return QComboBox(parent)
    else:
      return super().createEditor(parent, option, index)

  def setEditorData(self, editor: QWidget, index: QModelIndex) -> None:
    #print('createEditor')
    item, key = self.model.get_location(index)
    if key == 'ID':
      value = item[key] if key in item else None
      editor.setValue(value)
    elif key == 'Sample Mode':
      async def fetch_sample_modes():
        response_json = await self.stub.node_request(thalamus_pb2.NodeRequest(
          node=self.node_name(),
          json=json.dumps({'type': 'get_sample_modes', 'id': item['ID']})
        ))
        response = json.loads(response_json.json)
        editor.addItems(response['sample_modes'])
      create_task_with_exc_handling(fetch_sample_modes())
    else:
      return super().setEditorData(editor, index)

  def setModelData(self, editor: QWidget, model: QAbstractItemModel, index: QModelIndex):
    item, key = self.model.get_location(index)
    if key == 'ID':
      value = editor.value()
      ids = set(c['ID'] for c in self.model.config)
      if value in ids:
        QMessageBox.warning(None, "Error", "That component ID already exists")
        return

      item[key] = value
    elif key == 'Sample Mode':
      item['Sample Mode Index'] = editor.currentIndex()
      item['Sample Mode'] = editor.currentText()
    else:
      return super().setModelData(editor, model, index)

class DelsysWidget(QWidget):
  def __init__(self, config: ObservableDict, stub: thalamus_pb2_grpc.ThalamusStub):
    super().__init__()
    config_parent = config.parent
    assert config_parent is not None

    if 'Components' not in config:
      config['Components'] = []
    components = config['Components']

    columns = [
      'ID',
      'Paired',
      'Scanned',
      'Selected',
      'Sample Mode'
    ]

    model = TreeObservableCollectionModel(components, key_column='#', columns=columns, show_extra_values=False, is_editable=lambda item, key: key in ('ID', 'Selected', 'Sample Mode'))
    delegate = DelsysDelegate(lambda: config['name'], model, stub)

    components_view = QTreeView()
    components_view.setModel(model)
    components_view.setItemDelegate(delegate)

    add_component_button = QPushButton('Add')
    pair_component_button = QPushButton('Pair')
    remove_component_button = QPushButton('Remove')
    add_component_button.clicked.connect(lambda: components.append({
      'ID': next_id(components),
      'Paired': False,
      'Scanned': False,
      'Selected': False,
      'Sample Mode': '',
      'Sample Mode Index': -1
    }))

    async def on_pair():
      for row in get_selected_rows(components_view):
        await request({'type': 'pair', 'id': row})
    pair_component_button.clicked.connect(on_pair)

    def on_remove():
      for row in get_selected_rows(components_view):
        del components_view[i]
    remove_component_button.clicked.connect(on_remove)

    components_widget = QWidget()
    components_layout = QGridLayout()
    components_layout.addWidget(components_view, 0, 0, 1, 2)
    components_layout.addWidget(add_component_button, 1, 0)
    components_layout.addWidget(remove_component_button, 1, 1)
    components_widget.setLayout(components_layout)

    qlist = QListWidget()
    qlist.setAutoScroll(True)

    def request(message):
      payload = thalamus_pb2.NodeRequest(node=config['name'],json=json.dumps(message))
      invoke = stub.node_request(payload)
      create_task_with_exc_handling(invoke)

    scan_button = QPushButton('Scan')
    scan_button.clicked.connect(lambda: request({'type': 'scan'}))

    connect_button = QPushButton('Connect')
    connect_button.clicked.connect(lambda: request({'type': 'connect'}))

    clear_button = QPushButton('Clear')
    clear_button.clicked.connect(lambda: qlist.clear())

    log_widget = QWidget()
    log_layout = QVBoxLayout()
    log_layout.addWidget(qlist)
    log_layout.addWidget(clear_button)
    log_widget.setLayout(log_layout)

    tabs = QTabWidget()
    tabs.addTab(log_widget, 'Log')
    tabs.addTab(components_widget, 'Components')

    layout = QVBoxLayout()
    layout.addWidget(scan_button)
    layout.addWidget(connect_button)
    layout.addWidget(tabs)

    self.setLayout(layout)

    async def stream():
      try:
        async for m in stub.text(thalamus_pb2.TextRequest(node=thalamus_pb2.NodeSelector(name=config['name']))):
          qlist.addItem(m.text)
      except asyncio.CancelledError:
        pass
      
    self.loop_task = create_task_with_exc_handling(stream())

    def on_change(source, action, key, value):
      pass

    config.add_recursive_observer(on_change, lambda: isdeleted(self), True)

  def closeEvent(self, e):
    print('DelsysWidget.closeEvent')
    self.loop_task.cancel()