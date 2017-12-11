/*** Copyright (c), The Regents of the University of California            ***
 *** For more information please refer to files in the COPYRIGHT directory ***/
/*** This code is written by Illyoung Choi (iychoi@email.arizona.edu)      ***
 *** funded by iPlantCollaborative (www.iplantcollaborative.org).          ***/
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <time.h>
#include <assert.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include "iFuseCmdLineOpt.hpp"
#include "iFuse.FS.hpp"
#include "iFuse.Lib.Conn.hpp"
#include "iFuse.Lib.MetadataCache.hpp"
#include "iFuse.Lib.RodsClientAPI.hpp"
#include "iFuse.Lib.Util.hpp"
#include "iFuse.Preload.hpp"
#include "miscUtil.h"

static iFuseOpt_t g_Opt;

static bool _atob(const char *str) {
    if(str == NULL) {
        return false;
    } else if(iFuseUtilStricmp(str, "true") == 0) {
        return true;
    }
    return false;
}

void iFuseCmdOptsInit() {
    char *value;

    bzero(&g_Opt, sizeof(iFuseOpt_t));

    g_Opt.directio = true;
    g_Opt.bufferedFS = true;
    g_Opt.preload = true;
    g_Opt.cacheMetadata = true;
    g_Opt.maxConn = IFUSE_MAX_NUM_CONN;
    g_Opt.blocksize = IFUSE_BUFFER_CACHE_BLOCK_SIZE;
#ifdef USE_CONNREUSE
    g_Opt.connReuse = true;
#else
    g_Opt.connReuse = false;
#endif
    g_Opt.connTimeoutSec = IFUSE_FREE_CONN_TIMEOUT_SEC;
    g_Opt.connKeepAliveSec = IFUSE_FREE_CONN_KEEPALIVE_SEC;
    g_Opt.connCheckIntervalSec = IFUSE_FREE_CONN_CHECK_INTERVAL_SEC;
    g_Opt.rodsapiTimeoutSec = IFUSE_RODSCLIENTAPI_TIMEOUT_SEC;
    g_Opt.preloadNumBlocks = IFUSE_PRELOAD_PBLOCK_NUM;
    g_Opt.metadataCacheTimeoutSec = IFUSE_METADATA_CACHE_TIMEOUT_SEC;

    // check environmental variables
    value = getenv("IRODSFS_NOCACHE"); // true/false
    if(_atob(value)) {
        g_Opt.bufferedFS = false;
        g_Opt.preload = false;
        g_Opt.cacheMetadata = false;
    }

    value = getenv("IRODSFS_NOPRELOAD"); // true/false
    if(_atob(value)) {
        g_Opt.preload = false;
    }

    value = getenv("IRODSFS_NOCACHEMETADATA"); // true/false
    if(_atob(value)) {
        g_Opt.cacheMetadata = false;
    }

    value = getenv("IRODSFS_MAXCONN"); // number
    if(value != NULL) {
        g_Opt.maxConn = atoi(value);
    }

    value = getenv("IRODSFS_BLOCKSIZE"); // number
    if(value != NULL) {
        g_Opt.blocksize = atoi(value);
    }

    value = getenv("IRODSFS_CONNREUSE"); // true/false
    if(_atob(value)) {
        g_Opt.connReuse = true;
    }

    value = getenv("IRODSFS_NOCONNREUSE"); // true/false
    if(_atob(value)) {
        g_Opt.connReuse = false;
    }

    value = getenv("IRODSFS_CONNTIMEOUT"); // number
    if(value != NULL) {
        g_Opt.connTimeoutSec = atoi(value);
    }

    value = getenv("IRODSFS_CONNKEEPALIVE"); // number
    if(value != NULL) {
        g_Opt.connKeepAliveSec = atoi(value);
    }

    value = getenv("IRODSFS_CONNCHECKINTERVAL"); // number
    if(value != NULL) {
        g_Opt.connCheckIntervalSec = atoi(value);
    }

    value = getenv("IRODSFS_APITIMEOUT"); // number
    if(value != NULL) {
        g_Opt.rodsapiTimeoutSec = atoi(value);
    }

    value = getenv("IRODSFS_PRELOADBLOCKS"); // number
    if(value != NULL) {
        g_Opt.preloadNumBlocks = atoi(value);
    }

    value = getenv("IRODSFS_METADATACACHETIMEOUT"); // number
    if(value != NULL) {
        g_Opt.metadataCacheTimeoutSec = atoi(value);
    }
}

