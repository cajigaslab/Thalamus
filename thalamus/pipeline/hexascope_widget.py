import typing
import functools

from ..config import *
from ..qt import *
from ..util import NodeSelector
from ..task_controller.util import create_task_with_exc_handling
from .. import thalamus_pb2
from .. import thalamus_pb2_grpc
from ..observable_item_models import FlatObservableCollectionModel

class HexascopeWidget(QWidget):
  def __init__(self, config: ObservableDict, stub):
    super().__init__()

    if 'Motion Tracking Node' not in config:
      config['Motion Tracking Node'] = ''
    if 'Objective Pose' not in config:
      config['Objective Pose'] = 0
    if 'Field Pose' not in config:
      config['Field Pose'] = 1
    if 'hexa_to_camera' not in config:
      config['hexa_to_camera'] = [1.0, 0.0, 0.0, 0.0,
                                  0.0, 1.0, 0.0, 0.0,
                                  0.0, 0.0, 1.0, 0.0,
                                  0.0, 0.0, 0.0, 1.0]

    layout = QGridLayout()
    node_selector = QComboBox()
    node_selector.setModel(FlatObservableCollectionModel(config.parent, lambda n: n['name']))
    objective_spinbox = QSpinBox()
    field_spinbox = QSpinBox()
    
    layout.addWidget(QLabel('Motion Tracking Node'), 0, 0)
    layout.addWidget(node_selector, 0, 1, 1, 2)
    layout.addWidget(QLabel('Objective Pose'), 1, 0)
    layout.addWidget(objective_spinbox, 1, 1, 1, 2)
    layout.addWidget(QLabel('Field Pose'), 2, 0)
    layout.addWidget(field_spinbox, 2, 1, 1, 2)
    layout.setRowStretch(0, 0)
    layout.setRowStretch(1, 0)
    layout.setRowStretch(2, 0)
    layout.setRowStretch(3, 0)
    layout.addWidget(QLabel(''), 5, 0)
    layout.setRowStretch(5, 1)

    calibrate_button = QPushButton('Calibrate')
    align_button = QPushButton('Align')
    lock_button = QPushButton('Lock')
    dirs = [
      QPushButton('left'),
      QPushButton('right'),
      QPushButton('up'),
      QPushButton('down'),
      QPushButton('forward'),
      QPushButton('back')
    ]
    get_button = QPushButton('Home')
    #descend_button = QPushButton('Descend')
    #ascend_button = QPushButton('Ascend')

    layout.addWidget(calibrate_button, 3, 0)
    layout.addWidget(align_button, 3, 1)
    layout.addWidget(lock_button, 3, 2)
    layout.addWidget(dirs[0], 4, 0)
    layout.addWidget(dirs[1], 4, 1)
    layout.addWidget(dirs[2], 4, 2)
    layout.addWidget(dirs[3], 4, 3)
    layout.addWidget(dirs[4], 4, 4)
    layout.addWidget(dirs[5], 4, 5)
    layout.addWidget(dirs[5], 4, 6)
    layout.addWidget(get_button, 4, 7)
    #layout.addWidget(descend_button, 4, 0, 1, 2)
    #layout.addWidget(ascend_button, 5, 0, 1, 2)

    self.setLayout(layout)

    def send_request(request):
      request = thalamus_pb2.NodeRequest(
        node = config['name'],
        json = json.dumps(request)
      )
      create_task_with_exc_handling(stub.node_request(request))
    calibrate_button.clicked.connect(lambda: send_request({'type': 'calibrate'}))
    align_button.clicked.connect(lambda: send_request({'type': 'align'}))

    dirs[0].clicked.connect(lambda: send_request({'type': 'move_objective', 'value':[20e-3,0.0,0.0]}))
    dirs[1].clicked.connect(lambda: send_request({'type': 'move_objective', 'value':[-20e-3,0.0,0.0]}))
    dirs[2].clicked.connect(lambda: send_request({'type': 'move_objective', 'value':[0.0,20e-3,0.0]}))
    dirs[3].clicked.connect(lambda: send_request({'type': 'move_objective', 'value':[0.0,-20e-3,0.0]}))
    dirs[4].clicked.connect(lambda: send_request({'type': 'move_objective', 'value':[0.0,0.0,20e-3]}))
    dirs[5].clicked.connect(lambda: send_request({'type': 'move_objective', 'value':[0.0,0.0,-20e-3]}))
    get_button.clicked.connect(lambda: send_request({'type': 'move_hexa', 'value':[0.0, 0.0, 0.0, 0.0, 0.0, 0.0]}))

    def lock(request):

      async def async_lock():
        request = thalamus_pb2.NodeRequest(
          node = config['name'],
          json = json.dumps({'type': 'lock'})
        )
        print('LOCKING')
        response = await stub.node_request(request)
        print('LOCKED')
        print(response)
        lock_button.setText("Locked")

      create_task_with_exc_handling(async_lock())
      
    lock_button.clicked.connect(lock)

    self.positions = [
      [0.0, 0.0,  0.0, 0.0,  0.0,  0.0],
      [30.0, 0.0,  0.0, 0.0,  0.0,  0.0],
      [0.0, 30.0,  0.0, 0.0, 0.0,  0.0],
      [0.0,  0.0, 30.0, 0.0,  0.0, 0.0],
    ]
    self.pi = 0
    def jog(request):

      async def async_lock():
        request = thalamus_pb2.NodeRequest(
          node = config['name'],
          json = json.dumps({'type': 'move_hexa', 'value': self.positions[self.pi]})
        )
        self.pi = (self.pi + 1) % len(self.positions)
        print('Jogging')
        response = await stub.node_request(request)
        print('jogged')
        print(response)

      create_task_with_exc_handling(async_lock())
      
    #jog_button.clicked.connect(jog)
    #descend_button.clicked.connect(lambda: send_request({'type': 'descend'}))
    #ascend_button.clicked.connect(lambda: send_request({'type': 'ascend'}))

    objective_spinbox.valueChanged.connect(lambda v: config.update({'Objective Pose' : v}))
    field_spinbox.valueChanged.connect(lambda v: config.update({'Field Pose' : v}))

    node_selector.currentTextChanged.connect(lambda v: config.update({'Motion Tracking Node' : v}))


    def on_change(a, k, v):
      if k == 'Objective Pose':
        objective_spinbox.setValue(v)
      elif k == 'Field Pose':
        field_spinbox.setValue(v)

    config.add_observer(on_change, lambda: isdeleted(self))
    for k, v in config.items():
      on_change(None, k, v)
