cmake_minimum_required (VERSION 2.8.11)
project (logos)

include(ExternalProject)
set (CPACK_PACKAGE_VERSION_MAJOR "0")
set (CPACK_PACKAGE_VERSION_MINOR "2")
set (CPACK_PACKAGE_VERSION_PATCH "0")
if (DEFINED GIT_COMMIT)
set (CPACK_PACKAGE_VERSION_PATCH "GIT-${GIT_COMMIT}")
endif (DEFINED GIT_COMMIT)
set (CPACK_PACKAGE_VENDOR "Nano Currency")

set(CMAKE_INSTALL_RPATH "@executable_path/../Frameworks")

set (LOGOS_TEST ON CACHE BOOL "")
set (LOGOS_SECURE_RPC OFF CACHE BOOL "")

option(LOGOS_ASAN_INT "Enable ASan+UBSan+Integer overflow" OFF)
option(LOGOS_ASAN "Enable ASan+UBSan" OFF)
option(LOGOS_SIMD_OPTIMIZATIONS "Enable CPU-specific SIMD optimizations (SSE/AVX or NEON, e.g.)" OFF)
set (BOOST_CUSTOM OFF CACHE BOOL "")

if(NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Choose the type of build, options are: Debug Release RelWithDebInfo MinSizeRel." FORCE)
endif(NOT CMAKE_BUILD_TYPE)

if (WIN32)
	set (PLATFORM_COMPILE_FLAGS "-DBOOST_SPIRIT_THREADSAFE -D_WIN32_WINNT=0x0600 -DWINVER=0x0600 -DWIN32_LEAN_AND_MEAN -DMINIUPNP_STATICLIB")
	set (BLAKE2_IMPLEMENTATION "blake2/blake2b.c")
else (WIN32)
	set (PLATFORM_COMPILE_FLAGS "-DBOOST_SPIRIT_THREADSAFE -Werror=switch -fPIC")
	if (LOGOS_ASAN)
		set (PLATFORM_COMPILE_FLAGS "${PLATFORM_COMPILE_FLAGS} -fsanitize=address,undefined -DED25519_NO_INLINE_ASM")
	elseif (LOGOS_ASAN_INT)
		set (PLATFORM_COMPILE_FLAGS "${PLATFORM_COMPILE_FLAGS} -fsanitize=address,undefined,integer -DED25519_NO_INLINE_ASM")
	elseif (LOGOS_TSAN)
		set (PLATFORM_COMPILE_FLAGS "${PLATFORM_COMPILE_FLAGS} -fsanitize=thread -DED25519_NO_INLINE_ASM")
	endif()

	IF (CMAKE_SYSTEM_PROCESSOR MATCHES "^(i.86|x86(_64)?)$")
		if (LOGOS_SIMD_OPTIMIZATIONS)
			set (PLATFORM_COMPILE_FLAGS "${PLATFORM_COMPILE_FLAGS} -msse4")
		elseif(CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64")
			set (PLATFORM_COMPILE_FLAGS "${PLATFORM_COMPILE_FLAGS} -msse2")
		endif()

		set (BLAKE2_IMPLEMENTATION "blake2/blake2b.c")
		if (ENABLE_AVX2)
			set (PLATFORM_COMPILE_FLAGS "${PLATFORM_COMPILE_FLAGS} -mavx2 -mbmi -mbmi2")
			if (PERMUTE_WITH_GATHER)
				set (PLATFORM_COMPILE_FLAGS "${PLATFORM_COMPILE_FLAGS} -DPERMUTE_WITH_GATHER")
			elseif (PERMUTE_WITH_SHUFFLES)
				set (PLATFORM_COMPILE_FLAGS "${PLATFORM_COMPILE_FLAGS} -DPERMUTE_WITH_SHUFFLES")
			endif()
		endif()
	else()
		set (BLAKE2_IMPLEMENTATION "blake2/blake2b-ref.c")
	endif()
endif (WIN32)

if (WIN32)
	set (PLATFORM_C_FLAGS "/std=c11")
else (WIN32)
	set (PLATFORM_C_FLAGS "-std=c11")
endif (WIN32)

if (WIN32)
	set (PLATFORM_CXX_FLAGS "/bigobj")
else (WIN32)
	set (PLATFORM_CXX_FLAGS "-std=c++14 -Werror")
endif (WIN32)

if (WIN32)
	set (PLATFORM_GUI_TARGET_PROPERTIES WIN32)
else (WIN32)
	set (PLATFORM_GUI_TARGET_PROPERTIES "")
endif (WIN32)

if (APPLE)
	set (PLATFORM_LINK_FLAGS "-framework Foundation -framework OpenCL")
elseif (WIN32)
	set (PLATFORM_LINK_FLAGS "")
else ()
	set (PLATFORM_LINK_FLAGS "-static-libgcc -static-libstdc++")
	if (LOGOS_ASAN)
		set (PLATFORM_LINK_FLAGS "${PLATFORM_LINK_FLAGS} -fsanitize=address,undefined")
	elseif (LOGOS_ASAN_INT)
		set (PLATFORM_LINK_FLAGS "${PLATFORM_LINK_FLAGS} -fsanitize=address,undefined,integer")
	elseif (LOGOS_TSAN)
		set (PLATFORM_LINK_FLAGS "${PLATFORM_LINK_FLAGS} -fsanitize=thread")
	endif()
endif (APPLE)

if (LOGOS_SECURE_RPC)
	find_package (OpenSSL 1.0 EXACT REQUIRED)
	include_directories(${OPENSSL_INCLUDE_DIR})
	add_definitions (-DLOGOS_SECURE_RPC)
	message("OpenSSL include dir: ${OPENSSL_INCLUDE_DIR}")
	message("OpenSSL libraries: ${OPENSSL_LIBRARIES}")
	message("OpenSSL lib: ${OPENSSL_SSL_LIBRARY}")
	message("Crypto lib: ${OPENSSL_CRYPTO_LIBRARY}")
else ()
	set (OPENSSL_LIBRARIES "")
endif (LOGOS_SECURE_RPC)

include_directories (${CMAKE_SOURCE_DIR})

set(Boost_USE_STATIC_LIBS        ON)
set(Boost_USE_MULTITHREADED      ON)

if (BOOST_CUSTOM)
    list(APPEND CMAKE_MODULE_PATH ".\\" ${CMAKE_SOURCE_PATH})
endif (BOOST_CUSTOM)

find_package (Boost 1.67.0 REQUIRED COMPONENTS date_time filesystem system log log_setup thread program_options regex chrono atomic)
include_directories (${Boost_INCLUDE_DIR})

add_library (ed25519
	ed25519-donna/ed25519-donna-portable.h
	ed25519-donna/ed25519-hash-custom.h
	ed25519-donna/ed25519-randombytes-custom.h
	ed25519-donna/ed25519.h
	ed25519-donna/ed25519.c)

##########################################################################

#message ("${CMAKE_CURRENT_BINARY_DIR}")
#message ("${CMAKE_SOURCE_DIR}")
#find_package(GMP REQUIRED)

include_directories(${CMAKE_SOURCE_DIR}/bls_agg/bls/include/)
include_directories(${CMAKE_SOURCE_DIR}/bls_agg/cybozulib/include/)
include_directories(${CMAKE_SOURCE_DIR}/bls_agg/mcl/include/)
include_directories(${CMAKE_SOURCE_DIR}/bls_agg/xbyak/include/)

ExternalProject_Add(project_BLS
  SOURCE_DIR ${CMAKE_SOURCE_DIR}/bls_agg/bls
  PREFIX ${CMAKE_SOURCE_DIR}/bls_agg/bls
  CONFIGURE_COMMAND ""
  BUILD_COMMAND make
  INSTALL_COMMAND ""
  BUILD_IN_SOURCE 1
)

add_library(BLS STATIC IMPORTED)
set_property(TARGET BLS PROPERTY IMPORTED_LOCATION ${CMAKE_SOURCE_DIR}/bls_agg/bls/lib/libbls.a)
add_dependencies(BLS project_BLS)

add_library(MCL STATIC IMPORTED)
set_target_properties( MCL PROPERTIES IMPORTED_LOCATION ${CMAKE_SOURCE_DIR}/bls_agg/mcl/lib/libmcl.a)

add_library(MCLbn256 STATIC IMPORTED)
set_target_properties( MCLbn256 PROPERTIES IMPORTED_LOCATION ${CMAKE_SOURCE_DIR}/bls_agg/mcl/lib/libmclbn256.a)

set (BLS_libs BLS MCL MCLbn256 crypto gmpxx gmp)

##########################################################################

include_directories("${CMAKE_SOURCE_DIR}/phc-winner-argon2/include")
include_directories("${CMAKE_SOURCE_DIR}/phc-winner-argon2/src")
include_directories("${CMAKE_SOURCE_DIR}/blake2")

if (CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64")
	set (ARGON_CORE phc-winner-argon2/src/opt.c)
else ()
	set (ARGON_CORE phc-winner-argon2/src/ref.c)
endif ()

if (WIN32)
	set (gtest_force_shared_crt ON)
else ()
	set (gtest_force_shared_crt OFF)
endif()
add_subdirectory (gtest)
include_directories ("gtest/include")

include_directories (beast/include)

set (UPNPC_BUILD_SHARED OFF CACHE BOOL "")
add_subdirectory (miniupnp/miniupnpc)
include_directories (miniupnp/miniupnpc)

set (BUILD_SHARED OFF CACHE BOOL "")
set (BUILD_TESTING OFF CACHE BOOL "")
set (USE_INTERMEDIATE_OBJECTS_TARGET OFF CACHE BOOL "")
if (CRYPTOPP_CUSTOM OR NOT LOGOS_SIMD_OPTIMIZATIONS)
	set (CRYPTOPP_LIBRARY cryptopp)
	add_library (cryptopp
	cryptopp/algparam.cpp
	cryptopp/asn.cpp
	cryptopp/basecode.cpp
	cryptopp/cpu.cpp
	cryptopp/cryptlib.cpp
	cryptopp/default.cpp
	cryptopp/des.cpp
	cryptopp/dessp.cpp
	cryptopp/dll.cpp
	cryptopp/ec2n.cpp
	cryptopp/ecp.cpp
	cryptopp/filters.cpp
	cryptopp/fips140.cpp
	cryptopp/gcm.cpp
	cryptopp/gf2n.cpp
	cryptopp/gfpcrypt.cpp
	cryptopp/hex.cpp
	cryptopp/hmac.cpp
	cryptopp/hrtimer.cpp
	cryptopp/integer.cpp
	cryptopp/iterhash.cpp
	cryptopp/misc.cpp
	cryptopp/modes.cpp
	cryptopp/mqueue.cpp
	cryptopp/nbtheory.cpp
	cryptopp/oaep.cpp
	cryptopp/osrng.cpp
	cryptopp/pubkey.cpp
	cryptopp/queue.cpp
	cryptopp/randpool.cpp
	cryptopp/rdtables.cpp
	cryptopp/rijndael.cpp
	cryptopp/rng.cpp
	cryptopp/sha.cpp
	cryptopp/simple.cpp
	cryptopp/winpipes.cpp)
	set_target_properties (cryptopp PROPERTIES COMPILE_FLAGS "${PLATFORM_CXX_FLAGS} ${PLATFORM_COMPILE_FLAGS}")
else ()
	set (CRYPTOPP_LIBRARY cryptopp-static)
	add_subdirectory (cryptopp)
endif ()

add_library (argon2
	phc-winner-argon2/src/argon2.c
	phc-winner-argon2/include/argon2.h
	phc-winner-argon2/src/core.c
	phc-winner-argon2/src/thread.c
	phc-winner-argon2/src/encoding.c
	${ARGON_CORE})

add_library (xxhash
	xxhash/xxhash.c
	xxhash/xxhash.h)

add_library (lmdb
	lmdb/libraries/liblmdb/lmdb.h
	lmdb/libraries/liblmdb/mdb.c
	lmdb/libraries/liblmdb/midl.c)

add_library (blake2
	blake2/blake2-config.h
	blake2/blake2-impl.h
	blake2/blake2.h
	${BLAKE2_IMPLEMENTATION})

if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
	set (PLATFORM_LIB_SOURCE logos/plat/default/priority.cpp)
	set (PLATFORM_SECURE_SOURCE logos/plat/osx/working.mm)
	set (PLATFORM_WALLET_SOURCE logos/plat/default/icon.cpp)
elseif (${CMAKE_SYSTEM_NAME} MATCHES "Windows")
	set (PLATFORM_LIB_SOURCE logos/plat/windows/priority.cpp)
	set (PLATFORM_SECURE_SOURCE logos/plat/windows/working.cpp)
	set (PLATFORM_NODE_SOURCE logos/plat/windows/openclapi.cpp)
	set (PLATFORM_WALLET_SOURCE logos/plat/windows/icon.cpp Logos.rc)
elseif (${CMAKE_SYSTEM_NAME} MATCHES "Linux")
	set (PLATFORM_LIB_SOURCE logos/plat/linux/priority.cpp)
	set (PLATFORM_SECURE_SOURCE logos/plat/posix/working.cpp)
	set (PLATFORM_NODE_SOURCE logos/plat/posix/openclapi.cpp)
	set (PLATFORM_WALLET_SOURCE logos/plat/default/icon.cpp)
elseif (${CMAKE_SYSTEM_NAME} MATCHES "FreeBSD")
	set (PLATFORM_LIB_SOURCE logos/plat/default/priority.cpp)
	set (PLATFORM_SECURE_SOURCE logos/plat/posix/working.cpp)
	set (PLATFORM_NODE_SOURCE logos/plat/posix/openclapi.cpp)
	set (PLATFORM_WALLET_SOURCE logos/plat/default/icon.cpp)
else ()
	error ("Unknown platform: ${CMAKE_SYSTEM_NAME}")
endif ()
	
if (LOGOS_SECURE_RPC)
	set (SECURE_RPC_SOURCE logos/node/rpc_secure.cpp logos/node/rpc_secure.hpp)
endif ()

add_library (secure
	${PLATFORM_SECURE_SOURCE}
	logos/config.hpp
	logos/common.cpp
	logos/common.hpp
	logos/blockstore.cpp
	logos/blockstore.hpp
	logos/ledger.cpp
	logos/ledger.hpp
	logos/node/utility.cpp
	logos/node/utility.hpp
	logos/token/account.hpp
	logos/token/account.cpp
	logos/token/entry.hpp
	logos/token/entry.cpp
	logos/versioning.hpp
	logos/versioning.cpp)

SET (LOGOS_LIB_SOURCES
	${PLATFORM_LIB_SOURCE}
	logos/lib/blocks.cpp
	logos/lib/blocks.hpp
	logos/lib/interface.cpp
	logos/lib/interface.h
	logos/lib/merkle.cpp
	logos/lib/merkle.hpp
	logos/lib/numbers.cpp
	logos/lib/numbers.hpp
	logos/lib/epoch_time_util.cpp
	logos/lib/epoch_time_util.hpp
	logos/lib/utility.cpp
	logos/lib/utility.hpp
	logos/lib/work.hpp
	logos/lib/work.cpp)

add_library (logos_lib SHARED ${LOGOS_LIB_SOURCES})
add_library (logos_lib_static STATIC ${LOGOS_LIB_SOURCES})

add_library (node
	${PLATFORM_NODE_SOURCE}
	${SECURE_RPC_SOURCE}
    logos/bootstrap/attempt.cpp
    logos/bootstrap/bootstrap.cpp
    logos/bootstrap/bootstrap_messages.cpp
    logos/bootstrap/connection.cpp
    logos/bootstrap/pull_connection.cpp
    logos/bootstrap/pull.cpp
    logos/bootstrap/tip_connection.cpp
    logos/bootstrap/tips.cpp    
    logos/consensus/request/request_consensus_manager.cpp
	logos/consensus/request/request_backup_delegate.cpp
	logos/consensus/message_handler.cpp
	logos/consensus/request/request_internal_queue.cpp
	logos/consensus/delegate_bridge.cpp
	logos/consensus/backup_delegate.cpp
	logos/consensus/consensus_container.cpp
	logos/consensus/consensus_manager.cpp
	logos/consensus/delegate_key_store.cpp
	logos/consensus/epoch_manager.cpp
	logos/consensus/epoch/epoch_backup_delegate.cpp
	logos/consensus/epoch/epoch_consensus_manager.cpp
	logos/consensus/message_validator.cpp
	logos/consensus/messages/common.cpp
	logos/consensus/messages/messages.cpp
	logos/consensus/consensus_msg_sink.cpp
	logos/consensus/messages/receive_block.cpp
	logos/consensus/messages/request_block.cpp
	logos/consensus/messages/tip.cpp
	logos/consensus/microblock/microblock_backup_delegate.cpp
	logos/consensus/microblock/microblock_consensus_manager.cpp
	logos/consensus/p2p/consensus_p2p.cpp
	logos/consensus/p2p/consensus_p2p_bridge.cpp
	logos/consensus/persistence/persistence.cpp
	logos/consensus/persistence/validator_builder.cpp
	logos/consensus/persistence/request/request_persistence.cpp
	logos/consensus/persistence/epoch/epoch_persistence.cpp
	logos/consensus/persistence/microblock/microblock_persistence.cpp
	logos/consensus/persistence/reservations.cpp
    logos/consensus/persistence/block_cache.cpp
    logos/consensus/persistence/block_cache.hpp
    logos/consensus/persistence/block_container.cpp
    logos/consensus/persistence/block_container.hpp
    logos/consensus/persistence/block_write_queue.cpp
    logos/consensus/persistence/block_write_queue.hpp
    logos/consensus/primary_delegate.cpp
	logos/elections/requests.cpp
    logos/elections/representative.cpp
    logos/elections/candidate.cpp
	logos/epoch/archiver.cpp
	logos/epoch/epoch.cpp
	logos/epoch/epoch_handler.cpp
	logos/epoch/recall_handler.cpp
	logos/epoch/epoch_voting_manager.cpp
	logos/epoch/event_proposer.cpp
	logos/identity_management/delegate_identity_manager.hpp
	logos/identity_management/delegate_identity_manager.cpp
	logos/microblock/microblock.cpp
	logos/microblock/microblock_handler.cpp
	logos/microblock/microblock_tester.cpp
	logos/network/consensus_netio.cpp
	logos/network/consensus_netio_manager.cpp
	logos/network/net_io_assembler.cpp
	logos/network/net_io_send.cpp
	logos/network/peer_acceptor.cpp
	logos/network/epoch_peer_manager.cpp
	logos/node/client_callback.cpp
	logos/node/client_callback.hpp
	logos/node/common.cpp
	logos/node/common.hpp
	logos/node/node.hpp
	logos/node/node.cpp
	logos/node/openclwork.cpp
	logos/node/openclwork.hpp
	logos/node/rpc.hpp
	logos/node/rpc.cpp
	logos/node/rpc_logic.hpp
	logos/node/rpc_logic.cpp
	logos/node/testing.hpp
	logos/node/testing.cpp
	logos/node/wallet.hpp
	logos/node/wallet.cpp
	logos/node/stats.hpp
	logos/node/stats.cpp
	logos/node/working.hpp
	logos/node/xorshift.hpp
	logos/request/requests.cpp
	logos/request/utility.cpp
	logos/request/utility.cpp
    logos/rewards/epoch_rewards_manager.cpp
    logos/staking/thawing_funds.cpp
    logos/staking/staked_funds.cpp
    logos/staking/liability.cpp
    logos/staking/liability_manager.cpp
    logos/staking/voting_power_manager.cpp
    logos/staking/staking_manager.cpp
    logos/staking/requests.cpp
	logos/token/account.cpp
	logos/token/common.cpp
	logos/token/entry.cpp
	logos/token/requests.cpp
	logos/token/utility.cpp
	logos/tx_acceptor/tx_acceptor.cpp
	logos/tx_acceptor/tx_acceptor_channel.cpp
	logos/tx_acceptor/tx_message_header.cpp
	logos/tx_acceptor/tx_receiver.cpp
	logos/tx_acceptor/tx_receiver_channel.cpp
	logos/wallet_server/client/callback_handler.cpp
	logos/wallet_server/client/callback_manager.cpp
	logos/wallet_server/client/common.hpp
	logos/wallet_server/client/wallet_server_client.cpp)



SET (ACTIVE_NETWORK logos_live_network CACHE STRING "Selects which network parameters are used")
set_property (CACHE ACTIVE_NETWORK PROPERTY STRINGS logos_test_network logos_beta_network logos_live_network)

option(STRICT_CONSENSUS_THRESHOLD "Require approval from all delegates for transaction validation" OFF)
option(TEST_REJECT "Makes delegate reject random transactions inside PrePrepare messages" OFF)

if (LOGOS_TEST)
	add_executable (unit_test
		logos/unit_test/main.cpp
		logos/unit_test/account_serialization.cpp
        logos/unit_test/block_cache.cpp
        logos/unit_test/bootstrap.cpp
		logos/unit_test/consensus_msg_serialize.cpp
		logos/unit_test/msg_validator_setup.cpp
		logos/unit_test/dual_RB_tips.cpp
        logos/unit_test/request_serialization.cpp
        logos/unit_test/proxy.cpp
        logos/unit_test/staking_manager.cpp
        logos/unit_test/liability_manager.cpp
        logos/unit_test/tallying.cpp
		logos/unit_test/elections.cpp
        logos/unit_test/staking.cpp
        logos/unit_test/epoch_rewards_db.cpp
        logos/unit_test/voting_power.cpp
        logos/unit_test/p2p_test.cpp
        logos/p2p/test/propagate_test.cpp
        logos/p2p/test/storage_test.cpp
		logos/unit_test/advertise.cpp)

	set_target_properties (unit_test PROPERTIES COMPILE_FLAGS "${PLATFORM_CXX_FLAGS} ${PLATFORM_COMPILE_FLAGS} -DQT_NO_KEYWORDS -DACTIVE_NETWORK=${ACTIVE_NETWORK} -DLOGOS_VERSION_MAJOR=${CPACK_PACKAGE_VERSION_MAJOR} -DLOGOS_VERSION_MINOR=${CPACK_PACKAGE_VERSION_MINOR} -DBOOST_ASIO_HAS_STD_ARRAY=1 ")
	if (STRICT_CONSENSUS_THRESHOLD MATCHES ON)
		set_property (TARGET unit_test APPEND_STRING PROPERTY COMPILE_FLAGS "-DSTRICT_CONSENSUS_THRESHOLD ")
	endif (STRICT_CONSENSUS_THRESHOLD MATCHES ON)
	set_target_properties (unit_test PROPERTIES LINK_FLAGS "${PLATFORM_LINK_FLAGS}")
    target_include_directories (unit_test PRIVATE logos/p2p)
    target_link_libraries (unit_test p2p)
endif (LOGOS_TEST)

add_executable (logos_core
	logos/daemon.cpp
	logos/daemon.hpp
	logos/entry.cpp)

set_target_properties (argon2 PROPERTIES COMPILE_FLAGS "${PLATFORM_C_FLAGS} ${PLATFORM_COMPILE_FLAGS}")
set_target_properties (blake2 PROPERTIES COMPILE_FLAGS "${PLATFORM_C_FLAGS} ${PLATFORM_COMPILE_FLAGS} -D__SSE2__")
set_target_properties (ed25519 PROPERTIES COMPILE_FLAGS "${PLATFORM_C_FLAGS} ${PLATFORM_COMPILE_FLAGS} -DED25519_CUSTOMHASH -DED25519_CUSTOMRNG")
set_target_properties (secure node logos_core logos_lib logos_lib_static PROPERTIES COMPILE_FLAGS "${PLATFORM_CXX_FLAGS} ${PLATFORM_COMPILE_FLAGS} -DQT_NO_KEYWORDS -DACTIVE_NETWORK=${ACTIVE_NETWORK} -DLOGOS_VERSION_MAJOR=${CPACK_PACKAGE_VERSION_MAJOR} -DLOGOS_VERSION_MINOR=${CPACK_PACKAGE_VERSION_MINOR} -DBOOST_ASIO_HAS_STD_ARRAY=1 ")
if (STRICT_CONSENSUS_THRESHOLD MATCHES ON)
	set_property (TARGET secure node logos_core logos_lib logos_lib_static APPEND_STRING PROPERTY COMPILE_FLAGS "-DSTRICT_CONSENSUS_THRESHOLD ")
endif (STRICT_CONSENSUS_THRESHOLD MATCHES ON)
if (TEST_REJECT MATCHES ON)
	set_property (TARGET secure node logos_core logos_lib logos_lib_static APPEND_STRING PROPERTY COMPILE_FLAGS "-DTEST_REJECT ")
endif (TEST_REJECT MATCHES ON)
set_target_properties (secure node logos_core PROPERTIES LINK_FLAGS "${PLATFORM_LINK_FLAGS}")

if (WIN32)
	set (PLATFORM_LIBS Ws2_32 mswsock iphlpapi ntdll)
else (WIN32)
	set (PLATFORM_LIBS pthread ${CMAKE_DL_LIBS})
endif (WIN32)

if (WIN32)
	set (PLATFORM_WALLET_LIBS Qt5::WinExtras)
else (WIN32)
	set (PLATFORM_WALLET_LIBS)
endif (WIN32)

if (LOGOS_TEST)
	target_link_libraries (unit_test node p2p secure lmdb ed25519 ${BLS_libs} logos_lib_static argon2 ${OPENSSL_LIBRARIES} ${CRYPTOPP_LIBRARY} gtest_main gtest libminiupnpc-static ${Boost_ATOMIC_LIBRARY} ${Boost_CHRONO_LIBRARY} ${Boost_REGEX_LIBRARY} ${Boost_DATE_TIME_LIBRARY} ${Boost_FILESYSTEM_LIBRARY} ${Boost_SYSTEM_LIBRARY} ${Boost_LOG_LIBRARY} ${Boost_LOG_SETUP_LIBRARY} ${Boost_PROGRAM_OPTIONS_LIBRARY} ${Boost_THREAD_LIBRARY} ${PLATFORM_LIBS})
endif (LOGOS_TEST)

target_link_libraries (logos_lib ed25519 ${BLS_libs} xxhash blake2 ${CRYPTOPP_LIBRARY})
target_link_libraries (logos_lib_static ed25519 ${BLS_libs} xxhash blake2 ${CRYPTOPP_LIBRARY})

target_link_libraries (logos_core node p2p secure lmdb ed25519 ${BLS_libs} logos_lib_static argon2 ${OPENSSL_LIBRARIES} ${CRYPTOPP_LIBRARY} libminiupnpc-static ${Boost_ATOMIC_LIBRARY} ${Boost_CHRONO_LIBRARY} ${Boost_REGEX_LIBRARY} ${Boost_DATE_TIME_LIBRARY} ${Boost_FILESYSTEM_LIBRARY} ${Boost_SYSTEM_LIBRARY} ${Boost_LOG_LIBRARY} ${Boost_PROGRAM_OPTIONS_LIBRARY} ${Boost_LOG_SETUP_LIBRARY} ${Boost_THREAD_LIBRARY} ${PLATFORM_LIBS})

set (CPACK_RESOURCE_FILE_LICENSE ${CMAKE_SOURCE_DIR}/LICENSE)

include (CPack)

add_subdirectory(logos/p2p)
