import sys
import subprocess
import pkg_resources

EXECUTABLE_EXTENSION = '.exe' if sys.platform == 'win32' else ''
BMBI_EXECUTABLE = pkg_resources.resource_filename('thalamus', 'native' + EXECUTABLE_EXTENSION)

def main():
  result = subprocess.run([BMBI_EXECUTABLE, 'hydrate'] + sys.argv[1:])
  sys.exit(result.returncode)

if __name__ == '__main__':
  main()
