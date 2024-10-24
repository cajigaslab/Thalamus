import typing
import functools

from ..config import *
from ..qt import *
from ..util import NodeSelector
from ..task_controller.util import create_task_with_exc_handling
from .. import thalamus_pb2
from .. import thalamus_pb2_grpc

class HexascopeWidget(QWidget):
  def __init__(self, config: ObservableDict, stub):
    super().__init__()

    if 'Motion Tracking Node' not in config:
      config['Motion Tracking Node'] = ''
    if 'Objective Pose' not in config:
      config['Objective Pose'] = 0
    if 'Field Pose' not in config:
      config['Field Pose'] = 1

    layout = QGridLayout()
    node_selector = NodeSelector(config, 'Motion Tracking Node', False)
    objective_spinbox = QSpinBox()
    field_spinbox = QSpinBox()
    
    layout.addWidget(QLabel('Motion Tracking Node'), 0, 0)
    layout.addWidget(node_selector, 0, 1)
    layout.addWidget(QLabel('Objective Pose'), 1, 0)
    layout.addWidget(objective_spinbox, 1, 1)
    layout.addWidget(QLabel('Field Pose'), 2, 0)
    layout.addWidget(field_spinbox, 2, 1)

    orient_button = QPushButton('Orient to Field')
    descend_button = QPushButton('Descend')
    ascend_button = QPushButton('Ascend')

    layout.addWidget(orient_button, 3, 0, 1, 2)
    layout.addWidget(descend_button, 4, 0, 1, 2)
    layout.addWidget(ascend_button, 5, 0, 1, 2)

    def send_request(request):
      request = thalamus_pb2.NodeRequest(
        node = config['name'],
        json = json.dumps(request)
      )
      create_task_with_exc_handling(stub.node_request(request))
    orient_button.clicked.connect(lambda: send_request({'type': 'orient'}))
    descend_button.clicked.connect(lambda: send_request({'type': 'descend'}))
    ascend_button.clicked.connect(lambda: send_request({'type': 'ascend'}))

    def on_change(a, k, v):
      if k == 'Objective Pose':
        objective_spinbox.setValue(v)
      elif k == 'Field Pose':
        field_spinbox.setValue(v)

    config.add_observer(on_change, lambda: isdeleted(self))
    for k, v in config.items():
      on_change(None, k, v)
