#include "utils.h"

/* Parse an octal number, ignoring leading and trailing nonsense. */
int
parseoct(const char *p, size_t n)
{
  int i = 0;

  while ((*p < '0' || *p > '7') && n > 0) {
    ++p;
    --n;
  }
  while (*p >= '0' && *p <= '7' && n > 0) {
    i *= 8;
    i += *p - '0';
    ++p;
    --n;
  }
  return (i);
}

/* Returns true if this is 512 zero bytes. */
int
is_end_of_archive(const char *p)
{
  int n;
  for (n = 511; n >= 0; --n)
    if (p[n] != '\0')
      return (0);
  return (1);
}

/* Create a directory, including parent directories as necessary. */
void
create_dir(char *pathname, int mode)
{
  char *p;
  int r;

  /* Strip trailing '/' */
  if (pathname[strlen(pathname) - 1] == '/')
    pathname[strlen(pathname) - 1] = '\0';

  /* Try creating the directory. */
  r = mkdir(pathname, mode);

  if (r != 0) {
    /* On failure, try creating parent directory. */
    p = strrchr(pathname, '/');
    if (p != NULL) {
      *p = '\0';
      create_dir(pathname, 0755);
      *p = '/';
      r = mkdir(pathname, mode);
    }
  }
  if (r != 0)
    LOGE("Could not create directory ", pathname, "\n");
}

/* Create a file, including parent directory as necessary. */
FILE *
create_file(char *pathname, int mode)
{
  FILE *f;
  f = fopen(pathname, "wb+");
  if (f == NULL) {
    /* Try creating parent dir and then creating file. */
    char *p = strrchr(pathname, '/');
    if (p != NULL) {
      *p = '\0';
      create_dir(pathname, 0755);
      *p = '/';
      f = fopen(pathname, "wb+");
    }
  }
  return (f);
}

/* Verify the tar checksum. */
int
verify_checksum(const char *p)
{
  int n, u = 0;
  for (n = 0; n < 512; ++n) {
    if (n < 148 || n > 155)
      /* Standard tar checksum adds unsigned bytes. */
      u += ((unsigned char *)p)[n];
    else
      u += 0x20;

  }
  return (u == parseoct(p + 148, 8));
}

/* Extract a tar archive. */
int
untar(concurrency::streams::streambuf<char> *a, const char *directory)
{
  char buff[512];
  char newPath[512];
  FILE *f = NULL;
  size_t bytes_read;
  int filesize;

  LOGI("Extracting to ", directory);
  for (;;) {
    bytes_read = a->getn(buff, sizeof buff).get();
    if (bytes_read < 512) {
      LOGE("Short read. expected 512, got ", (int)bytes_read);
      return -1;
    }
    if (is_end_of_archive(buff)) {
      LOGI("End of file");
      return 0;
    }
    if (!verify_checksum(buff)) {
      LOGE("Checksum failure");
      return -1;
    }
    filesize = parseoct(buff + 124, 12);
    switch (buff[156]) {
    case '1':
      LOGI(" Ignoring hardlink ", buff);
      break;
    case '2':
      LOGI(" Ignoring symlink ", buff);
      break;
    case '3':
      LOGI(" Ignoring character device ", buff);
      break;
    case '4':
      LOGI(" Ignoring block device ", buff);
      break;
    case '5':
      LOGI(" Extracting dir ", buff);
      sprintf(newPath, "%s%s%s", directory, ((directory[strlen(directory) -1] == '/') ? "" : "/"), buff);
      create_dir(newPath, parseoct(buff + 100, 8));
      filesize = 0;
      break;
    case '6':
      LOGI(" Ignoring FIFO ", buff);
      break;
    default:
      LOGI(" Extracting file ", buff);
      sprintf(newPath, "%s%s%s", directory, ((directory[strlen(directory) -1] == '/') ? "" : "/"), buff);
      f = create_file(newPath, parseoct(buff + 100, 8));
      break;
    }
    while (filesize > 0) {
      bytes_read = a->getn(buff, sizeof buff).get();
      if (bytes_read < 512) {
        LOGE("Short read. Expected 512, got ",(int)bytes_read);
        return -1;
      }
      if (filesize < 512)
        bytes_read = filesize;
      if (f != NULL) {
        if (fwrite(buff, 1, bytes_read, f) != bytes_read)
        {
          LOGE("Failed write");
          fclose(f);
          f = NULL;

          return -1;
        }
      }
      filesize -= bytes_read;
    }
    if (f != NULL) {
      fclose(f);
      f = NULL;
    }
  }
}

