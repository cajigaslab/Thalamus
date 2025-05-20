import time
import json
import asyncio
import grpc

from ..qt import *
from .. import thalamus_pb2
from ..task_controller.util import create_task_with_exc_handling
from ..util import IterableQueue

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
    self.qlist.setAutoScroll(True)
    edit = ChatEdit()

    stream = stub.logout(thalamus_pb2.Empty())
    queue = IterableQueue()
    log_call = stub.log(queue)

    self.task = create_task_with_exc_handling(self.stream_processor(stream))

    def send_message(item = None):
      text = edit.toPlainText()
      edit.clear()
      create_task_with_exc_handling(queue.put(thalamus_pb2.Text(text=text,time=time.perf_counter_ns())))

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
        scrollbar = self.qlist.verticalScrollBar()
        rescroll = scrollbar is not None and scrollbar.value() == scrollbar.maximum()
        self.qlist.addItem(text.text)
        if rescroll:
          self.qlist.scrollToBottom()
    except asyncio.CancelledError:
      pass
    except grpc.aio.AioRpcError:
      pass

  def closeEvent(self, e):
    self.task.cancel()
  
