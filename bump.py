import sys
import toml
import subprocess
import configparser

def main():
  config = toml.load('pyproject.toml')

  version = config['project']['version']
  parts = [int(i) for i in version.split('.')]
  if sys.argv[1] == 'major':
    parts[0] += 1
    parts[1] = 0
    parts[2] = 0
  elif sys.argv[1] == 'minor':
    parts[1] += 1
    parts[2] = 0
  elif sys.argv[1] == 'patch':
    parts[2] += 1

  new_version = '.'.join(str(i) for i in parts)
  config['project']['version'] = new_version
  
  with open('pyproject.toml', 'w') as pyproject_file:
    toml.dump(config, pyproject_file)

  subprocess.check_call(['git', 'add', 'pyproject.toml'])
  subprocess.check_call(['git', 'commit', '-m', f'[skip ci] v{new_version}'])
  subprocess.check_call(['git', 'tag', '-a', f'v{new_version}', '-m', f'[skip ci] v{new_version}'])
 

if __name__ == '__main__':
  main()
