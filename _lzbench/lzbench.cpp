/*
(C) 2011-2015 by Przemyslaw Skibinski (inikep@gmail.com)

    LICENSE

    This program is free software; you can redistribute it and/or
    modify it under the terms of the GNU General Public License as
    published by the Free Software Foundation; either version 3 of
    the License, or (at your option) any later version.

    This program is distributed in the hope that it will be useful, but
    WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    General Public License for more details at
    Visit <http://www.gnu.org/copyleft/gpl.html>.

*/

#define _CRT_SECURE_NO_WARNINGS
#define PROGNAME "lzbench"
#define PROGVERSION "0.7.2"
#define LZBENCH_DEBUG(fmt, args...) ;// printf(fmt, ##args)

#define MAX(a,b) ((a)>(b))?(a):(b)
#ifndef MIN
	#define MIN(a,b) ((a)<(b)?(a):(b))
#endif

#if defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(WIN64) || defined(_WIN64)
	#define WINDOWS
#endif

#define __STDC_FORMAT_MACROS // now PRIu64 will work
#include <inttypes.h> // now PRIu64 will work
#define _FILE_OFFSET_BITS 64  // turn off_t into a 64-bit type for ftello() and fseeko()

#include <vector>
#include <numeric>
#include <algorithm> // sort
#include <stdlib.h> 
#include <stdio.h> 
#include <stdint.h> 
#include <string.h> 
#include "compressors.h"
#include "pithy/pithy.h"
#include "lzo/lzo1b.h"
#include "yappy/yappy.hpp"
#include "quicklz/quicklz.h"
#include "wflz/wfLZ.h"
extern "C"
{
	#include "lzrw/lzrw.h"
}

#ifdef WINDOWS
	#include <windows.h>
	#define InitTimer(x) if (!QueryPerformanceFrequency(&x)) { printf("QueryPerformance not present"); };
	#define GetTime(x) QueryPerformanceCounter(&x); 
	#define GetDiffTime(ticksPerSecond, start_ticks, end_ticks) (1000*(end_ticks.QuadPart - start_ticks.QuadPart)/ticksPerSecond.QuadPart)
	void uni_sleep(UINT usec) { Sleep(usec); };
	#ifndef __GNUC__
		#define fseeko64 _fseeki64 
		#define ftello64 _ftelli64
	#endif
	#define PROGOS "Windows"
#else
	#include <time.h>   
	#include <unistd.h>
	#include <sys/resource.h>
	typedef struct timespec LARGE_INTEGER;
	#define InitTimer(x) 
	#define GetTime(x) if(clock_gettime( CLOCK_REALTIME, &x) == -1 ){ printf("clock_gettime error"); };
	#define GetDiffTime(ticksPerSecond, start_ticks, end_ticks) (1000*( end_ticks.tv_sec - start_ticks.tv_sec ) + ( end_ticks.tv_nsec - start_ticks.tv_nsec )/1000000)
	void uni_sleep(uint32_t usec) { usleep(usec * 1000); };
	#define PROGOS "Linux"
#endif

#define ITERS(count) for(int ii=0; ii<count; ii++)

bool show_full_stats = false;
bool turbobench_format = false;

void print_stats(const char* func_name, int level, std::vector<uint32_t> &ctime, std::vector<uint32_t> &dtime, uint32_t insize, uint32_t outsize, bool decomp_error, int cspeed)
{
    std::sort(ctime.begin(), ctime.end());
    std::sort(dtime.begin(), dtime.end());

    uint32_t cmili_fastest = ctime[0];
    uint32_t dmili_fastest = dtime[0];
    uint32_t cmili_med = ctime[ctime.size()/2];
    uint32_t dmili_med = dtime[dtime.size()/2];
    uint32_t cmili_avg = std::accumulate(ctime.begin(),ctime.end(),0) / ctime.size();
    uint32_t dmili_avg = std::accumulate(dtime.begin(),dtime.end(),0) / dtime.size();

    if (cmili_fastest == 0) cmili_fastest = 1;
    if (dmili_fastest == 0) dmili_fastest = 1;
    if (cmili_med == 0) cmili_med = 1;
    if (dmili_med == 0) dmili_med = 1;
    if (cmili_avg == 0) cmili_avg = 1;
    if (dmili_avg == 0) dmili_avg = 1;

    if (cspeed > insize / cmili_fastest / 1024) { LZBENCH_DEBUG("%s FULL slower than %d MB/s, %d\n", func_name, insize / cmili_fastest / 1024); return; } 

    char desc[256];
    snprintf(desc, sizeof(desc), "%s", func_name);

    int len = strlen(func_name);
    if (level > 0 && len >= 2)
    {
        desc[len - 1] += level % 10;
        desc[len - 2] += level / 10;
    }

    if (show_full_stats)
    {
        printf("%-19s fastest %d ms (%d MB/s), %d, %d ms (%d MB/s)\n", desc, cmili_fastest, insize / cmili_fastest / 1024, outsize, dmili_fastest, insize / dmili_fastest / 1024);
        printf("%-19s median  %d ms (%d MB/s), %d, %d ms (%d MB/s)\n", desc, cmili_med, insize / cmili_med / 1024, outsize, dmili_med, insize / dmili_med / 1024);
        printf("%-19s average %d ms (%d MB/s), %d, %d ms (%d MB/s)\n", desc, cmili_avg, insize / cmili_avg / 1024, outsize, dmili_avg, insize / dmili_avg / 1024);
    }
    else
    {
        if (turbobench_format)
            printf("%12d%6.1f%9.2f%9.2f  %s\n", outsize, outsize * 100.0/ insize, insize / cmili_fastest / 1024.0, insize / dmili_fastest / 1024.0, desc);
        else
        {
            printf("| %-27s ", desc);
            if (insize / cmili_fastest / 1024 < 10) printf("|%6.2f MB/s ", insize / cmili_fastest / 1024.0); else printf("|%6d MB/s ", insize / cmili_fastest / 1024);
            if (decomp_error)
                printf("|      ERROR ");
            else
                if (insize / dmili_fastest / 1024 < 10) printf("|%6.2f MB/s ", insize / dmili_fastest / 1024.0); else printf("|%6d MB/s ", insize / dmili_fastest / 1024); 
            printf("|%12d |%6.2f |\n", outsize, outsize * 100.0/ insize);
        }
    }

    ctime.clear();
    dtime.clear();
};


