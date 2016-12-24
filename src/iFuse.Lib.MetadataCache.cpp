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
#include <map>
#include <list>
#include <string>
#include <cstring>
#include "iFuse.Lib.hpp"
#include "iFuse.Lib.RodsClientAPI.hpp"
#include "iFuse.Lib.MetadataCache.hpp"
#include "iFuse.Lib.Util.hpp"

static pthread_mutexattr_t g_StatCacheLockAttr;
static pthread_mutex_t g_StatCacheLock;
static pthread_mutexattr_t g_DirCacheLockAttr;
static pthread_mutex_t g_DirCacheLock;

static std::map<std::string, iFuseStatCache_t*> g_StatCacheMap;
static std::map<std::string, iFuseDirCache_t*> g_DirCacheMap;

static int g_metadataCacheTimeoutSec = IFUSE_METADATA_CACHE_TIMEOUT_SEC;
static time_t g_lastStatCacheTimeoutCheck = 0;
static time_t g_lastDirCacheTimeoutCheck = 0;

static int _newStatCache(iFuseStatCache_t **iFuseStatCache) {
    iFuseStatCache_t *tmpIFuseStatCache = NULL;

    assert(iFuseStatCache != NULL);

    tmpIFuseStatCache = (iFuseStatCache_t *) calloc(1, sizeof ( iFuseStatCache_t));
    if(tmpIFuseStatCache == NULL) {
        *iFuseStatCache = NULL;
        return SYS_MALLOC_ERR;
    }
    
    tmpIFuseStatCache->timestamp = iFuseLibGetCurrentTime();

    *iFuseStatCache = tmpIFuseStatCache;
    return 0;
}

static int _newDirCache(iFuseDirCache_t **iFuseDirCache) {
    iFuseDirCache_t *tmpIFuseDirCache = NULL;

    assert(iFuseDirCache != NULL);

    tmpIFuseDirCache = (iFuseDirCache_t *) calloc(1, sizeof ( iFuseDirCache_t));
    if(tmpIFuseDirCache == NULL) {
        *iFuseDirCache = NULL;
        return SYS_MALLOC_ERR;
    }
    
    tmpIFuseDirCache->entries = new std::list<char*>();
    if(tmpIFuseDirCache->entries == NULL) {
        *iFuseDirCache = NULL;
        free(tmpIFuseDirCache);
        return SYS_MALLOC_ERR;
    }
    
    tmpIFuseDirCache->timestamp = iFuseLibGetCurrentTime();

    *iFuseDirCache = tmpIFuseDirCache;
    return 0;
}

static int _freeStatCache(iFuseStatCache_t *iFuseStatCache) {
    assert(iFuseStatCache != NULL);

    if(iFuseStatCache->iRodsPath != NULL) {
        free(iFuseStatCache->iRodsPath);
        iFuseStatCache->iRodsPath = NULL;
    }
    
    if(iFuseStatCache->stbuf != NULL) {
        free(iFuseStatCache->stbuf);
        iFuseStatCache->stbuf = NULL;
    }
    
    iFuseStatCache->timestamp = 0;
    free(iFuseStatCache);
    return 0;
}

static int _freeDirCache(iFuseDirCache_t *iFuseDirCache) {
    char *str;
    
    assert(iFuseDirCache != NULL);

    if(iFuseDirCache->iRodsPath != NULL) {
        free(iFuseDirCache->iRodsPath);
        iFuseDirCache->iRodsPath = NULL;
    }
    
    if(iFuseDirCache->entries != NULL) {
        while(!iFuseDirCache->entries->empty()) {
            str = iFuseDirCache->entries->front();
            iFuseDirCache->entries->pop_front();

            free(str);
        }
        
        delete iFuseDirCache->entries;
    }
    
    iFuseDirCache->timestamp = 0;
    free(iFuseDirCache);
    return 0;
}

