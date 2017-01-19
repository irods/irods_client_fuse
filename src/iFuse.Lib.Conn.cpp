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
#include <map>
#include "iFuse.Lib.hpp"
#include "iFuse.Lib.RodsClientAPI.hpp"
#include "iFuse.Lib.Conn.hpp"
#include "iFuse.Lib.Util.hpp"

static pthread_rwlock_t g_ConnectedConnLock;
static pthread_rwlockattr_t g_ConnectedConnLockAttr;

static iFuseConn_t* g_InUseShortopConn;
static iFuseConn_t** g_InUseConn;
static std::map<unsigned long, iFuseConn_t*> g_InUseOnetimeuseConn;
static iFuseConn_t* g_FreeShortopConn;
static std::list<iFuseConn_t*> g_FreeConn;

static pthread_rwlockattr_t g_IDGenLockAttr;
static pthread_rwlock_t g_IDGenLock;

static unsigned long g_ConnIDGen;

static int g_MaxConnNum = IFUSE_MAX_NUM_CONN;
static int g_ConnTimeoutSec = IFUSE_FREE_CONN_TIMEOUT_SEC;
static int g_ConnKeepAliveSec = IFUSE_FREE_CONN_KEEPALIVE_SEC;
static int g_ConnCheckIntervalSec = IFUSE_FREE_CONN_CHECK_INTERVAL_SEC;

static time_t g_LastConnCheck = 0;

/*
 * Lock order :
 * - g_ConnectedConnLock
 * - iFuseConn_t
 */

static unsigned long _genNextConnID() {
    unsigned long newId;
    
    pthread_rwlock_wrlock(&g_IDGenLock);
    
    newId = g_ConnIDGen++;
    
    pthread_rwlock_unlock(&g_IDGenLock);
    
    return newId;
}

static int _connect(iFuseConn_t *iFuseConn) {
    int status = 0;
    rodsEnv *myRodsEnv = iFuseLibGetRodsEnv();
    
    int reconnFlag = NO_RECONN;

    assert(myRodsEnv != NULL);
    assert(iFuseConn != NULL);

    if (iFuseConn->conn == NULL) {
        rErrMsg_t errMsg;
        iFuseConn->conn = iFuseRodsClientConnect(myRodsEnv->rodsHost, myRodsEnv->rodsPort,
                myRodsEnv->rodsUserName, myRodsEnv->rodsZone, reconnFlag, &errMsg);
        if (iFuseConn->conn == NULL) {
            // try one more
            iFuseConn->conn = iFuseRodsClientConnect(myRodsEnv->rodsHost, myRodsEnv->rodsPort,
                    myRodsEnv->rodsUserName, myRodsEnv->rodsZone, reconnFlag, &errMsg);
            if (iFuseConn->conn == NULL) {
                // failed
                iFuseLibLogError(LOG_ERROR, errMsg.status,
                        "_connect: iFuseRodsClientConnect failure %s", errMsg.msg);
                iFuseLibLog(LOG_ERROR, "Cannot connect to iRODS Host - %s:%d error - %s", myRodsEnv->rodsHost, myRodsEnv->rodsPort, errMsg.msg);
                if (errMsg.status < 0) {
                    return errMsg.status;
                } else {
                    return -1;
                }
            }
        }

        assert(iFuseConn->conn != NULL);

        status = iFuseRodsClientLogin(iFuseConn->conn);
        if (status != 0) {
            iFuseRodsClientDisconnect(iFuseConn->conn);
            iFuseConn->conn = NULL;

            // failed
            iFuseLibLog(LOG_ERROR, "iFuseRodsClientLogin failure, status = %d", status);
            iFuseLibLog(LOG_ERROR, "Cannot log in to iRODS - account %s", myRodsEnv->rodsUserName);
            return status;
        }
        
        if(iFuseLibGetOption()->ticket != NULL) {
            char* ticket = iFuseLibGetOption()->ticket;
            ticketAdminInp_t ticketAdminInp;
            
            bzero(&ticketAdminInp, sizeof ( ticketAdminInp_t));
            ticketAdminInp.arg1 = "session";
            ticketAdminInp.arg2 = ticket;
            ticketAdminInp.arg3 = "";
            ticketAdminInp.arg4 = "";
            ticketAdminInp.arg5 = "";
            ticketAdminInp.arg6 = "";
            
            status = iFuseRodsClientSetSessionTicket(iFuseConn->conn, &ticketAdminInp);
            if (status != 0) {
                iFuseRodsClientDisconnect(iFuseConn->conn);
                iFuseConn->conn = NULL;

                // failed
                iFuseLibLog(LOG_ERROR, "iFuseRodsClientSetSessionTicket failure, status = %d", status);
                iFuseLibLog(LOG_ERROR, "Cannot set session-ticket - ticket %s", ticket);
                return status;
            }
        }
    }

    return status;
}

