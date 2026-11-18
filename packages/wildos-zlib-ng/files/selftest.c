/* Packs and unpacks a buffer through whichever libz.so.1 the loader gives
 * this program, at every level, and checks the stream calls programs rely
 * on. Exit 0 only if every byte comes back and the library is zlib-ng. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#define N (1 << 20)

int main(void)
{
    unsigned char *in = malloc(N), *out = malloc(N);
    uLong bound = compressBound(N);
    unsigned char *packed = malloc(bound);
    unsigned seed = 12345;
    int level;

    if (!in || !out || !packed)
        return 2;
    /* Half noise, half repeats, so both the match finder and the literal
     * path are used. */
    for (size_t i = 0; i < N; i++) {
        seed = seed * 1103515245u + 12345u;
        in[i] = i < N / 2 ? (unsigned char)(seed >> 16) : (unsigned char)("wildos"[i % 6]);
    }

    for (level = 0; level <= 9; level++) {
        uLongf plen = bound, olen = N;
        if (compress2(packed, &plen, in, N, level) != Z_OK)
            return 3;
        if (uncompress(out, &olen, packed, plen) != Z_OK || olen != N || memcmp(in, out, N))
            return 4;
    }

    {
        z_stream s;
        uLong plen;

        memset(&s, 0, sizeof s);
        /* gzip framing, as web servers and git's HTTP transport use. */
        if (deflateInit2(&s, 6, Z_DEFLATED, 15 + 16, 8, Z_DEFAULT_STRATEGY) != Z_OK)
            return 5;
        s.next_in = in; s.avail_in = N; s.next_out = packed; s.avail_out = bound;
        if (deflate(&s, Z_FINISH) != Z_STREAM_END)
            return 6;
        plen = s.total_out;
        deflateEnd(&s);
        memset(&s, 0, sizeof s);
        if (inflateInit2(&s, 15 + 32) != Z_OK)
            return 7;
        s.next_in = packed; s.avail_in = plen; s.next_out = out; s.avail_out = N;
        if (inflate(&s, Z_FINISH) != Z_STREAM_END || s.total_out != N || memcmp(in, out, N))
            return 8;
        inflateEnd(&s);
    }

    if (crc32(0L, in, N) != crc32_z(0L, in, N) || adler32(1L, in, 16) == 1L)
        return 9;
    if (!strstr(zlibVersion(), "zlib-ng"))
        return 10;
    puts(zlibVersion());
    return 0;
}
