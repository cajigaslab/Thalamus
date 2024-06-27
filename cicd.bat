where python

set
dir

python setup.py generate
python setup.py bdist_wheel --generator Ninja --boost-binary-dir=C:\boost --cwd %cd%

dir dist
