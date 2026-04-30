import pathlib
from thalamus.pipeline.thalamus_window import Factory, UserData, UserDataType
from thalamus.task_controller.task_context import TaskDescription
#import ext_task
import platform

def widgets():
  return {
    'EXT_CECI': Factory(None, [
    ])
  }

def library():
  #return [pathlib.Path.cwd() / 'ext.dll', pathlib.Path.cwd() / 'ext2.dll']
  if platform.system() == 'Windows':
    #return pathlib.Path.cwd() / 'rust/target/debug/thalamus_rs.dll'
    return pathlib.Path.cwd() / 'ceci/ext.dll'
  else:
    return pathlib.Path.cwd() / 'ceci/libext.so'

def tasks():
  return [
    #TaskDescription('ext_task', 'Ext Task', ext_task.create_widget, ext_task.run)
  ]
