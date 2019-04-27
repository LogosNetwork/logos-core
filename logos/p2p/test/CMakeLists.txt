add_executable (p2p-standalone
	p2p-standalone.cpp
)

target_include_directories(p2p-standalone PRIVATE .)

target_link_libraries (p2p-standalone
	p2p
	blake2
    lmdb
    ${Boost_LOG_LIBRARY} ${Boost_LOG_SETUP_LIBRARY} ${Boost_FILESYSTEM_LIBRARY} ${Boost_SYSTEM_LIBRARY})

target_compile_definitions(p2p
	PRIVATE
		-DRAIBLOCKS_VERSION_MAJOR=${CPACK_PACKAGE_VERSION_MAJOR}
		-DRAIBLOCKS_VERSION_MINOR=${CPACK_PACKAGE_VERSION_MINOR}
		-DHAVE_CONFIG_H)

target_compile_options(p2p
	PRIVATE
		-g)

set_target_properties(p2p-standalone PROPERTIES
            CXX_STANDARD 14
        )

