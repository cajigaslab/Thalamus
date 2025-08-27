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

    qlist = QListWidget()
    qlist.setAutoScroll(True)

    pair_spin = QSpinBox()

    async def pair():
      name = config['name']
      response = await stub.node_request(thalamus_pb2.NodeRequest(node=name,json=json.dumps({'type':'pair','id':pair_spin.value()})))

    def sync_pair():
      create_task_with_exc_handling(pair())

    pair_button = QPushButton('Pair')
    pair_button.clicked.connect(sync_pair)

    async def scan():
      name = config['name']
      response = await stub.node_request(thalamus_pb2.NodeRequest(node=name,json=json.dumps({'type':'scan'})))

    def sync_scan():
      create_task_with_exc_handling(scan())

    scan_button = QPushButton('Scan')
    scan_button.clicked.connect(sync_scan)

    clear_button = QPushButton('Clear')
    clear_button.clicked.connect(lambda: qlist.clear())

    layout_top = QHBoxLayout()
    layout_top.addWidget(pair_spin)
    layout_top.addWidget(pair_button)

    layout = QVBoxLayout()
    layout.addLayout(layout_top)
    layout.addWidget(scan_button)
    layout.addWidget(qlist)
    layout.addWidget(clear_button)

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