static void _disconnect(iFuseConn_t *iFuseConn) {
    assert(iFuseConn != NULL);

    if (iFuseConn->conn != NULL) {
        iFuseRodsClientDisconnect(iFuseConn->conn);
        iFuseConn->conn = NULL;
    }
}

static int _newConn(iFuseConn_t **iFuseConn) {
    int status = 0;
    iFuseConn_t *tmpIFuseConn = NULL;

    assert(iFuseConn != NULL);

    tmpIFuseConn = (iFuseConn_t *) calloc(1, sizeof ( iFuseConn_t));
    if (tmpIFuseConn == NULL) {
        *iFuseConn = NULL;
        return SYS_MALLOC_ERR;
    }

    tmpIFuseConn->connId = _genNextConnID();

    iFuseLibLog(LOG_DEBUG, "_newConn: creating a new connection - %lu", tmpIFuseConn->connId);

    pthread_rwlockattr_init(&tmpIFuseConn->lockAttr);
    pthread_rwlock_init(&tmpIFuseConn->lock, &tmpIFuseConn->lockAttr);
    
    // connect
    status = _connect(tmpIFuseConn);
    tmpIFuseConn->lastActTime = iFuseLibGetCurrentTime();
    *iFuseConn = tmpIFuseConn;
    return status;
}

static int _freeConn(iFuseConn_t *iFuseConn) {
    assert(iFuseConn != NULL);

    iFuseLibLog(LOG_DEBUG, "_freeConn: disconnecting - %lu", iFuseConn->connId);

    // disconnect first
    _disconnect(iFuseConn);

    pthread_rwlock_destroy(&iFuseConn->lock);
    pthread_rwlockattr_destroy(&iFuseConn->lockAttr);

    free(iFuseConn);
    return 0;
}

static int _freeAllConn() {
    iFuseConn_t *tmpIFuseConn;
    std::map<unsigned long, iFuseConn_t*>::iterator it_connmap;
    int i;

    pthread_rwlock_wrlock(&g_ConnectedConnLock);

    // disconnect all free connections
    while(!g_FreeConn.empty()) {
        tmpIFuseConn = g_FreeConn.front();
        g_FreeConn.pop_front();

        _freeConn(tmpIFuseConn);
    }

    if(g_FreeShortopConn != NULL) {
        _freeConn(g_FreeShortopConn);
        g_FreeShortopConn = NULL;
    }

    // disconnect all inuse connections
    for(i=0;i<g_MaxConnNum;i++) {
        if(g_InUseConn[i] != NULL) {
            tmpIFuseConn = g_InUseConn[i];
            g_InUseConn[i] = NULL;
            _freeConn(tmpIFuseConn);
        }
    }

    if(g_InUseShortopConn != NULL) {
        _freeConn(g_InUseShortopConn);
        g_InUseShortopConn = NULL;
    }

    while(!g_InUseOnetimeuseConn.empty()) {
        it_connmap = g_InUseOnetimeuseConn.begin();
        if(it_connmap != g_InUseOnetimeuseConn.end()) {
            tmpIFuseConn = it_connmap->second;
            g_InUseOnetimeuseConn.erase(it_connmap);

            _freeConn(tmpIFuseConn);
        }
    }
    
    pthread_rwlock_unlock(&g_ConnectedConnLock);

    return 0;
}

