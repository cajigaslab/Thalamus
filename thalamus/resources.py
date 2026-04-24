import thalamus
import typing

try:
  #New resource import
  import importlib
  import importlib.resources

  def get_path(arg1: typing.Any, arg2: str = None) -> str:
    if arg2 is None:
      arg2 = arg1
      arg1 = thalamus
      
    try:
      with importlib.resources.path(arg1, arg2) as result:
        return str(result)
    except FileNotFoundError as e:
      return e.filename
        
  def read_text(arg1: typing.Any, arg2: str = None) -> str:
    if arg2 is None:
      arg2 = arg1
      arg1 = thalamus

    if isinstance(arg1, str):
      arg1 = importlib.__import__(arg1)
    return (importlib.resources.files(arg1) / arg2).read_text()

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
