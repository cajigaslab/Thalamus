echo CLANG
echo QQQQQQ $1 $2 $3 $4

PATH=/c/Program\ Files/LLVM/bin:$PATH
echo PATH=$PATH
$1 --target-os=win64 --arch=x86_64 --cc=clang --enable-static --disable-shared --extra-cflags="$3" $4 --prefix=$2 | tee ffmpeg_config.log  
sed -i s/LIBPREF=lib/LIBPREF=/ ffbuild/config.mak
sed -i s/LIBSUF=.a/LIBSUF=.lib/ ffbuild/config.mak
echo "void thalamus_m_stub() {}" > m_sub.cpp
clang++ -c m_sub.cpp -o m_sub.o
llvm-ar rc m.lib m_sub.o
mkdir /usr/local/bin || true
ln -s "`which llvm-strip`" /usr/local/bin/strip
