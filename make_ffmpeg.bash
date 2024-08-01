echo MAKE $1
VERBOSE=1 make -j $1 && make install
