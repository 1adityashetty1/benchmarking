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

#include <numeric>
#include <algorithm> // sort
#include <string>
#include <stdlib.h> 
#include <stdio.h> 
#include <stdint.h> 
#include <string.h> 
#include "lzbench.h"


void format(std::string& s,const char* formatstring, ...) 
{
   char buff[1024];
   va_list args;
   va_start(args, formatstring);

#ifdef WIN32
   _vsnprintf( buff, sizeof(buff), formatstring, args);
#else
   vsnprintf( buff, sizeof(buff), formatstring, args);
#endif

   va_end(args);

   s=buff;
} 

void print_header(lzbench_params_t *params)
{
    switch (params->textformat)
    {
        case CSV:
            printf("Compressor name,Compression speed,Decompression speed,Compressed size,Ratio\n"); break;
        case TURBOBENCH:
            printf("  Compressed  Ratio   Cspeed   Dspeed  Compressor name\n"); break;
        case TEXT:
            printf("Compressor name              Compression Decompress. Compr. size  Ratio \n"); break;
        case MARKDOWN:
            printf("| Compressor name             | Compression| Decompress.| Compr. size | Ratio |\n"); break;
    }
}

void print_row(lzbench_params_t *params, string_table_t& row)
{
    switch (params->textformat)
    {
        case CSV:
            printf("%s,%.2f,%.2f,%" PRId64 ",%.2f\n", row.column1.c_str(), row.column2, row.column3, row.column4, row.column5); break;
        case TURBOBENCH:
            printf("%12" PRId64 " %6.1f%9.2f%9.2f  %s\n", row.column4, row.column5, row.column2, row.column3, row.column1.c_str()); break;
        case TEXT:
            printf("%-27s ", row.column1.c_str());
            if (row.column2 < 10) printf("%6.2f MB/s ", row.column2); else printf("%6d MB/s ", (int)row.column2);
            if (!row.column3)
                printf("      ERROR ");
            else
                if (row.column3 < 10) printf("%6.2f MB/s ", row.column3); else printf("%6d MB/s ", (int)row.column3); 
            printf("%12" PRId64 " %6.2f\n", row.column4, row.column5);
            break;
        case MARKDOWN:
            printf("| %-27s ", row.column1.c_str());
            if (row.column2 < 10) printf("|%6.2f MB/s ", row.column2); else printf("|%6d MB/s ", (int)row.column2);
            if (!row.column3)
                printf("|      ERROR ");
            else
                if (row.column3 < 10) printf("|%6.2f MB/s ", row.column3); else printf("|%6d MB/s ", (int)row.column3); 
            printf("|%12" PRId64 " |%6.2f |\n", row.column4, row.column5);
            break;
    }
}


