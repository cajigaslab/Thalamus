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

class DelsysWidget(QWidget):
  def __init__(self, config: ObservableDict, stub: thalamus_pb2_grpc.ThalamusStub):
    super().__init__()
    config_parent = config.parent
    assert config_parent is not None

    status = QLabel()
    status.setText('Scan: Waiting')
    status.setStyleSheet('color: red')

    config["Scan Status"] = "Waiting"

    if 'Components' not in config:
      config['Components'] = []
    sources = config['Components']
    columns = [
      'ID',
      'Paired'
    ]

    model = TreeObservableCollectionModel(sources, key_column='#', columns=columns, show_extra_values=False, is_editable=lambda col, key: key == 'ID')

    qlist = QTreeView()
    qlist.setModel(model)

    add_button = QPushButton('Add')
    remove_button = QPushButton('Remove')

    def on_add():
      sources.append({
        'ID': 0,
        'Paired': False
      })

    def on_remove():
      for item in get_selected_rows(qlist):
        del sources[item]

    add_button.clicked.connect(on_add)
    remove_button.clicked.connect(on_remove)

    async def scan():
      name = self.config['name']
      response = await self.stub.node_request(thalamus_pb2.NodeRequest(node=name,json=json.dumps({'type':'scan'})))

    def sync_scan():
      create_task_with_exc_handling(scan())

    self.sync_button = QPushButton('Scan')
    self.sync_button.clicked.connect(sync_scan)

    layout = QVBoxLayout()
    layout.addWidget(status)
    layout.addWidget(QLabel('Components:'))
    layout.addWidget(qlist)
    button_layout = QHBoxLayout()
    button_layout.addWidget(add_button)
    button_layout.addWidget(remove_button)
    layout.addWidget(self.sync_button)

    def on_change(source, action, key, value):
      if key == 'Scan Status':
        status.setText(f'Scan: {value}')
        if value == 'Waiting':
          status.setStyleSheet('color: red')
        elif value == 'In Progress':
          status.setStyleSheet('color: black')
        elif value == 'Success':
          status.setStyleSheet('color: green')
        elif value == 'Failed':
          status.setStyleSheet('color: red')

    config.add_recursive_observer(on_change, lambda: isdeleted(self), True)

