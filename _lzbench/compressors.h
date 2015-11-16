#ifndef LZBENCH_COMPRESSORS_H
#define LZBENCH_COMPRESSORS_H

#include <stdlib.h> 
#include <stdint.h> // int64_t


#ifndef BENCH_REMOVE_BRIEFLZ
    char* lzbench_brieflz_init(size_t insize);
    void lzbench_brieflz_deinit(char* workmem);
	int64_t lzbench_brieflz_compress(char *inbuf, size_t insize, char *outbuf, size_t outsize, size_t level, size_t, char*);
	int64_t lzbench_brieflz_decompress(char *inbuf, size_t insize, char *outbuf, size_t outsize, size_t, size_t, char*);
#else
	#define lzbench_brieflz_init NULL
	#define lzbench_brieflz_deinit NULL
	#define lzbench_brieflz_compress NULL
	#define lzbench_brieflz_decompress NULL
#endif


#ifndef BENCH_REMOVE_BROTLI
	int64_t lzbench_brotli_compress(char *inbuf, size_t insize, char *outbuf, size_t outsize, size_t level, size_t, char*);
	int64_t lzbench_brotli_decompress(char *inbuf, size_t insize, char *outbuf, size_t outsize, size_t, size_t, char*);
#else
	#define lzbench_brotli_compress NULL
	#define lzbench_brotli_decompress NULL
#endif


#ifndef BENCH_REMOVE_CRUSH
	int64_t lzbench_crush_compress(char *inbuf, size_t insize, char *outbuf, size_t outsize, size_t level, size_t, char*);
	int64_t lzbench_crush_decompress(char *inbuf, size_t insize, char *outbuf, size_t outsize, size_t, size_t, char*);
#else
	#define lzbench_crush_compress NULL
	#define lzbench_crush_decompress NULL
#endif


#ifndef BENCH_REMOVE_CSC
	int64_t lzbench_csc_compress(char *inbuf, size_t insize, char *outbuf, size_t outsize, size_t level, size_t, char*);
	int64_t lzbench_csc_decompress(char *inbuf, size_t insize, char *outbuf, size_t outsize, size_t, size_t, char*);
#else
	#define lzbench_csc_compress NULL
	#define lzbench_csc_decompress NULL
#endif


#ifndef BENCH_REMOVE_DENSITY
	int64_t lzbench_density_compress(char *inbuf, size_t insize, char *outbuf, size_t outsize, size_t level, size_t, char*);
	int64_t lzbench_density_decompress(char *inbuf, size_t insize, char *outbuf, size_t outsize, size_t, size_t, char*);
#else
	#define lzbench_density_compress NULL
	#define lzbench_density_decompress NULL
#endif


#ifndef BENCH_REMOVE_FASTLZ
	int64_t lzbench_fastlz_compress(char *inbuf, size_t insize, char *outbuf, size_t outsize, size_t level, size_t, char*);
	int64_t lzbench_fastlz_decompress(char *inbuf, size_t insize, char *outbuf, size_t outsize, size_t, size_t, char*);
#else
	#define lzbench_fastlz_compress NULL
	#define lzbench_fastlz_decompress NULL
#endif


#ifndef BENCH_REMOVE_LZ4
	int64_t lzbench_lz4_compress(char *inbuf, size_t insize, char *outbuf, size_t outsize, size_t level, size_t, char*);
	int64_t lzbench_lz4fast_compress(char *inbuf, size_t insize, char *outbuf, size_t outsize,  size_t level, size_t, char*);
	int64_t lzbench_lz4hc_compress(char *inbuf, size_t insize, char *outbuf, size_t outsize,  size_t level, size_t, char*);
	int64_t lzbench_lz4_decompress(char *inbuf, size_t insize, char *outbuf, size_t outsize, size_t, size_t, char*);
