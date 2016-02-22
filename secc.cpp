/*
  SECC Native(c++) Frontend

  dependencies : c++ rest sdk(aka casablanca), zlib, md5
  ubuntu :
  $ sudo apt-get install libcpprest-dev libssl-dev zlib1g-dev
  $ make clean && make
  run :
  $ SECC_ADDRESS="172.17.42.1" [SECC_PORT="10509"] \
    /path/to/secc-native/bin/gcc ...
*/

#include <iostream>
#include <cpprest/http_client.h>
#include <cpprest/producerconsumerstream.h>
#include <sys/utsname.h>

#include "utils.h"

using namespace std;
using namespace web;                        // Common features like URIs.
using namespace web::http;                  // Common HTTP functionality

class _secc_exception : public std::exception
{
  virtual const char* what() const throw()
  {
    return "secc exception";
  }
} secc_exception;

int main(int argc, char* argv[])
{
  //debug
  if (getenv("SECC_CMDLINE")) {
    string cmd = "";
    for(int i = 0; i < argc; i++)
      cmd += "'" + string(argv[i]) + "' ";
    LOGI(cmd);
  }

  // init
  auto option = make_shared<json::value>();
  auto job = make_shared<json::value>();
  auto sourceHash = make_shared<string>();

  auto infileBuffer = make_shared<producer_consumer_buffer<unsigned char>>();
  auto outfileBuffer = make_shared<producer_consumer_buffer<unsigned char>>();

  string secc_scheduler_address = (getenv("SECC_ADDRESS")) ? getenv("SECC_ADDRESS") : "127.0.0.1";
  string secc_scheduler_port = (getenv("SECC_PORT")) ? getenv("SECC_PORT") : "10509";
  string secc_scheduler_host = "http://" + secc_scheduler_address + ":" + secc_scheduler_port; //FIXME : from settings
  string secc_compiler = _basename(argv[0], false);                // FIXME : exception
  string secc_compiler_path = "/usr/bin/" + secc_compiler;  // FIXME : from PATH
  string secc_cwd = getcwd(nullptr, 0);
  string secc_mode = "1";      //FIXME : mode 2
  string secc_cache = (getenv("SECC_CACHE") && strcmp(getenv("SECC_CACHE"),"1") == 0) ? "true" : "false";
  string secc_cross = (getenv("SECC_CROSS") && strcmp(getenv("SECC_CROSS"),"1") == 0) ? "true" : "false";

  bool _argv_c_exists = false;
  json::value secc_argv = json::value::array();
  for(int i = 1; i < argc; i++) {
    if (strcmp(argv[i],"-c") == 0) _argv_c_exists = true;
    secc_argv[i-1] = json::value(argv[i]);
  }

  //task
  pplx::task<void> requestTask = pplx::create_task([=](){
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

    http::client::http_client client(U(secc_scheduler_host));
    uri_builder builder(U("/option/analyze"));

    json::value data = json::value::object();
    data["compiler"] = json::value(secc_compiler);
    data["cwd"] = json::value(secc_cwd);
    data["mode"] = json::value(secc_mode);
    data["argv"] = json::value(secc_argv);

    LOGI("REQUEST body /option/analyze");
    LOGI(data);

    return client.request(methods::POST, builder.to_string(), data);
  })
  .then([=](http_response response)
  {
    LOGI("option - response status code: ", response.status_code());
    if (response.status_code() != 200)
      throw secc_exception;

    return response.extract_json();
  })
  .then([=](json::value result)
  {
    LOGI("RESPONSE body /option/analyze");
    LOGI(result);

    *option = result;

    //FIXME : check invalid the outfile dirname
    if (option->at("useLocal").as_bool()) {
      LOGE("useLocal from /option/analyze");
      throw secc_exception;
    }

    //preprocessed file.
    string cmd = secc_compiler_path;
    for(size_t i = 0; i < option->at("localArgv").size(); i++)
      cmd += " '" + option->at("localArgv")[i].as_string() + "'";

    LOGI("COMMAND for generating a preprocessed source.");
    LOGI(cmd);

    //const char *cmd = "/usr/bin/gcc -c /mnt/sda1/open/secc_test/test/test.c -E";
    size_t totalSize;
    int ret = getZippedStream(cmd.c_str(), infileBuffer, sourceHash, &totalSize);
    if (ret != 0)
      throw secc_exception;

    LOGI("request infile size : ", totalSize);

    return pplx::create_task([=](){
      return 1;
    });
  })
  .then([=](int ret)
  {
    //system information
    struct utsname u;
    if (uname(&u) != 0)
      throw secc_exception;

    string hostname = u.nodename;
    string platform = (strcmp(u.sysname,"Linux") == 0) ? "linux" : "unknown"; //FIXME : darwin
    string release = u.release;
    string arch = (strcmp(u.machine,"x86_64") == 0) ? "x64" : "unknown"; //FIXME : arm

    string compiler_version = _exec(string(secc_compiler_path + " --version").c_str());
    string compiler_dumpversion = _exec(string(secc_compiler_path + " -dumpversion").c_str());
    string compiler_dumpmachine = _exec(string(secc_compiler_path + " -dumpmachine").c_str());
    compiler_dumpversion = trim(compiler_dumpversion);
    compiler_dumpmachine = trim(compiler_dumpmachine);

    json::value data = json::value::object();
    data["systemInformation"] = json::value::object();
    data["systemInformation"]["hostname"] = json::value(hostname);
    data["systemInformation"]["platform"] = json::value(platform);
    data["systemInformation"]["release"] = json::value(release);
    data["systemInformation"]["arch"] = json::value(arch);
    data["compilerInformation"] = json::value::object();
    data["compilerInformation"]["version"] = json::value(compiler_version);
    data["compilerInformation"]["dumpversion"] = json::value(compiler_dumpversion);
    data["compilerInformation"]["dumpmachine"] = json::value(compiler_dumpmachine);
    data["mode"] = json::value(secc_mode);
    data["projectId"] = option->at("projectId");
    data["cachePrefered"] = json::value(secc_cache);
    data["crossPrefered"] = json::value(secc_cross);
    data["sourcePath"] = option->at("infile");
    data["sourceHash"] = json::value(*sourceHash);
    data["argvHash"] = option->at("argvHash");

    LOGI("REQUEST body /job/new");
    LOGI(data);

    http::client::http_client client(U(secc_scheduler_host));
    uri_builder builder(U("/job/new"));

    return client.request(methods::POST, builder.to_string(), data);
  })
  .then([=](http_response response){
    LOGI("JOB - response status code:", response.status_code());

    if (response.status_code() != 200)
      throw secc_exception;

    return response.extract_json();
  })
  .then([=](json::value result)
  {
    LOGI("RESPONSE body /job/new");
    LOGI(result);

    *job = result;

    if (job->at("local").as_bool()) {
      LOGE("useLocal from SCHEDULER /job/new");
      throw secc_exception;
    }

    string secc_daemon_host = "http://" + job->at("daemon")["daemonAddress"].as_string() + ":" + std::to_string(job->at("daemon")["system"]["port"].as_integer());
    string secc_daemon_compile_uri = secc_daemon_host + "/compile/preprocessed/" + job->at("archive")["archiveId"].as_string();

    string secc_filename = option->at("outfile").is_null()
                              ? _basename(option->at("infile").as_string(), true)
                              : _basename(option->at("outfile").as_string(), true);

    http::client::http_client_config client_config;
    client_config.set_timeout(utility::seconds(60));
    http::client::http_client client(U(secc_daemon_compile_uri), client_config);
    http_request msg(methods::POST);
    msg.headers().add(U("Content-Encoding"), U("gzip"));
    msg.headers().add(U("secc-jobid"), job->at("jobId"));
    msg.headers().add(U("secc-compiler"), secc_compiler);
    msg.headers().add(U("secc-language"), option->at("language"));
    msg.headers().add(U("secc-argv"), option->at("remoteArgv"));
    msg.headers().add(U("secc-filename"), secc_filename);
    msg.headers().add(U("secc-cross"), secc_cross);
    msg.headers().add(U("secc-target"), U("x86_64-linux-gnu")); // FIXME : from system

    LOGI("REQUEST headers ",secc_daemon_compile_uri);
    LOGI(msg.to_string());

    msg.set_body(infileBuffer->create_istream());

    return client.request(msg);
  })
  .then([=](http_response response)
  {
    //check secc-code
    LOGI("compile - response status code: ", response.status_code());
    if (response.status_code() != 200)
      throw secc_exception;

    LOGI("RESPONSE headers ");
    LOGI(response.to_string());

    return response.body().read_to_end(*outfileBuffer);
  })
  .then([=](size_t c)
  {
    LOGI("response outfile size : ", c);

    return outfileBuffer->close(std::ios_base::out);
  })
  .then([=](){
    string outdir = (option->at("outfile").is_null())
                  ? secc_cwd
                  : _dirname(option->at("outfile").as_string());

    auto is = outfileBuffer->create_istream();
    concurrency::streams::streambuf<char> streambuf = is.streambuf();
    LOGI("outdir : ", outdir);

    int ret = untar(&streambuf, outdir.c_str());
    if (ret != 0)
      throw secc_exception;

    LOGI("done");
  });

  try
  {
    requestTask.wait();
  }
  catch (const std::exception &e)
  {
    LOGE("Error exception: ", e.what());
    LOGI("passThrough ", secc_compiler_path.c_str());

    //passThrough
    strcpy(argv[0], secc_compiler_path.c_str());
    execv(secc_compiler_path.c_str(), argv);
  }

  return 0;
}