static int _cacheStat(const char *iRodsPath, const struct stat *stbuf) {
    int status = 0;
    std::map<std::string, iFuseStatCache_t*>::iterator it_statcachemap;
    iFuseStatCache_t *iFuseStatCache = NULL;
    iFuseStatCache_t *tmpIFuseStatCache = NULL;
    
    assert(iRodsPath != NULL);
    assert(stbuf != NULL);
    
    std::string pathkey(iRodsPath);

    pthread_mutex_lock(&g_StatCacheLock);
    
    status = _newStatCache(&iFuseStatCache);
    if(status != 0) {
        pthread_mutex_unlock(&g_StatCacheLock);
        return status;
    }
    
    iFuseStatCache->iRodsPath = strdup(iRodsPath);
    if(iFuseStatCache->iRodsPath == NULL) {
        _freeStatCache(iFuseStatCache);
        pthread_mutex_unlock(&g_StatCacheLock);
        return SYS_MALLOC_ERR;
    }
    
    iFuseStatCache->stbuf = (struct stat *) calloc(1, sizeof ( struct stat));
    if(iFuseStatCache->stbuf == NULL) {
        _freeStatCache(iFuseStatCache);
        pthread_mutex_unlock(&g_StatCacheLock);
        return SYS_MALLOC_ERR;
    }
    memcpy(iFuseStatCache->stbuf, stbuf, sizeof(struct stat));
    
    
    it_statcachemap = g_StatCacheMap.find(pathkey);
    if(it_statcachemap != g_StatCacheMap.end()) {
        tmpIFuseStatCache = it_statcachemap->second;
        g_StatCacheMap.erase(it_statcachemap);
        _freeStatCache(tmpIFuseStatCache);
    }
    g_StatCacheMap[pathkey] = iFuseStatCache;
    
    pthread_mutex_unlock(&g_StatCacheLock);
    return 0;
}

static int _cacheDirEntry(const char *iRodsPath, const char *iRodsFilename) {
    int status = 0;
    std::map<std::string, iFuseDirCache_t*>::iterator it_dircachemap;
    iFuseDirCache_t *iFuseDirCache = NULL;
    char *entry_name = NULL;
    bool create_new = true;
    
    assert(iRodsPath != NULL);
    assert(iRodsFilename != NULL);
    
    std::string pathkey(iRodsPath);

    pthread_mutex_lock(&g_DirCacheLock);
    
    // check if directory already exists
    it_dircachemap = g_DirCacheMap.find(pathkey);
    if(it_dircachemap != g_DirCacheMap.end()) {
        // has it
        iFuseDirCache = it_dircachemap->second;
        if(iFuseDirCache != NULL) {
            // append
            create_new = false;
        } else {
            g_DirCacheMap.erase(it_dircachemap);
        }
    }
    
    if(create_new) {
        status = _newDirCache(&iFuseDirCache);
        if(status != 0) {
            pthread_mutex_unlock(&g_DirCacheLock);
            return status;
        }
        
        iFuseDirCache->iRodsPath = strdup(iRodsPath);
        if(iFuseDirCache->iRodsPath == NULL) {
            _freeDirCache(iFuseDirCache);
            pthread_mutex_unlock(&g_DirCacheLock);
            return SYS_MALLOC_ERR;
        }
        
        g_DirCacheMap[pathkey] = iFuseDirCache;
    }
    
    if(iFuseDirCache->entries != NULL) {
        entry_name = strdup(iRodsFilename);
        if(entry_name == NULL) {
            pthread_mutex_unlock(&g_DirCacheLock);
            return SYS_MALLOC_ERR;
        }
        
        iFuseDirCache->entries->push_back(entry_name);
    }
    
    pthread_mutex_unlock(&g_DirCacheLock);
    return 0;
}

