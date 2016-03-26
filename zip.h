#pragma once

#include <cpprest/producerconsumerstream.h>
#include <openssl/md5.h>
#include <zlib.h>

#include "log.h"
#include "utils.h"

using namespace concurrency::streams;       // Asynchronous streams

#define CHUNK 512

int getZippedStream(const char* cmd, std::shared_ptr<producer_consumer_buffer<unsigned char>> buf, std::shared_ptr<std::string> hash, size_t *totalSize);
int unzip(concurrency::streams::streambuf<unsigned char> *source, concurrency::streams::streambuf<unsigned char> *dest);