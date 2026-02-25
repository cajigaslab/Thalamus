import pathlib
from thalamus.pipeline.thalamus_window import Factory, UserData, UserDataType

def widgets():
  return {
    'EXT_DEMO': Factory(None, [
      UserData(UserDataType.CHECK_BOX, 'Running', False, []),
      UserData(UserDataType.DOUBLE_SPINBOX, 'Amplitude', 1.0, []),
      UserData(UserDataType.DOUBLE_SPINBOX, 'Frequency', 1.0, []),
    ])
  }

def library():
  return pathlib.Path.cwd() / 'ext.dll'