static void _keepAlive(iFuseConn_t *iFuseConn) {
    int status = 0;
    char iRodsPath[MAX_NAME_LEN];
    dataObjInp_t dataObjInp;
    rodsObjStat_t *rodsObjStatOut = NULL;

    bzero(iRodsPath, MAX_NAME_LEN);
    status = iFuseRodsClientMakeRodsPath("/", iRodsPath);
    if (status < 0) {
        iFuseLibLogError(LOG_ERROR, status,
                "_keepAlive: iFuseRodsClientMakeRodsPath of %s error", iRodsPath);
        return;
    }

    iFuseLibLog(LOG_DEBUG, "_keepAlive: connection %lu", iFuseConn->connId);

    bzero(&dataObjInp, sizeof ( dataObjInp_t));
    rstrcpy(dataObjInp.objPath, iRodsPath, MAX_NAME_LEN);

    pthread_rwlock_wrlock(&iFuseConn->lock);

    status = iFuseRodsClientObjStat(iFuseConn->conn, &dataObjInp, &rodsObjStatOut);
    if (status < 0) {
        iFuseLibLogError(LOG_ERROR, status, "_keepAlive: iFuseRodsClientObjStat of %s error, status = %d",
            iRodsPath, status);
        pthread_rwlock_unlock(&iFuseConn->lock);
        return;
    }

    if(rodsObjStatOut != NULL) {
        freeRodsObjStat(rodsObjStatOut);
    }

    // update last act time
    iFuseConn->lastActTime = iFuseLibGetCurrentTime();

    pthread_rwlock_unlock(&iFuseConn->lock);
}

static void _connChecker() {
    std::list<iFuseConn_t*> removeList;
    std::list<iFuseConn_t*>::iterator it_conn;
    std::map<unsigned long, iFuseConn_t*>::iterator it_connmap;
    iFuseConn_t *iFuseConn;
    time_t current;
    int i;
    
    //iFuseLibLog(LOG_DEBUG, "_connChecker is called");
    
    current = iFuseLibGetCurrentTime();

    if(iFuseLibDiffTimeSec(current, g_LastConnCheck) > g_ConnCheckIntervalSec) {
        //iFuseLibLog(LOG_DEBUG, "_connChecker: sending keep-alive requests");
        pthread_rwlock_rdlock(&g_ConnectedConnLock);

        for(i=0;i<g_MaxConnNum;i++) {
            if(g_InUseConn[i] != NULL) {
                if(iFuseLibDiffTimeSec(current, g_InUseConn[i]->lastActTime) >= g_ConnKeepAliveSec) {
                    _keepAlive(g_InUseConn[i]);
                }
            }
        }

        if(g_InUseShortopConn != NULL) {
            if(iFuseLibDiffTimeSec(current, g_InUseShortopConn->lastActTime) >= g_ConnKeepAliveSec) {
                _keepAlive(g_InUseShortopConn);
            }
        }

        for(it_connmap=g_InUseOnetimeuseConn.begin();it_connmap!=g_InUseOnetimeuseConn.end();it_connmap++) {
            iFuseConn = it_connmap->second;

            if(iFuseLibDiffTimeSec(current, iFuseConn->lastActTime) >= g_ConnKeepAliveSec) {
                _keepAlive(iFuseConn);
            }
        }
        
        for(it_conn=g_FreeConn.begin();it_conn!=g_FreeConn.end();it_conn++) {
            iFuseConn = *it_conn;

            if(iFuseLibDiffTimeSec(current, iFuseConn->lastActTime) >= g_ConnKeepAliveSec) {
                _keepAlive(iFuseConn);
            }
        }
        
        if(g_FreeShortopConn != NULL) {
            if(iFuseLibDiffTimeSec(current, g_FreeShortopConn->lastActTime) >= g_ConnKeepAliveSec) {
                _keepAlive(g_FreeShortopConn);
            }
        }
        
        pthread_rwlock_unlock(&g_ConnectedConnLock);

        pthread_rwlock_wrlock(&g_ConnectedConnLock);
        // iterate free conn list to check timedout connections
        for(it_conn=g_FreeConn.begin();it_conn!=g_FreeConn.end();it_conn++) {
            iFuseConn = *it_conn;

            if(iFuseLibDiffTimeSec(current, iFuseConn->lastUseTime) >= g_ConnTimeoutSec) {
                iFuseLibLog(LOG_DEBUG, "_connChecker: release idle connection %lu", iFuseConn->connId);
                removeList.push_back(iFuseConn);
            }
        }

        // iterate remove list
        while(!removeList.empty()) {
            iFuseConn = removeList.front();
            removeList.pop_front();
            g_FreeConn.remove(iFuseConn);
            _freeConn(iFuseConn);
        }

        if(g_FreeShortopConn != NULL) {
            if(iFuseLibDiffTimeSec(current, g_FreeShortopConn->lastUseTime) >= g_ConnTimeoutSec) {
                _freeConn(g_FreeShortopConn);
                g_FreeShortopConn = NULL;
            }
        }

        pthread_rwlock_unlock(&g_ConnectedConnLock);
        
        g_LastConnCheck = iFuseLibGetCurrentTime();
    }
}

