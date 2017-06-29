/*** Copyright (c), The Regents of the University of California            ***
 *** For more information please refer to files in the COPYRIGHT directory ***/
/*** This code is rewritten by Illyoung Choi (iychoi@email.arizona.edu)    ***
 *** funded by iPlantCollaborative (www.iplantcollaborative.org).          ***/
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <assert.h>
#include <pthread.h>
#include "rodsClient.h"
#include "rcMisc.h"
#include "parseCommandLine.h"
#include "iFuse.Preload.hpp"
#include "iFuse.BufferedFS.hpp"
#include "iFuse.FS.hpp"
#include "iFuse.Lib.hpp"
#include "iFuse.Lib.Conn.hpp"
#include "iFuse.Lib.Fd.hpp"
#include "iFuse.Lib.Util.hpp"
#include "iFuse.Lib.RodsClientAPI.hpp" 
#include "iFuseOper.hpp"
#include "iFuseCmdLineOpt.hpp"
#include "iFuseVersion.hpp"

static struct fuse_operations irodsOper;

static void usage();
static int checkMountPoint(char *mountPoint, bool nonempty);
static void registerClientProgram(char *prog);

int main(int argc, char **argv) {
    int status;
    rodsEnv myRodsEnv;
    int fuse_argc;
    char **fuse_argv;
    iFuseOpt_t myiFuseOpt;

    bzero(&irodsOper, sizeof ( irodsOper));
    irodsOper.getattr = iFuseGetAttr;
    irodsOper.readlink = iFuseReadLink;
    irodsOper.mkdir = iFuseMakeDir;
    irodsOper.unlink = iFuseUnlink;
    irodsOper.rmdir = iFuseRemoveDir;
    irodsOper.open = iFuseOpen;
    irodsOper.read = iFuseRead;
    irodsOper.write = iFuseWrite;
    irodsOper.statfs = iFuseStatfs;
    irodsOper.release = iFuseClose;
    irodsOper.opendir = iFuseOpenDir;
    irodsOper.readdir = iFuseReadDir;
    irodsOper.releasedir = iFuseCloseDir;
    irodsOper.init = iFuseInit;
    irodsOper.destroy = iFuseDestroy;
    irodsOper.link = iFuseLink;
    irodsOper.symlink = iFuseSymlink;
    irodsOper.rename = iFuseRename;
    irodsOper.utimens = iFuseUtimens;
    irodsOper.truncate = iFuseTruncate;
    irodsOper.chmod = iFuseChmod;
    irodsOper.chown = iFuseChown;
    irodsOper.flush = iFuseFlush;
    irodsOper.mknod = iFuseCreate;
    irodsOper.fsync = iFuseFsync;
    irodsOper.ioctl = iFuseIoctl;

    status = getRodsEnv(&myRodsEnv);
    if (status < 0) {
        fprintf(stderr, "iRods Fuse abort: getRodsEnv error with status %d\n", status);
        return 1;
    }

    iFuseCmdOptsInit();

    iFuseCmdOptsParse(argc, argv);
    iFuseCmdOptsAdd("-odirect_io");

    iFuseGetOption(&myiFuseOpt);
    
    if (myiFuseOpt.help) {
        usage();
        return 0;
    }
    if (myiFuseOpt.version) {
        printf("iRODS RELEASE VERSION: %s\n", RODS_REL_VERSION);
        printf("iRODS FUSE CLIENT VERSION: %s\n", IFUSE_VERSION);
        return 0;
    }
    
    // check mount point
    status = checkMountPoint(myiFuseOpt.mountpoint, myiFuseOpt.nonempty);
    if(status != 0) {
        fprintf(stderr, "iRods Fuse abort: mount point check failed\n");
        iFuseCmdOptsDestroy();
        return 1;
    }
    
    if (myiFuseOpt.workdir != NULL) {
        if(strlen(myiFuseOpt.workdir) < MAX_NAME_LEN) {
            bzero(myRodsEnv.rodsCwd, MAX_NAME_LEN);
            strcpy(myRodsEnv.rodsCwd, myiFuseOpt.workdir);
        }
    }
    
    registerClientProgram(argv[0]);
    
    iFuseLibSetRodsEnv(&myRodsEnv);
    iFuseLibSetOption(&myiFuseOpt);

    // Init libraries
    iFuseLibInit();
    iFuseFsInit();
    iFuseBufferedFSInit();
    iFusePreloadInit();

    // check iRODS iCAT host connectivity
    status = iFuseConnTest();
    if(status != 0) {
        fprintf(stderr, "iRods Fuse abort: cannot connect to iCAT\n");
        
        // Destroy libraries
        iFusePreloadDestroy();
        iFuseBufferedFSDestroy();
        iFuseFsDestroy();
        iFuseLibDestroy();
        
        iFuseCmdOptsDestroy();
        return 1;
    }
    
    iFuseGenCmdLineForFuse(&fuse_argc, &fuse_argv);
    
    iFuseLibLog(LOG_DEBUG, "main: iRods Fuse gets started.");
    status = fuse_main(fuse_argc, fuse_argv, &irodsOper, NULL);
    iFuseLibLog(LOG_DEBUG, "main: iRods Fuse gets stopped.");
    iFuseReleaseCmdLineForFuse(fuse_argc, fuse_argv);

    // Destroy libraries
    iFusePreloadDestroy();
    iFuseBufferedFSDestroy();
    iFuseFsDestroy();
    iFuseLibDestroy();

    iFuseCmdOptsDestroy();

    if (status < 0) {
        return 1;
    } else {
        return 0;
    }
}