size_t common(uint8_t *p1, uint8_t *p2)
{
	size_t size = 0;

	while (*(p1++) == *(p2++))
        size++;

	return size;
}


void add_time(std::vector<uint32_t> &ctime, std::vector<uint32_t> &dtime, uint32_t comp_time, uint32_t decomp_time)
{
	ctime.push_back(comp_time);
	dtime.push_back(decomp_time);
}


int64_t lzbench_compress(compress_func compress, size_t chunk_size, std::vector<size_t> &compr_lens, uint8_t *inbuf, size_t insize, uint8_t *outbuf, size_t outsize, size_t param1, size_t param2, size_t param3)
{
    int64_t clen;
    size_t part, sum = 0;
    uint8_t *start = inbuf;
    compr_lens.clear();
    
    while (insize > 0)
    {
        part = MIN(insize, chunk_size);
        clen = compress((char*)inbuf, part, (char*)outbuf, outsize, param1, param2, param3);
		LZBENCH_DEBUG("part=%lld clen=%lld in=%d\n", part, clen, inbuf-start);

        if (clen <= 0 || clen == part)
        {
            memcpy(outbuf, inbuf, part);
            clen = part;
        }
        
        inbuf += part;
        insize -= part;
        outbuf += clen;
        outsize -= clen;
        compr_lens.push_back(clen);
        sum += clen;
    }
    return sum;
}


int64_t lzbench_decompress(compress_func decompress, size_t chunk_size, std::vector<size_t> &compr_lens, uint8_t *inbuf, size_t insize, uint8_t *outbuf, size_t outsize, uint8_t *origbuf, size_t param1, size_t param2, size_t param3)
{
    int64_t dlen;
    int num=0;
    size_t part, sum = 0;
    uint8_t *outstart = outbuf;

    while (insize > 0)
    {
        part = compr_lens[num++];
        if (part > insize) return 0;
        if (part == MIN(chunk_size,outsize)) // uncompressed
        {
            memcpy(outbuf, inbuf, part);
            dlen = part;
        }
        else
        {
            dlen = decompress((char*)inbuf, part, (char*)outbuf, MIN(chunk_size,outsize), param1, param2, param3);
        }
		LZBENCH_DEBUG("part=%lld dlen=%lld out=%d\n", part, dlen, outbuf - outstart);
        if (dlen <= 0) return dlen;

        inbuf += part;
        insize -= part;
        outbuf += dlen;
        outsize -= dlen;
        sum += dlen;
    }
    
    return sum;
}