void print_stats(lzbench_params_t *params, const compressor_desc_t* desc, int level, std::vector<uint64_t> &ctime, std::vector<uint64_t> &dtime, uint32_t insize, uint32_t outsize, bool decomp_error)
{
    std::string column1;
    std::sort(ctime.begin(), ctime.end());
    std::sort(dtime.begin(), dtime.end());
    uint64_t cnano, dnano;
    
    switch (params->timetype)
    {
        case FASTEST: 
            cnano = ctime[0] + (ctime[0] == 0);
            dnano = dtime[0] + (dtime[0] == 0);
            break;
        case AVERAGE: 
            cnano = std::accumulate(ctime.begin(),ctime.end(),0) / ctime.size();
            dnano = std::accumulate(dtime.begin(),dtime.end(),0) / dtime.size();
            if (cnano == 0) cnano = 1;
            if (dnano == 0) dnano = 1;
            break;
        case MEDIAN: 
            cnano = ctime[ctime.size()/2] + (ctime[ctime.size()/2] == 0);
            dnano = dtime[dtime.size()/2] + (dtime[dtime.size()/2] == 0);
            break;
    }
    
    if (params->cspeed > insize/cnano) { LZBENCH_DEBUG(9, "%s FULL slower than %d MB/s\n", desc->name, insize/cnano); return; } 

    if (desc->first_level == 0 && desc->last_level==0)
        format(column1, "%s %s", desc->name, desc->version);
    else
        format(column1, "%s %s level %d", desc->name, desc->version, level);

    params->results.push_back(string_table_t(column1, (float)insize/cnano, (decomp_error)?0:((float)insize/dnano), outsize, outsize * 100.0 / insize));
    print_row(params, params->results[params->results.size()-1]);

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


inline int64_t lzbench_compress(lzbench_params_t *params, compress_func compress, std::vector<size_t> &compr_lens, uint8_t *inbuf, size_t insize, uint8_t *outbuf, size_t outsize, size_t param1, size_t param2, char* workmem)
{
    int64_t clen;
    size_t part, sum = 0;
    uint8_t *start = inbuf;
    compr_lens.clear();
    
    while (insize > 0)
    {
        part = MIN(insize, params->chunk_size);
        clen = compress((char*)inbuf, part, (char*)outbuf, outsize, param1, param2, workmem);
		LZBENCH_DEBUG(5,"ENC part=%d clen=%d in=%d\n", (int)part, (int)clen, (int)(inbuf-start));

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


inline int64_t lzbench_decompress(lzbench_params_t *params, compress_func decompress, std::vector<size_t> &compr_lens, uint8_t *inbuf, size_t insize, uint8_t *outbuf, size_t outsize, uint8_t *origbuf, size_t param1, size_t param2, char* workmem)
{
    int64_t dlen;
    int num=0;
    size_t part, sum = 0;
    uint8_t *outstart = outbuf;

    while (insize > 0)
    {
        part = compr_lens[num++];
        if (part > insize) return 0;
        if (part == MIN(params->chunk_size, outsize)) // uncompressed
        {
            memcpy(outbuf, inbuf, part);
            dlen = part;
        }
        else
        {
            dlen = decompress((char*)inbuf, part, (char*)outbuf, MIN(params->chunk_size, outsize), param1, param2, workmem);
        }
		LZBENCH_DEBUG(5, "DEC part=%d dlen=%d out=%d\n", (int)part, (int)dlen, (int)(outbuf - outstart));
        if (dlen <= 0) return dlen;

        inbuf += part;
        insize -= part;
        outbuf += dlen;
        outsize -= dlen;
        sum += dlen;
    }
    
    return sum;
}


void lzbench_test(lzbench_params_t *params, const compressor_desc_t* desc, int level, uint8_t *inbuf, size_t insize, uint8_t *compbuf, size_t comprsize, uint8_t *decomp, LARGE_INTEGER ticksPerSecond, size_t param1, size_t param2)
{
    LARGE_INTEGER start_ticks, end_ticks;
    int64_t complen=0, decomplen;
    std::vector<uint64_t> ctime, dtime;
    std::vector<size_t> compr_lens;
    bool decomp_error = false;
    char* workmem = NULL;

    if (!desc->compress || !desc->decompress) goto done;
    if (desc->init) workmem = desc->init(params->chunk_size);

    LZBENCH_DEBUG(1, "*** trying %s insize=%d comprsize=%d chunk_size=%d\n", desc->name, (int)insize, (int)comprsize, (int)params->chunk_size);

    if (params->cspeed > 0)
    {
        uint64_t part = MIN(100*1024, params->chunk_size);
        GetTime(start_ticks);
        int64_t clen = desc->compress((char*)inbuf, part, (char*)compbuf, comprsize, param1, param2, workmem);
        GetTime(end_ticks);
        uint64_t nanosec = GetDiffTime(ticksPerSecond, start_ticks, end_ticks);
        if (clen>0 && nanosec>=3000) // longer than 3 milisec = slower than 33 MB/s
        {
            part = (part / nanosec); // speed in MB/s
            if (part < params->cspeed) { LZBENCH_DEBUG(9, "%s (100K) slower than %d MB/s\n", desc->name, (uint32_t)part); goto done; }
        }
    }

    for (int ii=1; ii<=params->c_iters; ii++)
    {
        printf("%s compr iter %d/%d\r", desc->name, ii, params->c_iters);
        GetTime(start_ticks);
        complen = lzbench_compress(params, desc->compress, compr_lens, inbuf, insize, compbuf, comprsize, param1, param2, workmem);
        GetTime(end_ticks);
        
        uint64_t nanosec = GetDiffTime(ticksPerSecond, start_ticks, end_ticks);
        if (complen>0 && nanosec>=3000) // longer than 3 nanosec
        {
            if ((insize/nanosec) < params->cspeed) { LZBENCH_DEBUG(9, "%s 1ITER slower than %d MB/s\n", desc->name, (uint32_t)((insize/nanosec))); goto done; }
        }
        ctime.push_back(nanosec);
    }

    for (int ii=1; ii<=params->d_iters; ii++)
    {
        printf("%s decompr iter %d/%d\r", desc->name, ii, params->d_iters);
        GetTime(start_ticks);
        decomplen = lzbench_decompress(params, desc->decompress, compr_lens, compbuf, complen, decomp, insize, inbuf, param1, param2, workmem);
        GetTime(end_ticks);

        dtime.push_back(GetDiffTime(ticksPerSecond, start_ticks, end_ticks));

        if (insize != decomplen)
        {   
            decomp_error = true; 
            LZBENCH_DEBUG(1, "ERROR: inlen[%d] != outlen[%d]\n", (int32_t)insize, (int32_t)decomplen);
        }
        
        if (memcmp(inbuf, decomp, insize) != 0)
        {
            decomp_error = true; 

            size_t cmn = common(inbuf, decomp);
            LZBENCH_DEBUG(1, "ERROR in %s: common=%d/%d\n", desc->name, (int32_t)cmn, (int32_t)insize);
            
            if (params->verbose >= 10)
            {
                char text[256];
                snprintf(text, sizeof(text), "%s_failed", desc->name);
                cmn /= params->chunk_size;
                size_t err_size = MIN(insize, (cmn+1)*params->chunk_size);
                err_size -= cmn*params->chunk_size;
                printf("ERROR: fwrite %d-%d to %s\n", (int32_t)(cmn*params->chunk_size), (int32_t)(cmn*params->chunk_size+err_size), text);
                FILE *f = fopen(text, "wb");
                if (f) fwrite(inbuf+cmn*params->chunk_size, 1, err_size, f), fclose(f);
                exit(0);
            }
        }

        memset(decomp, 0, insize); // clear output buffer
        uni_sleep(1); // give processor to other processes
        
        if (decomp_error) break;
    }
    print_stats(params, desc, level, ctime, dtime, insize, complen, decomp_error);
done:
    if (desc->deinit) desc->deinit(workmem);
};


void lzbench_test_with_params(lzbench_params_t *params, char *namesWithParams, uint8_t *inbuf, size_t insize, uint8_t *compbuf, size_t comprsize, uint8_t *decomp, LARGE_INTEGER ticksPerSecond)
{
    const char delimiters[] = "/";
    const char delimiters2[] = ",";
    char *copy, *copy2, *token, *token2, *token3, *save_ptr, *save_ptr2;

    copy = (char*)strdup(namesWithParams);
    token = strtok_r(copy, delimiters, &save_ptr);

    while (token != NULL) 
    {
        for (int i=0; i<LZBENCH_ALIASES_COUNT; i++)
        {
            if (strcmp(token, alias_desc[i].name)==0)
            {
                lzbench_test_with_params(params, (char*)alias_desc[i].params, inbuf, insize, compbuf, comprsize, decomp, ticksPerSecond);
                goto next_token; 
           }
        }

        copy2 = (char*)strdup(token);
        LZBENCH_DEBUG(1, "params = %s\n", token);
        token2 = strtok_r(copy2, delimiters2, &save_ptr2);

        if (token2)
        {
            token3 = strtok_r(NULL, delimiters2, &save_ptr2);
            do
            {
                bool found = false;
                for (int i=1; i<LZBENCH_COMPRESSOR_COUNT; i++)
                {
                    if (strcmp(comp_desc[i].name, token2) == 0)
                    {
                        found = true;
    //                        printf("%s %s %s\n", token2, comp_desc[i].version, token3);
                        if (!token3)
                        {                          
                            for (int level=comp_desc[i].first_level; level<=comp_desc[i].last_level; level++)
                                lzbench_test(params, &comp_desc[i], level, inbuf, insize, compbuf, comprsize, decomp, ticksPerSecond, level, 0);
                        }
                        else
                            lzbench_test(params, &comp_desc[i], atoi(token3), inbuf, insize, compbuf, comprsize, decomp, ticksPerSecond, atoi(token3), 0);
                        break;
                    }
                }
                if (!found) printf("NOT FOUND: %s %s\n", token2, token3);
                token3 = strtok_r(NULL, delimiters2, &save_ptr2);
            }
            while (token3 != NULL);
        }

        free(copy2);
next_token:
        token = strtok_r(NULL, delimiters, &save_ptr);
    }

    free(copy);
}


void lzbenchmark(lzbench_params_t* params, FILE* in, char* encoder_list)
{
	LARGE_INTEGER ticksPerSecond, start_ticks, mid_ticks, end_ticks;
	uint32_t comprsize, insize;
	uint8_t *inbuf, *compbuf, *decomp;

	InitTimer(ticksPerSecond);

	fseek(in, 0L, SEEK_END);
	insize = ftell(in);
	rewind(in);

	comprsize = insize + insize/6 + PAD_SIZE; // for pithy

//	printf("insize=%lld comprsize=%lld\n", insize, comprsize);
	inbuf = (uint8_t*)malloc(insize + PAD_SIZE);
	compbuf = (uint8_t*)malloc(comprsize);
	decomp = (uint8_t*)calloc(1, insize + PAD_SIZE);

	if (!inbuf || !compbuf || !decomp)
	{
		printf("Not enough memory!");
		exit(1);
	}

	insize = fread(inbuf, 1, insize, in);
	if (params->chunk_size > insize) params->chunk_size = insize;

    print_header(params);

    lzbench_test(params, &comp_desc[0], 0, inbuf, insize, compbuf, comprsize, decomp, ticksPerSecond, 0, 0);
    lzbench_test_with_params(params, encoder_list?encoder_list:(char*)alias_desc[1].params, inbuf, insize, compbuf, comprsize, decomp, ticksPerSecond);

	free(inbuf);
	free(compbuf);
	free(decomp);

	if (params->chunk_size > 10 * (1<<20))
		printf("done... (%d compr/%d decompr iters, chunk_size=%d MB, min_compr_speed=%d MB)\n", params->c_iters, params->d_iters, params->chunk_size >> 20, params->cspeed);
	else
		printf("done... (%d compr/%d decompr iters, chunk_size=%d KB, min_compr_speed=%d MB)\n", params->c_iters, params->d_iters, params->chunk_size >> 10, params->cspeed);
}


int main( int argc, char** argv) 
{
	FILE *in;
    lzbench_params_t params;
    char* encoder_list = NULL;
    int sort_col = 0;
    
    params.timetype = FASTEST;
    params.textformat = TEXT;
    params.verbose = 0;
	params.d_iters = 3;
    params.c_iters = 1;
	params.chunk_size = 1 << 31;
	params.cspeed = 0;
    
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
		params.d_iters=atoi(argv[1]+2);
		break;
	case 'j':
		params.c_iters=atoi(argv[1]+2);
		break;
	case 'c':
		sort_col = atoi(argv[1] + 2);
		break;
	case 'b':
		params.chunk_size = atoi(argv[1] + 2) << 10;
		break;
	case 'e':
		encoder_list = strdup(argv[1] + 2);
		break;
	case 's':
		params.cspeed = atoi(argv[1] + 2);
		break;
	case 't':
        params.textformat = (textformat_e)atoi(argv[1] + 2);
		break;
	case 'p':
        params.timetype = (timetype_e)atoi(argv[1] + 2);
		break;
	case 'v':
		params.verbose = atoi(argv[1] + 2);
		break;
	case '-': // --help
	case 'h':
        break;
    case 'l':
        printf("\nAvailable compressors for -e option:\n");
        printf("all - alias for all available compressors\n");
        printf("fast - alias for compressors with compression speed over 100 MB/s\n");
        printf("opt - compressors with optimal parsing (slow compression, fast decompression)\n");
        printf("lzo / ucl - aliases for all levels of given compressors\n");
        for (int i=1; i<LZBENCH_COMPRESSOR_COUNT; i++)
        {
            printf("%s %s\n", comp_desc[i].name, comp_desc[i].version);
        }
        return 0;
    default:
		fprintf(stderr, "unknown option: %s\n", argv[1]);
		exit(1);
		}
		argv++;
		argc--;
	}

	if (argc<2) {
		fprintf(stderr, "usage: " PROGNAME " [options] input\n");
		fprintf(stderr, " -bX  set block/chunk size to X KB (default = %d KB)\n", params.chunk_size>>10);
		fprintf(stderr, " -cX  sort results by column number X\n");
		fprintf(stderr, " -eX  X = compressors separated by '/' with parameters specified after ','\n");
		fprintf(stderr, " -iX  number of decompression iterations (default = %d)\n", params.d_iters);
		fprintf(stderr, " -jX  number of compression iterations (default = %d)\n", params.c_iters);
		fprintf(stderr, " -l   list of available compressors and aliases\n");
        fprintf(stderr, " -tX  output text format 1=markdown, 2=text, 3=CSV (default = %d)\n", params.textformat);
		fprintf(stderr, " -pX  print time for all iterations: 1=fastest 2=average 3=median (default = %d)\n", params.timetype);
		fprintf(stderr, " -sX  use only compressors with compression speed over X MB (default = %d MB)\n", params.cspeed);
        fprintf(stderr,"\nExample usage:\n");
        fprintf(stderr,"  " PROGNAME " -ebrotli filename - selects all levels of brotli\n");
        fprintf(stderr,"  " PROGNAME " -ebrotli,2,5/zstd filename - selects levels 2 & 5 of brotli and zstd\n");                    
		exit(1);
	}

	if (!(in=fopen(argv[1], "rb"))) {
		perror(argv[1]);
		exit(1);
	}

	lzbenchmark(&params, in, encoder_list);

    if (encoder_list) free(encoder_list);

	fclose(in);


    if (sort_col <= 0) return 0;

    printf("\nThe results sorted by column number %d:\n", sort_col);
    print_header(&params);

    switch (sort_col)
    {
        default:
        case 1: std::sort(params.results.begin(), params.results.end(), less_using_1st_column()); break;
        case 2: std::sort(params.results.begin(), params.results.end(), less_using_2nd_column()); break;
        case 3: std::sort(params.results.begin(), params.results.end(), less_using_3rd_column()); break;
        case 4: std::sort(params.results.begin(), params.results.end(), less_using_4th_column()); break;
        case 5: std::sort(params.results.begin(), params.results.end(), less_using_5th_column()); break;
    }

    for (std::vector<string_table_t>::iterator it = params.results.begin(); it!=params.results.end(); it++)
    {
        print_row(&params, *it);
    }
}