static int _getStatCache(const char *iRodsPath, struct stat *stbuf) {
    int status = 0;
    std::string pathkey(iRodsPath);
    std::map<std::string, iFuseStatCache_t*>::iterator it_statcachemap;
    iFuseStatCache_t *iFuseStatCache = NULL;
    
    assert(iRodsPath != NULL);
    assert(stbuf != NULL);
    
    pthread_mutex_lock(&g_StatCacheLock);

    it_statcachemap = g_StatCacheMap.find(pathkey);
    if(it_statcachemap != g_StatCacheMap.end()) {
        // has it
        iFuseStatCache = it_statcachemap->second;
        if(iFuseStatCache != NULL) {
            if(iFuseLibDiffTimeSec(iFuseLibGetCurrentTime(), iFuseStatCache->timestamp) <= g_metadataCacheTimeoutSec) {
                memcpy(stbuf, iFuseStatCache->stbuf, sizeof(struct stat));
                status = 0;
            } else {
                // expired
                g_StatCacheMap.erase(it_statcachemap);
                _freeStatCache(iFuseStatCache);
                status = -ENOENT;
            }
        } else {
            g_StatCacheMap.erase(it_statcachemap);
            status = -ENOENT;
        }
    } else {
        status = -ENOENT;
    }
    
    pthread_mutex_unlock(&g_StatCacheLock);
    return status;
}

static int _getDirCache(const char *iRodsPath, char **entries, unsigned int *bufferLen) {
    int status = 0;
    std::string pathkey(iRodsPath);
    std::map<std::string, iFuseDirCache_t*>::iterator it_dircachemap;
    iFuseDirCache_t *iFuseDirCache = NULL;
    std::list<char*>::iterator it_entrylist;
    int entrybufferlen = 0;
    char *entrybuffer;
    char *entrybufferPtr;
    
    assert(iRodsPath != NULL);
    assert(entries != NULL);
    assert(bufferLen != NULL);

    *entries = NULL;
    *bufferLen = 0;
    
    pthread_mutex_lock(&g_DirCacheLock);

    it_dircachemap = g_DirCacheMap.find(pathkey);
    if(it_dircachemap != g_DirCacheMap.end()) {
        // has it
        iFuseDirCache = it_dircachemap->second;
        if(iFuseDirCache != NULL) {
            if(iFuseLibDiffTimeSec(iFuseLibGetCurrentTime(), iFuseDirCache->timestamp) <= g_metadataCacheTimeoutSec) {
                for(it_entrylist=iFuseDirCache->entries->begin();it_entrylist!=iFuseDirCache->entries->end();it_entrylist++) {
                    char *entryName = *it_entrylist;
                    int entryNameLen = strlen(entryName);
                    entrybufferlen += entryNameLen + 1;
                }
                
                if(entrybufferlen == 0) {
                    entrybufferlen = 1; // empty null-terminated buffer
                }
                
                entrybuffer = (char *) calloc(1, entrybufferlen);
                if(entrybuffer == NULL) {
                    pthread_mutex_unlock(&g_DirCacheLock);
                    return SYS_MALLOC_ERR;
                }
                
                entrybufferPtr = entrybuffer;
                for(it_entrylist=iFuseDirCache->entries->begin();it_entrylist!=iFuseDirCache->entries->end();it_entrylist++) {
                    char *entryName = *it_entrylist;
                    int entryNameLen = strlen(entryName);
                    
                    memcpy(entrybufferPtr, entryName, entryNameLen);
                    entrybufferPtr += entryNameLen;
                    *entrybufferPtr = '\0';
                    entrybufferPtr++;
                }
                
                *entries = entrybuffer;
                *bufferLen = entrybufferlen;
                status = 0;
            } else {
                // expired
                g_DirCacheMap.erase(it_dircachemap);
                _freeDirCache(iFuseDirCache);
                status = -ENOENT;
            }
        } else {
            g_DirCacheMap.erase(it_dircachemap);
            status = -ENOENT;
        }
    } else {
       status = -ENOENT;
    }
    
    pthread_mutex_unlock(&g_DirCacheLock);
    return status;
}

