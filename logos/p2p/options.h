Arg("addnode", "=<ip>", true, "Add a node to connect to and attempt to keep the connection open. This option can be specified multiple times to add multiple nodes.", false, OptionsCategory::CONNECTION);
Arg("banscore", "=<n>", false, strprintf("Threshold for disconnecting misbehaving peers (default: %u)", DEFAULT_BANSCORE_THRESHOLD), false, OptionsCategory::CONNECTION);
Arg("bantime", "=<n>", false, strprintf("Number of seconds to keep misbehaving peers from reconnecting (default: %u)", DEFAULT_MISBEHAVING_BANTIME), false, OptionsCategory::CONNECTION);
Arg("bind", "=<addr>", false, "Bind to given address and always listen on it. Use [host]:port notation for IPv6", false, OptionsCategory::CONNECTION);
Arg("connect", "=<ip>", true, "Connect only to the specified node; -noconnect disables automatic connections (the rules for this peer are the same as for -addnode). This option can be specified multiple times to connect to multiple nodes.", false, OptionsCategory::CONNECTION);
Arg("discover", "", false, "Discover own IP addresses (default: 1 when listening and no -externalip or -proxy)", false, OptionsCategory::CONNECTION);
Arg("dns", "", false, strprintf("Allow DNS lookups for -addnode, -seednode and -connect (default: %u)", DEFAULT_NAME_LOOKUP), false, OptionsCategory::CONNECTION);
Arg("dnsseed", "", false, "Query for peer addresses via DNS lookup, if low on addresses (default: 1 unless -connect used)", false, OptionsCategory::CONNECTION);
Arg("externalip", "=<ip>", false, "Specify your own public address", false, OptionsCategory::CONNECTION);
Arg("forcednsseed", "", false, strprintf("Always query for peer addresses via DNS lookup (default: %u)", DEFAULT_FORCEDNSSEED), false, OptionsCategory::CONNECTION);
Arg("listen", "", false, "Accept connections from outside (default: 1 if no -connect)", false, OptionsCategory::CONNECTION);
Arg("maxconnections", "=<n>", false, strprintf("Maintain at most <n> connections to peers (default: %u)", DEFAULT_MAX_PEER_CONNECTIONS), false, OptionsCategory::CONNECTION);
Arg("maxreceivebuffer", "=<n>", false, strprintf("Maximum per-connection receive buffer, <n>*1000 bytes (default: %u)", DEFAULT_MAXRECEIVEBUFFER), false, OptionsCategory::CONNECTION);
Arg("maxsendbuffer", "=<n>", false, strprintf("Maximum per-connection send buffer, <n>*1000 bytes (default: %u)", DEFAULT_MAXSENDBUFFER), false, OptionsCategory::CONNECTION);
Arg("maxtimeadjustment", "", false, strprintf("Maximum allowed median peer time offset adjustment. Local perspective of time may be influenced by peers forward or backward by this amount. (default: %u seconds)", DEFAULT_MAX_TIME_ADJUSTMENT), false, OptionsCategory::CONNECTION);
Arg("maxuploadtarget", "=<n>", false, strprintf("Tries to keep outbound traffic under the given target (in MiB per 24h), 0 = no limit (default: %d)", DEFAULT_MAX_UPLOAD_TARGET), false, OptionsCategory::CONNECTION);
Arg("onlynet", "=<net>", true, "Make outgoing connections only through network <net> (ipv4, ipv6 or onion). Incoming connections are not affected by this option. This option can be specified multiple times to allow multiple networks.", false, OptionsCategory::CONNECTION);
Arg("port", "=<port>", false, strprintf("Listen for connections on <port> (default: %u, testnet: %u, regtest: %u)", defaultChainParams->GetDefaultPort(), testnetChainParams->GetDefaultPort(), regtestChainParams->GetDefaultPort()), false, OptionsCategory::CONNECTION);
Arg("seednode", "=<ip>", true, "Connect to a node to retrieve peer addresses, and disconnect. This option can be specified multiple times to connect to multiple nodes.", false, OptionsCategory::CONNECTION);
Arg("timeout", "=<n>", false, strprintf("Specify connection timeout in milliseconds (minimum: 1, default: %d)", DEFAULT_CONNECT_TIMEOUT), false, OptionsCategory::CONNECTION);
#ifdef USE_UPNP
#if USE_UPNP
Arg("upnp", "", false, "Use UPnP to map the listening port (default: 1 when listening and no -proxy)", false, OptionsCategory::CONNECTION);
#else
Arg("upnp", "", false, strprintf("Use UPnP to map the listening port (default: %u)", 0), false, OptionsCategory::CONNECTION);
#endif
#endif
Arg("whitebind", "=<addr>", false, "Bind to given address and whitelist peers connecting to it. Use [host]:port notation for IPv6", false, OptionsCategory::CONNECTION);
Arg("whitelist", "=<IP address or network>", true, "Whitelist peers connecting from the given IP address (e.g. 1.2.3.4) or CIDR notated network (e.g. 1.2.3.0/24). Can be specified multiple times."
    " Whitelisted peers cannot be DoS banned and their transactions are always relayed, even if they are already in the mempool, useful e.g. for a gateway", false, OptionsCategory::CONNECTION);

Arg("dropmessagestest", "=<n>", false, "Randomly drop 1 of every <n> network messages", true, OptionsCategory::DEBUG_TEST);
Arg("addrmantest", "", false, "Allows to test address relay on localhost", true, OptionsCategory::DEBUG_TEST);
Arg("debug", "=<category>", true, "Output debugging information (default: -nodebug, supplying <category> is optional). "
    "If <category> is not supplied or if <category> = 1, output all debugging information. <category> can be: " + ListLogCategories() + ".", false, OptionsCategory::DEBUG_TEST);
Arg("debugexclude", "=<category>", true, strprintf("Exclude debugging information for a category. Can be used in conjunction with -debug=1 to output debug logs for all categories except one or more specified categories."), false, OptionsCategory::DEBUG_TEST);
Arg("help-debug", "", false, "Show all debugging options (usage: --help -help-debug)", false, OptionsCategory::DEBUG_TEST);
Arg("logips", "", false, strprintf("Include IP addresses in debug output (default: %u)", DEFAULT_LOGIPS), false, OptionsCategory::DEBUG_TEST);
Arg("mocktime", "=<n>", false, "Replace actual time with <n> seconds since epoch (default: 0)", true, OptionsCategory::DEBUG_TEST);