int iFuseConnTest() {
    int status;
    rodsEnv *myRodsEnv = iFuseLibGetRodsEnv();
    iFuseConn_t *iFuseConn = NULL;
    
    //check host
    if(myRodsEnv == NULL) {
        iFuseLibLog(LOG_ERROR, "Cannot read rods environment");
        fprintf(stderr, "Cannot read rods environment\n");
        return -1;
    }
    
    if(strlen(myRodsEnv->rodsHost) == 0) {
        iFuseLibLog(LOG_ERROR, "iRODS Host is not configured in rods environment");
        fprintf(stderr, "iRODS Host is not configured in rods environment\n");
        return -1;
    }
    
    if(myRodsEnv->rodsPort <= 0) {
        iFuseLibLog(LOG_ERROR, "iRODS Port is not configured in rods environment");
        fprintf(stderr, "iRODS Port is not configured in rods environment\n");
        return -1;
    }
    
    if(strlen(myRodsEnv->rodsUserName) == 0) {
        iFuseLibLog(LOG_ERROR, "iRODS User Account is not configured in rods environment");
        fprintf(stderr, "iRODS User Account is not configured in rods environment\n");
        return -1;
    }
    
    if(strlen(myRodsEnv->rodsZone) == 0) {
        iFuseLibLog(LOG_ERROR, "iRODS Zone is not configured in rods environment");
        fprintf(stderr, "iRODS Zone is not configured in rods environment\n");
        return -1;
    }
    
    iFuseLibLog(LOG_DEBUG, "iFuseConnTest: make a test connection to iRODS host - %s:%d", myRodsEnv->rodsHost, myRodsEnv->rodsPort);
    
    status = iFuseConnGetAndUse(&iFuseConn, IFUSE_CONN_TYPE_FOR_SHORTOP);
    if (status < 0) {
        iFuseLibLogError(LOG_ERROR, status, "iFuseConnTest: iFuseConnGetAndUse error");
        fprintf(stderr, "Cannot establish a connection");
        return status;
    }
    
    iFuseConnUnuse(iFuseConn);
    return 0;
}

/*
 * Initialize Conn Manager
 */