static int _checkFreshessOfDirCache(const char *iRodsPath) {
    int status = 0;
    std::string pathkey(iRodsPath);
    std::map<std::string, iFuseDirCache_t*>::iterator it_dircachemap;
    iFuseDirCache_t *iFuseDirCache = NULL;
    
    assert(iRodsPath != NULL);
    
    pthread_mutex_lock(&g_DirCacheLock);

    it_dircachemap = g_DirCacheMap.find(pathkey);
    if(it_dircachemap != g_DirCacheMap.end()) {
        // has it
        iFuseDirCache = it_dircachemap->second;
        if(iFuseDirCache != NULL) {
            if(iFuseLibDiffTimeSec(iFuseLibGetCurrentTime(), iFuseDirCache->timestamp) <= g_metadataCacheTimeoutSec) {
                status = 0;
            } else {
                // expired
                g_DirCacheMap.erase(it_dircachemap);
                _freeDirCache(iFuseDirCache);
                status = -ENOENT;
            }
        } else {
            g_DirCacheMap.erase(it_dircachemap);
            status = -ENOENT;
        }
    } else {
       status = -ENOENT;
    }
    
    pthread_mutex_unlock(&g_DirCacheLock);
    return status;
}

static int _removeStatCache(const char *iRodsPath) {
    std::string pathkey(iRodsPath);
    std::map<std::string, iFuseStatCache_t*>::iterator it_statcachemap;
    iFuseStatCache_t *iFuseStatCache = NULL;
    
    assert(iRodsPath != NULL);
    
    pthread_mutex_lock(&g_StatCacheLock);

    it_statcachemap = g_StatCacheMap.find(pathkey);
    if(it_statcachemap != g_StatCacheMap.end()) {
        // has it
        iFuseStatCache = it_statcachemap->second;
        g_StatCacheMap.erase(it_statcachemap);
       
        if(iFuseStatCache != NULL) {
           _freeStatCache(iFuseStatCache);  
        }
    }
    
    pthread_mutex_unlock(&g_StatCacheLock);
    return 0;
}

static int _removeDirCache(const char *iRodsPath) {
    std::string pathkey(iRodsPath);
    std::map<std::string, iFuseDirCache_t*>::iterator it_dircachemap;
    iFuseDirCache_t *iFuseDirCache = NULL;
    
    assert(iRodsPath != NULL);
    
    pthread_mutex_lock(&g_DirCacheLock);

    it_dircachemap = g_DirCacheMap.find(pathkey);
    if(it_dircachemap != g_DirCacheMap.end()) {
        // has it
        iFuseDirCache = it_dircachemap->second;
        g_DirCacheMap.erase(it_dircachemap);
       
        if(iFuseDirCache != NULL) {
           _freeDirCache(iFuseDirCache);  
        }
    }
    
    pthread_mutex_unlock(&g_DirCacheLock);
    return 0;
}

static int _removeDirCacheEntry(const char *iRodsPath, const char *iRodsFilename) {
    int status = 0;
    std::string pathkey(iRodsPath);
    std::map<std::string, iFuseDirCache_t*>::iterator it_dircachemap;
    iFuseDirCache_t *iFuseDirCache = NULL;
    std::list<char*>::iterator it_entrylist;
    
    assert(iRodsPath != NULL);
    assert(iRodsFilename != NULL);
    
    pthread_mutex_lock(&g_DirCacheLock);

    it_dircachemap = g_DirCacheMap.find(pathkey);
    if(it_dircachemap != g_DirCacheMap.end()) {
        // has it
        iFuseDirCache = it_dircachemap->second;
        if(iFuseDirCache != NULL) {
            for(it_entrylist=iFuseDirCache->entries->begin();it_entrylist!=iFuseDirCache->entries->end();it_entrylist++) {
                char *entry = *it_entrylist;
                if(strcmp(entry, iRodsFilename) == 0) {
                    iFuseDirCache->entries->erase(it_entrylist);
                    free(entry);
                    status = 0;
                    break;
                }
            }
            status = -ENOENT;
        } else {
            g_DirCacheMap.erase(it_dircachemap);
            status = -ENOENT;
        }
    } else {
        status = -ENOENT;
    }
    
    pthread_mutex_unlock(&g_DirCacheLock);
    return status;
}

