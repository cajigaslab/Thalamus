import sys
import subprocess
from .resources import get_path

EXECUTABLE_EXTENSION = '.exe' if sys.platform == 'win32' else ''
BMBI_EXECUTABLE = get_path('native' + EXECUTABLE_EXTENSION)

def main():
  result = subprocess.run([BMBI_EXECUTABLE, 'hydrate'] + sys.argv[1:])
  sys.exit(result.returncode)

if __name__ == '__main__':
  main()