void iFuseConnInit() {
    int i;

    if(iFuseLibGetOption()->maxConn > 0) {
        g_MaxConnNum = iFuseLibGetOption()->maxConn;
    }

    if(iFuseLibGetOption()->connTimeoutSec > 0) {
        g_ConnTimeoutSec = iFuseLibGetOption()->connTimeoutSec;
    }

    if(iFuseLibGetOption()->connKeepAliveSec > 0) {
        g_ConnKeepAliveSec = iFuseLibGetOption()->connKeepAliveSec;
    }
    
    if(iFuseLibGetOption()->connCheckIntervalSec > 0) {
        g_ConnCheckIntervalSec = iFuseLibGetOption()->connCheckIntervalSec;
   }

    pthread_rwlockattr_init(&g_ConnectedConnLockAttr);
    pthread_rwlock_init(&g_ConnectedConnLock, &g_ConnectedConnLockAttr);

    g_InUseConn = (iFuseConn_t**)calloc(g_MaxConnNum, sizeof(iFuseConn_t*));

    for(i=0;i<g_MaxConnNum;i++) {
        g_InUseConn[i] = NULL;
    }

    g_ConnIDGen = 0;
    
    pthread_rwlockattr_init(&g_IDGenLockAttr);
    pthread_rwlock_init(&g_IDGenLock, &g_IDGenLockAttr);

    iFuseLibSetTimerTickHandler(_connChecker);
}

/*
 * Destroy Conn Manager
 */
void iFuseConnDestroy() {
    iFuseLibUnsetTimerTickHandler(_connChecker);
    
    g_ConnIDGen = 0;

    _freeAllConn();

    pthread_rwlock_destroy(&g_ConnectedConnLock);
    pthread_rwlockattr_destroy(&g_ConnectedConnLockAttr);

    free(g_InUseConn);
    
    pthread_rwlock_destroy(&g_IDGenLock);
    pthread_rwlockattr_destroy(&g_IDGenLockAttr);
}

/*
 * Report status of connections
 */
void iFuseConnReport(iFuseFsConnReport_t *report) {
    std::list<iFuseConn_t*>::iterator it_conn;
    std::map<unsigned long, iFuseConn_t*>::iterator it_connmap;
    iFuseConn_t *iFuseConn;
    int i;
    time_t current;
    
    assert(report != NULL);
    
    bzero(report, sizeof(iFuseFsConnReport_t));

    pthread_rwlock_rdlock(&g_ConnectedConnLock);
    
    current = iFuseLibGetCurrentTime();

    if(g_InUseShortopConn != NULL) {
        iFuseLibLog(LOG_DEBUG, "iFuseConnReport: short-op connection (%lu) is in use, last act = %d sec ago, last use = %d sec ago", g_InUseShortopConn->connId, (int)iFuseLibDiffTimeSec(current, g_InUseShortopConn->lastActTime), (int)iFuseLibDiffTimeSec(current, g_InUseShortopConn->lastUseTime));
        report->inuseShortOpConn++;
    }
    
    for(i=0;i<g_MaxConnNum;i++) {
        if(g_InUseConn[i] != NULL) {
            iFuseLibLog(LOG_DEBUG, "iFuseConnReport: general connection (%lu) is in use, last act = %d sec ago, last use = %d sec ago", g_InUseConn[i]->connId, (int)iFuseLibDiffTimeSec(current, g_InUseConn[i]->lastActTime), (int)iFuseLibDiffTimeSec(current, g_InUseConn[i]->lastUseTime));
            report->inuseConn++;
        }
    }

    for(it_connmap=g_InUseOnetimeuseConn.begin();it_connmap!=g_InUseOnetimeuseConn.end();it_connmap++) {
        iFuseConn = it_connmap->second;
        
        iFuseLibLog(LOG_DEBUG, "iFuseConnReport: one-time-use connection (%lu) is in use, last act = %d sec ago, last use = %d sec ago", iFuseConn->connId, (int)iFuseLibDiffTimeSec(current, iFuseConn->lastActTime), (int)iFuseLibDiffTimeSec(current, iFuseConn->lastUseTime));
        report->inuseOnetimeuseConn++;
    }
    
    if(g_FreeShortopConn != NULL) {
        iFuseLibLog(LOG_DEBUG, "iFuseConnReport: short-op connection (%lu) is free, last act = %d sec ago, last use = %d sec ago", g_FreeShortopConn->connId, (int)iFuseLibDiffTimeSec(current, g_FreeShortopConn->lastActTime), (int)iFuseLibDiffTimeSec(current, g_FreeShortopConn->lastUseTime));
        
        report->freeShortopConn++;
    }
    
    for(it_conn=g_FreeConn.begin();it_conn!=g_FreeConn.end();it_conn++) {
        iFuseConn = *it_conn;
        
        iFuseLibLog(LOG_DEBUG, "iFuseConnReport: connection (%lu) is free, last act = %d sec ago, last use = %d sec ago", iFuseConn->connId, (int)iFuseLibDiffTimeSec(current, iFuseConn->lastActTime), (int)iFuseLibDiffTimeSec(current, iFuseConn->lastUseTime));
        report->freeConn++;
    }
    
    pthread_rwlock_unlock(&g_ConnectedConnLock);
}