#else
	#define lzbench_lz4_compress NULL
	#define lzbench_lz4fast_compress NULL
	#define lzbench_lz4hc_compress NULL
	#define lzbench_lz4_decompress NULL
#endif


#ifndef BENCH_REMOVE_LZ5
	int64_t lzbench_lz5_compress(char *inbuf, size_t insize, char *outbuf, size_t outsize, size_t level, size_t, char*);
	int64_t lzbench_lz5fast_compress(char *inbuf, size_t insize, char *outbuf, size_t outsize,  size_t level, size_t, char*);
	int64_t lzbench_lz5hc_compress(char *inbuf, size_t insize, char *outbuf, size_t outsize,  size_t level, size_t, char*);
	int64_t lzbench_lz5_decompress(char *inbuf, size_t insize, char *outbuf, size_t outsize, size_t, size_t, char*);
#else
	#define lzbench_lz5_compress NULL
	#define lzbench_lz5fast_compress NULL
	#define lzbench_lz5hc_compress NULL
	#define lzbench_lz5_decompress NULL
#endif


#ifndef BENCH_REMOVE_LZF
	int64_t lzbench_lzf_compress(char *inbuf, size_t insize, char *outbuf, size_t outsize, size_t level, size_t, char*);
	int64_t lzbench_lzf_decompress(char *inbuf, size_t insize, char *outbuf, size_t outsize, size_t, size_t, char*);
#else
	#define lzbench_lzf_compress NULL
	#define lzbench_lzf_decompress NULL
#endif


#ifndef BENCH_REMOVE_LZG
	int64_t lzbench_lzg_compress(char *inbuf, size_t insize, char *outbuf, size_t outsize, size_t level, size_t, char*);
	int64_t lzbench_lzg_decompress(char *inbuf, size_t insize, char *outbuf, size_t outsize, size_t, size_t, char*);
#else
	#define lzbench_lzg_compress NULL
	#define lzbench_lzg_decompress NULL
#endif


#ifndef BENCH_REMOVE_LZHAM
	int64_t lzbench_lzham_compress(char *inbuf, size_t insize, char *outbuf, size_t outsize, size_t level, size_t, char*);
	int64_t lzbench_lzham_decompress(char *inbuf, size_t insize, char *outbuf, size_t outsize, size_t, size_t, char*);
#else
	#define lzbench_lzham_compress NULL
	#define lzbench_lzham_decompress NULL
#endif


#ifndef BENCH_REMOVE_LZJB
	int64_t lzbench_lzjb_compress(char *inbuf, size_t insize, char *outbuf, size_t outsize, size_t level, size_t, char*);
	int64_t lzbench_lzjb_decompress(char *inbuf, size_t insize, char *outbuf, size_t outsize, size_t, size_t, char*);
#else
	#define lzbench_lzjb_compress NULL
	#define lzbench_lzjb_decompress NULL
#endif


#ifndef BENCH_REMOVE_LZLIB
	int64_t lzbench_lzlib_compress(char *inbuf, size_t insize, char *outbuf, size_t outsize, size_t level, size_t, char*);
	int64_t lzbench_lzlib_decompress(char *inbuf, size_t insize, char *outbuf, size_t outsize, size_t, size_t, char*);
#else
	#define lzbench_lzlib_compress NULL
	#define lzbench_lzlib_decompress NULL
#endif


#ifndef BENCH_REMOVE_LZMA
	int64_t lzbench_lzma_compress(char *inbuf, size_t insize, char *outbuf, size_t outsize, size_t level, size_t, char*);
	int64_t lzbench_lzma_decompress(char *inbuf, size_t insize, char *outbuf, size_t outsize, size_t, size_t, char*);
#else
	#define lzbench_lzma_compress NULL
	#define lzbench_lzma_decompress NULL
#endif


