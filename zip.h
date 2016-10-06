#pragma once

#include <openssl/md5.h>
#include <zlib.h>

#include "utils.h"

#define CHUNK 512


int getZippedStream(const char* cmd, stringstream& buf, std::shared_ptr<std::string> hash, size_t *totalSize)
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

      size_t size = buf.rdbuf()->sputn((const char*)out, have);  // FIXME :: Bytef to const char*
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

  //buf->close(); // FIXME : not need

  unsigned char md5[16];
  MD5_Final(md5, &md5ctx);
  char hex[32];
  for(int i = 0; i<16; i++)
    sprintf(&hex[i*2], "%02x",md5[i]);

  *hash = hex;

  return pclose(fp);
}

/* Decompress from file source to file dest until stream ends or EOF.
   inf() returns Z_OK on success, Z_MEM_ERROR if memory could not be
   allocated for processing, Z_DATA_ERROR if the deflate data is
   invalid or incomplete, Z_VERSION_ERROR if the version of zlib.h and
   the version of the library linked do not match, or Z_ERRNO if there
   is an error reading or writing the files. */
int unzip(stringstream &source, stringstream &dest)
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
    strm.avail_in = source.rdbuf()->sgetn((char*)in, sizeof in);

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

      // FIXME : unsigned char to  char*
      if (dest.rdbuf()->sputn((char*)out, have) != have) {
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
