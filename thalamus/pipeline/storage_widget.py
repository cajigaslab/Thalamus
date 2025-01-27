from ..qt import *
from ..util import NodeSelector
from .. import thalamus_pb2_grpc
from ..config import ObservableDict
from ..task_controller.util import create_task_with_exc_handling
import datetime
import asyncio
import time

class StorageWidget(QWidget):
  def __init__(self, config: ObservableDict, stub: thalamus_pb2_grpc.ThalamusStub):
    super().__init__()

    status = QLabel()
    status.setText('WAITING')
    status.setStyleSheet('color: red')
    node_select = NodeSelector(config, 'Sources')

    if 'rec' not in config:
      config['rec'] = 0

    layout = QVBoxLayout()
    layout.addWidget(status)
    layout.addWidget(node_select)
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
          async with condition:
            try:
              await asyncio.wait_for(condition.wait(), timeout=1)
            except TimeoutError:
              pass
      except asyncio.CancelledError:
        pass

    def on_change(source, action, key, value):
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
          