#ifndef BENCH_REMOVE_LZMAT
	int64_t lzbench_lzmat_compress(char *inbuf, size_t insize, char *outbuf, size_t outsize, size_t level, size_t, char*);
	int64_t lzbench_lzmat_decompress(char *inbuf, size_t insize, char *outbuf, size_t outsize, size_t, size_t, char*);
#else
	#define lzbench_lzmat_compress NULL
	#define lzbench_lzmat_decompress NULL
#endif


#ifndef BENCH_REMOVE_LZO
    char* lzbench_lzo_init(size_t );
    void lzbench_lzo_deinit(char* workmem);
    int64_t lzbench_lzo1b_compress(char *inbuf, size_t insize, char *outbuf, size_t outsize, size_t level, size_t, char* workmem);
    int64_t lzbench_lzo1b_decompress(char *inbuf, size_t insize, char *outbuf, size_t outsize, size_t level, size_t, char*);
    int64_t lzbench_lzo1c_compress(char *inbuf, size_t insize, char *outbuf, size_t outsize, size_t level, size_t, char* workmem);
    int64_t lzbench_lzo1c_decompress(char *inbuf, size_t insize, char *outbuf, size_t outsize, size_t level, size_t, char*);
    int64_t lzbench_lzo1f_compress(char *inbuf, size_t insize, char *outbuf, size_t outsize, size_t level, size_t, char* workmem);
    int64_t lzbench_lzo1f_decompress(char *inbuf, size_t insize, char *outbuf, size_t outsize, size_t level, size_t, char*);
    int64_t lzbench_lzo1x_compress(char *inbuf, size_t insize, char *outbuf, size_t outsize, size_t level, size_t, char* workmem);
    int64_t lzbench_lzo1x_decompress(char *inbuf, size_t insize, char *outbuf, size_t outsize, size_t level, size_t, char*);
    int64_t lzbench_lzo1y_compress(char *inbuf, size_t insize, char *outbuf, size_t outsize, size_t level, size_t, char* workmem);
    int64_t lzbench_lzo1y_decompress(char *inbuf, size_t insize, char *outbuf, size_t outsize, size_t level, size_t, char*);
    int64_t lzbench_lzo1z_compress(char *inbuf, size_t insize, char *outbuf, size_t outsize, size_t level, size_t, char* workmem);
    int64_t lzbench_lzo1z_decompress(char *inbuf, size_t insize, char *outbuf, size_t outsize, size_t level, size_t, char*);
    int64_t lzbench_lzo2a_compress(char *inbuf, size_t insize, char *outbuf, size_t outsize, size_t level, size_t, char* workmem);
    int64_t lzbench_lzo2a_decompress(char *inbuf, size_t insize, char *outbuf, size_t outsize, size_t level, size_t, char*);
#else
	#define lzbench_lzo_init NULL
	#define lzbench_lzo_deinit NULL
	#define lzbench_lzo1b_compress NULL
	#define lzbench_lzo1b_decompress NULL
	#define lzbench_lzo1c_compress NULL
	#define lzbench_lzo1c_decompress NULL
	#define lzbench_lzo1f_compress NULL
	#define lzbench_lzo1f_decompress NULL
	#define lzbench_lzo1x_compress NULL
	#define lzbench_lzo1x_decompress NULL
	#define lzbench_lzo1y_compress NULL
	#define lzbench_lzo1y_decompress NULL
	#define lzbench_lzo1z_compress NULL
	#define lzbench_lzo1z_decompress NULL
	#define lzbench_lzo2a_compress NULL
	#define lzbench_lzo2a_decompress NULL
#endif


#ifndef BENCH_REMOVE_LZRW
    char* lzbench_lzrw_init(size_t );
    void lzbench_lzrw_deinit(char* workmem);
	int64_t lzbench_lzrw_compress(char *inbuf, size_t insize, char *outbuf, size_t outsize, size_t level, size_t, char*);
	int64_t lzbench_lzrw_decompress(char *inbuf, size_t insize, char *outbuf, size_t outsize, size_t, size_t, char*);
