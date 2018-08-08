#!/bin/bash
REPO_ROOT=`git rev-parse --show-toplevel`
pushd $REPO_ROOT
docker build -f docker/ci/Dockerfile -t logos-ci:latest .
popd
