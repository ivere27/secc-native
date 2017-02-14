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

struct SECC {
  string scheduler_address, scheduler_port, scheduler_host, driver, driver_path, cwd, mode;
  bool cache, cross;
  char** argv;
};

static uv_loop_t* loop = uv_default_loop();

int main(int argc, char* argv[])
{
  SECC *secc = new SECC();
  secc->argv = argv;

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

  secc->scheduler_address = (getenv("SECC_ADDRESS")) ? getenv("SECC_ADDRESS") : "127.0.0.1";
  secc->scheduler_port = (getenv("SECC_PORT")) ? getenv("SECC_PORT") : "10509";
  secc->scheduler_host = "http://" + secc->scheduler_address + ":" + secc->scheduler_port; //FIXME : from settings
  secc->driver = _basename(argv[0], false);                // FIXME : exception
  secc->driver_path = "/usr/bin/" + secc->driver;  // FIXME : from PATH
  secc->cwd = getcwd(nullptr, 0);
  secc->mode = "1";      //FIXME : mode 2
  secc->cache = (getenv("SECC_CACHE") && strcmp(getenv("SECC_CACHE"),"1") == 0) ? true : false;
  secc->cross = (getenv("SECC_CROSS") && strcmp(getenv("SECC_CROSS"),"1") == 0) ? true : false;

  bool _argv_c_exists = false;
  auto secc_argv = json::array();
  for(int i = 1; i < argc; i++) {
    if (strcmp(argv[i],"-c") == 0) _argv_c_exists = true;
    secc_argv[i-1] = argv[i];
  }


  auto passThrough = [&]() {
    LOGE("passThrough");
    static uv_work_t work_req;
    work_req.data = (void*)secc;
    int r = uv_queue_work(loop, &work_req,
      [](uv_work_t* req) {},
      [](uv_work_t* req, int status) {
        SECC *secc = (SECC*)(work_req.data);
        ASSERT(req);
        ASSERT(status == 0);

        // FIXME : check the driver's path in PATH ENV
        char* pathBuf = new char[secc->driver_path.length()];
        strcpy(pathBuf, secc->driver_path.c_str());
        secc->argv[0] = pathBuf;

        //passThrough
        static spawn::SimpleProcessSpawn processPassThrough(loop, secc->argv);
        processPassThrough.timeout = 60*60*1000;
        processPassThrough.on("error", [](spawn::Error &&error){
          LOGE(error.name);
          LOGE(error.message);
        })
        .on("response", [&](spawn::Response &&response){
          cout << response.stdout.str();
          cerr << response.stderr.str();

          _exit(response.exitStatus);
        })
        .spawn();
      }
    );
    ASSERT(r==0);
  };

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
    .setHeader("x-secc-jobid", to_string(job["jobId"].get<int>()))
    .setHeader("x-secc-driver", secc->driver)
    .setHeader("x-secc-language", option["language"])
    .setHeader("x-secc-argv", option["remoteArgv"].dump())
    .setHeader("x-secc-filename", secc_filename)
    .setHeader("x-secc-outfile", option["outfile"].is_null() ? "null" : option["outfile"].get<string>())
    .setHeader("x-secc-cross", secc->cross ? "true" : "false")
    .setHeader("x-secc-target", "x86_64-linux-gnu") // FIXME : from system
    .post(secc_daemon_compile_uri, infileBuffer.str())
    .on("error", [&](request::Error&& err){
      return passThrough();
    });
    request.on("response", [&](request::Response&& res){
      //check x-secc-code
      LOGI("compile - response status code: ", res.statusCode);
      if ( res.statusCode != 200
        || res.headers["x-secc-code"].compare("0") != 0) {
        return passThrough();
      }

      if (res.headers.count("x-secc-stdout"))
        cout << UriDecode(res.headers["x-secc-stdout"]);
      if (res.headers.count("x-secc-stderr"))
        cerr << UriDecode(res.headers["x-secc-stderr"]);

      string outdir = (option["outfile"].is_null())
                    ? secc->cwd
                    : _dirname(option["outfile"].get<string>());
      LOGI("outdir : ", outdir);


      stringstream tarStream;

      int ret = unzip(res, tarStream);
      if (ret != 0) {
        return passThrough();
      }

      LOGI("unzip done.");
      ret = untar(tarStream, outdir.c_str());
      if (ret != 0) {
        return passThrough();
      }

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
      .on("error", [&](request::Error&& err){
        return passThrough();
      }).on("response", [&](request::Response&& res){
        //check x-secc-code
        LOGI("cache - response status code: ", res.statusCode);
        if (res.statusCode != 200) {
          LOGE("unable to get the cache");
          return passThrough();
        }

        if (res.headers.count("x-secc-stdout"))
          cout << UriDecode(res.headers["x-secc-stdout"]);
        if (res.headers.count("x-secc-stderr"))
          cerr << UriDecode(res.headers["x-secc-stderr"]);

        string outdir = (option["outfile"].is_null())
                      ? secc->cwd
                      : _dirname(option["outfile"].get<string>());
        LOGI("outdir : ", outdir);


        stringstream tarStream;
        int ret = unzip(res, tarStream);
        if (ret != 0) {
          return passThrough();
        }

        LOGI("unzip done.");
        ret = untar(tarStream, outdir.c_str());
        if (ret != 0) {
          return passThrough();
        }

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
    childArgv[0] = new char[secc->driver_path.length()];
    strcpy(childArgv[0], secc->driver_path.c_str());
    for(size_t i = 1; i < option["localArgv"].size(); i++) {
      string str = option["localArgv"][i].get<string>();
      childArgv[i] = new char[str.length()];
      strcpy(childArgv[i], str.c_str());
    }

    LOGI("generating a preprocessed source.");

    //preprocessed file.
    static spawn::SimpleProcessSpawn process(loop, childArgv);
    process.timeout = 10000;
    process.on("error", [&](spawn::Error &&error){
      LOGE(error.name);
      LOGE(error.message);
      return passThrough();
    })
    .on("response", [&](spawn::Response &&response){
      if (response.exitStatus != 0 ||
         (response.exitStatus == 0 && response.termSignal != 0)) {
        LOGE(response.exitStatus);
        LOGE(response.termSignal);
        LOGE(response.stderr.str());
        return passThrough(); // something wrong in generating.
      }
      LOGI(response.stdout.tellp());

      size_t totalSize;
      int ret = getZippedStream(response.stdout, infileBuffer, sourceHash, &totalSize);
      if (ret != 0) {
        return passThrough();
      }

      LOGI("request infile size : ", totalSize);


      //system information
      struct utsname u;
      if (uname(&u) != 0) {
        return passThrough();
      }


      string hostname = u.nodename;
      string platform = (strcmp(u.sysname,"Linux") == 0)
                        ? "linux"
                        : (strcmp(u.sysname,"Darwin") == 0)
                          ? "darwin"
                          : "unknown";
      string release = u.release;
      string arch = (strcmp(u.machine,"x86_64") == 0) ? "x64" : "unknown"; //FIXME : arm

      string compiler_version = _exec(string(secc->driver_path + " --version").c_str());
      string compiler_dumpversion = _exec(string(secc->driver_path + " -dumpversion").c_str());
      string compiler_dumpmachine = _exec(string(secc->driver_path + " -dumpmachine").c_str());
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
      data["mode"] = secc->mode;
      data["projectId"] = option["projectId"];
      data["cachePrefered"] = secc->cache;
      data["crossPrefered"] = secc->cross;
      data["sourcePath"] = option["infile"];
      data["sourceHash"] = *sourceHash;
      data["argvHash"] = option["argvHash"];

      LOGI("REQUEST body /job/new");
      LOGI(data);

      static request::SimpleHttpRequest requestJobNew(loop);
      requestJobNew.timeout = 50000;
      requestJobNew.setHeader("content-type","application/json")
      .post(secc->scheduler_host + "/job/new", data.dump())
      .on("error", [&](request::Error&& err){
        return passThrough();
      }).on("response", [&](request::Response&& res){
        LOGI("JOB - response status code:", res.statusCode);

        if (res.statusCode != 200) {
          return passThrough();
        }

        job = json::parse(res.str());
        LOGI(job.dump());

        if (job["local"].get<bool>()) {
          LOGE("useLocal from SCHEDULER /job/new");
          return passThrough();
        }

        // FIXME : implement CACHE logic!!
        if (secc->cache && job["cache"].get<bool>()) {
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
    data["driver"] = secc->driver;
    data["cwd"] = secc->cwd;
    data["mode"] = secc->mode;
    data["argv"] = secc_argv;

    LOGI(data.dump());
    static request::SimpleHttpRequest requestOptionAnalyze(loop);
    requestOptionAnalyze.timeout = 1000;
    requestOptionAnalyze.setHeader("content-type","application/json")
    .post(secc->scheduler_host + "/option/analyze", data.dump())
    .on("error", [&](request::Error&& err){
      LOGE("error on /option/analyze");
      return passThrough();
    }).on("response", [&](request::Response&& res){
      LOGI("option - response status code: ", res.statusCode);
      if (res.statusCode != 200) {
        return passThrough();
      }

      option = json::parse(res.str());
      LOGI(option.dump());

      //FIXME : check invalid the outfile dirname
      if (option["useLocal"].get<bool>()) {
        LOGE("useLocal from /option/analyze");
        return passThrough();
      }

      funcJobNew();
    })
    .end();
  };  // funcOptionAnalyze

  try
  {
    //quick checks.
    if (secc->scheduler_host.size() == 0) {
      LOGE("secc_scheduler_host error");
      throw secc_exception;
    }
    if (secc->cwd.find("CMakeFiles") != std::string::npos) {
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
    LOGI("passThrough ", secc->driver_path.c_str());

    //passThrough
    passThrough();
  }

  return uv_run(loop, UV_RUN_DEFAULT);
}