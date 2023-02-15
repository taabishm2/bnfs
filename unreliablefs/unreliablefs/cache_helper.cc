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

using namespace ::std;

static const string CLIENT_DIR_ROOT = "/users/chrahul5/bnfs_client";

extern "C"
{
    struct CacheHelper
    {

        CacheHelper(void) {
            cout << "#*#*#*#*#* CACHE HELPER initialize #*#*#*#*#*";
        }

        bool fileExists(const string &path)
        {
            ifstream file(path);
            return file.good();
        }

        string getCachePath(void)
        {
            return CLIENT_DIR_ROOT + "/cache";
        }

        string getTempPath(void)
        {
            return CLIENT_DIR_ROOT + "/temp";
        }

        void initDir(string path)
        {
            struct stat st;
            if (stat(path.c_str(), &st) != 0)
            {
                if (mkdir(path.c_str(), 0755) == 0)
                    cout << "Directory created at " << path << endl;
                else
                    cerr << "Failed to create directory at " << path << endl;
            }
        }

        void cleanTempDir()
        {
            string temp_path = getTempPath();

            struct stat st;
            if (stat(temp_path.c_str(), &st) == 0)
            {
                // If the directory exists, delete all of its files.
                DIR *dir = opendir(temp_path.c_str());
                if (dir != nullptr)
                {
                    struct dirent *entry;
                    while ((entry = readdir(dir)) != nullptr)
                    {
                        string entry_path = temp_path + "/" + entry->d_name;
                        if (entry->d_type == DT_REG)
                            remove(entry_path.c_str());
                    }
                    closedir(dir);
                }
            }
        }

        string getHashedPath(const char *rel_path)
        {
            unsigned char hash[SHA256_DIGEST_LENGTH];
            SHA256_CTX sha256;
            SHA256_Init(&sha256);
            SHA256_Update(&sha256, rel_path, strlen(rel_path));
            SHA256_Final(hash, &sha256);

            stringstream ss;
            for (int i = 0; i < SHA256_DIGEST_LENGTH; ++i)
                ss << hex << setw(2) << setfill('0') << ((int)hash[i]);

            return ss.str();
        }

        string getCachePath(const char *rel_path)
        {
            return getCachePath() + "/" + getHashedPath(rel_path);
        }

        string getTempPath(const char *rel_path)
        {
            return getTempPath() + "/" + getHashedPath(rel_path);
        }

        bool getCheckInCache(const char *path, int *file_descriptor, bool close_file, int open_mode)
        {
            string cache_path = getCachePath(path);
            *file_descriptor = open(cache_path.c_str(), open_mode);

            if (file_descriptor >= 0)
            {
                if (close_file)
                    close(*file_descriptor);
                return true;
            }
            return false;
        }

        bool getCheckInTemp(const char *path, int *file_descriptor, bool close_file, int open_mode)
        {
            string temp_path = getTempPath(path);
            *file_descriptor = open(temp_path.c_str(), open_mode);

            if (file_descriptor >= 0)
            {
                if (close_file)
                    close(*file_descriptor);
                return true;
            }
            return false;
        }

        bool isCacheOutOfDate(const char *path, int server_modified_at_epoch, int *file_descriptor, bool close_file, int open_mode)
        {
            bool file_exists_in_cache = getCheckInCache(path, file_descriptor, false, open_mode);

            if (!file_exists_in_cache)
                return false;

            struct stat file_stat;
            fstat(*file_descriptor, &file_stat);

            time_t local_modified_at = file_stat.st_mtim.tv_nsec;
            int local_modified_at_epoch = static_cast<int>(local_modified_at);

            if (file_exists_in_cache && close_file)
                close(*file_descriptor);

            return server_modified_at_epoch > local_modified_at_epoch;
        }

        /* Returns file descriptor of cache file */
        int syncFileServerToCache(const char *path, const char* data, bool close_file, int open_mode)
        {
            string cache_path = getCachePath(path);
            ofstream file(cache_path, std::ios::binary);
            if (!file.is_open())
            {
                cerr << "Failed to sync file: " << path << endl;
                return;
            }

            cout << "Writing to file: " << data;
            // file.write(data.data(), data.size());
            // file.close();

            int file_descriptor;
            // if (!close_file)
            //     file_descriptor = open(cache_path.c_str(), open_mode);

            return file_descriptor;
        }

        void commitToCache(void)
        {
            // copy from temp to cache
        }

        void initCache()
        {
            initDir(CLIENT_DIR_ROOT);
            cout << "initDir" << endl;
            initDir(getCachePath());
            cout << "getCachePath" << endl;
            initDir(getTempPath());
            cout << "getTempPath" << endl;
            cleanTempDir();
            cout << "cleanTempDir" << endl;
        }
    
    };
}

// Port calls to C-code.

CacheHelper *NewCacheHelper()
{
    CacheHelper *cacheHelper = new CacheHelper();
    return cacheHelper;
}

int CH_getCheckInCache(CacheHelper* helper, const char *path, int *file_descriptor, bool close_file, int open_mode) {
    return helper->getCheckInCache(path, file_descriptor, close_file, open_mode); 
}

int CH_getCheckInTemp(CacheHelper* helper, const char *path, int *file_descriptor, bool close_file, int open_mode) {
    return helper->getCheckInTemp(path, file_descriptor, close_file, open_mode);
}

bool CH_isCacheOutOfDate(CacheHelper* helper, const char *path, int server_modified_at_epoch, int *file_descriptor, bool close_file, int open_mode) {
    return helper->isCacheOutOfDate(path, server_modified_at_epoch, file_descriptor, close_file, open_mode);
}

int CH_syncFileServerToCache(CacheHelper* helper, const char *path, char* data, bool close_file, int open_mode) {
    return helper->syncFileServerToCache(path, data, close_file, open_mode);
}

void CH_commitToCache(CacheHelper* helper) {
    return helper->commitToCache();
}

void CH_initCache(CacheHelper* helper) {
    return helper->initCache();
}

// int main()
// {
//     initCache();
//     cout << getCachePath("abc") << endl;
//     cout << getTempPath("abc") << endl;
//     cout << getTempPath("def") << endl;
//     cout << "Client setup completed!" << endl;
//     return 0;
// }