void lzbench_test(const char* func_name, int level, compress_func compress, compress_func decompress, int cspeed, size_t chunk_size, int iters, uint8_t *inbuf, size_t insize, uint8_t *compbuf, size_t comprsize, uint8_t *decomp, LARGE_INTEGER ticksPerSecond, size_t param1, size_t param2, size_t param3)
{
    LARGE_INTEGER start_ticks, mid_ticks, end_ticks, start_all;
    int64_t complen, decomplen;
    std::vector<uint32_t> ctime, dtime;
    std::vector<size_t> compr_lens;
    bool decomp_error = false;

    if (!compress || !decompress) return;

    if (cspeed > 0)
    {
        uint32_t part = MIN(100*1024,chunk_size);
        GetTime(start_ticks);
//        lzbench_compress(compress, size, compr_lens, inbuf, size, compbuf, comprsize, param1, param2, param3);
        int64_t clen = compress((char*)inbuf, part, (char*)compbuf, comprsize, param1, param2, param3);
        GetTime(end_ticks);
        uint32_t milisec = GetDiffTime(ticksPerSecond, start_ticks, end_ticks);
  //      printf("\nclen=%d milisec=%d %s\n", clen, milisec, func_name);
        if (clen>0 && milisec>=3) // longer than 3 milisec = slower than 33 MB/s
        {
            part = part / milisec / 1024; // speed in MB/s
    //        printf("%s = %d MB/s, %d\n", func_name, part, clen);
            if (part < cspeed) { LZBENCH_DEBUG("%s (100K) slower than %d MB/s, %d\n", func_name, part); return; }
        }
    }
    
    ITERS(iters)
    {
        GetTime(start_ticks);
        complen = lzbench_compress(compress, chunk_size, compr_lens, inbuf, insize, compbuf, comprsize, param1, param2, param3);
        GetTime(mid_ticks);
        
        uint32_t milisec = GetDiffTime(ticksPerSecond, start_ticks, mid_ticks);
        if (complen>0 && milisec>=3) // longer than 3 milisec
        {
            milisec = insize / milisec / 1024; // speed in MB/s
            if (milisec < cspeed) { LZBENCH_DEBUG("%s 1ITER slower than %d MB/s, %d\n", func_name, milisec); return; }
        }

        GetTime(mid_ticks);
        decomplen = lzbench_decompress(decompress, chunk_size, compr_lens, compbuf, complen, decomp, insize, inbuf, param1, param2, param3);
        GetTime(end_ticks);


        add_time(ctime, dtime, GetDiffTime(ticksPerSecond, start_ticks, mid_ticks), GetDiffTime(ticksPerSecond, mid_ticks, end_ticks)); 

        if (insize != decomplen)
        {   
            decomp_error = true; 
//            printf("ERROR: inlen[%d] != outlen[%d]\n", (int32_t)insize, (int32_t)decomplen);
        }
        
        if (memcmp(inbuf, decomp, insize) != 0)
        {
            decomp_error = true; 
#if 0
            size_t cmn = common(inbuf, decomp);
            printf("ERROR in %s: common=%d/%d\n", func_name, (int32_t)cmn, (int32_t)insize);
            char desc[256];
            snprintf(desc, sizeof(desc), "%s_failed", func_name);
            cmn /= chunk_size;
            printf("ERROR: fwrite %d-%d\n", (int32_t)(cmn*chunk_size), (int32_t)((cmn+1)*chunk_size));
            
            FILE *f = fopen(desc, "wb");
            if (f) fwrite(inbuf+cmn*chunk_size, 1, chunk_size, f), fclose(f);
            exit(0);
#endif
        }

        memset(decomp, 0, insize); // clear output buffer
        uni_sleep(1); // give processor to other processes
        
        if (decomp_error) break;
    }
    print_stats(func_name, level, ctime, dtime, insize, complen, decomp_error, cspeed);
}


