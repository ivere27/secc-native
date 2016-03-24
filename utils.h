#include <openssl/md5.h>
#include <zlib.h>
#include <cpprest/producerconsumerstream.h>

/* This is for mkdir(); this may need to be changed for some platforms. */
#include <sys/stat.h>  /* For mkdir() */

#define CHUNK 512
using namespace concurrency::streams;       // Asynchronous streams

// FIXME : need a logger
#define LOGE(...) LOGI(__VA_ARGS__)       // FIXME : to stderr?
template <class T>
void LOGI(T t)
{
  if (!getenv("DEBUG")) return;
  if (!getenv("SECC_LOG"))  {
    std::cout << t << std::endl;
    return;
  }

  try {
    std::ofstream logFile;
    logFile.open(getenv("SECC_LOG"), std::ios::out | std::ios::app);
    logFile << "[" << getpid() << "] " << t <<std::endl;
    logFile.close();
  } catch(const std::exception &e) {
    std::cout << e.what() << std::endl;
  }
}
template <class T, class... Args>
void LOGI(T t, Args... args)
{
  if (!getenv("DEBUG")) return;
  if (getenv("SECC_LOG")) {
    try {
      std::ofstream logFile;
      logFile.open(getenv("SECC_LOG"), std::ios::out | std::ios::app);
      logFile << "[" << getpid() << "] " << t;
      logFile.close();
    } catch(const std::exception &e) {
      std::cout << e.what() << std::endl;
    }
  } else
    std::cout << "[" << getpid() << "] " << t;

  LOGI(args...);
}

// https://github.com/libarchive/libarchive/blob/master/contrib/untar.c
/* Parse an octal number, ignoring leading and trailing nonsense. */
int parseoct(const char *p, size_t n);
/* Returns true if this is 512 zero bytes. */
int is_end_of_archive(const char *p);
/* Create a directory, including parent directories as necessary. */
void create_dir(char *pathname, int mode);
/* Create a file, including parent directory as necessary. */
FILE *create_file(char *pathname, int mode);
/* Verify the tar checksum. */
int verify_checksum(const char *p);
/* Extract a tar archive. */
int untar(concurrency::streams::streambuf<char> *a, const char *directory);

std::string _exec(const char* cmd);
std::string _basename(const std::string &path, bool removeExt);
std::string _dirname(const std::string &path);
int getZippedStream(const char* cmd, std::shared_ptr<producer_consumer_buffer<unsigned char>> buf, std::shared_ptr<std::string> hash, size_t *totalSize);
int unzip(concurrency::streams::streambuf<unsigned char> *source, concurrency::streams::streambuf<unsigned char> *dest);

static inline std::string &ltrim(std::string &s) {
  s.erase(s.begin(), std::find_if(s.begin(), s.end(), std::not1(std::ptr_fun<int, int>(std::isspace))));
  return s;
}
static inline std::string &rtrim(std::string &s) {
  s.erase(std::find_if(s.rbegin(), s.rend(), std::not1(std::ptr_fun<int, int>(std::isspace))).base(), s.end());
  return s;
}
static inline std::string &trim(std::string &s) {
  return ltrim(rtrim(s));
}
