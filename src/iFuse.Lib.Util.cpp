/*** Copyright (c), The Regents of the University of California            ***
 *** For more information please refer to files in the COPYRIGHT directory ***/
/*** This code is rewritten by Illyoung Choi (iychoi@email.arizona.edu)    ***
 *** funded by iPlantCollaborative (www.iplantcollaborative.org).          ***/
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <string>
#include <cstring>
#include "iFuse.Lib.hpp"
#include "iFuse.Lib.Util.hpp"

static pthread_rwlock_t g_LogLock;
static pthread_rwlockattr_t g_LogLockAttr;

void iFuseUtilInit() {
    pthread_rwlockattr_init(&g_LogLockAttr);
    pthread_rwlock_init(&g_LogLock, &g_LogLockAttr);
}

void iFuseUtilDestroy() {
    pthread_rwlock_destroy(&g_LogLock);
    pthread_rwlockattr_destroy(&g_LogLockAttr);
}

#define TOUPPER(CH) \
    (((CH) >= 'a' && (CH) <= 'z') ? ((CH) - 'a' + 'A') : (CH))

int iFuseUtilStricmp (const char *s1, const char *s2) {
    while (*s2 != 0 && TOUPPER (*s1) == TOUPPER (*s2)) {
        s1++, s2++;
    }
    return (int) (TOUPPER (*s1) - TOUPPER (*s2));
}

time_t iFuseLibGetCurrentTime() {
    return time(NULL);
}

void iFuseLibGetStrCurrentTime(char *buff) {
    time_t cur = iFuseLibGetCurrentTime();
    struct tm curtm;
    localtime_r(&cur, &curtm);
    
    asctime_r(&curtm, buff);
    buff[strlen(buff) - 1] = 0;
}

void iFuseLibGetStrTime(time_t time, char *buff) {
    struct tm curtm;
    localtime_r(&time, &curtm);
    
    asctime_r(&curtm, buff);
    buff[strlen(buff) - 1] = 0;
}

double iFuseLibDiffTimeSec(time_t end, time_t beginning) {
    return difftime(end, beginning);
}

int iFuseLibSplitPath(const char *srcPath, char *dir, unsigned int maxDirLen, char *file, unsigned int maxFileLen) {
    const std::string srcPathString(srcPath);
    if(srcPathString.size() == 0) {
        *dir = '\0';
        *file = '\0';
        return 0;
    }

    const size_t index_of_last_key = srcPathString.rfind('/');
    if(std::string::npos == index_of_last_key) {
        *dir = '\0';
        rstrcpy(file, srcPathString.c_str(), maxFileLen);
        return 0;
    }

    // If dir is the root directory, we want to return the single-character
    // string consisting of the key, NOT the empty string.
    const std::string dirPathString = srcPathString.substr( 0, std::max< size_t >( index_of_last_key, 1 ) );
    const std::string filePathString = srcPathString.substr( index_of_last_key + 1 ) ;

    if(dirPathString.size() >= maxDirLen || filePathString.size() >= maxFileLen) {
        return -ENOBUFS;
    }

    rstrcpy(dir, dirPathString.c_str(), maxDirLen);
    rstrcpy(file, filePathString.c_str(), maxFileLen);
    return 0;
}

int iFuseLibJoinPath(const char *dir, const char *file, char *destPath, unsigned int maxDestPathLen) {
    const std::string dirString(dir);
    const std::string fileString(file);
    int file_start = 0;
    int dir_len = dirString.size();
    
    if(fileString.at(0) == '/') {
        file_start = 1;
    }
    
    if(dirString.at(dir_len - 1) == '/') {
        dir_len -= 1;
    }
    
    const std::string fileString2 = fileString.substr(file_start);
    const std::string dirString2 = dirString.substr(0, dir_len);
    
    if(dirString2.size() + fileString2.size() >= maxDestPathLen) {
        return -ENOBUFS;
    }
    
    unsigned int offset = 0;
    rstrcpy(destPath, dirString2.c_str(), maxDestPathLen);
    offset += dirString2.size();
    rstrcpy(destPath + offset, "/", maxDestPathLen - offset);
    offset += 1;
    rstrcpy(destPath + offset, fileString2.c_str(), maxDestPathLen - offset);
    return 0;
}

int iFuseLibGetFilename(const char *srcPath, char *file, unsigned int maxFileLen) {
    char myDir[MAX_NAME_LEN];
    int status;

    status = iFuseLibSplitPath(srcPath, myDir, MAX_NAME_LEN, file, maxFileLen);
    if(status != 0) {
        return status;
    }
    
    if(strlen(file) != 0) {
        return 0;
    }
    return -ENOENT;
}

void iFuseLibLogLock() {
    pthread_rwlock_wrlock(&g_LogLock);
}

void iFuseLibLogUnlock() {
    pthread_rwlock_unlock(&g_LogLock);
}

void iFuseLibLogToFile(int level, const char *formatStr, ...) {
    va_list args;
    va_start(args, formatStr);
    
    FILE *logFile;
    
    logFile = fopen(IFUSE_LIB_LOG_OUT_FILE_PATH, "a");
    if(logFile != NULL) {
        if(level == 7) {
            // debug
            fprintf(logFile, "DEBUG: ");
        } else if(level == 3) {
            // error
            fprintf(logFile, "ERROR: ");
        } else {
            fprintf(logFile, "errorLevel : %d\n", level);
        }
        vfprintf(logFile, formatStr, args);
        fprintf(logFile, "\n");
        
        fflush(logFile);
        fsync(fileno(logFile));
        fclose(logFile);
    }
    
    va_end(args);
}

void iFuseLibLogErrorToFile(int level, int errCode, char *formatStr, ...) {
    va_list args;
    va_start(args, formatStr);
    
    FILE *logFile;
    
    logFile = fopen(IFUSE_LIB_LOG_OUT_FILE_PATH, "a");
    if(logFile != NULL) {
        if(level == 7) {
            // debug
            fprintf(logFile, "DEBUG - ERROR_CODE(%d): ", errCode);
        } else if(level == 3) {
            // error
            fprintf(logFile, "ERROR - ERROR_CODE(%d): ", errCode);
        } else {
            fprintf(logFile, "errorLevel : %d, errorCode : %d\n", level, errCode);
        }
        vfprintf(logFile, formatStr, args);
        fprintf(logFile, "\n");
        
        fflush(logFile);
        fsync(fileno(logFile));
        fclose(logFile);
    }
    
    va_end(args);
}