static int _clearExpiredStatCache() {
    std::map<std::string, iFuseStatCache_t*>::iterator it_statcachemap;
    std::list<std::string> removeListStatKey;
    std::list<iFuseStatCache_t*> removeListStatData;
    iFuseStatCache_t *iFuseStatCache = NULL;

    pthread_mutex_lock(&g_StatCacheLock);

    // release all caches
    for(it_statcachemap=g_StatCacheMap.begin();it_statcachemap!=g_StatCacheMap.end();it_statcachemap++) {
        iFuseStatCache = it_statcachemap->second;
        if(iFuseStatCache != NULL) {
            if(iFuseLibDiffTimeSec(iFuseLibGetCurrentTime(), iFuseStatCache->timestamp) > g_metadataCacheTimeoutSec) {
                // expired
                removeListStatKey.push_back(it_statcachemap->first);
                removeListStatData.push_back(it_statcachemap->second);
            }
        } else {
           removeListStatKey.push_back(it_statcachemap->first);
        }
    }

    while(!removeListStatKey.empty()) {
        std::string key = removeListStatKey.front();
        removeListStatKey.pop_front();
        
        g_StatCacheMap.erase(key);
    }
    
    while(!removeListStatData.empty()) {
        iFuseStatCache = removeListStatData.front();
        removeListStatData.pop_front();
        
        _freeStatCache(iFuseStatCache);
    }
    
    pthread_mutex_unlock(&g_StatCacheLock);
    
    g_lastStatCacheTimeoutCheck = iFuseLibGetCurrentTime();
    return 0;
}

static int _clearExpiredDirCache() {
    std::map<std::string, iFuseDirCache_t*>::iterator it_dircachemap;
    std::list<std::string> removeListDirKey;
    std::list<iFuseDirCache_t*> removeListDirData;
    iFuseDirCache_t *iFuseDirCache = NULL;

    pthread_mutex_lock(&g_DirCacheLock);

    // release all caches
    for(it_dircachemap=g_DirCacheMap.begin();it_dircachemap!=g_DirCacheMap.end();it_dircachemap++) {
        iFuseDirCache = it_dircachemap->second;
        if(iFuseDirCache != NULL) {
            if(iFuseLibDiffTimeSec(iFuseLibGetCurrentTime(), iFuseDirCache->timestamp) > g_metadataCacheTimeoutSec) {
                // expired
                removeListDirKey.push_back(it_dircachemap->first);
                removeListDirData.push_back(it_dircachemap->second);
            }
        } else {
           removeListDirKey.push_back(it_dircachemap->first);
        }
    }

    while(!removeListDirKey.empty()) {
        std::string key = removeListDirKey.front();
        removeListDirKey.pop_front();
        
        g_DirCacheMap.erase(key);
    }
    
    while(!removeListDirData.empty()) {
        iFuseDirCache = removeListDirData.front();
        removeListDirData.pop_front();
        
        _freeDirCache(iFuseDirCache);
    }
    
    pthread_mutex_unlock(&g_DirCacheLock);
    return 0;
}

