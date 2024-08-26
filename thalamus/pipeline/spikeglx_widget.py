from ..qt import *

from .. import thalamus_pb2

from ..task_controller.util import create_task_with_exc_handling

class SpikeGlxWidget(QWidget):
  def __init__(self, config, stub):
    super().__init__()
    layout = QVBoxLayout()
    layout.setAlignment(Qt.AlignmentFlag.AlignTop)
    self.setLayout(layout)
    edits = []

    refresh_button = QPushButton('Refresh')
    layout.addWidget(refresh_button)

    def on_refresh():
      request = thalamus_pb2.NodeRequest(
        node = config['name'],
        json = '"Refresh"'
      )
      create_task_with_exc_handling(stub.node_request(request))

    refresh_button.clicked.connect(on_refresh)

    def fill_edits(count):
      while len(edits) < count:
        edit = QLineEdit()
        edit.setText('*')
        edit.textChanged.connect(lambda text, i=len(edits): config.update({f'imec_subset_{i}': text}))
        layout.addWidget(edit)
        edits.append(edit)
    
    def on_change(a, k, v):
      if "imec_subset" in k:
        index = int(k.split('_')[-1])
        fill_edits(index+1)
        edits[index].setText(v)
      elif k == 'imec_count':
        fill_edits(v)
        while len(edits) > v:
          edit = edits.pop()
          layout.removeWidget(edit)
          edit.deleteLater()

    for k, v in config.items():
      on_change(None, k, v)
    config.add_observer(on_change, lambda: isdeleted(self))