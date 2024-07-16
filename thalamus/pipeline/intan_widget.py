from ..qt import *
import bisect
from ..config import ObservableCollection
import typing
import functools

class IntanWidget(QWidget):
  def __init__(self, node, stub):
    super().__init__()

    if 'Channels' not in node:
      node['Channels'] = []
    channels = node['Channels']

    layout = QVBoxLayout()
    qlist = QListWidget()
    edit = QLineEdit()
    remove_button = QPushButton('Remove')

    layout.addWidget(qlist)
    layout.addWidget(edit)
    layout.addWidget(remove_button)

    self.setLayout(layout)

    def on_enter():
      text = edit.text()
      i = bisect.bisect_left(channels, text)
      if i < len(channels) and text == channels[i]:
        return
      channels.insert(i, text)
      edit.clear()
    edit.returnPressed.connect(on_enter)

    def on_remove():
      selected_text = (i.text() for i in qlist.selectedItems())
      for text in selected_text:
        i = bisect.bisect_left(channels, text)
        del channels[i]
    remove_button.clicked.connect(on_remove)
    
    def on_change(action: ObservableCollection.Action, key: typing.Any, value: typing.Any):
      if action == ObservableCollection.Action.SET:
        qlist.insertItem(key, value)
      else:
        qlist.takeItem(key)

    channels.add_observer(on_change, functools.partial(isdeleted, self))
    for k, v in enumerate(channels):
      on_change(ObservableCollection.Action.SET, k, v)

