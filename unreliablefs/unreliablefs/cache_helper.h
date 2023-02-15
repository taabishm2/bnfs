#include <iostream>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <openssl/sha.h>
#include <string.h>
#include <sstream>
#include <iomanip>
#include <string>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>
#include <fstream>
#include <vector>
#include <cstdlib>
#include <unordered_set>
#include <chrono>
#include <sys/types.h>
#include <utime.h>

using namespace std;

struct CacheHelper
{
    CacheHelper(void);

    string getCachePath(void);

    string getTempPath(void);

    void initDir(string path);

    void cleanTempDir();

    string getHashedPath(const char *rel_path);

    string getCachePath(const char *rel_path);

    string getTempPath(const char *rel_path);

    /* Don't pass O_CREAT as open mode, only use for read/write*/
    bool getCheckInCache(const char *path, int *file_descriptor, bool close_file, int open_mode);

    bool getCheckInTemp(const char *path, int *file_descriptor, bool close_file, int open_mode, bool create_new);

    bool isCacheOutOfDate(const char *path, int server_modified_at_epoch, int *file_descriptor, bool close_file, int open_mode);

    /* Returns file descriptor of cache file */
    int syncFileServerToCache(const char *path, const char *data, bool close_file, int open_mode);

    bool setFileModifiedTime(const char *path, int epoch_time);

    /* Returns status: 0 is success, else failure */
    int commitToCache(const char *path, int server_modified_at_epoch);

    void initCache();

    bool canOpenFile(const char *path);

    void markFileDirty(const char *path);
};

static std::unordered_set<string> dirty_files;

CacheHelper* NewCacheHelper();
