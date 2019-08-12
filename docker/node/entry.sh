#!/bin/bash

PATH="${PATH:-/bin}:/usr/bin"
export PATH

set -euo pipefail
IFS=$'\n\t'

network="Test"


nanodir="${HOME}/LogosTest"
dbFile="${nanodir}/data.ldb"
mkdir -p "${nanodir}"
if [ ! -f "${nanodir}/config.json" ]; then
        echo "Config File not found, adding default."
        cp "/usr/share/logos/config/config.json" "${nanodir}/config.json"
fi

if [ ! -f "${nanodir}/data.ldb" ]; then
        echo "DB File not found, adding default."
        cp "/usr/share/logos/config/data.ldb" "${nanodir}/data.ldb"
fi

pid=''
firstTimeComplete=''

if [ -n "${firstTimeComplete}" ]; then
	sleep 10
fi
firstTimeComplete='true'

if [ -n "${pid}" ]; then
	if ! kill -0 "${pid}" >/dev/null 2>/dev/null; then
		pid=''
	fi
fi

if [ -z "${pid}" ]; then
	logos_core --daemon &
	pid="$!"
fi
