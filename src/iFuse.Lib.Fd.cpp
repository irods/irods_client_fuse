/*** Copyright (c), The Regents of the University of California            ***
 *** For more information please refer to files in the COPYRIGHT directory ***/
/*** This code is rewritten by Illyoung Choi (iychoi@email.arizona.edu)    ***
 *** funded by iPlantCollaborative (www.iplantcollaborative.org).          ***/
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <time.h>
#include <assert.h>
#include <pthread.h>
#include <list>
#include "iFuse.Lib.hpp"
#include "iFuse.Lib.RodsClientAPI.hpp"
#include "iFuse.Lib.Fd.hpp"
#include "iFuse.Lib.Conn.hpp"
#include "iFuse.Lib.Util.hpp"
#include "sockComm.h"
#include "miscUtil.h"

static pthread_rwlockattr_t g_AssignedFdLockAttr;
static pthread_rwlock_t g_AssignedFdLock;
static std::list<iFuseFd_t*> g_AssignedFd;

static pthread_rwlockattr_t g_AssignedDirLockAttr;
static pthread_rwlock_t g_AssignedDirLock;
static std::list<iFuseDir_t*> g_AssignedDir;

static pthread_rwlockattr_t g_IDGenLockAttr;
static pthread_rwlock_t g_IDGenLock;

static unsigned long g_FdIDGen;
static unsigned long g_DdIDGen;

/*
 * Lock order : 
 * - g_AssignedFdLock or g_AssignedDirLock
 * - iFuseFd_t or iFuseDir_t
 */

static unsigned long _genNextFdID() {
    unsigned long newId;
    
    pthread_rwlock_wrlock(&g_IDGenLock);
    
    newId = g_FdIDGen++;
    
    pthread_rwlock_unlock(&g_IDGenLock);
    
    return newId;
}

static unsigned long _genNextDdID() {
    unsigned long newId;
    
    pthread_rwlock_wrlock(&g_IDGenLock);
    
    newId = g_DdIDGen++;
    
    pthread_rwlock_unlock(&g_IDGenLock);
    
    return newId;
}

static int _closeFd(iFuseFd_t *iFuseFd) {
    int status = 0;
    openedDataObjInp_t dataObjCloseInp;
    iFuseConn_t *iFuseConn = NULL;
    
    assert(iFuseFd != NULL);
    
    pthread_rwlock_wrlock(&iFuseFd->lock);
    
    if(iFuseFd->fd > 0 && iFuseFd->conn != NULL) {
        iFuseConn = iFuseFd->conn;
        iFuseConnLock(iFuseConn);
        
        bzero(&dataObjCloseInp, sizeof (openedDataObjInp_t));
        dataObjCloseInp.l1descInx = iFuseFd->fd;

        status = iFuseRodsClientDataObjClose(iFuseConn->conn, &dataObjCloseInp);
        iFuseConnUpdateLastActTime(iFuseConn, false);
        if (status < 0) {
            if (iFuseRodsClientReadMsgError(status)) {
                // reconnect and retry 
                if(iFuseConnReconnect(iFuseConn) < 0) {
                    iFuseLibLogError(LOG_ERROR, status, "iFuseFdClose: iFuseConnReconnect of %s (%d) error",
                        iFuseFd->iRodsPath, iFuseFd->fd);
                } else {
                    status = iFuseRodsClientDataObjClose(iFuseConn->conn, &dataObjCloseInp);
                    if (status < 0) {
                        iFuseLibLogError(LOG_ERROR, status, "iFuseFdClose: close of %s (%d) error",
                            iFuseFd->iRodsPath, iFuseFd->fd);
                    }
                }
            } else {
                iFuseLibLogError(LOG_ERROR, status, "iFuseFdClose: close of %s (%d) error",
                    iFuseFd->iRodsPath, iFuseFd->fd);
            }
        }

        iFuseConnUnlock(iFuseConn);
    }
    
    iFuseFd->conn = NULL;
    iFuseFd->fd = 0;
    iFuseFd->openFlag = 0;
    iFuseFd->lastFilePointer = -1;
    
    pthread_rwlock_unlock(&iFuseFd->lock);
    return status;
}

