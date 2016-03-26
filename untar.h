#pragma once

#include "log.h"

#include <cpprest/producerconsumerstream.h>

/* This is for mkdir(); this may need to be changed for some platforms. */
#include <sys/stat.h>  /* For mkdir() */

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