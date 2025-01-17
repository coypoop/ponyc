#!/bin/bash

set -e

API_KEY=$1
if [[ ${API_KEY} == "" ]]
then
  echo "API_KEY needs to be supplied as first script argument."
  exit 1
fi

TODAY=$(date +%Y%m%d)

# Compiler target parameters
ARCH=x86-64
PIC=true

# Triple construction
VENDOR=unknown
OS=linux-gnu
TRIPLE=${ARCH}-${VENDOR}-${OS}

# Build parameters
MAKE_PARALLELISM=4
BUILD_PREFIX=$(mktemp -d)
DESTINATION=${BUILD_PREFIX}/lib/pony
PONY_VERSION="nightly-${TODAY}"

# Asset information
PACKAGE_DIR=$(mktemp -d)
PACKAGE=ponyc-${TRIPLE}

# Cloudsmith configuration
CLOUDSMITH_VERSION=${TODAY}
ASSET_OWNER=main-pony
ASSET_REPO=pony-nightlies
ASSET_PATH=${ASSET_OWNER}/${ASSET_REPO}
ASSET_FILE=${PACKAGE_DIR}/${PACKAGE}.tar.gz
ASSET_SUMMARY="Pony compiler"
ASSET_DESCRIPTION="https://github.com/ponylang/ponyc"

# Build pony installation
echo "Building ponyc installation..."
make install prefix=${BUILD_PREFIX} default_pic=${PIC} arch=${ARCH} \
  -j${MAKE_PARALLELISM} -f Makefile-lib-llvm symlink=no use=llvm_link_static \
  version="${PONY_VERSION}"

# Package it all up
echo "Creating .tar.gz of ponyc installation..."
pushd ${DESTINATION} || exit 1
tar -cvzf ${ASSET_FILE} *
popd || exit 1

# Ship it off to cloudsmith
echo "Uploading package to cloudsmith..."
cloudsmith push raw --version "${CLOUDSMITH_VERSION}" --api-key ${API_KEY} \
  --summary "${ASSET_SUMMARY}" --description "${ASSET_DESCRIPTION}" \
  ${ASSET_PATH} ${ASSET_FILE}
