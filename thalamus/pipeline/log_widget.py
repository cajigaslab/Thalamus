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

    self.qlist = QListWidget()
    edit = ChatEdit()

    stream = stub.logout(thalamus_pb2.Empty())

    self.task = create_task_with_exc_handling(self.stream_processor(stream))

    def send_message(item = None):
      text = edit.toPlainText()
      request = thalamus_pb2.NodeRequest(
        node = config['name'],
        json = json.dumps(text)
      )
      self.qlist.addItem(text)
      edit.clear()
      create_task_with_exc_handling(stub.node_request(request))

    def on_current_text_changed(text):
      if text is None:
        return
      edit.setPlainText(text)

    edit.on_apply = send_message
    self.qlist.currentTextChanged.connect(on_current_text_changed)
    self.qlist.itemDoubleClicked.connect(send_message)

    splitter = QSplitter(Qt.Orientation.Vertical)
    splitter.addWidget(self.qlist)
    splitter.addWidget(edit)
    splitter.setSizes([1_000_000, 1])

    layout = QVBoxLayout()
    layout.addWidget(splitter)
    self.setLayout(layout)

  async def stream_processor(self, stream):
    try:
      async for text in stream:
        self.qlist.addItem(text.text)
    except asyncio.CancelledError:
      pass
    except grpc.aio.AioRpcError:
      pass

  def closeEvent(self, e):
    self.task.cancel()
  
