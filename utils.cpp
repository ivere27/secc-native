#include "untar.h"
#include "utils.h"
#include "zip.h"

std::string _basename(const std::string &path, bool removeExt)
{
  size_t i = path.find_last_of('/');
  std::string base = (i == std::string::npos) ? path : path.substr(i +1);
  if (!removeExt)
    return base;

  i = base.find_last_of(".");
  return (i == std::string::npos) ? base : base.substr(0, i);
}

std::string _dirname(const std::string &path)
{
  size_t i = path.find_last_of('/');
  return (i == std::string::npos) ? "." : path.substr(0, i);
}

std::string _exec(const char* cmd) {
  std::shared_ptr<FILE> pipe(popen(cmd, "r"), pclose);
  if (!pipe) return nullptr;
  char buffer[128];
  std::string result = "";
  while (!feof(pipe.get())) {
    if (fgets(buffer, 128, pipe.get()) != NULL)
      result += buffer;
  }
  return result;
}