/*
 * Get connection and increase reference count
 */
int iFuseConnGetAndUse(iFuseConn_t **iFuseConn, int connType) {
    int status;
    iFuseConn_t *tmpIFuseConn;
    int i;
    int targetIndex;
    int inUseCount;

    assert(iFuseConn != NULL);

    *iFuseConn = NULL;

    pthread_rwlock_wrlock(&g_ConnectedConnLock);

    if(connType == IFUSE_CONN_TYPE_FOR_SHORTOP) {
        if(g_InUseShortopConn != NULL) {
            pthread_rwlock_wrlock(&g_InUseShortopConn->lock);
            g_InUseShortopConn->lastUseTime = iFuseLibGetCurrentTime();
            g_InUseShortopConn->inuseCnt++;
            pthread_rwlock_unlock(&g_InUseShortopConn->lock);

            *iFuseConn = g_InUseShortopConn;
            pthread_rwlock_unlock(&g_ConnectedConnLock);
            return 0;
        }

        // not in inuseshortopconn
        // check free conn
        if (g_FreeShortopConn != NULL) {
            // reuse existing connection
            tmpIFuseConn = g_FreeShortopConn;
            g_FreeShortopConn = NULL;

            pthread_rwlock_wrlock(&tmpIFuseConn->lock);
            tmpIFuseConn->lastUseTime = iFuseLibGetCurrentTime();
            tmpIFuseConn->inuseCnt++;
            pthread_rwlock_unlock(&tmpIFuseConn->lock);

            *iFuseConn = tmpIFuseConn;

            g_InUseShortopConn = tmpIFuseConn;

            pthread_rwlock_unlock(&g_ConnectedConnLock);
            return 0;
        }

        // need to create new
        status = _newConn(&tmpIFuseConn);
        if (status < 0) {
            _freeConn(tmpIFuseConn);
            pthread_rwlock_unlock(&g_ConnectedConnLock);
            return status;
        }

        tmpIFuseConn->type = IFUSE_CONN_TYPE_FOR_SHORTOP;
        tmpIFuseConn->lastUseTime = iFuseLibGetCurrentTime();
        tmpIFuseConn->inuseCnt++;

        g_InUseShortopConn = tmpIFuseConn;
        *iFuseConn = tmpIFuseConn;

        pthread_rwlock_unlock(&g_ConnectedConnLock);
        return 0;
    } else if(connType == IFUSE_CONN_TYPE_FOR_FILE_IO) {
        // Decide whether creating a new connection or reuse one of existing connections
        targetIndex = -1;
        for(i=0;i<g_MaxConnNum;i++) {
            if(g_InUseConn[i] == NULL) {
                targetIndex = i;
                break;
            }
        }

        if(targetIndex >= 0) {
            if (!g_FreeConn.empty()) {
                // reuse existing connection
                tmpIFuseConn = g_FreeConn.front();
                pthread_rwlock_wrlock(&tmpIFuseConn->lock);
                tmpIFuseConn->lastUseTime = iFuseLibGetCurrentTime();
                tmpIFuseConn->inuseCnt++;
                pthread_rwlock_unlock(&tmpIFuseConn->lock);

                *iFuseConn = tmpIFuseConn;

                g_InUseConn[targetIndex] = tmpIFuseConn;
                g_FreeConn.remove(tmpIFuseConn);

                pthread_rwlock_unlock(&g_ConnectedConnLock);
                return 0;
            } else {
                // create new
                status = _newConn(&tmpIFuseConn);
                if (status < 0) {
                    _freeConn(tmpIFuseConn);
                    pthread_rwlock_unlock(&g_ConnectedConnLock);
                    return status;
                }

                tmpIFuseConn->type = IFUSE_CONN_TYPE_FOR_FILE_IO;
                tmpIFuseConn->lastUseTime = iFuseLibGetCurrentTime();
                tmpIFuseConn->inuseCnt++;

                g_InUseConn[targetIndex] = tmpIFuseConn;
                *iFuseConn = tmpIFuseConn;

                pthread_rwlock_unlock(&g_ConnectedConnLock);
                return 0;
            }
        } else {
            // reuse existing connection
            inUseCount = -1;
            tmpIFuseConn = NULL;
            for(i=0;i<g_MaxConnNum;i++) {
                if(g_InUseConn[i] != NULL) {
                    if(inUseCount < 0) {
                        inUseCount = g_InUseConn[i]->inuseCnt;
                        tmpIFuseConn = g_InUseConn[i];
                    } else {
                        if(inUseCount > g_InUseConn[i]->inuseCnt) {
                            inUseCount = g_InUseConn[i]->inuseCnt;
                            tmpIFuseConn = g_InUseConn[i];
                        }
                    }
                }
            }

            assert(tmpIFuseConn != NULL);

            pthread_rwlock_wrlock(&tmpIFuseConn->lock);
            tmpIFuseConn->lastUseTime = iFuseLibGetCurrentTime();
            tmpIFuseConn->inuseCnt++;
            pthread_rwlock_unlock(&tmpIFuseConn->lock);

            *iFuseConn = tmpIFuseConn;

            pthread_rwlock_unlock(&g_ConnectedConnLock);
            return 0;
        }
    } else if(connType == IFUSE_CONN_TYPE_FOR_ONETIMEUSE) {
        // create new
        status = _newConn(&tmpIFuseConn);
        if (status < 0) {
            _freeConn(tmpIFuseConn);
            pthread_rwlock_unlock(&g_ConnectedConnLock);
            return status;
        }

        tmpIFuseConn->type = IFUSE_CONN_TYPE_FOR_ONETIMEUSE;
        tmpIFuseConn->lastUseTime = iFuseLibGetCurrentTime();
        tmpIFuseConn->inuseCnt++;

        g_InUseOnetimeuseConn[tmpIFuseConn->connId] = tmpIFuseConn;
        *iFuseConn = tmpIFuseConn;

        pthread_rwlock_unlock(&g_ConnectedConnLock);
        return 0;
    } else {
        assert(0);
    }
    assert(0);
    return 0;
}