static int _closeDir(iFuseDir_t *iFuseDir) {
    int status = 0;
    iFuseConn_t *iFuseConn = NULL;
    
    assert(iFuseDir != NULL);
    
    pthread_rwlock_wrlock(&iFuseDir->lock);

    if(iFuseDir->handle != NULL && iFuseDir->conn != NULL) {
        iFuseConn = iFuseDir->conn;
        iFuseConnLock(iFuseConn);
        status = iFuseRodsClientCloseCollection(iFuseDir->handle);
        if (status < 0) {
            if (iFuseRodsClientReadMsgError(status)) {
                // reconnect and retry 
                if(iFuseConnReconnect(iFuseConn) < 0) {
                    iFuseLibLogError(LOG_ERROR, status, "iFuseDirClose: iFuseConnReconnect of %s error",
                        iFuseDir->iRodsPath);
                } else {
                    status = iFuseRodsClientCloseCollection(iFuseDir->handle);
                    if (status < 0) {
                        iFuseLibLogError(LOG_ERROR, status, "iFuseDirClose: iFuseRodsClientCloseCollection of %s error",
                            iFuseDir->iRodsPath);
                    }
                }
            } else {
                iFuseLibLogError(LOG_ERROR, status, "iFuseDirClose: iFuseRodsClientCloseCollection of %s error",
                    iFuseDir->iRodsPath);
            }
        }

        iFuseConnUnlock(iFuseConn);
    }
    
    iFuseDir->conn = NULL;
    
    if(iFuseDir->handle != NULL) {
        free(iFuseDir->handle);
        iFuseDir->handle = NULL;
    }
    
    pthread_rwlock_unlock(&iFuseDir->lock);
    return status;
}

static int _freeFd(iFuseFd_t *iFuseFd) {
    assert(iFuseFd != NULL);
    
    _closeFd(iFuseFd);
    
    pthread_rwlock_destroy(&iFuseFd->lock);
    pthread_rwlockattr_destroy(&iFuseFd->lockAttr);
    
    if(iFuseFd->iRodsPath != NULL) {
        free(iFuseFd->iRodsPath);
        iFuseFd->iRodsPath = NULL;
    }

    free(iFuseFd);
    return 0;
}

static int _freeDir(iFuseDir_t *iFuseDir) {
    assert(iFuseDir != NULL);
    
    _closeDir(iFuseDir);
    
    pthread_rwlock_destroy(&iFuseDir->lock);
    pthread_rwlockattr_destroy(&iFuseDir->lockAttr);
    
    if(iFuseDir->iRodsPath != NULL) {
        free(iFuseDir->iRodsPath);
        iFuseDir->iRodsPath = NULL;
    }
    
    if(iFuseDir->handle != NULL) {
        free(iFuseDir->handle);
        iFuseDir->handle = NULL;
    }
    
    if(iFuseDir->cachedEntries != NULL) {
        free(iFuseDir->cachedEntries);
        iFuseDir->cachedEntries = NULL;
    }
    
    iFuseDir->cachedEntryBufferLen = 0;

    free(iFuseDir);
    return 0;
}

static int _closeAllFd() {
    iFuseFd_t *iFuseFd;
    
    pthread_rwlock_wrlock(&g_AssignedFdLock);

    // close all opened file descriptors
    while(!g_AssignedFd.empty()) {
        iFuseFd = g_AssignedFd.front();
        g_AssignedFd.pop_front();
        
        _freeFd(iFuseFd);
    }
    
    pthread_rwlock_unlock(&g_AssignedFdLock);
    return 0;
}

static int _closeAllDir() {
    iFuseDir_t *iFuseDir;

    pthread_rwlock_wrlock(&g_AssignedDirLock);
    
    // close all opened dir descriptors
    while(!g_AssignedDir.empty()) {
        iFuseDir = g_AssignedDir.front();
        g_AssignedDir.pop_front();
        
        _freeDir(iFuseDir);
    }
    
    pthread_rwlock_unlock(&g_AssignedDirLock);
    return 0;
}

