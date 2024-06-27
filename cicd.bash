source /$1/bin/activate
which python3
ls -l
source /root/.thalamusrc
source /etc/profile.d/VimbaUSBTL_64bit.sh
env | sort
echo $PATH
python3 setup.py bdist_wheel --generator Ninja
PLATFORM_TAG=`ldd --version | awk '/ldd/ {print "manylinux_" $NF}' | sed "s/\\./_/"`
LINUX_NAME=`ls dist`
MANYLINUX_NAME=`echo $LINUX_NAME | sed s/linux/$PLATFORM_TAG/`
echo dist/$LINUX_NAME dist/$MANYLINUX_NAME
mv dist/$LINUX_NAME dist/$MANYLINUX_NAME
ls dist
