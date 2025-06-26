#ifndef _COMPRESS_H_
#define _COMPRESS_H_
#include "zlib.h"

// should manually free returned buffer after use
inline uint8_t* defl(const uint8_t* source, size_t src_len, size_t* out_len, int level)
{
    int ret;
    unsigned have;
    z_stream strm;
    int out_buf_len = src_len + 16;
    *out_len = 0;
    unsigned char* out = (unsigned char*)malloc(out_buf_len);
    if (!out) {
        printf("malloc failed not enough memory:%d\n", out_buf_len);
        return nullptr;
    }
    /* allocate deflate state */
    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;
    strm.opaque = Z_NULL;
    ret = deflateInit(&strm, level);
    if (ret != Z_OK)
        return nullptr;
    strm.avail_in = src_len;
    strm.next_in = (unsigned char*)source;
    /* run deflate() on input until output buffer not full, finish
        compression if all of source has been read in */
    do {
        strm.avail_out = out_buf_len;
        strm.next_out = out;
        ret = deflate(&strm, Z_FINISH);    /* no bad return value */
        assert(ret != Z_STREAM_ERROR);  /* state not clobbered */
        have = out_buf_len - strm.avail_out;
        *out_len += have;
    } while (strm.avail_out == 0);
    assert(strm.avail_in == 0);     /* all input will be used */
    assert(ret == Z_STREAM_END);        /* stream will be complete */
    /* clean up and return */
    (void)deflateEnd(&strm);
    return out;
}

#define MAX_DEC_CACHE_LEN  1024 * 32
// should manually free returned buffer after use, max cache buffer size is 32k
inline uint8_t* infl(const uint8_t *source, size_t src_len, size_t* out_len)
{
    int ret;
    unsigned have;
    z_stream strm;
    *out_len = 0;
    int out_buf_len = src_len * 3; //let's say 3x compression at most
    if (out_buf_len > MAX_DEC_CACHE_LEN) {
        out_buf_len = MAX_DEC_CACHE_LEN;
    }
    unsigned char *out = (unsigned char*)malloc(out_buf_len);
    if (!out) {
        printf("malloc failed not enough memory:%d\n", out_buf_len);
        return nullptr;
    }
    /* allocate inflate state */
    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;
    strm.opaque = Z_NULL;
    strm.avail_in = 0;
    strm.next_in = Z_NULL;
    ret = inflateInit(&strm);
    if (ret != Z_OK)
        return nullptr;
    /* decompress until deflate stream ends */
    do {
        strm.avail_in = src_len;
        if (strm.avail_in == 0)
            break;
        strm.next_in = (unsigned char*)source;
        /* run inflate() on input until output buffer not full */
        do {
            strm.avail_out = out_buf_len;
            strm.next_out = out;
            ret = inflate(&strm, Z_NO_FLUSH);
            assert(ret != Z_STREAM_ERROR);  /* state not clobbered */
            switch (ret) {
            case Z_NEED_DICT:
                ret = Z_DATA_ERROR;     /* and fall through */
            case Z_DATA_ERROR:
            case Z_MEM_ERROR:
                (void)inflateEnd(&strm);
                return nullptr;
            }
            have = out_buf_len - strm.avail_out;
            *out_len += have;
        } while (strm.avail_out == 0);
        /* done when inflate() says it's done */
    } while (ret != Z_STREAM_END);
    /* clean up and return */
    (void)inflateEnd(&strm);
    return out;
}

#endif