/*
 * Initialize file descriptor manager
 */
void iFuseFdInit() {
    pthread_rwlockattr_init(&g_AssignedFdLockAttr);
    pthread_rwlock_init(&g_AssignedFdLock, &g_AssignedFdLockAttr);
    
    g_FdIDGen = 0;
    
    pthread_rwlockattr_init(&g_AssignedDirLockAttr);
    pthread_rwlock_init(&g_AssignedDirLock, &g_AssignedDirLockAttr);
    
    g_DdIDGen = 0;
    
    pthread_rwlockattr_init(&g_IDGenLockAttr);
    pthread_rwlock_init(&g_IDGenLock, &g_IDGenLockAttr);
}

/*
 * Destroy file descriptor manager
 */
void iFuseFdDestroy() {
    
    // file
    g_FdIDGen = 0;
    
    _closeAllFd();
    
    pthread_rwlock_destroy(&g_AssignedFdLock);
    pthread_rwlockattr_destroy(&g_AssignedFdLockAttr);
    
    // dir
    g_DdIDGen = 0;
    
    _closeAllDir();
    
    pthread_rwlock_destroy(&g_AssignedDirLock);
    pthread_rwlockattr_destroy(&g_AssignedDirLockAttr);
    
    pthread_rwlock_destroy(&g_IDGenLock);
    pthread_rwlockattr_destroy(&g_IDGenLockAttr);
}

/*
 * Open a new file descriptor
 */
int iFuseFdOpen(iFuseFd_t **iFuseFd, iFuseConn_t *iFuseConn, const char* iRodsPath, int openFlag) {
    int status = 0;
    dataObjInp_t dataObjOpenInp;
    int fd;
    iFuseFd_t *tmpIFuseDesc;

    assert(iFuseFd != NULL);
    assert(iFuseConn != NULL);
    assert(iRodsPath != NULL);
    
    *iFuseFd = NULL;

    iFuseConnLock(iFuseConn);
    
    bzero(&dataObjOpenInp, sizeof ( dataObjInp_t));
    dataObjOpenInp.openFlags = openFlag;
    rstrcpy(dataObjOpenInp.objPath, iRodsPath, MAX_NAME_LEN);

    assert(iFuseConn->conn != NULL);
    
    fd = iFuseRodsClientDataObjOpen(iFuseConn->conn, &dataObjOpenInp);
    iFuseConnUpdateLastActTime(iFuseConn, false);
    if (fd <= 0) {
        if (iFuseRodsClientReadMsgError(fd)) {
            // reconnect and retry 
            if(iFuseConnReconnect(iFuseConn) < 0) {
                iFuseLibLogError(LOG_ERROR, fd, "iFuseFdOpen: iFuseConnReconnect of %s error, status = %d",
                    iRodsPath, fd);
                iFuseConnUnlock(iFuseConn);
                return -ENOENT;
            } else {
                fd = iFuseRodsClientDataObjOpen(iFuseConn->conn, &dataObjOpenInp);
                if (fd <= 0) {
                    iFuseLibLogError(LOG_ERROR, fd, "iFuseFdOpen: iFuseRodsClientDataObjOpen of %s error, status = %d",
                        iRodsPath, fd);
                    iFuseConnUnlock(iFuseConn);
                    return -ENOENT;
                }
            }
        } else {
            iFuseLibLogError(LOG_ERROR, fd, "iFuseFdOpen: iFuseRodsClientDataObjOpen of %s error, status = %d",
                iRodsPath, fd);
            iFuseConnUnlock(iFuseConn);
            return -ENOENT;
        }
    }
    
    iFuseConnUnlock(iFuseConn);
    
    tmpIFuseDesc = (iFuseFd_t *) calloc(1, sizeof ( iFuseFd_t));
    if (tmpIFuseDesc == NULL) {
        *iFuseFd = NULL;
        return SYS_MALLOC_ERR;
    }
    
    tmpIFuseDesc->fdId = _genNextFdID();
    tmpIFuseDesc->conn = iFuseConn;
    tmpIFuseDesc->fd = fd;
    tmpIFuseDesc->iRodsPath = strdup(iRodsPath);
    tmpIFuseDesc->openFlag = openFlag;
    tmpIFuseDesc->lastFilePointer = -1;
    
    pthread_rwlockattr_init(&tmpIFuseDesc->lockAttr);
    pthread_rwlock_init(&tmpIFuseDesc->lock, &tmpIFuseDesc->lockAttr);
    
    *iFuseFd = tmpIFuseDesc;
    
    pthread_rwlock_wrlock(&g_AssignedFdLock);
    
    g_AssignedFd.push_back(tmpIFuseDesc);
    
    pthread_rwlock_unlock(&g_AssignedFdLock);
    return status;
}

