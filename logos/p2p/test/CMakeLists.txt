add_executable (p2p-standalone
	p2p-standalone.cpp
)

target_include_directories(p2p-standalone PRIVATE .)

target_link_libraries (p2p-standalone
	p2p)

target_compile_definitions(p2p
	PRIVATE
		-DRAIBLOCKS_VERSION_MAJOR=${CPACK_PACKAGE_VERSION_MAJOR}
		-DRAIBLOCKS_VERSION_MINOR=${CPACK_PACKAGE_VERSION_MINOR}
		-DHAVE_CONFIG_H)

target_compile_options(p2p
	PRIVATE
		-g)