void iFuseCmdOptsDestroy() {
    iFuseExtendedOpt_t *peopt = NULL;

    if(g_Opt.program != NULL) {
        free(g_Opt.program);
        g_Opt.program = NULL;
    }

    if(g_Opt.mountpoint != NULL) {
        free(g_Opt.mountpoint);
        g_Opt.mountpoint = NULL;
    }

    if(g_Opt.workdir != NULL) {
        free(g_Opt.workdir);
        g_Opt.workdir = NULL;
    }

    if(g_Opt.ticket != NULL) {
        free(g_Opt.ticket);
        g_Opt.ticket = NULL;
    }

    peopt = g_Opt.extendedOpts;
    while(peopt != NULL) {
        iFuseExtendedOpt_t *next = peopt->next;

        if(peopt->opt != NULL) {
            free(peopt->opt);
            peopt->opt = NULL;
        }
        free(peopt);

        peopt = next;
    }
    g_Opt.extendedOpts = NULL;
}

static iFuseExtendedOpt_t *_newExtendedOpt(char *opt) {
    iFuseExtendedOpt_t *eopt = NULL;
    eopt = (iFuseExtendedOpt_t *)calloc(1, sizeof(iFuseExtendedOpt_t));
    if(eopt != NULL) {
        eopt->opt = strdup(opt);
    }
    return eopt;
}

static int _parseFuseCommandArg(char **argv, int argc, int index, iFuseCmdArg_t *option) {
    // handle -- or -oxxx or -o xxx
    int tokens = 0;
    int i;

    bzero(option, sizeof(iFuseCmdArg_t));
    for(i=index;i<argc;i++) {
        if(strcmp(argv[i], "-o") == 0) {
            if(tokens > 0) {
                // already processed a command
                break;
            }
            // following is a command
            option->start = i;
            option->end = i + 1;
            tokens++;
            i++;
            if(i < argc) {
                strncpy(option->command, argv[i], IFUSE_CMD_ARG_MAX_TOKEN_LEN);
                option->end = i + 1;
                tokens++;
            }
        } else if(strncmp(argv[i], "--", 2) == 0 || strncmp(argv[i], "-o", 2) == 0) {
            if(tokens > 0) {
                // already processed a command
                break;
            }
            option->start = i;
            strncpy(option->command, argv[i] + 2, IFUSE_CMD_ARG_MAX_TOKEN_LEN);
            option->end = i + 1;
            tokens++;
        } else if(strncmp(argv[i], "-", 1) == 0) {
            if(tokens > 0) {
                // already processed a command
                break;
            }
            option->start = i;
            strncpy(option->command, argv[i] + 1, IFUSE_CMD_ARG_MAX_TOKEN_LEN);
            option->end = i + 1;
            tokens++;
        } else {
            // value
            if(tokens == 0) {
                // if there's no command found
                continue;
            }
            strncpy(option->value, argv[i], IFUSE_CMD_ARG_MAX_TOKEN_LEN);
            option->end = i + 1;
            tokens++;
            break;
        }
    }
    return tokens;
}