#else
	#define lzbench_lzrw_init NULL
	#define lzbench_lzrw_deinit NULL
	#define lzbench_lzrw_compress NULL
	#define lzbench_lzrw_decompress NULL
#endif



#ifndef BENCH_REMOVE_PITHY
	int64_t lzbench_pithy_compress(char *inbuf, size_t insize, char *outbuf, size_t outsize, size_t level, size_t, char*);
	int64_t lzbench_pithy_decompress(char *inbuf, size_t insize, char *outbuf, size_t outsize, size_t, size_t, char*);
#else
	#define lzbench_pithy_compress NULL
	#define lzbench_pithy_decompress NULL
#endif


#ifndef BENCH_REMOVE_QUICKLZ
	int64_t lzbench_quicklz_compress(char *inbuf, size_t insize, char *outbuf, size_t outsize, size_t level, size_t, char*);
	int64_t lzbench_quicklz_decompress(char *inbuf, size_t insize, char *outbuf, size_t outsize, size_t, size_t, char*);
#else
	#define lzbench_quicklz_compress NULL
	#define lzbench_quicklz_decompress NULL
#endif


#ifndef BENCH_REMOVE_SHRINKER
	int64_t lzbench_shrinker_compress(char *inbuf, size_t insize, char *outbuf, size_t outsize, size_t level, size_t, char*);
	int64_t lzbench_shrinker_decompress(char *inbuf, size_t insize, char *outbuf, size_t outsize, size_t, size_t, char*);
#else
	#define lzbench_shrinker_compress NULL
	#define lzbench_shrinker_decompress NULL
#endif


#ifndef BENCH_REMOVE_SNAPPY
	int64_t lzbench_snappy_compress(char *inbuf, size_t insize, char *outbuf, size_t outsize, size_t level, size_t, char*);
	int64_t lzbench_snappy_decompress(char *inbuf, size_t insize, char *outbuf, size_t outsize, size_t, size_t, char*);
#else
	#define lzbench_snappy_compress NULL
	#define lzbench_snappy_decompress NULL
#endif


#ifndef BENCH_REMOVE_TORNADO
	int64_t lzbench_tornado_compress(char *inbuf, size_t insize, char *outbuf, size_t outsize, size_t level, size_t, char*);
	int64_t lzbench_tornado_decompress(char *inbuf, size_t insize, char *outbuf, size_t outsize, size_t, size_t, char*);
#else
	#define lzbench_tornado_compress NULL
	#define lzbench_tornado_decompress NULL
#endif


#ifndef BENCH_REMOVE_UCL
    int64_t lzbench_ucl_nrv2b_compress(char *inbuf, size_t insize, char *outbuf, size_t outsize, size_t level, size_t, char*);
    int64_t lzbench_ucl_nrv2b_decompress(char *inbuf, size_t insize, char *outbuf, size_t outsize, size_t level, size_t, char*);
    int64_t lzbench_ucl_nrv2d_compress(char *inbuf, size_t insize, char *outbuf, size_t outsize, size_t level, size_t, char*);
    int64_t lzbench_ucl_nrv2d_decompress(char *inbuf, size_t insize, char *outbuf, size_t outsize, size_t level, size_t, char*);
    int64_t lzbench_ucl_nrv2e_compress(char *inbuf, size_t insize, char *outbuf, size_t outsize, size_t level, size_t, char*);
    int64_t lzbench_ucl_nrv2e_decompress(char *inbuf, size_t insize, char *outbuf, size_t outsize, size_t level, size_t, char*);
#else
	#define lzbench_ucl_nrv2b_compress NULL
	#define lzbench_ucl_nrv2b_decompress NULL
	#define lzbench_ucl_nrv2d_compress NULL
	#define lzbench_ucl_nrv2d_decompress NULL
	#define lzbench_ucl_nrv2e_compress NULL
	#define lzbench_ucl_nrv2e_decompress NULL
