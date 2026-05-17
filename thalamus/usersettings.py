import json
import typing
import pathlib
import logging

from .qt import *

DEFAULT_SETTINGS = {
  'crashpad_consent_requested': False,
  'crashpad_consent_answer': False
}

SETTINGS_PATH = pathlib.Path.home() / '.thalamus' / 'settings.json'

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


  