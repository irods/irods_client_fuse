/*** This code is written by Illyoung Choi (iychoi@email.arizona.edu)    ***
 *** funded by iPlantCollaborative (www.iplantcollaborative.org).        ***/
#ifndef IFUSE_LIB_METADATACACHE_HPP
#define IFUSE_LIB_METADATACACHE_HPP

#include <sys/stat.h>
#include <list>
#include <time.h>

#define IFUSE_METADATA_CACHE_TIMEOUT_SEC           (3*60)

typedef struct IFuseStatCache {
    char *iRodsPath;
    struct stat *stbuf;
    time_t timestamp;
} iFuseStatCache_t;

typedef struct IFuseDirCache {
    char *iRodsPath;
    std::list<char*> *entries;
    time_t timestamp;
} iFuseDirCache_t;

void iFuseMetadataCacheInit();
void iFuseMetadataCacheDestroy();
void iFuseMetadataCacheClear();
int iFuseMetadataCacheClearExpiredStat(bool force);
int iFuseMetadataCacheClearExpiredDir(bool force);
int iFuseMetadataCachePutStat(const char *iRodsPath, const struct stat *stbuf);
int iFuseMetadataCachePutStat2(const char *iRodsDirPath, const char *iRodsFilename, const struct stat *stbuf);
int iFuseMetadataCacheAddDirEntry(const char *iRodsPath, const char *iRodsFilename);
int iFuseMetadataCacheAddDirEntryIfFresh(const char *iRodsPath, const char *iRodsFilename);
int iFuseMetadataCacheAddDirEntryIfFresh2(const char *iRodsPath);
int iFuseMetadataCacheGetStat(const char *iRodsPath, struct stat *stbuf);
int iFuseMetadataCacheGetDirEntry(const char *iRodsPath, char **entries, unsigned int *bufferLen);
int iFuseMetadataCacheCheckExistanceOfDirEntry(const char *iRodsPath);
int iFuseMetadataCacheRemoveStat(const char *iRodsPath);
int iFuseMetadataCacheRemoveDir(const char *iRodsPath);
int iFuseMetadataCacheRemoveDirEntry(const char *iRodsPath, const char *iRodsFilename);
int iFuseMetadataCacheRemoveDirEntry2(const char *iRodsPath);

#endif	/* IFUSE_LIB_METADATACACHE_HPP */