int iFuseCmdOptsParse(int argc, char **argv) {
    int c;
    char buff[MAX_NAME_LEN];
    int index;
    int i;
    int j;

    g_Opt.program = strdup(argv[0]);

    i = 0;
    while(i<argc) {
        iFuseCmdArg_t cmd;

        int tokens = _parseFuseCommandArg(argv, argc, i, &cmd); // except mount-point path
        if(tokens > 0) {
            bool processed = false;

            if(strcmp(cmd.command, "version") == 0) {
                g_Opt.version = true;
                processed = true;
            } else if(strcmp(cmd.command, "nodirectio") == 0) {
                g_Opt.directio = false;
                processed = true;
            } else if(strcmp(cmd.command, "nocache") == 0) {
                g_Opt.bufferedFS = false;
                g_Opt.preload = false;
                g_Opt.cacheMetadata = false;
                processed = true;
            } else if(strcmp(cmd.command, "nopreload") == 0) {
                g_Opt.preload = false;
                processed = true;
            } else if(strcmp(cmd.command, "nocachemetadata") == 0) {
                g_Opt.cacheMetadata = false;
                processed = true;
            } else if(strcmp(cmd.command, "maxconn") == 0) {
                if(strlen(cmd.value) > 0) {
                    g_Opt.maxConn = atoi(cmd.value);
                }
                processed = true;
            } else if(strcmp(cmd.command, "blocksize") == 0) {
                if(strlen(cmd.value) > 0) {
                    g_Opt.blocksize = atoi(cmd.value);
                }
                processed = true;
            } else if(strcmp(cmd.command, "connreuse") == 0) {
                g_Opt.connReuse = true;
                processed = true;
            } else if(strcmp(cmd.command, "noconnreuse") == 0) {
                g_Opt.connReuse = false;
                processed = true;
            } else if(strcmp(cmd.command, "conntimeout") == 0) {
                if(strlen(cmd.value) > 0) {
                    g_Opt.connTimeoutSec = atoi(cmd.value);
                }
                processed = true;
            } else if(strcmp(cmd.command, "connkeepalive") == 0) {
                if(strlen(cmd.value) > 0) {
                    g_Opt.connKeepAliveSec = atoi(cmd.value);
                }
                processed = true;
            } else if(strcmp(cmd.command, "conncheckinterval") == 0) {
                if(strlen(cmd.value) > 0) {
                    g_Opt.connCheckIntervalSec = atoi(cmd.value);
                }
                processed = true;
            } else if(strcmp(cmd.command, "apitimeout") == 0) {
                if(strlen(cmd.value) > 0) {
                    g_Opt.rodsapiTimeoutSec = atoi(cmd.value);
                }
                processed = true;
            } else if(strcmp(cmd.command, "preloadblocks") == 0) {
                if(strlen(cmd.value) > 0) {
                    g_Opt.preloadNumBlocks = atoi(cmd.value);
                }
                processed = true;
            } else if(strcmp(cmd.command, "metadatacachetimeout") == 0) {
                if(strlen(cmd.value) > 0) {
                    g_Opt.metadataCacheTimeoutSec = atoi(cmd.value);
                }
                processed = true;
            } else if(strcmp(cmd.command, "ticket") == 0) {
                if(strlen(cmd.value) > 0) {
                    g_Opt.ticket = strdup(cmd.value);
                }
                processed = true;
            } else if(strcmp(cmd.command, "workdir") == 0) {
                if(strlen(cmd.value) > 0) {
                    g_Opt.workdir = strdup(cmd.value);
                }
                processed = true;
            } else if(strcmp(cmd.command, "default_permissions") == 0) {
                // skip - fuse command
            } else if(strcmp(cmd.command, "allow_other") == 0) {
                // skip - fuse command
            } else if(strcmp(cmd.command, "nonempty") == 0) {
                // skip - fuse command
            } else if(strcmp(cmd.command, "use_ino") == 0) {
                // skip - fuse command
            } else if(strcmp(cmd.command, "blkdev") == 0) {
                // skip - fuse command
            } else if(strcmp(cmd.command, "allow_root") == 0) {
                // skip - fuse command
            } else if(strcmp(cmd.command, "auto_unmount") == 0) {
                // skip - fuse command
            } else if(strncmp(cmd.command, "rootmode", strlen("rootmode")) == 0) {
                // skip - fuse command
            } else if(strncmp(cmd.command, "blksize", strlen("blksize")) == 0) {
                // skip - fuse command
            } else if(strncmp(cmd.command, "max_read", strlen("max_read")) == 0) {
                // skip - fuse command
            } else if(strncmp(cmd.command, "fd", strlen("fd")) == 0) {
                // skip - fuse command
            } else if(strncmp(cmd.command, "user_id", strlen("user_id")) == 0) {
                // skip - fuse command
            } else if(strncmp(cmd.command, "fsname", strlen("fsname")) == 0) {
                // skip - fuse command
            } else if(strncmp(cmd.command, "subtype", strlen("subtype")) == 0) {
                // skip - fuse command
            } else if(strlen(cmd.command) == 1) {
                // skip - short commands
            } else {
                // unknown parameters
                fprintf(stderr, "unknown parameter - %s\n", cmd.command);
                return 1;
            }

            if(processed) {
                for(j=cmd.start;j<cmd.end;j++) {
                    argv[j] = "-Z";
                }
            }
            i += tokens;
        } else {
            break;
        }
    }

    optind = 1;
    while((c = getopt(argc, argv, "hvVdfo:t:w:Z")) != -1) {
        switch(c) {
            case 'h':
                {
                    // help
                    g_Opt.help = true;
                }
                break;
            case 'v':
            case 'V':
                {
                    g_Opt.version = true;
                }
                break;
            case 'd':
                {
                    // fuse debug mode
                    g_Opt.debug = true;
                }
                break;
            case 'f':
                {
                    // fuse foreground mode
                    g_Opt.foreground = true;
                }
                break;
            case 'o':
                {
                    // fuse options
                    if (strcmp("use_ino", optarg) == 0) {
                        fprintf(stderr, "use_ino fuse option not supported\n");
                        return 1;
                    } else if (strcmp("nonempty", optarg) == 0) {
                        // fuse nonempty option
                        g_Opt.nonempty = true;
                        break;
                    }
                    bzero(buff, MAX_NAME_LEN);
                    sprintf(buff, "-o%s", optarg);
                    iFuseCmdOptsAdd(buff);
                }
                break;
            case 't':
                {
                    // ticket
                    if (strlen(optarg) > 0) {
                        g_Opt.ticket = strdup(optarg);
                    }
                }
                break;
            case 'w':
                {
                    // work-dir
                    if (strlen(optarg) > 0) {
                        g_Opt.workdir = strdup(optarg);
                    }
                }
                break;
            case 'Z':
                // ignore (from irods parse)
                break;
            case '?':
                fprintf(stderr, "%c %s", c, optarg);
                break;
            default:
                fprintf(stderr, "%c %s", c, optarg);
                break;
        }
    }

    for(index=optind;index<argc;index++) {
        if(argv[index] != NULL && strlen(argv[index]) > 0) {
            g_Opt.mountpoint = strdup(argv[index]);
            break;
        }
    }

    return 0;
}

