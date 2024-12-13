import sys
import ast
import numpy
import queue
import typing
import asyncio
import traceback
import threading
import multiprocessing

from matplotlib.figure import Figure
from thalamus.qt import *

from ..thalamus_pb2 import StimRequest, AnalogResponse, Span, NodeSelector
from ..thalamus_pb2_grpc import ThalamusStub
from ..config import ObservableDict
from ..util import IterableQueue
from ..task_controller.util import create_task_with_exc_handling

def evaluate(code_str: str, queue: multiprocessing.Queue):
  try:
    code_ast = ast.parse(code_str)
    assert len(code_ast.body) > 0, 'Expression must end with a function declaration'
    assert isinstance(code_ast.body[-1], ast.FunctionDef), 'Expression must end with a function declaration'
    func_name = code_ast.body[-1].name
    compiled = compile(code_ast, 'stim_subprocess', 'exec')
    exec(compiled,globals())
    output = globals()[func_name]()
    try:
      y_len = len(output)
    except TypeError:
      raise RuntimeError(f'Expected a list, got {output}')
    queue.put(output)
  except:
    queue.put(traceback.format_exc())
    raise

class StimWidget(QWidget):
  def __init__(self, config: ObservableDict, stub: ThalamusStub):
    super().__init__()
    if 'Stims' not in config:
      config['Stims'] = []

    loop = asyncio.get_running_loop()
    figure = Figure(figsize=(5, 4), dpi=100)
    canvas = FigureCanvasQTAgg(figure)
    canvas.axes = figure.add_subplot(111)
    t = numpy.linspace(0, 5, 2000)
    canvas.axes.plot(t, numpy.sin(t))
    
    message_queue = IterableQueue()
    stream = stub.stim(message_queue)
    request = StimRequest()
    request.node.name = config['name']
    create_task_with_exc_handling(message_queue.put(request))

    def update_view(y):
      x = numpy.arange(len(y[0]))
      canvas.axes.cla()
      while canvas_layout.count() > 1:
        item = canvas_layout.takeAt(1)
        assert item is not None
        widget = item.widget()
        assert widget is not None
        widget.deleteLater()
      channel_name_widgets.clear()
      for y2 in y:
        canvas.axes.plot(x, y2)
        edit = QLineEdit()
        edit.moveToThread(canvas_layout.thread())
        channel_name_widgets.append(edit)
        canvas_layout.addWidget(edit)
      canvas.draw()
      error.setText('')
      self.samples = y
    
    async def stream_processor():
      async for response in stream:
        if response.error.code:
          error.setText(response.error.message)
        elif response.HasField('declaration'):
          y = []
          for span in response.declaration.data.spans:
            y.append(response.declaration.data.data[span.begin:span.end])
          update_view(y)
          for i, span in enumerate(response.declaration.data.spans):
            channel_name_widgets[i].setText(span.name)
          frequency.setValue(1e9/response.declaration.data.sample_intervals[0])
          trigger.setText(response.declaration.trigger)

    self.stream_task = create_task_with_exc_handling(stream_processor())

    canvas_layout = QVBoxLayout()
    channel_name_widgets: typing.List[QLineEdit] = []
    canvas_layout.addWidget(canvas)
    canvas_container = QWidget()
    canvas_container.setLayout(canvas_layout)

    splitter = QSplitter(Qt.Orientation.Vertical)

    edit_layout = QVBoxLayout()
    edit = QTextEdit()
    error = QLabel()
    apply = QPushButton('Eval')
    cancel = QPushButton('Cancel')
    edit_layout.addWidget(edit)
    edit_layout.addWidget(error)
    eval_button_layout = QHBoxLayout()
    eval_button_layout.addWidget(apply)
    eval_button_layout.addWidget(cancel)
    edit_layout.addLayout(eval_button_layout)
    edit_widget = QWidget()
    edit_widget.setLayout(edit_layout)

    font = QFont('Monospace')
    font.setStyleHint(QFont.StyleHint.TypeWriter)
    edit.setFont(font)

    splitter.addWidget(edit_widget)
    splitter.addWidget(canvas_container)

    frequency = QDoubleSpinBox()
    frequency.setRange(1e-2, 1e100)
    frequency.setValue(1000.0)
    frequency_layout = QHBoxLayout()
    frequency_layout.addWidget(QLabel('Frequency:'))
    frequency_layout.addWidget(frequency)

    trigger = QLineEdit()
    trigger_layout = QHBoxLayout()
    trigger_layout.addWidget(QLabel('Trigger:'))
    trigger_layout.addWidget(trigger)

    id = QSpinBox()
    id_layout = QHBoxLayout()
    id_layout.addWidget(QLabel('ID:'))
    id_layout.addWidget(id)

    declare = QPushButton('Declare')
    view = QPushButton('View')
    arm = QPushButton('Arm')
    trigger_button = QPushButton('Trigger')

    button_layout = QHBoxLayout()
    button_layout.addWidget(declare)
    button_layout.addWidget(view)
    button_layout.addWidget(arm)
    button_layout.addWidget(trigger_button)

    layout = QVBoxLayout()
    layout.addWidget(splitter)
    layout.addLayout(frequency_layout)
    layout.addLayout(trigger_layout)
    layout.addLayout(id_layout)
    layout.addLayout(button_layout)
    self.setLayout(layout)
    request_id = 0

    self.process: typing.Optional[multiprocessing.Process] = None
    self.queue = multiprocessing.Queue()
    self.running = True
    self.samples = None
    def queue_processor():
      def inner(y):
        print('got', y)
        if isinstance(y, str):
          error.setText(y)
        else:
          try:
            y_len = len(y[0])
          except TypeError:
            x = numpy.arange(len(y))
            canvas.axes.cla()
            canvas.axes.plot(x, y)
            canvas.draw()
            error.setText('')
          else:
            update_view(y)

      try:
        while self.running:
          try:
            y = self.queue.get(True, 1)
          except queue.Empty:
            continue
          loop.call_soon_threadsafe(lambda y=y: inner(y))
      except:
        traceback.print_exc()
        raise


    self.queue_thread = threading.Thread(target=queue_processor)
    self.queue_thread.start()

    def on_apply():
      on_cancel()
      args = edit.toPlainText(), self.queue
      print(args)
      self.process = multiprocessing.Process(target=evaluate,args=args)
      self.process.start()
      error.setText('Busy')
    apply.clicked.connect(on_apply)

    def on_cancel():
      if self.process is not None:
        self.process.kill()
        self.process.join()
        self.process = None
      error.setText('')
    cancel.clicked.connect(on_cancel)

    def on_declare():
      nonlocal request_id
      request = StimRequest()
      declaration = request.declaration
      data = request.declaration.data
      sample_interval = int(1e9/frequency.value())
      if self.samples is None:
        return

      for i, s in enumerate(self.samples):
        begin = len(data.data)
        edit = channel_name_widgets[i]
        data.data.extend(s)
        data.spans.append(Span(begin=begin,end=len(data.data),name=edit.text()))
        data.sample_intervals.append(sample_interval)

      declaration.id = id.value()
      declaration.trigger = trigger.text()
      request_id += 1
      request.id = request_id
      create_task_with_exc_handling(message_queue.put(request))
    declare.clicked.connect(on_declare)

    def on_arm():
      nonlocal request_id
      request = StimRequest()
      request.arm = id.value()
      request_id += 1
      request.id = request_id
      create_task_with_exc_handling(message_queue.put(request))
    arm.clicked.connect(on_arm)

    def on_trigger():
      nonlocal request_id
      request = StimRequest()
      request.trigger = id.value()
      request_id += 1
      request.id = request_id
      create_task_with_exc_handling(message_queue.put(request))
    trigger_button.clicked.connect(on_trigger)

    def on_retrieve():
      nonlocal request_id
      request = StimRequest()
      request.retrieve = id.value()
      request_id += 1
      request.id = request_id
      create_task_with_exc_handling(message_queue.put(request))
    view.clicked.connect(on_retrieve)

  def closeEvent(self, e):
    self.cleanup()
    print('STIMCLOSE')
    self.running = False
    print(1)
    self.queue.cancel_join_thread()
    print(2)
    self.queue_thread.join()
    print(3)
    if self.process is not None:
      print(4)
      self.process.kill()
      print(5)
      self.process.join()
      print(6)

