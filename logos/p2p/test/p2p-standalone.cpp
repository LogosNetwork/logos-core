// Copyright (c) 2018 Logos Network
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// This file contains main function of p2p standalone application

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "../p2p.h"

class p2p_standalone : public p2p_interface {
	virtual bool ReceiveMessageCallback(const void *message, unsigned size) {
		printf("\nReceived %3d: %.*s\nType message: ", size, size, (const char *)message);
		fflush(stdout);
	}
};

int main(int argc, char **argv) {
	p2p_standalone p2p;
	char buf[256], *str;
	printf("This is p2p standalone application. Initializing...\n");
	if (!p2p.Init(argc, argv)) return 0;
	printf("Type 'exit' to exit the program or message to send.\n");
	for(;;) {
		printf("Type message: "); fflush(stdout);
		if (!fgets(buf, 256, stdin)) break;
		str = buf;
		while (*str && isspace(*str)) str++;
		while (*str && isspace(str[strlen(str) - 1])) str[strlen(str) - 1] = 0;
		if (!strcmp(str, "exit")) break;
		if (*str) p2p.PropagateMessage(str, strlen(str));
	}
	printf("Shutdown...\n");
	p2p.Shutdown();
	printf("Bye-bye!\n");
	return 0;
}