void benchmark(FILE* in, int iters, uint32_t chunk_size, int cspeed)
{
	std::vector<uint32_t> ctime, dtime;
	LARGE_INTEGER ticksPerSecond, start_ticks, mid_ticks, end_ticks, start_all;
	uint32_t comprsize, insize;
	uint8_t *inbuf, *compbuf, *decomp, *work;

	InitTimer(ticksPerSecond);

	fseek(in, 0L, SEEK_END);
	insize = ftell(in);
	rewind(in);

#ifndef BENCH_REMOVE_PITHY
	comprsize = pithy_MaxCompressedLength(insize);
#else
	comprsize = insize + 2048;
#endif

//	printf("insize=%lld comprsize=%lld\n", insize, comprsize);
	inbuf = (uint8_t*)malloc(insize + 2048);
	compbuf = (uint8_t*)malloc(comprsize);
	decomp = (uint8_t*)calloc(1, insize + 2048);

	if (!inbuf || !compbuf || !decomp)
	{
		printf("Not enough memory!");
		exit(1);
	}

	insize = fread(inbuf, 1, insize, in);
	if (chunk_size > insize) chunk_size = insize;

	GetTime(start_all);

	ITERS(iters)
	{
		GetTime(start_ticks);
		memcpy(compbuf, inbuf, insize);
		GetTime(mid_ticks);
		memcpy(decomp, compbuf, insize);
		GetTime(end_ticks);
		add_time(ctime, dtime, GetDiffTime(ticksPerSecond, start_ticks, mid_ticks), GetDiffTime(ticksPerSecond, mid_ticks, end_ticks));
	}
    printf("| Compressor name             | Compression| Decompress.| Compr. size | Ratio |\n");
	print_stats("memcpy", 0, ctime, dtime, insize, insize, false, 0);


//	goto done;

    goto middle;

middle:


	lzbench_test("brotli 2015-10-29 level 1", 0, lzbench_brotli_compress, lzbench_brotli_decompress, cspeed, chunk_size, iters, inbuf, insize, compbuf, comprsize, decomp, ticksPerSecond, 0, 0, 0);
    for (int level=2; level<=8; level+=3)
        lzbench_test("brotli 2015-10-29 level 0", level, lzbench_brotli_compress, lzbench_brotli_decompress, cspeed, chunk_size, iters, inbuf, insize, compbuf, comprsize, decomp, ticksPerSecond, level, 0, 0);
    lzbench_test("brotli 2015-10-29 level 11", 0, lzbench_brotli_compress, lzbench_brotli_decompress, cspeed, chunk_size, iters, inbuf, insize, compbuf, comprsize, decomp, ticksPerSecond, 11, 0, 0);

	lzbench_test("crush 1.0 level 0", 0, lzbench_crush_compress, lzbench_crush_decompress, cspeed, chunk_size, iters, inbuf, insize, compbuf, comprsize, decomp, ticksPerSecond, 0, 0, 0);
	lzbench_test("crush 1.0 level 1", 0, lzbench_crush_compress, lzbench_crush_decompress, cspeed, chunk_size, iters, inbuf, insize, compbuf, comprsize, decomp, ticksPerSecond, 1, 0, 0);

    for (int level=1; level<=5; level+=1)
        lzbench_test("csc 3.3 level 0", level, lzbench_csc_compress, lzbench_csc_decompress, cspeed, chunk_size, iters, inbuf, insize, compbuf, comprsize, decomp, ticksPerSecond, level, 1<<24, 0);

    for (int level=1; level<=3; level+=1)
        lzbench_test("density 0.12.5 beta level 0", level, lzbench_density_compress, lzbench_density_decompress, cspeed, chunk_size, iters, inbuf, insize, compbuf, comprsize, decomp, ticksPerSecond, level, 0, 0);

	lzbench_test("fastlz 0.1 level 1", 0, lzbench_fastlz_compress, lzbench_fastlz_decompress, cspeed, chunk_size, iters, inbuf, insize, compbuf, comprsize, decomp, ticksPerSecond, 1, 0, 0);
	lzbench_test("fastlz 0.1 level 2", 0, lzbench_fastlz_compress, lzbench_fastlz_decompress, cspeed, chunk_size, iters, inbuf, insize, compbuf, comprsize, decomp, ticksPerSecond, 2, 0, 0);

	lzbench_test("lz4 r131", 0, lzbench_lz4_compress, lzbench_lz4_decompress, cspeed, chunk_size, iters, inbuf, insize, compbuf, comprsize, decomp, ticksPerSecond, 0, 0, 0);
	lzbench_test("lz4fast r131 acc=3", 0, lzbench_lz4fast_compress, lzbench_lz4_decompress, cspeed, chunk_size, iters, inbuf, insize, compbuf, comprsize, decomp, ticksPerSecond, 3, 0, 0);
	lzbench_test("lz4fast r131 acc=17", 0, lzbench_lz4fast_compress, lzbench_lz4_decompress, cspeed, chunk_size, iters, inbuf, insize, compbuf, comprsize, decomp, ticksPerSecond, 17, 0, 0);
	lzbench_test("lz4hc r131 -1", 0, lzbench_lz4hc_compress, lzbench_lz4_decompress, cspeed, chunk_size, iters, inbuf, insize, compbuf, comprsize, decomp, ticksPerSecond, 1, 0, 0);
	lzbench_test("lz4hc r131 -4", 0, lzbench_lz4hc_compress, lzbench_lz4_decompress, cspeed, chunk_size, iters, inbuf, insize, compbuf, comprsize, decomp, ticksPerSecond, 4, 0, 0);
	lzbench_test("lz4hc r131 -9", 0, lzbench_lz4hc_compress, lzbench_lz4_decompress, cspeed, chunk_size, iters, inbuf, insize, compbuf, comprsize, decomp, ticksPerSecond, 9, 0, 0);

	lzbench_test("lz5 r131b", 0, lzbench_lz5_compress, lzbench_lz5_decompress, cspeed, chunk_size, iters, inbuf, insize, compbuf, comprsize, decomp, ticksPerSecond, 0, 0, 0);
	lzbench_test("lz5hc r131b -1", 0, lzbench_lz5hc_compress, lzbench_lz5_decompress, cspeed, chunk_size, iters, inbuf, insize, compbuf, comprsize, decomp, ticksPerSecond, 1, 0, 0);
	lzbench_test("lz5hc r131b -4", 0, lzbench_lz5hc_compress, lzbench_lz5_decompress, cspeed, chunk_size, iters, inbuf, insize, compbuf, comprsize, decomp, ticksPerSecond, 4, 0, 0);
	lzbench_test("lz5hc r131b -9", 0, lzbench_lz5hc_compress, lzbench_lz5_decompress, cspeed, chunk_size, iters, inbuf, insize, compbuf, comprsize, decomp, ticksPerSecond, 9, 0, 0);

	lzbench_test("lzf level 0", 0, lzbench_lzf_compress, lzbench_lzf_decompress, cspeed, chunk_size, iters, inbuf, insize, compbuf, comprsize, decomp, ticksPerSecond, 0, 0, 0);
	lzbench_test("lzf level 1", 0, lzbench_lzf_compress, lzbench_lzf_decompress, cspeed, chunk_size, iters, inbuf, insize, compbuf, comprsize, decomp, ticksPerSecond, 1, 0, 0);

	lzbench_test("lzham 1.0 -m0d26 -0", 0, lzbench_lzham_compress, lzbench_lzham_decompress, cspeed, chunk_size, iters, inbuf, insize, compbuf, comprsize, decomp, ticksPerSecond, 0, 26, 0);
	lzbench_test("lzham 1.0 -m0d26 -0", 1, lzbench_lzham_compress, lzbench_lzham_decompress, cspeed, chunk_size, iters, inbuf, insize, compbuf, comprsize, decomp, ticksPerSecond, 1, 26, 0);

	lzbench_test("lzjb 2010", 0, lzbench_lzjb_compress, lzbench_lzjb_decompress, cspeed, chunk_size, iters, inbuf, insize, compbuf, comprsize, decomp, ticksPerSecond, 0, 0, 0);

    for (int level=0; level<=5; level+=1)
        lzbench_test("lzma 9.38 level 0", level, lzbench_lzma_compress, lzbench_lzma_decompress, cspeed, chunk_size, iters, inbuf, insize, compbuf, comprsize, decomp, ticksPerSecond, level, 0, 0);

	lzbench_test("lzmat 1.01", 0, lzbench_lzmat_compress, lzbench_lzmat_decompress, cspeed, chunk_size, iters, inbuf, insize, compbuf, comprsize, decomp, ticksPerSecond, 0, 0, 0);

	work=(uint8_t*)calloc(1, LZO1B_999_MEM_COMPRESS);
	if (work)
	{
		lzo_init();
		lzbench_test("lzo1b 2.09 -1", 0, lzbench_lzo_compress, lzbench_lzo_decompress, cspeed, chunk_size, iters, inbuf, insize, compbuf, comprsize, decomp, ticksPerSecond, 1, (size_t)work, 0);
		lzbench_test("lzo1b 2.09 -9", 0, lzbench_lzo_compress, lzbench_lzo_decompress, cspeed, chunk_size, iters, inbuf, insize, compbuf, comprsize, decomp, ticksPerSecond, 9, (size_t)work, 0);
		lzbench_test("lzo1b 2.09 -99", 0, lzbench_lzo_compress, lzbench_lzo_decompress, cspeed, chunk_size, iters, inbuf, insize, compbuf, comprsize, decomp, ticksPerSecond, 99, (size_t)work, 0);
		lzbench_test("lzo1b 2.09 -999", 0, lzbench_lzo_compress, lzbench_lzo_decompress, cspeed, chunk_size, iters, inbuf, insize, compbuf, comprsize, decomp, ticksPerSecond, 999, (size_t)work, 0);
		lzbench_test("lzo1c 2.09 -1", 0, lzbench_lzo_compress, lzbench_lzo_decompress, cspeed, chunk_size, iters, inbuf, insize, compbuf, comprsize, decomp, ticksPerSecond, 1001, (size_t)work, 0);
		lzbench_test("lzo1c 2.09 -9", 0, lzbench_lzo_compress, lzbench_lzo_decompress, cspeed, chunk_size, iters, inbuf, insize, compbuf, comprsize, decomp, ticksPerSecond, 1009, (size_t)work, 0);
		lzbench_test("lzo1c 2.09 -99", 0, lzbench_lzo_compress, lzbench_lzo_decompress, cspeed, chunk_size, iters, inbuf, insize, compbuf, comprsize, decomp, ticksPerSecond, 1099, (size_t)work, 0);
		lzbench_test("lzo1c 2.09 -999", 0, lzbench_lzo_compress, lzbench_lzo_decompress, cspeed, chunk_size, iters, inbuf, insize, compbuf, comprsize, decomp, ticksPerSecond, 1999, (size_t)work, 0);
		lzbench_test("lzo1f 2.09 -1", 0, lzbench_lzo_compress, lzbench_lzo_decompress, cspeed, chunk_size, iters, inbuf, insize, compbuf, comprsize, decomp, ticksPerSecond, 2001, (size_t)work, 0);
		lzbench_test("lzo1f 2.09 -999", 0, lzbench_lzo_compress, lzbench_lzo_decompress, cspeed, chunk_size, iters, inbuf, insize, compbuf, comprsize, decomp, ticksPerSecond, 2999, (size_t)work, 0);
		lzbench_test("lzo1x 2.09 -1", 0, lzbench_lzo_compress, lzbench_lzo_decompress, cspeed, chunk_size, iters, inbuf, insize, compbuf, comprsize, decomp, ticksPerSecond, 3001, (size_t)work, 0);
		lzbench_test("lzo1x 2.09 -999", 0, lzbench_lzo_compress, lzbench_lzo_decompress, cspeed, chunk_size, iters, inbuf, insize, compbuf, comprsize, decomp, ticksPerSecond, 3999, (size_t)work, 0);
		lzbench_test("lzo1y 2.09 -1", 0, lzbench_lzo_compress, lzbench_lzo_decompress, cspeed, chunk_size, iters, inbuf, insize, compbuf, comprsize, decomp, ticksPerSecond, 4001, (size_t)work, 0);
		lzbench_test("lzo1y 2.09 -999", 0, lzbench_lzo_compress, lzbench_lzo_decompress, cspeed, chunk_size, iters, inbuf, insize, compbuf, comprsize, decomp, ticksPerSecond, 4999, (size_t)work, 0);
		lzbench_test("lzo1z 2.09 -999", 0, lzbench_lzo_compress, lzbench_lzo_decompress, cspeed, chunk_size, iters, inbuf, insize, compbuf, comprsize, decomp, ticksPerSecond, 5999, (size_t)work, 0);
		lzbench_test("lzo2a 2.09 -999", 0, lzbench_lzo_compress, lzbench_lzo_decompress, cspeed, chunk_size, iters, inbuf, insize, compbuf, comprsize, decomp, ticksPerSecond, 6999, (size_t)work, 0);
		free(work);
	}

	work=(uint8_t*)calloc(1, lzrw2_req_mem());
	if (work)
	{
		lzbench_test("lzrw1", 0, lzbench_lzrw_compress, lzbench_lzrw_decompress, cspeed, chunk_size, iters, inbuf, insize, compbuf, comprsize, decomp, ticksPerSecond, 1, (size_t)work, 0);
		lzbench_test("lzrw1a", 0, lzbench_lzrw_compress, lzbench_lzrw_decompress, cspeed, chunk_size, iters, inbuf, insize, compbuf, comprsize, decomp, ticksPerSecond, 2, (size_t)work, 0);
		lzbench_test("lzrw2", 0, lzbench_lzrw_compress, lzbench_lzrw_decompress, cspeed, chunk_size, iters, inbuf, insize, compbuf, comprsize, decomp, ticksPerSecond, 3, (size_t)work, 0);
		lzbench_test("lzrw3", 0, lzbench_lzrw_compress, lzbench_lzrw_decompress, cspeed, chunk_size, iters, inbuf, insize, compbuf, comprsize, decomp, ticksPerSecond, 4, (size_t)work, 0);
		lzbench_test("lzrw3a", 0, lzbench_lzrw_compress, lzbench_lzrw_decompress, cspeed, chunk_size, iters, inbuf, insize, compbuf, comprsize, decomp, ticksPerSecond, 5, (size_t)work, 0);
		free(work);
	}
	
    for (int level=0; level<=9; level+=3)
        lzbench_test("pithy 2011-12-24 level 0", level, lzbench_pithy_compress, lzbench_pithy_decompress, cspeed, chunk_size, iters, inbuf, insize, compbuf, comprsize, decomp, ticksPerSecond, level, 0, 0);

    {
        qlz150_state_compress* state;
        int state_size, dstate_size;
        state_size = MAX(qlz_get_setting_3(1),MAX(qlz_get_setting_1(1), qlz_get_setting_2(1)));
        dstate_size = MAX(qlz_get_setting_3(2),MAX(qlz_get_setting_1(2), qlz_get_setting_2(2)));
        state_size = MAX(state_size, dstate_size);
        state = (qlz150_state_compress*) calloc(1, state_size);
        //	memset(state,0,state_size);
        lzbench_test("quicklz 1.5.0 -1", 0, lzbench_quicklz_compress, lzbench_quicklz_decompress, cspeed, chunk_size, iters, inbuf, insize, compbuf, comprsize, decomp, ticksPerSecond, 1, (size_t)state, 0);
        lzbench_test("quicklz 1.5.0 -2", 0, lzbench_quicklz_compress, lzbench_quicklz_decompress, cspeed, chunk_size, iters, inbuf, insize, compbuf, comprsize, decomp, ticksPerSecond, 2, (size_t)state, 0);
        lzbench_test("quicklz 1.5.0 -3", 0, lzbench_quicklz_compress, lzbench_quicklz_decompress, cspeed, chunk_size, iters, inbuf, insize, compbuf, comprsize, decomp, ticksPerSecond, 3, (size_t)state, 0);
        lzbench_test("quicklz 1.5.1 b7 -1", 0, lzbench_quicklz_compress, lzbench_quicklz_decompress, cspeed, chunk_size, iters, inbuf, insize, compbuf, comprsize, decomp, ticksPerSecond, 4, (size_t)state, 0);
    }
	
	lzbench_test("shrinker", 0, lzbench_shrinker_compress, lzbench_shrinker_decompress, cspeed, chunk_size, iters, inbuf, insize, compbuf, comprsize, decomp, ticksPerSecond, 0, 0, 0);

	lzbench_test("snappy 1.1.3", 0, lzbench_snappy_compress, lzbench_snappy_decompress, cspeed, chunk_size, iters, inbuf, insize, compbuf, comprsize, decomp, ticksPerSecond, 0, 0, 0);

    for (int level=1; level<=7; level+=1)
        lzbench_test("tornado 0.6a -0", level, lzbench_tornado_compress, lzbench_tornado_decompress, cspeed, chunk_size, iters, inbuf, insize, compbuf, comprsize, decomp, ticksPerSecond, level, 0, 0);
	lzbench_test("tornado 0.6a -10", 0, lzbench_tornado_compress, lzbench_tornado_decompress, cspeed, chunk_size, iters, inbuf, insize, compbuf, comprsize, decomp, ticksPerSecond, 10, 0, 0);
	lzbench_test("tornado 0.6a -13", 0, lzbench_tornado_compress, lzbench_tornado_decompress, cspeed, chunk_size, iters, inbuf, insize, compbuf, comprsize, decomp, ticksPerSecond, 13, 0, 0);
	lzbench_test("tornado 0.6a -16", 0, lzbench_tornado_compress, lzbench_tornado_decompress, cspeed, chunk_size, iters, inbuf, insize, compbuf, comprsize, decomp, ticksPerSecond, 16, 0, 0);

	lzbench_test("tornado 0.6a h16k b1m", 0, lzbench_tornado_compress, lzbench_tornado_decompress, cspeed, chunk_size, iters, inbuf, insize, compbuf, comprsize, decomp, ticksPerSecond, 21, 0, 0);
	lzbench_test("tornado 0.6a h128k b2m", 0, lzbench_tornado_compress, lzbench_tornado_decompress, cspeed, chunk_size, iters, inbuf, insize, compbuf, comprsize, decomp, ticksPerSecond, 22, 0, 0);
	lzbench_test("tornado 0.6a h128k b8m", 0, lzbench_tornado_compress, lzbench_tornado_decompress, cspeed, chunk_size, iters, inbuf, insize, compbuf, comprsize, decomp, ticksPerSecond, 23, 0, 0);
	lzbench_test("tornado 0.6a h4m b8m", 0, lzbench_tornado_compress, lzbench_tornado_decompress, cspeed, chunk_size, iters, inbuf, insize, compbuf, comprsize, decomp, ticksPerSecond, 24, 0, 0);
	lzbench_test("tornado h128k b8m bitio", 0, lzbench_tornado_compress, lzbench_tornado_decompress, cspeed, chunk_size, iters, inbuf, insize, compbuf, comprsize, decomp, ticksPerSecond, 25, 0, 0);
	lzbench_test("tornado h4m b8m bitio", 0, lzbench_tornado_compress, lzbench_tornado_decompress, cspeed, chunk_size, iters, inbuf, insize, compbuf, comprsize, decomp, ticksPerSecond, 26, 0, 0);
	lzbench_test("tornado h4m b32m bitio", 0, lzbench_tornado_compress, lzbench_tornado_decompress, cspeed, chunk_size, iters, inbuf, insize, compbuf, comprsize, decomp, ticksPerSecond, 27, 0, 0);

	lzbench_test("ucl_nrv2b 1.03 -1", 0, lzbench_ucl_compress, lzbench_ucl_decompress, cspeed, chunk_size, iters, inbuf, insize, compbuf, comprsize, decomp, ticksPerSecond, 1, 1, 0);
	lzbench_test("ucl_nrv2b 1.03 -6", 0, lzbench_ucl_compress, lzbench_ucl_decompress, cspeed, chunk_size, iters, inbuf, insize, compbuf, comprsize, decomp, ticksPerSecond, 1, 6, 0);
	lzbench_test("ucl_nrv2d 1.03 -1", 0, lzbench_ucl_compress, lzbench_ucl_decompress, cspeed, chunk_size, iters, inbuf, insize, compbuf, comprsize, decomp, ticksPerSecond, 2, 1, 0);
	lzbench_test("ucl_nrv2d 1.03 -6", 0, lzbench_ucl_compress, lzbench_ucl_decompress, cspeed, chunk_size, iters, inbuf, insize, compbuf, comprsize, decomp, ticksPerSecond, 2, 6, 0);
	lzbench_test("ucl_nrv2e 1.03 -1", 0, lzbench_ucl_compress, lzbench_ucl_decompress, cspeed, chunk_size, iters, inbuf, insize, compbuf, comprsize, decomp, ticksPerSecond, 3, 1, 0);
	lzbench_test("ucl_nrv2e 1.03 -6", 0, lzbench_ucl_compress, lzbench_ucl_decompress, cspeed, chunk_size, iters, inbuf, insize, compbuf, comprsize, decomp, ticksPerSecond, 3, 6, 0);

	work=(uint8_t*)calloc(1, wfLZ_GetWorkMemSize());
	if (work)
	{
		lzbench_test("wflz 2015-09-16", 0, lzbench_wflz_compress, lzbench_wflz_decompress, cspeed, chunk_size, iters, inbuf, insize, compbuf, comprsize, decomp, ticksPerSecond, 1, (size_t)work, 0);
		free(work);
	}
	
	YappyFillTables();
	lzbench_test("yappy 1", 0, lzbench_yappy_compress, lzbench_yappy_decompress, cspeed, chunk_size, iters, inbuf, insize, compbuf, comprsize, decomp, ticksPerSecond, 1, 0, 0);
	lzbench_test("yappy 10", 0, lzbench_yappy_compress, lzbench_yappy_decompress, cspeed, chunk_size, iters, inbuf, insize, compbuf, comprsize, decomp, ticksPerSecond, 10, 0, 0);
	lzbench_test("yappy 100", 0, lzbench_yappy_compress, lzbench_yappy_decompress, cspeed, chunk_size, iters, inbuf, insize, compbuf, comprsize, decomp, ticksPerSecond, 100, 0, 0);

	lzbench_test("zlib 1.2.8 -1", 0, lzbench_zlib_compress, lzbench_zlib_decompress, cspeed, chunk_size, iters, inbuf, insize, compbuf, comprsize, decomp, ticksPerSecond, 1, 0, 0);
	lzbench_test("zlib 1.2.8 -6", 0, lzbench_zlib_compress, lzbench_zlib_decompress, cspeed, chunk_size, iters, inbuf, insize, compbuf, comprsize, decomp, ticksPerSecond, 6, 0, 0);
	lzbench_test("zlib 1.2.8 -9", 0, lzbench_zlib_compress, lzbench_zlib_decompress, cspeed, chunk_size, iters, inbuf, insize, compbuf, comprsize, decomp, ticksPerSecond, 9, 0, 0);

    for (int level=0; level<=4; level+=1)
        lzbench_test("zling 2015-09-15 level 0", level, lzbench_zling_compress, lzbench_zling_decompress, cspeed, chunk_size, iters, inbuf, insize, compbuf, comprsize, decomp, ticksPerSecond, level, 0, 0);

	lzbench_test("zstd v0.3.2", 0, lzbench_zstd_compress, lzbench_zstd_decompress, cspeed, chunk_size, iters, inbuf, insize, compbuf, comprsize, decomp, ticksPerSecond, 0, 0, 0);
    for (int level=1; level<=9; level+=4)
        lzbench_test("zstd_HC v0.3.2 -0", level, lzbench_zstdhc_compress, lzbench_zstdhc_decompress, cspeed, chunk_size, iters, inbuf, insize, compbuf, comprsize, decomp, ticksPerSecond, level, 0, 0);
    for (int level=13; level<=21; level+=4)
        lzbench_test("zstd_HC v0.3.2 -00", level, lzbench_zstdhc_compress, lzbench_zstdhc_decompress, cspeed, chunk_size, iters, inbuf, insize, compbuf, comprsize, decomp, ticksPerSecond, level, 0, 0);


done:
	//Print_Time("all", 0, &ticksPerSecond, NULL, &start_all, 0, 0, 1);

	free(inbuf);
	free(compbuf);
	free(decomp);

	if (chunk_size > 10 * (1<<20))
		printf("done... (%d iterations, chunk_size=%d MB, min_compr_speed=%d MB)\n", iters, chunk_size >> 20, cspeed);
	else
		printf("done... (%d iterations, chunk_size=%d KB, min_compr_speed=%d MB)\n", iters, chunk_size >> 10, cspeed);
}


