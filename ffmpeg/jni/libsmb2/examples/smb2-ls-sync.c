/* -*-  mode:c; tab-width:8; c-basic-offset:8; indent-tabs-mode:nil;  -*- */
/*
   Copyright (C) 2016 by Ronnie Sahlberg <ronniesahlberg@gmail.com>

Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#define _GNU_SOURCE

#include <inttypes.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "smb2.h"
#include "libsmb2.h"
#include "libsmb2-raw.h"

int usage(void)
{
        fprintf(stderr, "Usage:\n"
                "smb2-ls-sync <smb2-url>\n\n"
                "URL format: "
                "smb://[<domain;][<username>@]<host>[:<port>]/<share>/<path>\n");
        exit(1);
}

int main(int argc, char *argv[])
{
        struct smb2_context *smb2;
        struct smb2_url *url;
        struct smb2dir *dir;
        struct smb2dirent *ent;
        char *link;

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

	if (smb2_connect_share(smb2, url->server, url->share, url->user) < 0) {
		printf("smb2_connect_share failed. %s\n", smb2_get_error(smb2));
		exit(10);
	}

	dir = smb2_opendir(smb2, url->path);
	if (dir == NULL) {
		printf("smb2_opendir failed. %s\n", smb2_get_error(smb2));
		exit(10);
	}

        while ((ent = smb2_readdir(smb2, dir))) {
                char *type;
                time_t t;

                t = (time_t)ent->st.smb2_mtime;
                switch (ent->st.smb2_type) {
                case SMB2_TYPE_LINK:
                        type = "LINK";
                        break;
                case SMB2_TYPE_FILE:
                        type = "FILE";
                        break;
                case SMB2_TYPE_DIRECTORY:
                        type = "DIRECTORY";
                        break;
                default:
                        type = "unknown";
                        break;
                }
                printf("%-20s %-9s %15"PRIu64" %s", ent->name, type, ent->st.smb2_size, asctime(localtime(&t)));
                if (ent->st.smb2_type == SMB2_TYPE_LINK) {
                        char buf[256];

                        if (url->path && url->path[0]) {
                                asprintf(&link, "%s/%s", url->path, ent->name);
                        } else {
                                asprintf(&link, "%s", ent->name);
                        }
                        smb2_readlink(smb2, link, buf, 256);
                        printf("    -> [%s]\n", buf);
                        free(link);
                }
        }

        smb2_closedir(smb2, dir);
        smb2_disconnect_share(smb2);
        smb2_destroy_url(url);
        smb2_destroy_context(smb2);
        
	return 0;
}
