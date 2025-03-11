from ..qt import *
from .. import thalamus_pb2_grpc
from ..observable_item_models import FlatObservableCollectionModel
from ..config import ObservableDict
from ..task_controller.util import create_task_with_exc_handling
import datetime
import asyncio
import time
import bisect

class StorageWidget(QWidget):
  def __init__(self, config: ObservableDict, stub: thalamus_pb2_grpc.ThalamusStub):
    super().__init__()

    status = QLabel()
    status.setText('WAITING')
    status.setStyleSheet('color: red')

    if "Sources List" not in config:
      config["Sources List"] = []
    sources_list = config["Sources List"]

    combo_model = FlatObservableCollectionModel(config.parent, lambda n: n['name'])
    selected_model = FlatObservableCollectionModel(sources_list, lambda n: n)
    combo = QComboBox()
    combo.setModel(combo_model)
    qlist = QTreeView()
    qlist.setHeaderHidden(True)
    qlist.setRootIsDecorated(False)
    qlist.setModel(selected_model)
    add_button = QPushButton("Add")
    remove_button = QPushButton("Remove")

    def on_add():
      data = combo.currentText()
      print('on_add', data)
      if data and data not in sources_list:
        i = bisect.bisect_left(sources_list, data)
        sources_list.insert(i, data)

    def on_remove():
      for item in list(qlist.selectedIndexes())[::-1]:
        del sources_list[item.row()]

    add_button.clicked.connect(on_add)
    remove_button.clicked.connect(on_remove)

    if 'rec' not in config:
      config['rec'] = 0

    layout = QVBoxLayout()
    layout.addWidget(status)
    layout.addWidget(combo)
    layout.addWidget(qlist)
    button_layout = QHBoxLayout()
    button_layout.addWidget(add_button)
    button_layout.addWidget(remove_button)
    layout.addLayout(button_layout)
    self.setLayout(layout)
    self.task = None
    condition = asyncio.Condition()

    async def __status_loop():
      try:
        start = time.perf_counter()
        while True:
          timestr = ''
          rec = config['rec']
          elapsed = int(time.perf_counter() - start)
          seconds = elapsed % 60
          minutes = elapsed // 60
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

    def on_change(source, action, key, value):
      print(source, action, key, value)
      if source is sources_list:
        config['Sources'] = ','.join(sources_list)
        return

      if key == 'Running':
        if self.task:
          self.task.cancel()
          self.task = None
          status.setText('WAITING')
          status.setStyleSheet('color: red')

        if value:
          status.setStyleSheet('color: green')
          self.task = create_task_with_exc_handling(__status_loop())
      async def wake():
        async with condition:
          condition.notify_all()
      create_task_with_exc_handling(wake())

    config.add_recursive_observer(on_change, lambda: isdeleted(self), True)
          
