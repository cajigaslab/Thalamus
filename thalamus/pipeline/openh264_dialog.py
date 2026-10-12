"""Preferences > H264: turns the use of Cisco's OpenH264 binary on and off and
shows the text Cisco's license requires (see thalamus.openh264). The choice is
applied when the dialog closes."""
from __future__ import annotations

import typing

from ..qt import *
from .. import openh264

class OpenH264Dialog(QDialog):
  def __init__(self, parent: typing.Optional[QWidget] = None):
    super().__init__(parent)
    self.setWindowTitle('H264')

    layout = QVBoxLayout()

    # License condition 2: users control the binary's use.
    self.was_enabled = not openh264.is_opted_out()
    self.enabled_checkbox = QCheckBox('Use OpenH264 to encode H.264')
    self.enabled_checkbox.setChecked(self.was_enabled)
    layout.addWidget(self.enabled_checkbox)

    # License condition 3: shown where the binary's use is controlled.
    layout.addWidget(QLabel(openh264.ATTRIBUTION))

    # License condition 4: Cisco's license text where licensing information
    # is presented.
    license_view = QTextEdit()
    license_view.setReadOnly(True)
    license_view.setPlainText(openh264.LICENSE_TEXT)
    layout.addWidget(license_view)

    close_button = QPushButton('Close')
    close_button.clicked.connect(self.close)
    buttons = QHBoxLayout()
    buttons.addStretch()
    buttons.addWidget(close_button)
    layout.addLayout(buttons)

    self.setLayout(layout)
    # Emitted however the dialog closes (Close, the window's close button,
    # Escape).
    self.finished.connect(self.on_finished)

  def on_finished(self, _result: int) -> None:
    enabled = self.enabled_checkbox.isChecked()
    if enabled == self.was_enabled:
      return
    if enabled:
      openh264.opt_in()
    else:
      openh264.opt_out()
    self.was_enabled = enabled
