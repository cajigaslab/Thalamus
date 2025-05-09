from ..qt import *
from .. import thalamus_pb2_grpc
from ..observable_item_models import FlatObservableCollectionModel, TreeObservableCollectionModel, TreeObservableCollectionDelegate
from ..config import ObservableDict
from ..task_controller.util import create_task_with_exc_handling
import datetime
import asyncio
import time
import bisect
import pathlib
import typing

class FilePicker(QWidget):
  def __init__(self, value = '', parent = None):
    super().__init__(parent)
    self.text = QLineEdit(value)
    button = QPushButton('...')
    layout = QHBoxLayout()
    layout.addWidget(self.text)
    layout.addWidget(button)
    self.setLayout(layout)

    def on_button():
      dir = pathlib.Path(self.value()).parent
      file_name = QFileDialog.getOpenFileName(self.parentWidget(), "Load Reward Schedule", str(dir))
      if file_name and file_name[0]:
        self.text.setText(file_name[0])

    button.clicked.connect(on_button)

  def value(self):
    return self.text.text()

  def set_value(self, value):
    return self.text.setText(value)


class FileDelegate(QItemDelegate):
  def createEditor(self, parent: QWidget, option: QStyleOptionViewItem, index: QModelIndex) -> QWidget:
    print('createEditor')
    return FilePicker('', parent)

  def setEditorData(self, editor: QWidget, index: QModelIndex) -> None:
    editor = typing.cast(FilePicker, editor)
    data = index.data(Qt.ItemDataRole.EditRole)
    print('setEditorData', data)
    editor.set_value(data)

  def setModelData(self, editor: QWidget, model: QAbstractItemModel, index: QModelIndex):
    print('setModelData')
    editor = typing.cast(FilePicker, editor)
    data = editor.value()
    model.setData(index, data, Qt.ItemDataRole.EditRole)

  def sizeHint(self, option, index):
    return FilePicker('', None).sizeHint()

  def updateEditorGeometry(self, editor, option: QStyleOptionViewItem, index):
    editor = typing.cast(FilePicker, editor)
    editor.setGeometry(option.rect)

class Storage2Widget(QWidget):
  def __init__(self, config: ObservableDict, stub: thalamus_pb2_grpc.ThalamusStub):
    super().__init__()
    config_parent = config.parent
    assert config_parent is not None

    status = QLabel()
    status.setText('WAITING')
    status.setStyleSheet('color: red')

    if 'Sources' not in config:
      config['Sources'] = []
    sources = config['Sources']
    columns = [
      'Node',
      'Time Series',
      'Image',
      'Motion',
      'Text'
    ]

    if 'Files' not in config:
      config['Files'] = []
    files = config['Files']

    if 'Metadata' not in config:
      config['Metadata'] = []
    metadata = config['Metadata']

    def node_choices(collection, key):
      if key == 'Node':
        return sorted(node['name'] for node in config_parent)

    model = TreeObservableCollectionModel(sources, key_column='#', columns=columns, show_extra_values=False, is_editable=lambda *arg: True)
    delegate = TreeObservableCollectionDelegate(model, 3, node_choices)

    file_model = TreeObservableCollectionModel(files, key_column='#', columns=['Path'], show_extra_values=False, is_editable=lambda *arg: True)
    file_delegate = FileDelegate()

    metadata_model = TreeObservableCollectionModel(metadata, key_column='#', columns=['Key', 'Value'], show_extra_values=False, is_editable=lambda *arg: True)

    qlist = QTreeView()
    qlist.setModel(model)
    qlist.setItemDelegate(delegate)

    file_qlist = QTreeView()
    file_qlist.setModel(file_model)
    file_qlist.setItemDelegate(file_delegate)

    metadata_qlist = QTreeView()
    metadata_qlist.setModel(metadata_model)

    add_button = QPushButton('Add')
    remove_button = QPushButton('Remove')

    def on_add():
      sources.append({
        'Node': '',
        'Time Series': True,
        'Image': True,
        'Motion': True,
        'Text': True
      })

    def on_remove():
      for item in list(qlist.selectedIndexes())[::-1]:
        del sources[item.row()]

    add_button.clicked.connect(on_add)
    remove_button.clicked.connect(on_remove)

    add_file_button = QPushButton('Add')
    remove_file_button = QPushButton('Remove')

    add_metadata_button = QPushButton('Add')
    remove_metadata_button = QPushButton('Remove')

    def on_add_file():
      files.append({
        'Path': '',
      })

    def on_remove_file():
      for item in list(file_qlist.selectedIndexes())[::-1]:
        del files[item.row()]

    add_file_button.clicked.connect(on_add_file)
    remove_file_button.clicked.connect(on_remove_file)

    def on_add_metadata():
      metadata.append({
        'Key': 'Key',
        'Value': 'Value',
      })

    def on_remove_metadata():
      for item in list(file_qlist.selectedIndexes())[::-1]:
        del metadata[item.row()]

    add_metadata_button.clicked.connect(on_add_metadata)
    remove_metadata_button.clicked.connect(on_remove_metadata)

    if 'rec' not in config:
      config['rec'] = 0

    if 'start' not in config:
      config['start'] = time.perf_counter()

    layout = QVBoxLayout()
    layout.addWidget(status)
    layout.addWidget(qlist)
    button_layout = QHBoxLayout()
    button_layout.addWidget(add_button)
    button_layout.addWidget(remove_button)
    layout.addLayout(button_layout)
    layout.addWidget(file_qlist)
    button_layout = QHBoxLayout()
    button_layout.addWidget(add_file_button)
    button_layout.addWidget(remove_file_button)
    layout.addWidget(metadata_qlist)
    button_layout = QHBoxLayout()
    button_layout.addWidget(add_metadata_button)
    button_layout.addWidget(remove_metadata_button)
    layout.addLayout(button_layout)
    self.setLayout(layout)
    self.task = None
    condition = asyncio.Condition()

    async def __status_loop():
      try:
        while True:
          rec = config['rec']
          start = config['start']
          elapsed = int(time.perf_counter() - start)
          seconds = elapsed % 60
          minutes = (elapsed // 60) % 60
          hours = elapsed // 3600
          status.setText(f'RUNNING rec={rec:0>3} {hours:0>2}:{minutes:0>2}:{seconds:0>2}')
          await asyncio.sleep(1)
          if isdeleted(self):
            return
          #async with condition:
          #  try:
          #    await asyncio.wait_for(condition.wait(), timeout=1)
          #  except TimeoutError:
          #    pass
      except asyncio.CancelledError:
        pass

    last_running = config.get('Running', False)
    def on_change(source, action, key, value):
      nonlocal last_running

      print(source, action, key, value)
      if key == 'Running':
        if self.task:
          self.task.cancel()
          self.task = None
          status.setText('WAITING')
          status.setStyleSheet('color: red')

        if value:
          status.setStyleSheet('color: green')
          if value != last_running:
            config['start'] = time.perf_counter()
          self.task = create_task_with_exc_handling(__status_loop())
        last_running = value
      async def wake():
        async with condition:
          condition.notify_all()
      create_task_with_exc_handling(wake())

    config.add_recursive_observer(on_change, lambda: isdeleted(self), True)

