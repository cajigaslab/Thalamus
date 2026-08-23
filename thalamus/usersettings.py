import json
import typing
import pathlib
import logging

from .qt import *

DEFAULT_SETTINGS = {
  'crashpad_consent_requested': False,
  'crashpad_consent_answer': False,
  'dotnet_runtime_warning_suppressed': False
}

SETTINGS_PATH = pathlib.Path.home() / '.thalamus' / 'settings.json'

DOTNET_DOWNLOAD_URL = 'https://aka.ms/dotnet/8.0/aspnetcore-runtime-win-x64.exe'

LOGGER = logging.getLogger(__name__)

def get() -> typing.Dict[str, typing.Any]:
  SETTINGS_PATH.parent.mkdir(exist_ok=True)
  if not SETTINGS_PATH.exists():
    with SETTINGS_PATH.open("w") as f:
      json.dump(DEFAULT_SETTINGS, f)

  with SETTINGS_PATH.open("r") as f:
    return json.load(f)
  
def data_collection_consent():
  settings = get()

  if not settings['crashpad_consent_requested']:
    reply = QMessageBox.question(
      None,
      "Data Collection",
      "Allow Thalamus to upload crash reports?",
      QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No)
    settings['crashpad_consent_requested'] = True
    settings['crashpad_consent_answer'] = reply == QMessageBox.StandardButton.Yes

    with SETTINGS_PATH.open("w") as f:
      json.dump(settings, f)
    
  LOGGER.info(settings)
  return settings['crashpad_consent_answer']

def warn_dotnet_runtime_missing():
  '''
  Warns the user that the .NET runtime dotnet.exe needs is missing, with a
  link to the installer. Skipped entirely if the user previously checked
  "Don't show this again".
  '''
  settings = get()

  if settings.get('dotnet_runtime_warning_suppressed'):
    return

  box = QMessageBox()
  box.setIcon(QMessageBox.Icon.Warning)
  box.setWindowTitle('.NET Runtime Missing')
  box.setTextFormat(Qt.TextFormat.RichText)
  box.setText(
    'Thalamus could not find the .NET 8 ASP.NET Core Runtime, so some '
    'features such as Delsys integration will not be supported.<br><br>'
    f'Download and install it from:<br><a href="{DOTNET_DOWNLOAD_URL}">{DOTNET_DOWNLOAD_URL}</a>'
  )
  label = box.findChild(QLabel, 'qt_msgbox_label')
  if label is not None:
    label.setTextInteractionFlags(Qt.TextInteractionFlag.TextBrowserInteraction)
    label.setOpenExternalLinks(True)
  checkbox = QCheckBox("Don't show this again")
  box.setCheckBox(checkbox)
  box.exec()

  if checkbox.isChecked():
    settings['dotnet_runtime_warning_suppressed'] = True
    with SETTINGS_PATH.open("w") as f:
      json.dump(settings, f)