/*
 * Close and Reopen a file descriptor
 */
int iFuseFdReopen(iFuseFd_t *iFuseFd) {
    int status = 0;
    openedDataObjInp_t dataObjCloseInp;
    dataObjInp_t dataObjOpenInp;
    iFuseConn_t *iFuseConn = NULL;
    int fd;
    
    assert(iFuseFd != NULL);
    assert(iFuseFd->conn != NULL);
    assert(iFuseFd->fd > 0);

    pthread_rwlock_wrlock(&iFuseFd->lock);

    iFuseConn = iFuseFd->conn;
    iFuseConnLock(iFuseConn);

    bzero(&dataObjCloseInp, sizeof (openedDataObjInp_t));
    dataObjCloseInp.l1descInx = iFuseFd->fd;

    status = iFuseRodsClientDataObjClose(iFuseConn->conn, &dataObjCloseInp);
    iFuseConnUpdateLastActTime(iFuseConn, false);
    if (status < 0) {
        if (iFuseRodsClientReadMsgError(status)) {
            // reconnect and retry 
            if(iFuseConnReconnect(iFuseConn) < 0) {
                iFuseLibLogError(LOG_ERROR, status, "iFuseFdReopen: iFuseConnReconnect of %s (%d) error",
                    iFuseFd->iRodsPath, iFuseFd->fd);
            } else {
                status = iFuseRodsClientDataObjClose(iFuseConn->conn, &dataObjCloseInp);
                if (status < 0) {
                    iFuseLibLogError(LOG_ERROR, status, "iFuseFdReopen: close of %s (%d) error",
                        iFuseFd->iRodsPath, iFuseFd->fd);
                }
            }
        } else {
            iFuseLibLogError(LOG_ERROR, status, "iFuseFdReopen: close of %s (%d) error",
                iFuseFd->iRodsPath, iFuseFd->fd);
        }
    }
    
    if(status < 0) {
        iFuseConnUnlock(iFuseConn);
        pthread_rwlock_unlock(&iFuseFd->lock);
        return status;
    }
    
    iFuseFd->lastFilePointer = -1;

    bzero(&dataObjOpenInp, sizeof ( dataObjInp_t));
    dataObjOpenInp.openFlags = iFuseFd->openFlag;
    rstrcpy(dataObjOpenInp.objPath, iFuseFd->iRodsPath, MAX_NAME_LEN);

    assert(iFuseConn->conn != NULL);
    
    fd = iFuseRodsClientDataObjOpen(iFuseConn->conn, &dataObjOpenInp);
    iFuseConnUpdateLastActTime(iFuseConn, false);
    if (fd <= 0) {
        if (iFuseRodsClientReadMsgError(fd)) {
            // reconnect and retry 
            if(iFuseConnReconnect(iFuseConn) < 0) {
                iFuseLibLogError(LOG_ERROR, fd, "iFuseFdReopen: iFuseConnReconnect of %s error, status = %d",
                    iFuseFd->iRodsPath, fd);
                iFuseConnUnlock(iFuseConn);
                pthread_rwlock_unlock(&iFuseFd->lock);
                return -ENOENT;
            } else {
                fd = iFuseRodsClientDataObjOpen(iFuseConn->conn, &dataObjOpenInp);
                if (fd <= 0) {
                    iFuseLibLogError(LOG_ERROR, fd, "iFuseFdReopen: iFuseRodsClientDataObjOpen of %s error, status = %d",
                        iFuseFd->iRodsPath, fd);
                    iFuseConnUnlock(iFuseConn);
                    pthread_rwlock_unlock(&iFuseFd->lock);
                    return -ENOENT;
                }
            }
        } else {
            iFuseLibLogError(LOG_ERROR, fd, "iFuseFdReopen: iFuseRodsClientDataObjOpen of %s error, status = %d",
                iFuseFd->iRodsPath, fd);
            iFuseConnUnlock(iFuseConn);
            pthread_rwlock_unlock(&iFuseFd->lock);
            return -ENOENT;
        }
    }
    
    iFuseFd->fd = fd;
    
    iFuseConnUnlock(iFuseConn);
    
    pthread_rwlock_unlock(&iFuseFd->lock);
    return status;
}

