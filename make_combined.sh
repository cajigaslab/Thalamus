mkdir -p libc++-objects
mkdir -p libc++abi-objects
mkdir -p combined
cd combined
if [[ `uname` == 'Darwin' ]]; then
  echo Archive Darwin
  clang -r -nostdlib $1 -o libc++.o -Wl,-all_load ../libc++.a ../libc++abi.a
else
  echo Archive Linux
  clang -r -nostdlib -o libc++.o -Wl,--whole-archive ../libc++.a ../libc++abi.a
fi
ar cr "libc++.a" "libc++.o"