int getZippedStream(const char* cmd, std::shared_ptr<producer_consumer_buffer<unsigned char>> buf, std::shared_ptr<std::string> hash, size_t *totalSize)
{
  *totalSize = 0;
  MD5state_st md5ctx;
  MD5_Init(&md5ctx);

  int ret, flush;
  unsigned have;
  z_stream strm;

  strm.zalloc = Z_NULL;
  strm.zfree = Z_NULL;
  strm.opaque = Z_NULL;
  ret = deflateInit2(&strm, Z_DEFAULT_COMPRESSION, Z_DEFLATED, MAX_WBITS | 16, 8, Z_DEFAULT_STRATEGY); //gzip(MAX_WBITS | 16), memLevel(8=defaults)
  if (ret != Z_OK)
    return ret;

  char in[CHUNK];
  Bytef out[CHUNK];
  FILE *fp = popen(cmd, "r");
  if (!fp) return 1;

  do {
    int len = fread(in, 1, CHUNK, fp);
    MD5_Update(&md5ctx, in, len);
    //fwrite(in, 1, len, stdout); //debug output

    if(ferror(fp)) {
      (void)deflateEnd(&strm);
      return Z_ERRNO;
    }
    flush = feof(fp) ? Z_FINISH : Z_NO_FLUSH;
    strm.avail_in = len;
    strm.next_in = (Bytef*)in;

    do {
      strm.avail_out = CHUNK;
      strm.next_out = out;
      ret = deflate(&strm, flush);
      if (ret == Z_STREAM_ERROR)
        return Z_STREAM_ERROR;
      have = CHUNK - strm.avail_out;

      size_t size = buf->putn(out, have).get();  //FIXME : buf->putn_nocopy(out, have).get();
      if (size != have)
        return Z_ERRNO;

      *totalSize += size;
      // debug gziped output
      // if (fwrite(out, 1, have, stdout) != have || ferror(stdout)) {
      //   (void)deflateEnd(&strm);
      //   return Z_ERRNO;
      // }
    } while(strm.avail_out == 0);
  } while (flush != Z_FINISH);
  (void)deflateEnd(&strm);
  buf->close(std::ios_base::out).get();

  unsigned char md5[16];
  MD5_Final(md5, &md5ctx);
  char hex[32];
  for(int i = 0; i<16; i++)
    sprintf(&hex[i*2], "%02x",md5[i]);

  *hash = hex;

  return 0;
}

/* Decompress from file source to file dest until stream ends or EOF.
   inf() returns Z_OK on success, Z_MEM_ERROR if memory could not be
   allocated for processing, Z_DATA_ERROR if the deflate data is
   invalid or incomplete, Z_VERSION_ERROR if the version of zlib.h and
   the version of the library linked do not match, or Z_ERRNO if there
   is an error reading or writing the files. */
int unzip(concurrency::streams::streambuf<unsigned char> *source, concurrency::streams::streambuf<unsigned char> *dest)
{
    int ret;
    unsigned have;
    z_stream strm;
    unsigned char in[CHUNK];
    unsigned char out[CHUNK];

    /* allocate inflate state */
    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;
    strm.opaque = Z_NULL;
    strm.avail_in = 0;
    strm.next_in = Z_NULL;
    ret = inflateInit2(&strm, MAX_WBITS | 16);  //gzip(MAX_WBITS | 16)
    if (ret != Z_OK)
        return ret;

    /* decompress until deflate stream ends or end of file */
    do {
        strm.avail_in = source->getn(in, sizeof in).get(); // fread(in, 1, CHUNK, source);
        // FIXME : error handling.
        // if (ferror(source)) {
        //     (void)inflateEnd(&strm);
        //     return Z_ERRNO;
        // }
        if (strm.avail_in == 0)
            break;
        strm.next_in = in;

        /* run inflate() on input until output buffer not full */
        do {
            strm.avail_out = CHUNK;
            strm.next_out = out;
            ret = inflate(&strm, Z_NO_FLUSH);
            assert(ret != Z_STREAM_ERROR);  /* state not clobbered */
            switch (ret) {
            case Z_NEED_DICT:
                ret = Z_DATA_ERROR;     /* and fall through */
            case Z_DATA_ERROR:
            case Z_MEM_ERROR:
                (void)inflateEnd(&strm);
                return ret;
            }
            have = CHUNK - strm.avail_out;
            // FIXME : ERROR handling
            //if (fwrite(out, 1, have, dest) != have || ferror(dest)) {
            if (dest->putn(out, have).get() != have) {
                (void)inflateEnd(&strm);
                return Z_ERRNO;
            }
        } while (strm.avail_out == 0);

        /* done when inflate() says it's done */
    } while (ret != Z_STREAM_END);

    /* clean up and return */
    (void)inflateEnd(&strm);
    return ret == Z_STREAM_END ? Z_OK : Z_DATA_ERROR;
}


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