/*
 * Open a new directory descriptor
 */
int iFuseDirOpen(iFuseDir_t **iFuseDir, iFuseConn_t *iFuseConn, const char* iRodsPath) {
    int status = 0;
    collHandle_t collHandle;
    iFuseDir_t *tmpIFuseDesc;

    assert(iFuseDir != NULL);
    assert(iFuseConn != NULL);
    assert(iRodsPath != NULL);
    
    *iFuseDir = NULL;

    iFuseConnLock(iFuseConn);
    
    bzero(&collHandle, sizeof ( collHandle_t));
    
    assert(iFuseConn->conn != NULL);
    
    status = iFuseRodsClientOpenCollection(iFuseConn->conn, (char*) iRodsPath, 0, &collHandle);
    if (status < 0) {
        if (iFuseRodsClientReadMsgError(status)) {
            // reconnect and retry 
            if(iFuseConnReconnect(iFuseConn) < 0) {
                iFuseLibLogError(LOG_ERROR, status, "iFuseDirOpen: iFuseConnReconnect of %s error, status = %d",
                    iRodsPath, status);
                iFuseConnUnlock(iFuseConn);
                return -ENOENT;
            } else {
                status = iFuseRodsClientOpenCollection(iFuseConn->conn, (char*) iRodsPath, 0, &collHandle);
                if (status < 0) {
                    iFuseLibLogError(LOG_ERROR, status, "iFuseDirOpen: iFuseRodsClientOpenCollection of %s error, status = %d",
                        iRodsPath, status);
                    iFuseConnUnlock(iFuseConn);
                    return -ENOENT;
                }
            }
        }
        if (status < 0) {
            iFuseLibLogError(LOG_ERROR, status, "iFuseFdOpen: iFuseRodsClientOpenCollection of %s error, status = %d",
                iRodsPath, status);
            iFuseConnUnlock(iFuseConn);
            return -ENOENT;
        }
    }
    
    iFuseConnUnlock(iFuseConn);
    
    tmpIFuseDesc = (iFuseDir_t *) calloc(1, sizeof ( iFuseDir_t));
    if (tmpIFuseDesc == NULL) {
        *iFuseDir = NULL;
        return SYS_MALLOC_ERR;
    }
    
    tmpIFuseDesc->ddId = _genNextDdID();
    tmpIFuseDesc->conn = iFuseConn;
    tmpIFuseDesc->iRodsPath = strdup(iRodsPath);
    tmpIFuseDesc->handle = (collHandle_t*)calloc(1, sizeof(collHandle_t));
    if (tmpIFuseDesc->handle == NULL) {
        _freeDir(tmpIFuseDesc);
        return SYS_MALLOC_ERR;
    }
    
    memcpy(tmpIFuseDesc->handle, &collHandle, sizeof(collHandle_t));
    
    pthread_rwlockattr_init(&tmpIFuseDesc->lockAttr);
    pthread_rwlock_init(&tmpIFuseDesc->lock, &tmpIFuseDesc->lockAttr);
    
    *iFuseDir = tmpIFuseDesc;
    
    pthread_rwlock_wrlock(&g_AssignedDirLock);
    
    g_AssignedDir.push_back(tmpIFuseDesc);
    
    pthread_rwlock_unlock(&g_AssignedDirLock);
    return status;
}

