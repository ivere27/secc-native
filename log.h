#pragma once

#include <fstream>
#include <iostream>
#include <unistd.h>
#include <stdlib.h>

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