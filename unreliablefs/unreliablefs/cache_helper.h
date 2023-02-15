#ifndef CACHE_HELPER_HH
#define CACHE_HELPER_HH

#ifdef __cplusplus
extern "C"
{
#endif

    typedef struct CacheHelper CacheHelper;

    CacheHelper *NewCacheHelper();

    int CH_getCheckInCache(const char *path, int *file_descriptor, bool close_file, int open_mode);

    int CH_getCheckInTemp(const char *path, int *file_descriptor, bool close_file, int open_mode);

    bool CH_isCacheOutOfDate(const char *path, int server_modified_at_epoch, int *file_descriptor, bool close_file, int open_mode);

    int CH_syncFileServerToCache(const char *path, const char* data, bool close_file, int open_mode);

    void CH_commitToCache(void);

    void CH_initCache(void);

#ifdef __cplusplus
}
#endif

extern struct CacheHelper *cacheHelper;

#endif /* CACHE_HELPER_HH */