/*
 * Open a new directory descriptor
 */
int iFuseDirOpenWithCache(iFuseDir_t **iFuseDir, const char* iRodsPath, const char* cachedEntries, unsigned int entryBufferLen) {
    int status = 0;
    iFuseDir_t *tmpIFuseDesc;

    assert(iFuseDir != NULL);
    assert(iRodsPath != NULL);
    
    *iFuseDir = NULL;

    tmpIFuseDesc = (iFuseDir_t *) calloc(1, sizeof ( iFuseDir_t));
    if (tmpIFuseDesc == NULL) {
        *iFuseDir = NULL;
        pthread_rwlock_unlock(&g_AssignedDirLock);
        return SYS_MALLOC_ERR;
    }
    
    tmpIFuseDesc->ddId = _genNextDdID();
    tmpIFuseDesc->conn = NULL;
    tmpIFuseDesc->iRodsPath = strdup(iRodsPath);
    tmpIFuseDesc->handle = NULL;
    tmpIFuseDesc->cachedEntries = (char *) calloc(1, entryBufferLen);
    if (tmpIFuseDesc->cachedEntries == NULL) {
        *iFuseDir = NULL;
        free(tmpIFuseDesc->iRodsPath);
        free(tmpIFuseDesc);
        pthread_rwlock_unlock(&g_AssignedDirLock);
        return SYS_MALLOC_ERR;
    }
    memcpy(tmpIFuseDesc->cachedEntries, cachedEntries, entryBufferLen);
    tmpIFuseDesc->cachedEntryBufferLen = entryBufferLen;
    
    pthread_rwlockattr_init(&tmpIFuseDesc->lockAttr);
    pthread_rwlock_init(&tmpIFuseDesc->lock, &tmpIFuseDesc->lockAttr);
    
    *iFuseDir = tmpIFuseDesc;
    
    pthread_rwlock_wrlock(&g_AssignedDirLock);
    
    g_AssignedDir.push_back(tmpIFuseDesc);
    
    pthread_rwlock_unlock(&g_AssignedDirLock);
    return status;
}

/*
 * Close file descriptor
 */
int iFuseFdClose(iFuseFd_t *iFuseFd) {
    int status = 0;
    
    assert(iFuseFd != NULL);
    assert(iFuseFd->conn != NULL);
    assert(iFuseFd->fd > 0);
    
    pthread_rwlock_wrlock(&g_AssignedFdLock);
    
    g_AssignedFd.remove(iFuseFd);
    status = _freeFd(iFuseFd);
    
    pthread_rwlock_unlock(&g_AssignedFdLock);
    return status;
}

/*
 * Close directory descriptor
 */
int iFuseDirClose(iFuseDir_t *iFuseDir) {
    int status = 0;
    
    assert(iFuseDir != NULL);
    
    pthread_rwlock_wrlock(&g_AssignedDirLock);
    
    g_AssignedDir.remove(iFuseDir);
    status = _freeDir(iFuseDir);
    
    pthread_rwlock_unlock(&g_AssignedDirLock);
    return status;
}

/*
 * Lock file descriptor
 */
void iFuseFdLock(iFuseFd_t *iFuseFd) {
    assert(iFuseFd != NULL);
    
    pthread_rwlock_wrlock(&iFuseFd->lock);
}

/*
 * Lock directory descriptor
 */
void iFuseDirLock(iFuseDir_t *iFuseDir) {
    assert(iFuseDir != NULL);
    
    pthread_rwlock_wrlock(&iFuseDir->lock);
}

/*
 * Unlock file descriptor
 */
void iFuseFdUnlock(iFuseFd_t *iFuseFd) {
    assert(iFuseFd != NULL);
    
    pthread_rwlock_unlock(&iFuseFd->lock);
}

/*
 * Unlock directory descriptor
 */
void iFuseDirUnlock(iFuseDir_t *iFuseDir) {
    assert(iFuseDir != NULL);
    
    pthread_rwlock_unlock(&iFuseDir->lock);
}
