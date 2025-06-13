from ..qt import *
from ..observable_item_models import TreeObservableCollectionModel
from ..config import ObservableDict

import logging

LOGGER = logging.getLogger(__name__)

class PersistenceWidget(QWidget):
  def __init__(self, config: ObservableDict):
    super().__init__()
    print(config)

    if 'Cached' not in config:
      config['Cached'] = []
    cached = config['Cached']

    model = TreeObservableCollectionModel(cached, key_column='#', columns=['Address'], show_extra_values=False, is_editable=lambda *arg: True)

    qlist = QTreeView()
    qlist.setModel(model)

    add_button = QPushButton('Add')
    remove_button = QPushButton('Remove')

    def on_add():
      print(cached)
      cached.append({
        'Address': '',
      })

    def on_remove():
      for row in sorted(set(i.row() for i in qlist.selectedIndexes()), reverse=True):
        del cached[row]

    add_button.clicked.connect(on_add)
    remove_button.clicked.connect(on_remove)

    layout = QVBoxLayout()
    layout.addWidget(QLabel('Cached Properties:'))
    layout.addWidget(qlist)
    button_layout = QHBoxLayout()
    button_layout.addWidget(add_button)
    button_layout.addWidget(remove_button)
    layout.addLayout(button_layout)
    self.setLayout(layout)