/*
 * Decrease reference count
 */
int iFuseConnUnuse(iFuseConn_t *iFuseConn) {
    int i;
    std::map<unsigned long, iFuseConn_t*>::iterator it_connmap;

    assert(iFuseConn != NULL);

    pthread_rwlock_wrlock(&g_ConnectedConnLock);
    pthread_rwlock_wrlock(&iFuseConn->lock);

    iFuseConn->lastUseTime = iFuseLibGetCurrentTime();
    iFuseConn->inuseCnt--;

    assert(iFuseConn->inuseCnt >= 0);

    if(iFuseConn->inuseCnt == 0) {
        // move to free list
        if(iFuseConn->type == IFUSE_CONN_TYPE_FOR_SHORTOP) {
            assert(g_InUseShortopConn == iFuseConn);
            assert(g_FreeShortopConn == NULL);

            g_InUseShortopConn = NULL;
            g_FreeShortopConn = iFuseConn;

            pthread_rwlock_unlock(&iFuseConn->lock);
            pthread_rwlock_unlock(&g_ConnectedConnLock);
            return 0;
        } else if(iFuseConn->type == IFUSE_CONN_TYPE_FOR_FILE_IO) {
            for(i=0;i<g_MaxConnNum;i++) {
                if(g_InUseConn[i] == iFuseConn) {
                    g_InUseConn[i] = NULL;
                    break;
                }
            }

            g_FreeConn.push_front(iFuseConn);

            pthread_rwlock_unlock(&iFuseConn->lock);
            pthread_rwlock_unlock(&g_ConnectedConnLock);
            return 0;
        } else if(iFuseConn->type == IFUSE_CONN_TYPE_FOR_ONETIMEUSE) {
            it_connmap = g_InUseOnetimeuseConn.find(iFuseConn->connId);
            if(it_connmap != g_InUseOnetimeuseConn.end()) {
                // has it - remove
                g_InUseOnetimeuseConn.erase(it_connmap);
            }

            pthread_rwlock_unlock(&iFuseConn->lock);
            pthread_rwlock_unlock(&g_ConnectedConnLock);

            _freeConn(iFuseConn);
            return 0;
        } else {
            assert(0);
        }
    }

    pthread_rwlock_unlock(&iFuseConn->lock);
    pthread_rwlock_unlock(&g_ConnectedConnLock);
    return 0;
}

