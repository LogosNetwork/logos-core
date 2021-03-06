#!/bin/bash

function usage {
    echo "usage: ./build.sh [-t type_name] [-n network_name] [-s] [-c num_cpus] [--reject] [-r --rebuild] [clean]"
    echo "  -h  | display help"
    echo "  -t, --CMAKE_BUILD_TYPE type_name
      | specify build type, a CMake variable"
    echo "      | can be one of \"Debug\", \"Release\", \"RelWithDebInfo\", \"MinSizeRel\""
    echo "      | defaults to \"Debug\""
    echo "  -n, --ACTIVE_NETWORK network_name
      | specify active network, a CMake variable"
    echo "      | can be one of \"logos_test_network\", \"logos_beta_network\", \"logos_live_network\""
    echo "      | defaults to \"logos_test_network\""
    echo "  -c, --cpus num_cpus
      | specify number of (virtual) CPUs to use for compiling c++ executable"
    echo "      | defaults to max virtual cpu minus one"
    echo "  -s
      | require all delegates' approval for transaction validation instead of two thirds for testing purposes"
    echo "      | defaults to off"
    echo "  --reject
      | makes delegate randomly reject transactions inside PrePrepares"
    echo "  -r, --rebuild
      | bypasses all checks and simply call make logos_core"
    echo "  clean
      | clean up build directory."
    return 0
}

# Parse long arguments (optional) to be later used as CMake variables
OPTIONS=t:n:c:srh
LONGOPTS=CMAKE_BUILD_TYPE:,ACTIVE_NETWORK:,cpus:,reject,rebuild,help

# -temporarily store output to be able to check for errors
# -activate quoting/enhanced mode (e.g. by writing out “--options”)
# -pass arguments only via   -- "$@"   to separate them correctly
! PARSED=$(getopt --options=${OPTIONS} --longoptions=${LONGOPTS} --name "$0" -- "$@")
if [[ ${PIPESTATUS[0]} -ne 0 ]]; then
    # getopt has complained about wrong arguments to stdout
    usage
    exit 2
fi

# read getopt’s output this way to handle the quoting right:
eval set -- "${PARSED}"
if ! [[ -x "$(command -v nproc)" ]]; then
    # Mac
    numCPUs=$(($(sysctl -n hw.ncpu)))
else
    numCPUs=$(($(nproc)))
fi
cmakeBuildType="Debug" activeNetwork="logos_test_network" rebuild=false
# now enjoy the options in order and nicely split until we see --
while true; do
    case "$1" in
        -t|--CMAKE_BUILD_TYPE)
            cmakeBuildType="$2"
            shift 2
            ;;
        -n|--ACTIVE_NETWORK)
            activeNetwork="$2"
            shift 2
            ;;
        -c|--cpus)
            numCPUs="$2"
            shift 2
            ;;
        -s)
            threshold_flag=" -DSTRICT_CONSENSUS_THRESHOLD:BOOL="ON""
            shift
            ;;
        --reject)
            reject_flag=" -DTEST_REJECT:BOOL="ON""
            shift
            ;;
        -r|--rebuild)
            rebuild=true
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        --)
            shift
            break
            ;;
        *)
            echo "Programming error"
            usage
            exit 3
            ;;
    esac
done

buildTypeValues="Debug Release RelWithDebInfo MinSizeRel"
networkValues="logos_test_network logos_beta_network logos_live_network"
invalidOpt=false

if ! echo "$buildTypeValues" | grep -w "$cmakeBuildType" > /dev/null; then
    echo "Invalid input value for CMAKE_BUILD_TYPE, must be one of:
    \"Debug\", \"Release\", \"RelWithDebInfo\", \"MinSizeRel\"."
    invalidOpt=true
fi

if ! echo "$networkValues" | grep -w "$activeNetwork" > /dev/null; then
    echo "Invalid input value for ACTIVE_NETWORK, must be one of:
    \"logos_test_network\", \"logos_beta_network\", \"logos_live_network\"."
    invalidOpt=true
fi

if [[ ! "$numCPUs" =~ ^[1-9][0-9]*$ ]]; then
    echo "Invalid input value for cores, must be a valid integer"
    invalidOpt=true
fi

