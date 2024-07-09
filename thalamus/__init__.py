import platform
import importlib.resources

THALAMUS_ANCHOR = importlib.resources.files('thalamus')
EXE_SUFFIX = '.exe' if platform.system() == 'Windows' else ''
native_exe = THALAMUS_ANCHOR / ('native' + EXE_SUFFIX)
