from ..util import NodeSelector
from ..qt import *

class AlgebraWidget(QWidget):
  def __init__(self, config, stub):
    super().__init__()
    self.config = config
    node_selector = NodeSelector(config, 'Source', False)

    equation_edit = QLineEdit()

    grid = QGridLayout()
    grid.addWidget(QLabel('Equation:'), 0, 0)
    grid.addWidget(equation_edit, 0, 1)
    grid.addWidget(node_selector, 1, 0)
    grid.setColumnStretch(0, 0)
    grid.setColumnStretch(1, 1)
    grid.setRowStretch(0, 0)
    grid.setRowStretch(1, 1)
    self.setLayout(grid)

    def on_equation():
      pass

    equation_edit.editingFinished.connect(lambda: self.config.update({'Equation': equation_edit.text()}))

    def on_change(a, k, v):
      if k == 'Equation':
        equation_edit.setText(v)
      elif k == 'Parser Error':
        if v:
          equation_edit.setStyleSheet('color: red')
        else:
          equation_edit.setStyleSheet('color: black')

    self.config.add_observer(on_change, lambda: isdeleted(self))
