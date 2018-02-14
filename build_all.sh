#!/bin/bash
set -e
source setup_env.sh

# choose local directory where packages will be installed
export INSTDIR=`pwd`/install

# to build xtcdata with cmake
cd xtcdata
mkdir -p build
cd build
cmake -DCMAKE_INSTALL_PREFIX=$INSTDIR ..
make -j 4 install
cd ../..

# to build psdaq and drp (after building xtcdata) with cmake
cd psdaq
mkdir -p build
cd build
cmake -DCMAKE_INSTALL_PREFIX=$INSTDIR ..
make -j 4 install
cd ../..

pyver=$(python -c "import sys; print(str(sys.version_info.major)+'.'+str(sys.version_info.minor))")
# "python setup.py develop" seems to not create this for you
# (although "install" does)
mkdir -p $INSTDIR/lib/python$pyver/site-packages/

# to build psana with setuptools
cd psana
python setup.py develop --xtcdata=$INSTDIR --prefix=$INSTDIR
cd ..
# build ami
cd ami
python setup.py develop --prefix=$INSTDIR