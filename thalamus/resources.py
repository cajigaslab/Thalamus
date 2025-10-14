import thalamus
import typing

try:
  #New resource import
  import importlib.resources

  def get_path(arg1: typing.Any, arg2: str = None) -> str:
    if arg2 is None:
      arg2 = arg1
      arg1 = thalamus
      
    with importlib.resources.path(arg1, arg2) as p:
      return str(p)
        
  def read_text(arg1: typing.Any, arg2: str = None) -> str:
    if arg2 is None:
      arg2 = arg1
      arg1 = thalamus

    return importlib.resources.read_text(arg1, arg2)

except ImportError:
  #Old resource import
  import pkg_resources

  def get_path(arg1: typing.Any, arg2: str = None) -> str:
    if arg2 is None:
      arg2 = arg1
      arg1 = __name__

    return pkg_resources.resource_filename(arg1, arg2)

  def read_text(arg1: typing.Any, arg2: str = None) -> str:
    if arg2 is None:
      arg2 = arg1
      arg1 = __name__

    return pkg_resources.resource_string(arg1, arg2)