if [[ $# = 1 ]]; then
    clean=$1
    if [[ ! ${clean} = "clean" ]]; then
        echo "Invalid argument."
        invalidOpt=true
    fi
fi

if [ ${invalidOpt} = true ]; then
    usage
    exit 3
fi


# ========================================================================================
# Done with argparse, begin bash script execution
# ========================================================================================

# Get directory of the build.sh script
OLD_PWD=${PWD}
BUILD_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

if [[ ${clean} = "clean" ]]; then
    cd ${BUILD_DIR}
    make clean && rm -rf CMake* *.cmake gtest/ miniupnp/ Makefile *.cpp
    if [[ $? > 0 ]]; then
        echo "Could not clean build directory."
        exit 1
    fi
    cd ${OLD_PWD}
    exit 0
fi

if [[ ${rebuild} = true ]]; then
    cd ${BUILD_DIR}
    make logos_core -j"$numCPUs"
    if [[ $? > 0 ]]; then
        cd ${OLD_PWD}
        echo "Could not make binary."
        exit 1
    fi
    cd ${OLD_PWD}
    exit 0
fi


if ! [[ -x "$(command -v gcc)" ]]; then
    echo "gcc not found. Installing dependencies..."
    sudo apt-get update \
        && sudo apt-get install -y git g++ curl wget
else
    gcc_version=$(gcc -dumpversion)
    if [[ ${gcc_version::1} <5 ]]; then
        echo "Unsupported gcc version. Installing dependencies..."
    sudo apt-get update \
        && sudo apt-get install -y git g++ curl wget
    fi
fi

if ! [[ -x "$(command -v cmake)" ]]; then
    echo "cmake not found. Installing dependencies..."
    sudo apt-get update && sudo apt-get install -y cmake
fi

if [[ ! $(dpkg -l libgmp3-dev) ]]; then
    echo "libgmp3-dev not found. Installing dependencies..."
    sudo apt-get install -y libgmp3-dev
fi

if [[ ! $(dpkg -l libssl-dev) ]]; then
    echo "libgmp3-dev not found. Installing dependencies..."
    sudo apt-get install -y libssl-dev
fi

if [[ ! $(find /usr/include -name pyconfig.h) ]]; then
    echo "Missing pyconfig.h. Installing dependencies..."
    sudo apt-get update \
        && sudo apt-get install python-dev -y
fi

cd ${BUILD_DIR}/../..

# Install Boost if not present

BOOST_BASENAME=boost_1_67_0
BOOST_ROOT=${BOOST_ROOT-/usr/local/boost}
if [ -d "$BOOST_ROOT" ]; then
    echo "Boost directory already exists, please remove if you want Boost rebuilt"
else
    BOOST_URL=https://downloads.sourceforge.net/project/boost/boost/1.67.0/${BOOST_BASENAME}.tar.bz2
    BOOST_ARCHIVE="${BOOST_BASENAME}.tar.bz2"
    BOOST_ARCHIVE_SHA256='2684c972994ee57fc5632e03bf044746f6eb45d4920c343937a465fd67a5adba'
    if [ -f "$BOOST_ARCHIVE" ]; then
        echo "boost.tar.gz is already in the local directory, skipping the long download"
    else
        echo "Downloading Boost"
        wget --quiet -O "${BOOST_ARCHIVE}.new" "${BOOST_URL}"
        checkHash="$(openssl dgst -sha256 "${BOOST_ARCHIVE}.new" | sed 's@^.*= *@@')"
        if [ "${checkHash}" != "${BOOST_ARCHIVE_SHA256}" ]; then
            echo "Checksum mismatch.  Expected ${BOOST_ARCHIVE_SHA256}, got ${checkHash}" >&2
            cd ${OLD_PWD}
            exit 1
        fi
        mv "${BOOST_ARCHIVE}.new" "${BOOST_ARCHIVE}"
    fi

    if [ ! -d ${BOOST_BASENAME} ]; then
        tar xf "${BOOST_ARCHIVE}"
    fi

    cd ${BOOST_BASENAME} \
        && ./bootstrap.sh \
        && sudo mkdir -p ${BOOST_ROOT} && sudo chmod a+w ${BOOST_ROOT} \
        && ./b2 --prefix=${BOOST_ROOT} link=static install \
        && echo "Finished installing Boost, removing files..." \
        && sudo chmod 755 ${BOOST_ROOT} \
        && cd .. \
        && rm -rf ${BOOST_BASENAME} \
        && rm -f "${BOOST_ARCHIVE}"
fi

if [[ $? > 0 ]]
then
    echo "Could not download and build Boost"
    sudo rm -rf ${BOOST_ROOT}
    cd ${OLD_PWD}
    exit 2
else
    echo "Boost libraries were installed successfully."
fi

# Build Logos
echo "Building Logos..."
cd ${BUILD_DIR}

git submodule update --init --recursive
cmake -DBOOST_ROOT="$BOOST_ROOT" -DACTIVE_NETWORK="$activeNetwork" \
    -DCMAKE_BUILD_TYPE="$cmakeBuildType" ${threshold_flag}${reject_flag} \
    -std=c++14 -G "Unix Makefiles" ..\
    && make -j"$numCPUs"

if [[ $? > 0 ]]
then
    echo "Could not download and build Logos"
    cd ${OLD_PWD}
    exit 2
else
    echo "Logos was built successfully."
fi
cd ${OLD_PWD}
exit 0
