// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020 Park Ju Hyung
 */

#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>

struct cheeze_req_user {
	int id;
	int op;
	unsigned int pos; // sector_t
	unsigned int len;
} __attribute__((aligned(8), packed));

#define TRACE_TARGET "/trace"

int main() {
	int dumpfd;
	unsigned int i;
	uint32_t crc;
	struct cheeze_req_user ureq;

	dumpfd = open(TRACE_TARGET, O_RDONLY);
	if (dumpfd < 0) {
		perror("Failed to open " TRACE_TARGET);
		return 1;
	}

	while (read(dumpfd, &ureq, sizeof(ureq)) == sizeof(ureq)) {
		printf("id=%d\n    op=%d\n    pos=%u\n    len=%u\n\n", ureq.id, ureq.op, ureq.pos, ureq.len);
		if (ureq.len) {
			printf("    crc {\n");
			for (i = 0; i < ureq.len; i += 4096) {
				read(dumpfd, &crc, sizeof(crc));
				printf("        0x%x,\n", crc);
			}
			printf("    }\n");
		}
	}

	close(dumpfd);

	return 0;
}
