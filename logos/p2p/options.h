Arg("addnode", "Add a node to connect to and attempt to keep the connection open. This option can be specified multiple times to add multiple nodes.",
    false, OptionsCategory::CONNECTION, P2P_OPTION_MULTI);
Arg("banscore", strprintf("Threshold for disconnecting misbehaving peers (default: %u)", DEFAULT_BANSCORE_THRESHOLD),
    false, OptionsCategory::CONNECTION, P2P_OPTION_ARGUMENT);
Arg("bantime", strprintf("Number of seconds to keep misbehaving peers from reconnecting (default: %u)", DEFAULT_MISBEHAVING_BANTIME),
    false, OptionsCategory::CONNECTION, P2P_OPTION_ARGUMENT);
Arg("bind", "Bind to given address and always listen on it. Use [host]:port notation for IPv6",
    false, OptionsCategory::CONNECTION, P2P_OPTION_ARGUMENT);
Arg("blacklist", "Add specified ips to blacklist",
    false, OptionsCategory::CONNECTION, P2P_OPTION_MULTI);
Arg("connect", "Connect only to the specified node; --noconnect disables automatic connections (the rules for this peer are the same as for --addnode). "
               "This option can be specified multiple times to connect to multiple nodes.",
    false, OptionsCategory::CONNECTION, P2P_OPTION_MULTI);
Arg("discover", "Discover own IP addresses (default: 1 when listening and no --externalip)",
    false, OptionsCategory::CONNECTION, 0);
Arg("dns", strprintf("Allow DNS lookups for --addnode, --seednode and --connect (default: %u)", DEFAULT_NAME_LOOKUP),
    false, OptionsCategory::CONNECTION, 0);
Arg("dnsseed", "Query for peer addresses via DNS lookup, if low on addresses (default: 1 unless --connect used)",
    false, OptionsCategory::CONNECTION, 0);
Arg("externalip", "Specify your own public address",
    false, OptionsCategory::CONNECTION, P2P_OPTION_ARGUMENT);
Arg("forcednsseed", strprintf("Always query for peer addresses via DNS lookup (default: %u)", DEFAULT_FORCEDNSSEED),
    false, OptionsCategory::CONNECTION, 0);
Arg("listen", "Accept connections from outside (default: 1 if no --connect)",
    false, OptionsCategory::CONNECTION, 0);
Arg("maxconnections", strprintf("Maintain at most <n> connections to peers (default: %u)", DEFAULT_MAX_PEER_CONNECTIONS),
    false, OptionsCategory::CONNECTION, P2P_OPTION_ARGUMENT);
Arg("maxreceivebuffer", strprintf("Maximum per-connection receive buffer, <n>*1000 bytes (default: %u)", DEFAULT_MAXRECEIVEBUFFER),
    false, OptionsCategory::CONNECTION, P2P_OPTION_ARGUMENT);
Arg("maxsendbuffer", strprintf("Maximum per-connection send buffer, <n>*1000 bytes (default: %u)", DEFAULT_MAXSENDBUFFER),
    false, OptionsCategory::CONNECTION, P2P_OPTION_ARGUMENT);
Arg("maxtimeadjustment", strprintf("Maximum allowed median peer time offset adjustment. Local perspective of time may be influenced "
                                   "by peers forward or backward by this amount. (default: %u seconds)", DEFAULT_MAX_TIME_ADJUSTMENT),
    false, OptionsCategory::CONNECTION, P2P_OPTION_ARGUMENT);
Arg("maxuploadtarget", strprintf("Tries to keep outbound traffic under the given target (in MiB per 24h), 0 = no limit (default: %d)", DEFAULT_MAX_UPLOAD_TARGET),
    false, OptionsCategory::CONNECTION, P2P_OPTION_ARGUMENT);
Arg("onlynet", "Make outgoing connections only through network <net> (ipv4 or ipv6). Incoming connections are not affected by this option. "
               "This option can be specified multiple times to allow multiple networks.",
    false, OptionsCategory::CONNECTION, P2P_OPTION_MULTI);
Arg("port", strprintf("Listen for connections on <arg> (default: %u, testnet: %u, regtest: %u)",
                      defaultChainParams->GetDefaultPort(), testnetChainParams->GetDefaultPort(), regtestChainParams->GetDefaultPort()),
    false, OptionsCategory::CONNECTION, P2P_OPTION_ARGUMENT);
Arg("seednode", "Connect to a node to retrieve peer addresses, and disconnect. This option can be specified multiple times to connect to multiple nodes.",
    false, OptionsCategory::CONNECTION, P2P_OPTION_MULTI);
Arg("timeout", strprintf("Specify connection timeout in milliseconds (minimum: 1, default: %d)", DEFAULT_CONNECT_TIMEOUT),
    false, OptionsCategory::CONNECTION, P2P_OPTION_ARGUMENT);
Arg("whitebind", "Bind to given address and whitelist peers connecting to it. Use [host]:port notation for IPv6",
    false, OptionsCategory::CONNECTION, P2P_OPTION_ARGUMENT);
Arg("whitelist", "Whitelist peers connecting from the given IP address (e.g. 1.2.3.4) or CIDR notated network (e.g. 1.2.3.0/24). Can be specified multiple times. "
                 "Whitelisted peers cannot be DoS banned and their transactions are always relayed, even if they are already in the mempool, useful e.g. for a gateway",
    false, OptionsCategory::CONNECTION, P2P_OPTION_MULTI);
Arg("dropmessagestest", "Randomly drop 1 of every <n> network messages",
    true, OptionsCategory::DEBUG_TEST, P2P_OPTION_ARGUMENT);
Arg("addrmantest", "Allows to test address relay on localhost",
    true, OptionsCategory::DEBUG_TEST, 0);
Arg("debug", "Output debugging information (default: --nodebug). "
             "If <arg> = 1, output all debugging information. <arg> can be: " + ListLogCategories() + ".",
    false, OptionsCategory::DEBUG_TEST, P2P_OPTION_MULTI);
Arg("debugexclude", strprintf("Exclude debugging information for a category. Can be used in conjunction with --debug=1 to output debug logs "
                              "for all categories except one or more specified categories."),
    false, OptionsCategory::DEBUG_TEST, P2P_OPTION_MULTI);
Arg("logips", strprintf("Include IP addresses in debug output (default: %u)", DEFAULT_LOGIPS),
    false, OptionsCategory::DEBUG_TEST, P2P_OPTION_ARGUMENT);
Arg("mocktime", "Replace actual time with <n> seconds since epoch (default: 0)",
    true, OptionsCategory::DEBUG_TEST, P2P_OPTION_ARGUMENT);
