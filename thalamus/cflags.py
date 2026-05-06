import pathlib

def main():
  path = pathlib.Path(__file__)
  print(f'-I{path.parent}')

if __name__ == '__main__':
  main()