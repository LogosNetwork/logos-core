#!/usr/bin/env bash

set -o errexit
set -o xtrace

bootstrapArgs=()
while getopts 'm' OPT; do
	case "${OPT}" in
		m)
			bootstrapArgs+=('--with-libraries=atomic,chrono,thread,log,date_time,filesystem,program_options,regex')
			;;
	esac
done

BOOST_BASENAME=boost_1_67_0
BOOST_ROOT=${BOOST_ROOT-/usr/local/boost}
BOOST_URL=https://downloads.sourceforge.net/project/boost/boost/1.67.0/${BOOST_BASENAME}.tar.bz2
BOOST_ARCHIVE="${BOOST_BASENAME}.tar.bz2"
BOOST_ARCHIVE_SHA1='694ae3f4f899d1a80eb7a3b31b33be73c423c1ae'

wget --quiet -O "${BOOST_ARCHIVE}.new" "${BOOST_URL}"
checkHash="$(openssl dgst -sha1 "${BOOST_ARCHIVE}.new" | sed 's@^.*= *@@')"
if [ "${checkHash}" != "${BOOST_ARCHIVE_SHA1}" ]; then
	echo "Checksum mismatch.  Expected ${BOOST_ARCHIVE_SHA1}, got ${checkHash}" >&2

	exit 1
fi
mv "${BOOST_ARCHIVE}.new" "${BOOST_ARCHIVE}"

tar xf "${BOOST_ARCHIVE}"
cd ${BOOST_BASENAME}
./bootstrap.sh "${bootstrapArgs[@]}"
./b2 -d0 --prefix=${BOOST_ROOT} link=static install
cd ..
rm -rf ${BOOST_BASENAME}
rm -f "${BOOST_ARCHIVE}"
