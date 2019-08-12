#!/bin/bash

build='false'

print_usage() {
	echo 'build.sh [-h] [-b {true}]'
}

while getopts 'hb:' OPT; do
	case "${OPT}" in
		h)
			print_usage
			exit 0
			;;
		b)
			build="${OPTARG}"
			;;
		*)
			print_usage >&2
			exit 1
			;;
	esac
done

case "${network}" in
	live)
		network_tag=''
		;;
	test|beta)
		network_tag="-${network}"
		;;
	*)
		network_tag=''
		;;
esac

REPO_ROOT=`git rev-parse --show-toplevel`
COMMIT_SHA=`git rev-parse --short HEAD`
pushd $REPO_ROOT
docker build --build-arg build="${build}" -f docker/node/Dockerfile -t logos-node${network_tag}:latest .
popd
