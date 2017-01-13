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
        return 0;
    }
    
    // check mount point
    status = checkMountPoint(myiFuseOpt.mountpoint, myiFuseOpt.nonempty);
    if(status != 0) {
        fprintf(stderr, "iRods Fuse abort: mount point check failed\n");
        iFuseCmdOptsDestroy();
        return 1;
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
        "Usage: irodsFs [-hdfvV] [-o opt,[opt...]]",
        "Single user iRODS/Fuse server",
        "Options are:",
        " -h        this help",
        " -d        FUSE debug mode",
        " -f        FUSE foreground mode",
        " -o        opt,[opt...]  FUSE mount options",
        " -v, -V, --version print version information",
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