int main( int argc, char** argv) 
{
	FILE *in;
	uint32_t iterations, chunk_size, cspeed;

	iterations = 1;
	chunk_size = 1 << 31;
	cspeed = 100;

#ifdef WINDOWS
//	SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
	SetPriorityClass(GetCurrentProcess(), REALTIME_PRIORITY_CLASS);
#else
	setpriority(PRIO_PROCESS, 0, -20);
#endif

	printf(PROGNAME " " PROGVERSION " (%d-bit " PROGOS ")   Assembled by P.Skibinski\n", (uint32_t)(8 * sizeof(uint8_t*)));

	while ((argc>1)&&(argv[1][0]=='-')) {
		switch (argv[1][1]) {
	case 'i':
		iterations=atoi(argv[1]+2);
		break;
	case 'b':
		chunk_size = atoi(argv[1] + 2) << 10;
		break;
	case 's':
		cspeed = atoi(argv[1] + 2);
		break;
	case 't':
		turbobench_format = true;
		break;	
	default:
		fprintf(stderr, "unknown option: %s\n", argv[1]);
		exit(1);
		}
		argv++;
		argc--;
	}

	if (argc<2) {
		fprintf(stderr, "usage: " PROGNAME " [options] input\n");
		fprintf(stderr, " -iX: number of iterations (default = %d)\n", iterations);
		fprintf(stderr, " -bX: set block/chunk size to X KB (default = %d KB)\n", chunk_size>>10);
		fprintf(stderr, " -sX: use only compressors with compression speed over X MB (default = %d MB)\n", cspeed);
		exit(1);
	}

	if (!(in=fopen(argv[1], "rb"))) {
		perror(argv[1]);
		exit(1);
	}

	benchmark(in, iterations, chunk_size, cspeed);

	fclose(in);
}



