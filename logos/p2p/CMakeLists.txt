add_library (p2p
	addrdb.cpp
	addrdb.h
	addrman.cpp
	addrman.h
	bloom.cpp
	bloom.h
	chainparams.cpp
	chainparams.h
	clientversion.cpp
	clientversion.h
	compat.h
	compat/byteswap.h
	compat/endian.h
	compat/glibc_sanity.cpp
	compat/glibcxx_sanity.cpp
	compat/sanity.h
	crypto/chacha20.cpp
	crypto/chacha20.h
	crypto/common.h
	hash.cpp
	hash.h
	logging.cpp
	logging.h
	net.cpp
	net.h
	netaddress.cpp
	netaddress.h
	netbase.cpp
	netbase.h
	net_processing.cpp
	net_processing.h
	p2p.h
    p2p.cpp
    protocol.cpp
	protocol.h
	random.cpp
	random.h
	serialize.h
	streams.h
	support/allocators/zeroafterfree.h
	support/cleanse.cpp
	support/cleanse.h
	sync.h
	threadinterrupt.cpp
	threadinterrupt.h
	threadsafety.h
	timedata.cpp
	timedata.h
	tinyformat.h
	ui_interface.cpp
	ui_interface.h
    uint256.cpp
    uint256.h
	util.cpp
	util.h
	utilmemory.h
	utilstrencodings.cpp
	utilstrencodings.h
    version.h)

target_include_directories(p2p PRIVATE .)

target_link_libraries (p2p
	${Boost_CHRONO_LIBRARY}
	${Boost_FILESYSTEM_LIBRARY}
	${Boost_LOG_LIBRARY}
	${Boost_LOG_SETUP_LIBRARY}
	${Boost_PROGRAM_OPTIONS_LIBRARY}
	${Boost_SYSTEM_LIBRARY}
	${Boost_THREAD_LIBRARY}
	crypto
	pthread
	ssl
	${CMAKE_DL_LIBS}
	)

target_compile_definitions(p2p
	PRIVATE
		-DRAIBLOCKS_VERSION_MAJOR=${CPACK_PACKAGE_VERSION_MAJOR}
		-DRAIBLOCKS_VERSION_MINOR=${CPACK_PACKAGE_VERSION_MINOR}
		-DHAVE_CONFIG_H)

set_target_properties(p2p 
	PROPERTIES
            CXX_STANDARD 14
        )

add_subdirectory(test)
