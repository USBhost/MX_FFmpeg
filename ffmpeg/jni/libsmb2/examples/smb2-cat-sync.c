/* -*-  mode:c; tab-width:8; c-basic-offset:8; indent-tabs-mode:nil;  -*- */
/*
   Copyright (C) 2016 by Ronnie Sahlberg <ronniesahlberg@gmail.com>

Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "smb2.h"
#include "libsmb2.h"
#include "libsmb2-raw.h"

#define MAXBUF 16 * 1024 * 1024
uint8_t buf[MAXBUF];
uint32_t pos;

int usage(void)
{
        fprintf(stderr, "Usage:\n"
                "smb2-cat-sync <smb2-url>\n\n"
                "URL format: "
                "smb://[<domain;][<username>@]<host>[:<port>]/<share>/<path>\n");
        exit(1);
}

int main(int argc, char *argv[])
{
        struct smb2_context *smb2;
        struct smb2_url *url;
        struct smb2fh *fh;
        int count, rc = 0;

        if (argc < 2) {
                usage();
        }

	smb2 = smb2_init_context();
        if (smb2 == NULL) {
                fprintf(stderr, "Failed to init context\n");
                exit(0);
        }

        url = smb2_parse_url(smb2, argv[1]);
        if (url == NULL) {
                fprintf(stderr, "Failed to parse url: %s\n",
                        smb2_get_error(smb2));
                exit(0);
        }

        smb2_set_security_mode(smb2, SMB2_NEGOTIATE_SIGNING_ENABLED);

	if (smb2_connect_share(smb2, url->server, url->share, url->user) != 0) {
		printf("smb2_connect_share failed. %s\n", smb2_get_error(smb2));
		exit(10);
	}

        fh = smb2_open(smb2, url->path, O_RDONLY);
        if (fh == NULL) {
		printf("smb2_open failed. %s\n", smb2_get_error(smb2));
		exit(10);
        }

        while ((count = smb2_pread(smb2, fh, buf, MAXBUF, pos)) != 0) {
                if (count == -EAGAIN) {
                        continue;
                }
                if (count < 0) {
                        fprintf(stderr, "Failed to read file. %s\n",
                                smb2_get_error(smb2));
                        rc = 1;
                        break;
                }
                write(0, buf, count);
                pos += count;
        };
                
        smb2_close(smb2, fh);
        smb2_disconnect_share(smb2);
        smb2_destroy_url(url);
        smb2_destroy_context(smb2);
        
	return rc;
}