static int _releaseAllCache() {
    std::map<std::string, iFuseStatCache_t*>::iterator it_statcachemap;
    std::map<std::string, iFuseDirCache_t*>::iterator it_dircachemap;
    iFuseStatCache_t *iFuseStatCache = NULL;
    iFuseDirCache_t *iFuseDirCache = NULL;

    pthread_mutex_lock(&g_StatCacheLock);

    // release all caches
    while(!g_StatCacheMap.empty()) {
        it_statcachemap = g_StatCacheMap.begin();
        if(it_statcachemap != g_StatCacheMap.end()) {
            iFuseStatCache = it_statcachemap->second;
            g_StatCacheMap.erase(it_statcachemap);

            if(iFuseStatCache != NULL) {
                _freeStatCache(iFuseStatCache);
            }
        }
    }

    pthread_mutex_unlock(&g_StatCacheLock);
    
    pthread_mutex_lock(&g_DirCacheLock);

    // release all caches
    while(!g_DirCacheMap.empty()) {
        it_dircachemap = g_DirCacheMap.begin();
        if(it_dircachemap != g_DirCacheMap.end()) {
            iFuseDirCache = it_dircachemap->second;
            g_DirCacheMap.erase(it_dircachemap);

            if(iFuseDirCache != NULL) {
                _freeDirCache(iFuseDirCache);
            }
        }
    }

    pthread_mutex_unlock(&g_DirCacheLock);
    
    return 0;
}

/*
 * Initialize metadata cache manager
 */
void iFuseMetadataCacheInit() {
    if(iFuseLibGetOption()->metadataCacheTimeoutSec > 0) {
        g_metadataCacheTimeoutSec = iFuseLibGetOption()->metadataCacheTimeoutSec;
    }
    
    pthread_mutexattr_init(&g_StatCacheLockAttr);
    pthread_mutexattr_settype(&g_StatCacheLockAttr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&g_StatCacheLock, &g_StatCacheLockAttr);
    
    pthread_mutexattr_init(&g_DirCacheLockAttr);
    pthread_mutexattr_settype(&g_DirCacheLockAttr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&g_DirCacheLock, &g_DirCacheLockAttr);
}

/*
 * Destroy metadata cache manager
 */
void iFuseMetadataCacheDestroy() {
    _releaseAllCache();

    pthread_mutex_destroy(&g_StatCacheLock);
    pthread_mutexattr_destroy(&g_StatCacheLockAttr);
    
    pthread_mutex_destroy(&g_DirCacheLock);
    pthread_mutexattr_destroy(&g_DirCacheLockAttr);
}


int iFuseMetadataCacheClearExpiredStat(bool force) {
    
    iFuseRodsClientLog(LOG_DEBUG, "iFuseMetadataCacheClearExpiredStat");
    
    if(!force) {
        if(iFuseLibDiffTimeSec(iFuseLibGetCurrentTime(), g_lastStatCacheTimeoutCheck) <= (g_metadataCacheTimeoutSec / 2)) {
            return 0;
        }
    }
    
    iFuseRodsClientLog(LOG_DEBUG, "iFuseMetadataCacheClearExpiredStat: Clearing expired stat cache");

    return _clearExpiredStatCache();
}

int iFuseMetadataCacheClearExpiredDir(bool force) {
    
    iFuseRodsClientLog(LOG_DEBUG, "iFuseMetadataCacheClearExpiredDir");
    
    if(!force) {
        if(iFuseLibDiffTimeSec(iFuseLibGetCurrentTime(), g_lastDirCacheTimeoutCheck) <= (g_metadataCacheTimeoutSec / 2)) {
            return 0;
        }
    }
    
    iFuseRodsClientLog(LOG_DEBUG, "iFuseMetadataCacheClearExpiredDir: Clearing expired dir cache");

    return _clearExpiredDirCache();
}

int iFuseMetadataCachePutStat(const char *iRodsPath, const struct stat *stbuf) {
    
    iFuseRodsClientLog(LOG_DEBUG, "iFuseMetadataCachePutStat: %s", iRodsPath);
    
    return _cacheStat(iRodsPath, stbuf);
}

int iFuseMetadataCachePutStat2(const char *iRodsDirPath, const char *iRodsFilename, const struct stat *stbuf) {
    int status;
    char path[MAX_NAME_LEN];
    
    iFuseRodsClientLog(LOG_DEBUG, "iFuseMetadataCachePutStat2: %s, %s", iRodsDirPath, iRodsFilename);
    
    status = iFuseLibJoinPath(iRodsDirPath, iRodsFilename, path, MAX_NAME_LEN);
    if(status != 0) {
        return status;
    }
    return _cacheStat(path, stbuf);
}

