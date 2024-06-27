echo MSVC
echo QQQQQQ $1 $2 $3 $4

$1 --target-os=win64 --arch=x86_64 --toolchain=msvc --enable-sdl --enable-static --disable-shared --extra-cflags="$3" $4 --prefix=$2 | tee ffmpeg_config.log  
sed -i s/-Z7// ffbuild/config.mak
