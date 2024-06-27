from ..util import NodeSelector
from ..qt import *
from .. import thalamus_pb2
from ..task_controller.util import create_task_with_exc_handling

class NormalizeWidget(QWidget):
  def __init__(self, config, stub):
    super().__init__()
    self.config = config
    node_selector = NodeSelector(config, 'Source', False)

    if 'Min' not in config:
      config['Min'] = 0
    if 'Max' not in config:
      config['Max'] = 1

    min_spinbox = QDoubleSpinBox()
    min_spinbox.setRange(-1e9, 1e9)
    min_spinbox.editingFinished.connect(lambda: config.update({'Min': min_spinbox.value()}))
    max_spinbox = QDoubleSpinBox()
    max_spinbox.setRange(-1e9, 1e9)
    max_spinbox.editingFinished.connect(lambda: config.update({'Max': max_spinbox.value()}))

    reset_button = QPushButton('Reset')
    cache_button = QPushButton('Cache')

    def reset():
      request = thalamus_pb2.NodeRequest(
        node = self.config['name'],
        json = '"Reset"'
      )
      create_task_with_exc_handling(stub.node_request(request))
    def cache():
      request = thalamus_pb2.NodeRequest(
        node = self.config['name'],
        json = '"Cache"'
      )
      create_task_with_exc_handling(stub.node_request(request))

    reset_button.clicked.connect(reset)
    cache_button.clicked.connect(cache);

    grid = QGridLayout()
    grid.addWidget(QLabel('Max Out:'), 0, 0)
    grid.addWidget(max_spinbox, 0, 1)
    grid.addWidget(QLabel('Min Out:'), 1, 0)
    grid.addWidget(min_spinbox, 1, 1)
    grid.addWidget(reset_button, 2, 0, 1, 2)
    grid.addWidget(cache_button, 3, 0, 1, 2)
    grid.addWidget(node_selector, 4, 0)
    grid.setColumnStretch(0, 0)
    grid.setColumnStretch(1, 1)
    grid.setRowStretch(0, 0)
    grid.setRowStretch(1, 0)
    grid.setRowStretch(2, 0)
    grid.setRowStretch(3, 1)
    self.setLayout(grid)

    def on_change(a, k, v):
      if k == 'Min':
        min_spinbox.setValue(v)
      elif k == 'Max':
        max_spinbox.setValue(v)

    self.config.add_observer(on_change, lambda: isdeleted(self))
    for k, v in config.items():
      on_change(None, k, v)
