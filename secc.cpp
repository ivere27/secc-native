/*
  SECC Native(c++) Frontend

  dependencies : SimpleHttpRequest, nlohmann/json, zlib, md5
  ubuntu :
  $ sudo apt-get install libssl-dev zlib1g-dev
  $ make clean && make
  run :
  $ SECC_ADDRESS="172.17.42.1" [SECC_PORT="10509"] \
    /path/to/secc-native/bin/gcc ...
*/

#include <iostream>
#include "json.hpp"
#include "SimpleHttpRequest.hpp"
#include "SimpleProcessSpawn.hpp"

using request::LOGI;

#include <sys/utsname.h>

#include "untar.h"
#include "utils.h"
#include "zip.h"
#include "UriCodec.h"

using namespace std;
using namespace nlohmann;

class _secc_exception : public std::exception
{
  virtual const char* what() const throw()
  {
    return "secc exception";
  }
} secc_exception;

int main(int argc, char* argv[])
{
  static uv_loop_t* loop = uv_default_loop();

  //debug
  if (getenv("SECC_CMDLINE")) {
    string cmd = "";
    for(int i = 0; i < argc; i++)
      cmd += "'" + string(argv[i]) + "' ";
    LOGI(cmd);
  }


  // init
  auto option = json::object();
  auto job = json::object();
  auto sourceHash = make_shared<string>();

  stringstream infileBuffer;
  auto outfileBuffer = make_shared<stringstream>();
  auto tempBuffer = make_shared<stringstream>();

  string secc_scheduler_address = (getenv("SECC_ADDRESS")) ? getenv("SECC_ADDRESS") : "127.0.0.1";
  string secc_scheduler_port = (getenv("SECC_PORT")) ? getenv("SECC_PORT") : "10509";
  string secc_scheduler_host = "http://" + secc_scheduler_address + ":" + secc_scheduler_port; //FIXME : from settings
  string secc_driver = _basename(argv[0], false);                // FIXME : exception
  string secc_driver_path = "/usr/bin/" + secc_driver;  // FIXME : from PATH
  string secc_cwd = getcwd(nullptr, 0);
  string secc_mode = "1";      //FIXME : mode 2
  bool secc_cache = (getenv("SECC_CACHE") && strcmp(getenv("SECC_CACHE"),"1") == 0) ? true : false;
  bool secc_cross = (getenv("SECC_CROSS") && strcmp(getenv("SECC_CROSS"),"1") == 0) ? true : false;

  bool _argv_c_exists = false;
  auto secc_argv = json::array();
  for(int i = 1; i < argc; i++) {
    if (strcmp(argv[i],"-c") == 0) _argv_c_exists = true;
    secc_argv[i-1] = argv[i];
  }


  auto funcDaemonCompile = [&]() {
    string secc_daemon_host = "http://" + job["daemon"]["daemonAddress"].get<string>() + ":" + std::to_string(job["daemon"]["daemonPort"].get<int>());
    string secc_daemon_compile_uri = secc_daemon_host + "/compile/preprocessed/" + job["archive"]["archiveId"].get<string>();

    string secc_filename = option["outfile"].is_null()
                              ? _basename(option["infile"], true)
                              : _basename(option["outfile"], true);

    // FIXME : using write() with map<string, string> options
    //                        and map<string, string> headers
    LOGI("REQUEST headers ");
    //LOGI(headers)
    //LOGI(infileBuffer.str());

    static request::SimpleHttpRequest request(loop);
    request.timeout = 50000;
    request
    .setHeader("content-type","application/octet-stream")
    .setHeader("Content-Encoding", "gzip")
    .setHeader("secc-jobid", to_string(job["jobId"].get<int>()))
    .setHeader("secc-driver", secc_driver)
    .setHeader("secc-language", option["language"])
    .setHeader("secc-argv", option["remoteArgv"].dump())
    .setHeader("secc-filename", secc_filename)
    .setHeader("secc-outfile", option["outfile"].is_null() ? "null" : option["outfile"].get<string>())
    .setHeader("secc-cross", secc_cross ? "true" : "false")
    .setHeader("secc-target", "x86_64-linux-gnu") // FIXME : from system
    .post(secc_daemon_compile_uri, infileBuffer.str())
    .on("error", [](request::Error&& err){
      throw secc_exception;
    });
    request.on("response", [&](request::Response&& res){
      //check secc-code
      LOGI("compile - response status code: ", res.statusCode);
      if ( res.statusCode != 200
        || res.headers["secc-code"].compare("0") != 0)
        throw secc_exception;

      if (res.headers.count("secc-stdout"))
        cout << UriDecode(res.headers["secc-stdout"]);
      if (res.headers.count("secc-stderr"))
        cerr << UriDecode(res.headers["secc-stderr"]);

      string outdir = (option["outfile"].is_null())
                    ? secc_cwd
                    : _dirname(option["outfile"].get<string>());
      LOGI("outdir : ", outdir);


      stringstream tarStream;

      int ret = unzip(res, tarStream);
      if (ret != 0)
        throw secc_exception;

      LOGI("unzip done.");
      ret = untar(tarStream, outdir.c_str());
      if (ret != 0)
        throw secc_exception;

      LOGI("done");

    });
    request.end();
  };  // funcDaemonCompile

  auto funcDaemonCache = [&]() {
    try {
      string secc_daemon_host = "http://" + job["daemon"]["daemonAddress"].get<string>() + ":" + std::to_string(job["daemon"]["daemonPort"].get<int>());
      string secc_daemon_cache_uri = secc_daemon_host + "/cache/" + job["archive"]["archiveId"].get<string>() + "/" + *sourceHash + "/" + option["argvHash"].get<string>();

      LOGI("cache is available. try URL : ", secc_daemon_cache_uri);

      static request::SimpleHttpRequest requestCache(loop);
      requestCache.timeout = 50000;
      requestCache.get(secc_daemon_cache_uri)
      .on("error", [](request::Error&& err){
        throw secc_exception;
      }).on("response", [&](request::Response&& res){
        //check secc-code
        LOGI("cache - response status code: ", res.statusCode);
        if (res.statusCode != 200)
          throw std::runtime_error("unable to get the cache");

        if (res.headers.count("secc-stdout"))
          cout << UriDecode(res.headers["secc-stdout"]);
        if (res.headers.count("secc-stderr"))
          cerr << UriDecode(res.headers["secc-stderr"]);

        string outdir = (option["outfile"].is_null())
                      ? secc_cwd
                      : _dirname(option["outfile"].get<string>());
        LOGI("outdir : ", outdir);


        stringstream tarStream;
        int ret = unzip(res, tarStream);
        if (ret != 0)
          throw secc_exception;

        LOGI("unzip done.");
        ret = untar(tarStream, outdir.c_str());
        if (ret != 0)
          throw secc_exception;

        LOGI("cache done");
        _exit(0);

      }).end();
    } catch(const std::exception &e) {
      LOGE("failed to hit cache.");
      funcDaemonCompile();
    }
  };  // funcDaemonCache

  auto funcJobNew = [&]() {
    // make argv array
    char *childArgv[option["localArgv"].size() + 1 + 1] = {NULL};
    childArgv[0] = new char[secc_driver_path.length()];
    strcpy(childArgv[0], secc_driver_path.c_str());
    for(size_t i = 1; i < option["localArgv"].size(); i++) {
      string str = option["localArgv"][i].get<string>();
      childArgv[i] = new char[str.length()];
      strcpy(childArgv[i], str.c_str());
    }

    LOGI("generating a preprocessed source.");

    //preprocessed file.
    static spawn::SimpleProcessSpawn process(loop, childArgv);
    process.timeout = 10000;
    process.on("error", [](spawn::Error &&error){
      LOGE(error.name);
      LOGE(error.message);
      throw secc_exception; // spawn error or timeout
    })
    .on("response", [&](spawn::Response &&response){
      if (response.exitStatus != 0 ||
         (response.exitStatus == 0 && response.termSignal != 0)) {
        LOGE(response.exitStatus);
        LOGE(response.termSignal);
        LOGE(response.stderr.str());
        throw secc_exception; // something wrong in generating.
      }
      LOGI(response.stdout.tellp());

      size_t totalSize;
      int ret = getZippedStream(response.stdout, infileBuffer, sourceHash, &totalSize);
      if (ret != 0)
        throw secc_exception;

      LOGI("request infile size : ", totalSize);


      //system information
      struct utsname u;
      if (uname(&u) != 0)
        throw secc_exception;


      string hostname = u.nodename;
      string platform = (strcmp(u.sysname,"Linux") == 0)
                        ? "linux"
                        : (strcmp(u.sysname,"Darwin") == 0)
                          ? "darwin"
                          : "unknown";
      string release = u.release;
      string arch = (strcmp(u.machine,"x86_64") == 0) ? "x64" : "unknown"; //FIXME : arm

      string compiler_version = _exec(string(secc_driver_path + " --version").c_str());
      string compiler_dumpversion = _exec(string(secc_driver_path + " -dumpversion").c_str());
      string compiler_dumpmachine = _exec(string(secc_driver_path + " -dumpmachine").c_str());
      compiler_dumpversion = trim(compiler_dumpversion);
      compiler_dumpmachine = trim(compiler_dumpmachine);

      auto data = json::object();
      data["systemInformation"] = json::object();
      data["systemInformation"]["hostname"] = hostname;
      data["systemInformation"]["platform"] = platform;
      data["systemInformation"]["release"] = release;
      data["systemInformation"]["arch"] = arch;
      data["compilerInformation"] = json::object();
      data["compilerInformation"]["version"] = compiler_version;
      data["compilerInformation"]["dumpversion"] = compiler_dumpversion;
      data["compilerInformation"]["dumpmachine"] = compiler_dumpmachine;
      data["mode"] = secc_mode;
      data["projectId"] = option["projectId"];
      data["cachePrefered"] = secc_cache;
      data["crossPrefered"] = secc_cross;
      data["sourcePath"] = option["infile"];
      data["sourceHash"] = *sourceHash;
      data["argvHash"] = option["argvHash"];

      LOGI("REQUEST body /job/new");
      LOGI(data);

      static request::SimpleHttpRequest requestJobNew(loop);
      requestJobNew.timeout = 50000;
      requestJobNew.setHeader("content-type","application/json")
      .post(secc_scheduler_host + "/job/new", data.dump())
      .on("error", [](request::Error&& err){
        throw secc_exception;
      }).on("response", [&](request::Response&& res){
        LOGI("JOB - response status code:", res.statusCode);

        if (res.statusCode != 200)
          throw secc_exception;

        job = json::parse(res.str());
        LOGI(job.dump());

        if (job["local"].get<bool>()) {
          LOGE("useLocal from SCHEDULER /job/new");
          throw secc_exception;
        }

        // FIXME : implement CACHE logic!!
        if (secc_cache && job["cache"].get<bool>()) {
          funcDaemonCache();
        } else {
          funcDaemonCompile();
        }

      }).end();

    })
    .spawn();
  }; //funcJobNew

  auto funcOptionAnalyze = [&]() {
    auto data = json::object();
    data["driver"] = secc_driver;
    data["cwd"] = secc_cwd;
    data["mode"] = secc_mode;
    data["argv"] = secc_argv;

    LOGI(data.dump());

    static request::SimpleHttpRequest requestOptionAnalyze(loop);
    requestOptionAnalyze.timeout = 1000;
    requestOptionAnalyze.setHeader("content-type","application/json")
    .post(secc_scheduler_host + "/option/analyze", data.dump())
    .on("error", [](request::Error&& err){
      throw secc_exception;
    }).on("response", [&](request::Response&& res){
      LOGI("option - response status code: ", res.statusCode);
      if (res.statusCode != 200)
        throw secc_exception;

      option = json::parse(res.str());
      LOGI(option.dump());

      //FIXME : check invalid the outfile dirname
      if (option["useLocal"].get<bool>()) {
        LOGE("useLocal from /option/analyze");
        throw secc_exception;
      }

      funcJobNew();
    })
    .end();
  };  // funcOptionAnalyze

  try
  {
    //quick checks.
    if (secc_scheduler_host.size() == 0) {
      LOGE("secc_scheduler_host error");
      throw secc_exception;
    }
    if (secc_cwd.find("CMakeFiles") != std::string::npos) {
      LOGE("in CMakeFiles");
      throw secc_exception;
    }

    if (!_argv_c_exists) {
      LOGE("-c not exists");
      throw secc_exception;
    }

    funcOptionAnalyze();
  }
  catch (const std::exception &e)
  {
    LOGE("Error exception: ", e.what());
    LOGI("passThrough ", secc_driver_path.c_str());

    //passThrough
    char pathBuf[1024];
    strcpy(pathBuf, secc_driver_path.c_str());
    argv[0] = pathBuf;

    // FIXME : use uv_spawn
    execv(secc_driver_path.c_str(), argv);
  }

  return uv_run(loop, UV_RUN_DEFAULT);
}