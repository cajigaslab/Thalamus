from ..qt import *
from ..pipeline.data_widget import NodesModel, ChannelComboBox

class TouchDialog(QWidget):
  def __init__(self, config, stub):
    super().__init__()
    self.setWindowTitle('Touch Input Config')
    nodes_model = NodesModel(config['nodes'])

    if 'touch_config' not in config:
      config['touch_config'] = {
        'selected_node': '',
        'x': '',
        'y': '',
      }
    touch_config = config['touch_config']
    if touch_config.get('selected_node', '') == '':
      touch_config['selected_node'] = nodes_model.data(nodes_model.index(0, 0)) if nodes_model.rowCount(QModelIndex()) else ''

    node_combo = QComboBox()
    node_combo.setModel(nodes_model)

    x_channel_combo = ChannelComboBox(stub, touch_config, 'x')
    x_channel_combo.setEditable(True)
    y_channel_combo = ChannelComboBox(stub, touch_config, 'y')
    y_channel_combo.setEditable(True)

    layout = QGridLayout()
    layout.addWidget(QLabel('Node:'), 0, 0)
    layout.addWidget(QLabel('X:'), 1, 0)
    layout.addWidget(QLabel('Y:'), 2, 0)
    layout.addWidget(node_combo, 0, 1)
    layout.addWidget(x_channel_combo, 1, 1)
    layout.addWidget(y_channel_combo, 2, 1)

    self.setLayout(layout)

    node_combo.currentTextChanged.connect(lambda text: touch_config.update({'selected_node': text}))
    x_channel_combo.currentTextChanged.connect(lambda text: touch_config.update({'x': text}))
    y_channel_combo.currentTextChanged.connect(lambda text: touch_config.update({'y': text}))

    def on_change(action, key, value):
      if key == 'selected_node':
        node_combo.setCurrentText(value)
      elif key == 'x':
        x_channel_combo.setCurrentText(value)
      elif key == 'y':
        y_channel_combo.setCurrentText(value)

    touch_config.add_observer(on_change, lambda: isdeleted(self))

    for k, v in touch_config.items():
      on_change(None, k, v)


