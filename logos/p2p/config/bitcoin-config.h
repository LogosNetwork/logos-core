// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_CONFIG_H
#define BITCOIN_CONFIG_H

#define PACKAGE_NAME                    "logos"
#define COPYRIGHT_HOLDERS               "Logos Network"
#define COPYRIGHT_HOLDERS_SUBSTITUTION	"Promethean Labs"
#define URL_WEBSITE                     "https://www.logos.network"
#define URL_SOURCE_CODE                 "https://github.com/LogosNetwork"
#define FIRST_COPYRIGHT_YEAR            2018
#define COPYRIGHT_YEAR                  2019

#define PROTOCOL_VERSION                70015               /* network protocol version */
#define INIT_PROTO_VERSION              PROTOCOL_VERSION    /* initial protocol version */

#define CLIENT_VERSION_MAJOR            0
#define CLIENT_VERSION_MINOR            1
#define CLIENT_VERSION_REVISION         0
#define CLIENT_VERSION_BUILD            0
#define CLIENT_VERSION_IS_RELEASE       0
#define CLIENT_VERSION                  ( 1000000 * CLIENT_VERSION_MAJOR    \
                                        +   10000 * CLIENT_VERSION_MINOR    \
                                        +     100 * CLIENT_VERSION_REVISION \
                                        +       1 * CLIENT_VERSION_BUILD    )

#define STRINGIZE(X)                    DO_STRINGIZE(X)
#define DO_STRINGIZE(X)                 #X

#define FormatFullVersion()             "v" DO_STRINGIZE(CLIENT_VERSION_MAJOR)          \
                                        "." DO_STRINGIZE(CLIENT_VERSION_MINOR)          \
                                        "." DO_STRINGIZE(CLIENT_VERSION_REVISION)       \
                                        "." DO_STRINGIZE(CLIENT_VERSION_BUILD) "-unk"

#define MAINNET_DEFAULT_PORT            14495
#define TESTNET_DEFAULT_PORT            14496
#define REGTEST_DEFAULT_PORT            14497

#define MAX_BLOCK_SERIALIZED_SIZE       4000000
#define DEFAULT_BANSCORE_THRESHOLD      100
#define MAX_REJECT_MESSAGE_LENGTH       111
#define N_POW_TARGET_SPACING            (10 * 60)

#define REJECT_MALFORMED                0x01
#define REJECT_DUPLICATE                0x12

#endif // BITCOIN_CONFIG_H