static void usage() {
    char *msgs[] = {
        "Usage: irodsFs [[options]...] mount-point",
        "Options are:",
        " -h                               Print this help",
        " -d                               Run irodsFs in debug mode",
        " -f                               Run irodsFs in foreground mode",
        " -v, -V, --version                Print version information",
        " -t, --ticket <ticket_no>         Use ticket for authentication",
        " -w, --workdir <irods_dir>        Use given irods dir as a work dir",
        " --nocache                        Disable all caching features (Buffered IO, Preload, Metadata Cache)",
        " --nopreload                      Disable Preload feature that pre-fetches file blocks in advance",
        " --nocachemetadata                Disable metadata caching feature",
        " --connreuse                      Set to reuse network connections for performance. This may provide inconsistent metadata with mysql-backed iCAT. By default, connections are not reused",
        " --maxconn <num_conn>             Set max number of network connection to be established at the same time. By default, this is set to 10",
        " --blocksize <block_size>         Set block size at data transfer. All transfer is made in a block-level for performance. By default, this is set to 1048576(1MB)",
        " --conntimeout <timeout>          Set timeout of a network connection. After the timeout, idle connections will be automatically closed. By default, this is set to 300(5 minutes)",
        " --connkeepalive <interval>       Set interval of keepalive requests. For every keepalive interval, keepalive message is sent to iCAT to keep network connections live. By default, this is set to 180(3 minutes)",
        " --conncheckinterval <interval>   Set intervals of connection timeout check. For every check intervals, all connections established are checked to figure out if they are timed-out. By default, this is set to 10(10 seconds)",
        " --apitimeout <timeout>           Set timeout of iRODS client API calls. If an API call does not respond before the timeout, the API call and the network connection associated with are killed. By default, this is set to 90(90 seconds)",
        " --preloadblocks <num_blocks>     Set the number of blocks pre-fetched. By default, this is set to 3 (next 3 blocks in advance)",
        " --metadatacachetimeout <timeout> Set timeout of a metadata cache. Metadata caches are invalidated after the timeout. By default, this is set to 180(3 minutes)",
        ""
    };
    int i;
    for (i = 0;; i++) {
        if (strlen(msgs[i]) == 0) {
            return;
        }
        printf("%s\n", msgs[i]);
    }
}

static int checkMountPoint(char *mountPoint, bool nonempty) {
    char *absMountPath;
    DIR *dir = NULL;
    char *resolvedMountPath;
    
    if(mountPoint == NULL || strlen(mountPoint) == 0) {
        fprintf(stderr, "Mount point is not given\n");
        return -1;
    }
    
    if(mountPoint[0] == '/') {
        // absolute mount path
        absMountPath = strdup(mountPoint);
    } else {
        char *cwd = getcwd(NULL, 0);
        if(cwd != NULL) {
            int buffsize = strlen(cwd) + 1 + strlen(mountPoint) + 1;
            absMountPath = (char*)calloc(1, buffsize);
            
            strcpy(absMountPath, cwd);
            if(absMountPath[strlen(absMountPath)-1] != '/') {
                strcat(absMountPath, "/");
            }
            strcat(absMountPath, mountPoint);
            
            free(cwd);
        } else {
            fprintf(stderr, "Cannot get a current directory\n");
            return -1;
        }
    }
    
    resolvedMountPath = realpath(absMountPath, NULL);
    if(resolvedMountPath == NULL) {
        // if the given directory not exists or other errors
        if(errno == ENOENT) {
            // directory not accessible
            fprintf(stderr, "Cannot find a directory %s\n", absMountPath);
            free(absMountPath);
            return -1;
        } else {
            // not accessible
            fprintf(stderr, "The directory %s is not accessible\n", absMountPath);
            free(absMountPath);
            return -1;
        }
    }

    free(absMountPath);
    
    dir = opendir(resolvedMountPath);
    if(dir != NULL) {
        // exists
        bool filefound = false;
        if(!nonempty) {
            // check if directory is empty or not
            struct dirent *d;
            while ((d = readdir(dir)) != NULL) {
                if(!strcmp(d->d_name, ".")) {
                    continue;
                } else if(!strcmp(d->d_name, "..")) {
                    continue;
                } else {
                    filefound = true;
                    break;
                }
            }
        }
        
        closedir(dir);
        
        if(filefound) {
            fprintf(stderr, "A directory %s is not empty\nif you are sure this is safe, use the 'nonempty' mount option", resolvedMountPath);
            free(resolvedMountPath);
            return -1;
        }
    } else if(errno == ENOENT) {
        // directory not accessible
        fprintf(stderr, "Cannot find a directory %s\n", resolvedMountPath);
        free(resolvedMountPath);
        return -1;
    } else {
        // not accessible
        fprintf(stderr, "The directory %s is not accessible\n", resolvedMountPath);
        free(resolvedMountPath);
        return -1;
    }
    
    free(resolvedMountPath);
    return 0;
}

static void registerClientProgram(char *prog) {
    // set SP_OPTION to argv[0] so it can be passed to server
    char filename[MAX_NAME_LEN];
    
    *filename = '\0';
    iFuseLibGetFilename(prog, filename, MAX_NAME_LEN);
    if(strlen(filename) != 0) {
        mySetenvStr(SP_OPTION, filename);
    }
}
