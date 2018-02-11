#!/usr/bin/env bash

set -o xtrace

DISTRO_CFG=""
if [[ "$OSTYPE" == "linux-gnu" ]]; then
    CPACK_TYPE="TBZ2"
    distro=$(lsb_release -i -c -s|tr '\n' '_')
    DISTRO_CFG="-DRAIBLOCKS_DISTRO_NAME=${distro}"
elif [[ "$OSTYPE" == "darwin"* ]]; then
    CPACK_TYPE="DragNDrop"
elif [[ "$OSTYPE" == "cygwin" ]]; then
    CPACK_TYPE="TBZ2"
elif [[ "$OSTYPE" == "msys" ]]; then
    CPACK_TYPE="NSIS" #?
elif [[ "$OSTYPE" == "win32" ]]; then
    CPACK_TYPE="NSIS"
elif [[ "$OSTYPE" == "freebsd"* ]]; then
    CPACK_TYPE="TBZ2"
    DISTRO_CFG="-DRAIBLOCKS_DISTRO_NAME='freebsd'"
else
    CPACK_TYPE="TBZ2"
fi

if [[ ${SIMD} -eq 1 ]]; then
    SIMD_CFG="-DRAIBLOCKS_SIMD_OPTIMIZATIONS=ON"
    CRYPTOPP_CFG="-DCRYPTOPP_CUSTOM=ON"
else
    SIMD_CFG=""
    CRYPTOPP_CFG=""
fi

if [[ ${ASAN_INT} -eq 1 ]]; then
    SANITIZERS="-DRAIBLOCKS_ASAN_INT=ON"
elif [[ ${ASAN} -eq 1 ]]; then
    SANITIZERS="-DRAIBLOCKS_ASAN=ON"
elif [[ ${TSAN} -eq 1 ]]; then
    SANITIZERS="-DRAIBLOCKS_TSAN=ON"
else
    SANITIZERS=""
fi

if [[ "${BOOST_ROOT}" -ne "" ]]; then
    BOOST_CFG="-DBOOST_ROOT='${BOOST_ROOT}'"
else
    BOOST_CFG=""
fi

BUSYBOX_BASH=${BUSYBOX_BASH-0}
if [[ ${FLAVOR-_} == "_" ]]; then
    FLAVOR=""
fi

set -o nounset

run_build() {
    build_dir=build_${FLAVOR}

    mkdir ${build_dir}
    cd ${build_dir}
    cmake -GNinja \
       -DRAIBLOCKS_GUI=ON \
       -DCMAKE_BUILD_TYPE=Release \
       -DCMAKE_VERBOSE_MAKEFILE=ON \
       -DCMAKE_INSTALL_PREFIX="../install" \
       ${CRYPTOPP_CFG} \
       ${DISTRO_CFG} \
       ${SIMD_CFG} \
       -DBOOST_ROOT=/usr/local/boost \
       ${BOOST_CFG} \
       ${SANITIZERS} \
       ..

    cmake --build ${PWD} -- -v
    cmake --build ${PWD} -- install -v
    cpack -G ${CPACK_TYPE} ${PWD}
    sha1sum *.tar* > SHA1SUMS
}

run_build
