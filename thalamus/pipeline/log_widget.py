import json

from ..qt import *
from .. import thalamus_pb2
from ..task_controller.util import create_task_with_exc_handling

class ChatEdit(QTextEdit):
  def __init__(self):
    super().__init__()
    self.on_apply = lambda: None

  def keyPressEvent(self, e: QKeyEvent):
    if e.key() == Qt.Key.Key_Return and not bool(e.modifiers() & Qt.KeyboardModifier.ShiftModifier): # type: ignore
      self.on_apply()
      return
    return super().keyPressEvent(e)

class LogWidget(QWidget):
  def __init__(self, config, stub):
    super().__init__()

    list = QListWidget()
    edit = ChatEdit()

    def send_message(item = None):
      text = edit.toPlainText()
      request = thalamus_pb2.NodeRequest(
        node = config['name'],
        json = json.dumps(text)
      )
      list.addItem(text)
      edit.clear()
      create_task_with_exc_handling(stub.node_request(request))

    def on_current_text_changed(text):
      if text is None:
        return
      edit.setPlainText(text)

    edit.on_apply = send_message
    list.currentTextChanged.connect(on_current_text_changed)
    list.itemDoubleClicked.connect(send_message)

    splitter = QSplitter(Qt.Vertical)
    splitter.addWidget(list)
    splitter.addWidget(edit)
    splitter.setSizes([1_000_000, 1])

    layout = QVBoxLayout()
    layout.addWidget(splitter)
    self.setLayout(layout)