#endif


#ifndef BENCH_REMOVE_WFLZ
    char* lzbench_wflz_init(size_t );
    void lzbench_wflz_deinit(char* workmem);
	int64_t lzbench_wflz_compress(char *inbuf, size_t insize, char *outbuf, size_t outsize, size_t level, size_t, char*);
	int64_t lzbench_wflz_decompress(char *inbuf, size_t insize, char *outbuf, size_t outsize, size_t, size_t, char*);
#else
	#define lzbench_wflz_init NULL
	#define lzbench_wflz_deinit NULL
	#define lzbench_wflz_compress NULL
	#define lzbench_wflz_decompress NULL
#endif


#ifndef BENCH_REMOVE_XZ
	int64_t lzbench_xz_compress(char *inbuf, size_t insize, char *outbuf, size_t outsize, size_t level, size_t, char*);
	int64_t lzbench_xz_decompress(char *inbuf, size_t insize, char *outbuf, size_t outsize, size_t, size_t, char*);
#else
	#define lzbench_xz_compress NULL
	#define lzbench_xz_decompress NULL
#endif


#ifndef BENCH_REMOVE_YALZ77
	int64_t lzbench_yalz77_compress(char *inbuf, size_t insize, char *outbuf, size_t outsize, size_t level, size_t, char*);
	int64_t lzbench_yalz77_decompress(char *inbuf, size_t insize, char *outbuf, size_t outsize, size_t, size_t, char*);
#else
	#define lzbench_yalz77_compress NULL
	#define lzbench_yalz77_decompress NULL
#endif


#ifndef BENCH_REMOVE_YAPPY
	int64_t lzbench_yappy_compress(char *inbuf, size_t insize, char *outbuf, size_t outsize, size_t level, size_t, char*);
	int64_t lzbench_yappy_decompress(char *inbuf, size_t insize, char *outbuf, size_t outsize, size_t, size_t, char*);
#else
	#define lzbench_yappy_compress NULL
	#define lzbench_yappy_decompress NULL
#endif


#ifndef BENCH_REMOVE_ZLIB
	int64_t lzbench_zlib_compress(char *inbuf, size_t insize, char *outbuf, size_t outsize, size_t, size_t, char*);
	int64_t lzbench_zlib_decompress(char *inbuf, size_t insize, char *outbuf, size_t outsize, size_t, size_t, char*);
#else
	#define lzbench_zlib_compress NULL
	#define lzbench_zlib_decompress NULL
#endif


#ifndef BENCH_REMOVE_ZLING
	int64_t lzbench_zling_compress(char *inbuf, size_t insize, char *outbuf, size_t outsize, size_t, size_t, char*);
	int64_t lzbench_zling_decompress(char *inbuf, size_t insize, char *outbuf, size_t outsize, size_t, size_t, char*);
#else
	#define lzbench_zling_compress NULL
	#define lzbench_zling_decompress NULL
#endif


#ifndef BENCH_REMOVE_ZSTD
	int64_t lzbench_zstd_compress(char *inbuf, size_t insize, char *outbuf, size_t outsize, size_t, size_t, char*);
	int64_t lzbench_zstd_decompress(char *inbuf, size_t insize, char *outbuf, size_t outsize, size_t, size_t, char*);
#else
	#define lzbench_zstd_compress NULL
	#define lzbench_zstd_decompress NULL
#endif


#ifndef BENCH_REMOVE_ZSTDHC
	int64_t lzbench_zstdhc_compress(char *inbuf, size_t insize, char *outbuf, size_t outsize, size_t, size_t, char*);
	int64_t lzbench_zstdhc_decompress(char *inbuf, size_t insize, char *outbuf, size_t outsize, size_t, size_t, char*);
#else
	#define lzbench_zstdhc_compress NULL
	#define lzbench_zstdhc_decompress NULL
#endif

#endif
