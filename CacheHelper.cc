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
#include <sys/time.h>

using namespace ::std;

static const string CLIENT_DIR_ROOT = "/users/chrahul5/bnfs_client";

static std::unordered_set<string> dirty_files = {};

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

/* Don't pass O_CREAT as open mode, only use for read/write*/
bool getCheckInCache(const char *path, int *file_descriptor, bool close_file, int open_mode)
{
    string cache_path = getCachePath(path);
    *file_descriptor = open(cache_path.c_str(), open_mode);

    if (*file_descriptor >= 0)
    {
        if (close_file)
            close(*file_descriptor);
        return true;
    }
    return false;
}

bool getCheckInTemp(const char *path, int *file_descriptor, bool close_file, int open_mode, bool create_new)
{
    string temp_path = getTempPath(path);
    string cache_path = getCachePath(path);
    *file_descriptor = open(temp_path.c_str(), O_RDONLY | open_mode);

    if (*file_descriptor == -1 && create_new)
    {
        int cache_file_fd;
        bool is_cached = getCheckInCache(path, &cache_file_fd, true, O_RDONLY);

        if (is_cached)
        {
            string cp_command = "cp -f " + cache_path + " " + temp_path;
            int status = system(cp_command.c_str());

            if (status == -1)
                return false;

            if (WIFEXITED(status))
            {
                int exit_status = WEXITSTATUS(status);
                if (exit_status != 0)
                {
                    cerr << "Failed to copy file cache->temp: " << path << endl;
                    return false;
                }
            }
        }
        *file_descriptor = open(temp_path.c_str(), O_CREAT | open_mode, 0777);
    }

    if (*file_descriptor >= 0)
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
int syncFileServerToCache(const char *path, const string data, bool close_file, int open_mode)
{
    string cache_path = getCachePath(path);
    ofstream file(cache_path, std::ios::binary);
    if (!file.is_open())
    {
        cerr << "Failed to sync file: " << path << endl;
        return -1;
    }

    file.write(data.data(), data.size());
    file.close();

    int file_descriptor = -1;
    if (!close_file)
        file_descriptor = open(cache_path.c_str(), open_mode);

    return file_descriptor;
}

bool setFileModifiedTime(const char *path, int epochTime)
{
    struct timeval times[2];
    times[1].tv_sec = epochTime;
    times[1].tv_usec = 0;

    if (utimes(path, times) == -1) {
     cout << "It failed " << errno << endl;
        return false;
    }

    cout << "It works" << endl;
    return true;
}

/* Returns status: 0 is success, else failure */
int commitToCache(const char *path, int server_modified_at_epoch)
{
    string temp_path = getTempPath(path);
    string cache_path = getCachePath(path);

    int temp_file_fd = open(temp_path.c_str(), O_RDWR);
    if (temp_file_fd == -1)
        return 1;

    string cp_command = "cp -f " + temp_path + " " + cache_path;
    int status = system(cp_command.c_str());

    if (status == -1)
        return 1;

    if (WIFEXITED(status))
    {
        int exit_status = WEXITSTATUS(status);
        if (exit_status != 0)
            return 1;
    }

    cout << "Changing time of " << temp_path << endl;
    setFileModifiedTime(temp_path.c_str(), server_modified_at_epoch);
    cout << "Changing time of " << cache_path << endl;
    setFileModifiedTime(cache_path.c_str(), server_modified_at_epoch);

    dirty_files.erase(getHashedPath(path));
    cout << "dirty_files" << endl;

    return 0;
}

void initCache()
{
    initDir(CLIENT_DIR_ROOT);
    initDir(getCachePath());
    initDir(getTempPath());
    cleanTempDir();
}

bool canOpenFile(const char *path)
{
    return !(dirty_files.find(getHashedPath(path)) != dirty_files.end());
}

void markFileDirty(const char *path)
{
    dirty_files.insert(getHashedPath(path));
}

int main()
{
    /* Tested */
    initCache();

    /* Tested */
    cout << getCachePath("/dirname/test1.txt") << endl;

    /* Tested */
    cout << "A" << endl;
    syncFileServerToCache("/dirname/abc.txt", "hello world\nthis is a file\n", true, O_RDONLY);

    /* Tested */
    cout << "B" << endl;
    syncFileServerToCache("/dirname/abc.txt", "Hey new changes!\n", true, O_RDONLY);

    /* Tested */
    cout << "C" << endl;
    int fd;
    string tmp_txt = "hello from temp file!\n";
    bool ok = getCheckInTemp("/dirname/test1.txt", &fd, false, O_WRONLY, true);
    if (ok)
    {
        write(fd, tmp_txt.c_str(), tmp_txt.length());
        cout << "OK - created and wrote new" << endl;
        close(fd);
    }

    /* Tested */
    cout << "D" << endl;
    ok = getCheckInTemp("/dirname/test2.txt", &fd, true, O_RDONLY, false);
    if (!ok)
    {
        cout << "OK - Couldn't find in temp" << endl;
    }

    cout << "E" << endl;
    ok = getCheckInTemp("/dirname/test1.txt", &fd, true, O_RDONLY, false);
    if (ok)
    {
        cout << "OK - Read from temp" << endl;
    }

    cout << "F" << endl;
    string cmd = "touch " + getCachePath("/dir/newfile.txt");
    system(cmd.c_str());
    ok = getCheckInTemp("/dir/newfile.txt", &fd, true, O_RDONLY, true);
    if (ok)
    {
        cout << "OK - Read from cache->temp" << endl;
    }

    /* Tested */
    cout << "G" << endl;
    commitToCache("/dirname/test1.txt", 1613373778);

    return 0;
}