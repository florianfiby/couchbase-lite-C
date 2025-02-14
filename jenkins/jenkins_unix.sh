#!/bin/bash

set -e
shopt -s extglob dotglob

function build_xcode {
    xcodebuild -project CBL_C.xcodeproj -configuration Debug-EE -derivedDataPath ios -scheme "CBL_C Framework" -sdk iphonesimulator CODE_SIGNING_ALLOWED=NO
}

if ! [ -x "$(command -v git)" ]; then
  echo 'Error: git is not installed.' >&2
  exit 1
fi

if [ $CHANGE_TARGET == "master" ]; then
    BRANCH="main"
else
    BRANCH=$CHANGE_TARGET
fi

git submodule update --init --recursive
pushd vendor
git clone ssh://git@github.com/couchbase/couchbase-lite-c-ee --branch $BRANCH_NAME --recursive --depth 1 couchbase-lite-c-ee || \
    git clone ssh://git@github.com/couchbase/couchbase-lite-c-ee --branch $BRANCH --recursive --depth 1 couchbase-lite-c-ee
mv couchbase-lite-c-ee/couchbase-lite-core-EE .
popd

unameOut="$(uname -s)"
case "${unameOut}" in
    # Build XCode project on mac because it has stricter warnings
    Darwin*)    build_xcode;;
esac

ulimit -c unlimited # Enable crash dumps
mkdir -p build
pushd build
cmake -DBUILD_ENTERPRISE=ON -DCMAKE_BUILD_TYPE=Debug -DCMAKE_INSTALL_PREFIX=`pwd`/out ..
make -j8
pushd test
./CBL_C_Tests -r list
