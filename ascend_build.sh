#!/usr/bin/env bash

function build_dipu_py() {
    export CMAKE_BUILD_TYPE=debug
    export _GLIBCXX_USE_CXX11_ABI=1
    export MAX_JOBS=12
    # PYTORCH_INSTALL_DIR is /home/fandaoyi.p/torch20/pytorch/torch
    # python  setup.py build_clib 2>&1 | tee ./build1.log
    python setup.py build_ext 2>&1 | tee ./build1.log
    cp build/python_ext/torch_dipu/_C.cpython-*.so torch_dipu
}

function config_dipu_cmake() {
    cd ./build && rm -rf ./*
    PYTORCH_DIR="/home/zhaoguochun/pytorch"
    PYTHON_INCLUDE_DIR="/opt/rh/rh-python38/root/usr/include/python3.8/"
    # export NEUWARE_ROOT="/usr/local/neuware-2.4.1/"
    cmake ../  -DCMAKE_BUILD_TYPE=Debug \
     -DCAMB=OFF -DASCEND=ON -DPYTORCH_DIR=${PYTORCH_DIR} \
     -DPYTHON_INCLUDE_DIR=${PYTHON_INCLUDE_DIR} \
     -DCMAKE_C_FLAGS_DEBUG="-g -O0" \
     -DCMAKE_CXX_FLAGS_DEBUG="-g -O0"
    cd ../
}

function autogen_diopi_wrapper() {
    cd torch_dipu/csrc_dipu/aten/ops/autogen_diopi_wrapper/
    python autogen_diopi_wrapper2.py
    cd -
}



function build_dipu_lib() {

    export DIOPI_ROOT=/home/zhaoguochun/dipu_poc/torch_dipu
    export LIBRARY_PATH=$DIOPI_ROOT:$LIBRARY_PATH;

    config_dipu_cmake
    autogen_diopi_wrapper
    #  2>&1 | tee ./build1.log
    cd build && make -j8  2>&1 | tee ./build1.log &&  cd ..
    cp ./build/torch_dipu/csrc_dipu/libtorch_dipu.so   ./torch_dipu
    cp ./build/torch_dipu/csrc_dipu/libtorch_dipu_python.so   ./torch_dipu
}


if [[ "$1" == "builddl" ]]; then
    build_dipu_lib
elif [[ "$1" == "builddp" ]]; then
    build_dipu_py
fi