/*
 * Update last act time
 */
void iFuseConnUpdateLastActTime(iFuseConn_t *iFuseConn, bool lock) {
    assert(iFuseConn != NULL);

    if(lock) {
        pthread_rwlock_wrlock(&iFuseConn->lock);
    }

    iFuseConn->lastActTime = iFuseLibGetCurrentTime();

    if(lock) {
        pthread_rwlock_unlock(&iFuseConn->lock);
    }
}

/*
 * Reconnect
 */
int iFuseConnReconnect(iFuseConn_t *iFuseConn) {
    int status = 0;
    assert(iFuseConn != NULL);

    pthread_rwlock_wrlock(&iFuseConn->lock);

    iFuseLibLog(LOG_DEBUG, "iFuseConnReconnect: disconnecting - %lu", iFuseConn->connId);
    _disconnect(iFuseConn);

    iFuseLibLog(LOG_DEBUG, "iFuseConnReconnect: connecting - %lu", iFuseConn->connId);
    status = _connect(iFuseConn);
    
    iFuseConn->lastActTime = iFuseLibGetCurrentTime();

    pthread_rwlock_unlock(&iFuseConn->lock);

    return status;
}

/*
 * Lock connection
 */
void iFuseConnLock(iFuseConn_t *iFuseConn) {
    assert(iFuseConn != NULL);

    pthread_rwlock_wrlock(&iFuseConn->lock);

    iFuseLibLog(LOG_DEBUG, "iFuseConnLock: connection locked - %lu", iFuseConn->connId);
}

/*
 * Unlock connection
 */
void iFuseConnUnlock(iFuseConn_t *iFuseConn) {
    assert(iFuseConn != NULL);

    pthread_rwlock_unlock(&iFuseConn->lock);

    iFuseLibLog(LOG_DEBUG, "iFuseConnUnlock: connection unlocked - %lu", iFuseConn->connId);
}
