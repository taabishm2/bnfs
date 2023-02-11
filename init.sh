#!/bin/bash
export INSTALL_PATH="/bnfs/installs"

sudo apt-get update
sudo apt-get install -y build-essential wget gcc g++ gdb git vim htop curl autoconf libtool pkg-config fuse libfuse-dev libssl-dev

sudo apt-get remove cmake
wget https://github.com/Kitware/CMake/releases/download/v3.22.2/cmake-3.22.2-linux-x86_64.sh
/bin/sh cmake-3.22.2-linux-x86_64.sh -- --skip-license --prefix=$INSTALL_PATH

echo "PATH=$INSTALL_PATH/bin:\$PATH" >> ~/.bashrc
source ~/.bashrc
echo "set tabstop=4" > ~/.vimrc

sudo mkdir -p /bnfs
sudo chown chrahul5 /bnfs
mkdir -p $INSTALL_PATH /bnfs/repo /bnfs/grpc

sudo apt-get remove cmake
wget https://github.com/Kitware/CMake/releases/download/v3.22.2/cmake-3.22.2-linux-x86_64.sh
/bin/sh cmake-3.22.2-linux-x86_64.sh -- --skip-license --prefix=$INSTALL_PATH

echo "PATH=$INSTALL_PATH/bin:\$PATH" >> ~/.bashrc
source ~/.bashrc
git clone https://github.com/taabishm2/bnfs.git /bnfs/repo

git clone --recurse-submodules -b v1.43.0 https://github.com/grpc/grpc /bnfs/grpc
cd /bnfs/grpc
mkdir -p cmake/build
pushd cmake/build
$INSTALL_PATH/bin/cmake -DgRPC_INSTALL=ON \
      -DgRPC_BUILD_TESTS=OFF \
      -DCMAKE_INSTALL_PREFIX=$INSTALL_PATH \
      ../..
make -j 4
make install
popd

cd /etc/ld.so.conf.d
echo $INSTALL_PATH | sudo tee bnfs.conf
sudo /sbin/ldconfig

