/*** Copyright (c), The Regents of the University of California            ***
 *** For more information please refer to files in the COPYRIGHT directory ***/
/*** This code is written by Illyoung Choi (iychoi@email.arizona.edu)      ***
 *** funded by iPlantCollaborative (www.iplantcollaborative.org).          ***/
#ifndef IFUSECMDLINEOPT_HPP
#define IFUSECMDLINEOPT_HPP

#include "iFuse.Lib.hpp"

// for Postgresql-iCAT only
//#define USE_CONNREUSE

#define IFUSE_CMD_ARG_MAX_TOKEN_LEN 30

typedef struct IFuseCmdArg {
    int start;
    int end;
    char command[IFUSE_CMD_ARG_MAX_TOKEN_LEN];
    char value[IFUSE_CMD_ARG_MAX_TOKEN_LEN];
} iFuseCmdArg_t;

void iFuseCmdOptsInit();
void iFuseCmdOptsDestroy();
int iFuseCmdOptsParse(int argc, char **argv);
void iFuseCmdOptsAdd(char *opt);
void iFuseGetOption(iFuseOpt_t *opt);
void iFuseGenCmdLineForFuse(int *fuse_argc, char ***fuse_argv);
void iFuseReleaseCmdLineForFuse(int fuse_argc, char **fuse_argv);

#endif	/* IFUSECMDLINEOPT_HPP */