int iFuseMetadataCacheAddDirEntry(const char *iRodsPath, const char *iRodsFilename) {
    
    iFuseRodsClientLog(LOG_DEBUG, "iFuseMetadataCacheAddDirEntry: %s, %s", iRodsPath, iRodsFilename);
    
    return _cacheDirEntry(iRodsPath, iRodsFilename);
}

int iFuseMetadataCacheAddDirEntryIfFresh(const char *iRodsPath, const char *iRodsFilename) {
    int status;
    
    iFuseRodsClientLog(LOG_DEBUG, "iFuseMetadataCacheAddDirEntryIfFresh: %s, %s", iRodsPath, iRodsFilename);
    
    status = _checkFreshessOfDirCache(iRodsPath);
    if(status != 0) {
        return status;
    }
    
    return _cacheDirEntry(iRodsPath, iRodsFilename);
}

int iFuseMetadataCacheAddDirEntryIfFresh2(const char *iRodsPath) {
    int status;
    char myDir[MAX_NAME_LEN];
    char myEntry[MAX_NAME_LEN];
    
    iFuseRodsClientLog(LOG_DEBUG, "iFuseMetadataCacheAddDirEntryIfFresh2: %s", iRodsPath);

    status = iFuseLibSplitPath(iRodsPath, myDir, MAX_NAME_LEN, myEntry, MAX_NAME_LEN);
    if(status != 0) {
        return status;
    }
    
    status = _checkFreshessOfDirCache(myDir);
    if(status != 0) {
        return status;
    }
    
    return _cacheDirEntry(myDir, myEntry);
}

int iFuseMetadataCacheGetStat(const char *iRodsPath, struct stat *stbuf) {
    
    iFuseRodsClientLog(LOG_DEBUG, "iFuseMetadataCacheGetStat: %s", iRodsPath);
    
    return _getStatCache(iRodsPath, stbuf);
}

int iFuseMetadataCacheGetDirEntry(const char *iRodsPath, char **entries, unsigned int *bufferLen) {
    
    iFuseRodsClientLog(LOG_DEBUG, "iFuseMetadataCacheGetDirEntry: %s", iRodsPath);
    
    return _getDirCache(iRodsPath, entries, bufferLen);
}

int iFuseMetadataCacheRemoveStat(const char *iRodsPath) {
    
    iFuseRodsClientLog(LOG_DEBUG, "iFuseMetadataCacheRemoveStat: %s", iRodsPath);
    
    return _removeStatCache(iRodsPath);
}

int iFuseMetadataCacheRemoveDir(const char *iRodsPath) {
    
    iFuseRodsClientLog(LOG_DEBUG, "iFuseMetadataCacheRemoveDir: %s", iRodsPath);
    
    return _removeDirCache(iRodsPath);
}

int iFuseMetadataCacheRemoveDirEntry(const char *iRodsPath, const char *iRodsFilename) {
    
    iFuseRodsClientLog(LOG_DEBUG, "iFuseMetadataCacheRemoveDirEntry: %s, %s", iRodsPath, iRodsFilename);
    
    return _removeDirCacheEntry(iRodsPath, iRodsFilename);
}

int iFuseMetadataCacheRemoveDirEntry2(const char *iRodsPath) {
    int status;
    char myDir[MAX_NAME_LEN];
    char myEntry[MAX_NAME_LEN];

    iFuseRodsClientLog(LOG_DEBUG, "iFuseMetadataCacheRemoveDirEntry2: %s", iRodsPath);

    status = iFuseLibSplitPath(iRodsPath, myDir, MAX_NAME_LEN, myEntry, MAX_NAME_LEN);
    if(status != 0) {
        return status;
    }
    
    return _removeDirCacheEntry(myDir, myEntry);
}
