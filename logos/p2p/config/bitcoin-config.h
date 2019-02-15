// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_CONFIG_H
#define BITCOIN_CONFIG_H

#define PACKAGE_NAME			"logos"
#define COPYRIGHT_HOLDERS		"Logos Network"
#define COPYRIGHT_HOLDERS_SUBSTITUTION	"Promethean Labs"
#define URL_WEBSITE			"https://www.logos.network"
#define URL_SOURCE_CODE			"https://github.com/LogosNetwork"
#define FIRST_COPYRIGHT_YEAR		2018
#define COPYRIGHT_YEAR			2018
#define CLIENT_VERSION_MAJOR		0
#define CLIENT_VERSION_MINOR		1
#define CLIENT_VERSION_REVISION		0
#define CLIENT_VERSION_BUILD		0
#define CLIENT_VERSION_IS_RELEASE	0

#define MAINNET_DEFAULT_PORT		14495
#define TESTNET_DEFAULT_PORT		14496
#define REGTEST_DEFAULT_PORT		14497

#define MAX_BLOCK_SERIALIZED_SIZE	4000000
#define DEFAULT_BANSCORE_THRESHOLD	100
#define MAX_REJECT_MESSAGE_LENGTH	111
#define BLOCK_STALLING_TIMEOUT		2
#define BLOCK_DOWNLOAD_TIMEOUT_BASE	1000000
#define BLOCK_DOWNLOAD_TIMEOUT_PER_PEER	500000
#define N_POW_TARGET_SPACING		(10 * 60)

#define REJECT_MALFORMED		0x01
#define REJECT_INVALID			0x10
#define REJECT_OBSOLETE			0x11
#define REJECT_DUPLICATE		0x12
#define REJECT_NONSTANDARD		0x40
#define REJECT_INSUFFICIENTFEE		0x42
#define REJECT_CHECKPOINT		0x43

#define DEFAULT_WHITELISTFORCERELAY	true

// since NANO use boost 1.66
#define HAVE_WORKING_BOOST_SLEEP_FOR

#define HAVE_BYTESWAP_H
#define HAVE_DECL_BSWAP_16 1
#define HAVE_DECL_BSWAP_32 1
#define HAVE_DECL_BSWAP_64 1

#define HAVE_ENDIAN_H
#define HAVE_DECL_HTOBE16 1
#define HAVE_DECL_HTOLE16 1
#define HAVE_DECL_BE16TOH 1
#define HAVE_DECL_LE16TOH 1
#define HAVE_DECL_HTOBE32 1
#define HAVE_DECL_HTOLE32 1
#define HAVE_DECL_BE32TOH 1
#define HAVE_DECL_LE32TOH 1
#define HAVE_DECL_HTOBE64 1
#define HAVE_DECL_HTOLE64 1
#define HAVE_DECL_BE64TOH 1
#define HAVE_DECL_LE64TOH 1

#define HAVE_DECL_STRNLEN 1

typedef unsigned long CAmount;

#endif // BITCOIN_CONFIG_H