void iFuseCmdOptsAdd(char *opt) {
    iFuseExtendedOpt_t *eopt = _newExtendedOpt(opt);
    assert(eopt != NULL);

    if(strncmp(opt, "-o", 2) == 0) {
        // starting with -o (fuse options)
        eopt->fuse = true;
    } else {
        eopt->fuse = false;
    }

    // add to list
    eopt->next = g_Opt.extendedOpts;
    g_Opt.extendedOpts = eopt;
}

void iFuseGetOption(iFuseOpt_t *opt) {
    assert(opt != NULL);

    memcpy(opt, &g_Opt, sizeof(iFuseOpt_t));
    opt->extendedOpts = NULL;
}

void iFuseGenCmdLineForFuse(int *fuse_argc, char ***fuse_argv) {
    int i;
    int argc = 0;
    char **argv = NULL;
    iFuseExtendedOpt_t *peopt = NULL;

    *fuse_argc = 0;
    *fuse_argv = NULL;

    if(g_Opt.program) {
        argc++;
    }

    if(g_Opt.debug) {
        argc++;
    }

    if(g_Opt.foreground) {
        argc++;
    }

    if(g_Opt.nonempty) {
        argc++;
    }

    if(g_Opt.directio) {
        argc++;
    }

    if(g_Opt.mountpoint != NULL) {
        argc++;
    }

    peopt = g_Opt.extendedOpts;
    while(peopt != NULL) {
        if(peopt->fuse) {
            if(peopt->opt != NULL) {
                argc++;
            }
        }
        peopt = peopt->next;
    }

    argv = (char**)calloc(argc, sizeof(char*));
    assert(argv != NULL);

    i = 0;
    if(g_Opt.program) {
        argv[i] = strdup(g_Opt.program);
        i++;

        if(g_Opt.debug) {
            fprintf(stdout, "prog : %s\n", g_Opt.program);
        }
    }

    if(g_Opt.debug) {
        argv[i] = strdup("-d");
        i++;

        fprintf(stdout, "debug : %d\n", g_Opt.debug);
    }

    if(g_Opt.foreground) {
        argv[i] = strdup("-f");
        i++;

        if(g_Opt.debug) {
            fprintf(stdout, "foreground : %d\n", g_Opt.foreground);
        }
    }

    if(g_Opt.nonempty) {
        argv[i] = strdup("-nonempty");
        i++;

        if(g_Opt.debug) {
            fprintf(stdout, "nonempty : %d\n", g_Opt.nonempty);
        }
    }

    if(g_Opt.directio) {
        argv[i] = strdup("-odirect_io");
        i++;

        if(g_Opt.debug) {
            fprintf(stdout, "direct_io : %d\n", g_Opt.directio);
        }
    }

    peopt = g_Opt.extendedOpts;
    while(peopt != NULL) {
        if(peopt->fuse) {
            if(peopt->opt != NULL) {
                argv[i] = strdup(peopt->opt);
                i++;

                if(g_Opt.debug) {
                    fprintf(stdout, "opt : %s\n", peopt->opt);
                }
            }
        }
        peopt = peopt->next;
    }

    if(g_Opt.mountpoint != NULL) {
        argv[i] = strdup(g_Opt.mountpoint);
        i++;

        if(g_Opt.debug) {
            fprintf(stdout, "mountpoint : %s\n", g_Opt.mountpoint);
        }
    }

    *fuse_argc = argc;
    *fuse_argv = argv;
}

void iFuseReleaseCmdLineForFuse(int fuse_argc, char **fuse_argv) {
    int i;
    if (fuse_argv != NULL) {
        for (i=0;i<fuse_argc;i++) {
            if (fuse_argv[i] != NULL) {
                free(fuse_argv[i]);
            }
        }
        free(fuse_argv);
    }
}
