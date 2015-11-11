/*
    zstd - standard compression library
    Copyright (C) 2014-2015, Yann Collet.

    BSD 2-Clause License (http://www.opensource.org/licenses/bsd-license.php)

    Redistribution and use in source and binary forms, with or without
    modification, are permitted provided that the following conditions are
    met:
    * Redistributions of source code must retain the above copyright
    notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above
    copyright notice, this list of conditions and the following disclaimer
    in the documentation and/or other materials provided with the
    distribution.
    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
    "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
    LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
    A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
    OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
    SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
    LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
    DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
    THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
    (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
    OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

    You can contact the author at :
    - zstd source repository : https://github.com/Cyan4973/zstd
    - ztsd public forum : https://groups.google.com/forum/#!forum/lz4c
*/

/* ***************************************************************
*  Tuning parameters
*****************************************************************/
/*!
*  MEMORY_USAGE :
*  Memory usage formula : N->2^N Bytes (examples : 10 -> 1KB; 12 -> 4KB ; 16 -> 64KB; 20 -> 1MB; etc.)
*  Increasing memory usage improves compression ratio
*  Reduced memory usage can improve speed, due to cache effect
*/
#define ZSTD_MEMORY_USAGE 16

/*!
 * HEAPMODE :
 * Select how default compression functions will allocate memory for their hash table,
 * in memory stack (0, fastest), or in memory heap (1, requires malloc())
 * Note that compression context is fairly large, as a consequence heap memory is recommended.
 */
#ifndef ZSTD_HEAPMODE
#  define ZSTD_HEAPMODE 1
#endif /* ZSTD_HEAPMODE */

/*!
*  LEGACY_SUPPORT :
*  decompressor can decode older formats (starting from Zstd 0.1+)
*/
#ifndef ZSTD_LEGACY_SUPPORT
#  define ZSTD_LEGACY_SUPPORT 0
#endif


/* *******************************************************
*  Includes
*********************************************************/
#include <stdlib.h>      /* calloc */
#include <string.h>      /* memcpy, memmove */
#include <stdio.h>       /* debug : printf */
#include "mem.h"         /* low level memory routines */
#include "zstd_static.h"
#include "zstd_internal.h"
#include "fse_static.h"
#include "huff0.h"

#if defined(ZSTD_LEGACY_SUPPORT) && (ZSTD_LEGACY_SUPPORT==1)
#  include "zstd_legacy.h"
#endif


/* *******************************************************
*  Compiler specifics
*********************************************************/
#ifdef __AVX2__
#  include <immintrin.h>   /* AVX2 intrinsics */
#endif

#ifdef _MSC_VER    /* Visual Studio */
#  define FORCE_INLINE static __forceinline
#  include <intrin.h>                    /* For Visual 2005 */
#  pragma warning(disable : 4127)        /* disable: C4127: conditional expression is constant */
#  pragma warning(disable : 4324)        /* disable: C4324: padded structure */
#else
#  define GCC_VERSION (__GNUC__ * 100 + __GNUC_MINOR__)
#  ifdef __GNUC__
#    define FORCE_INLINE static inline __attribute__((always_inline))
#  else
#    define FORCE_INLINE static inline
#  endif
#endif


/* *******************************************************
*  Constants
*********************************************************/
#define HASH_LOG (ZSTD_MEMORY_USAGE - 2)
#define HASH_TABLESIZE (1 << HASH_LOG)
#define HASH_MASK (HASH_TABLESIZE - 1)

#define KNUTH 2654435761

#define BIT7 128
#define BIT6  64
#define BIT5  32
#define BIT4  16
#define BIT1   2
#define BIT0   1

#define KB *(1 <<10)
#define MB *(1 <<20)
#define GB *(1U<<30)

#define BLOCKSIZE (128 KB)                 /* define, for static allocation */
#define IS_RAW BIT0
#define IS_RLE BIT1

static const U32 g_maxDistance = 4 * BLOCKSIZE;
static const U32 g_maxLimit = 1 GB;

#define WORKPLACESIZE (BLOCKSIZE*3)
#define MINMATCH 4
#define LitFSELog  11
#define MLFSELog   10
#define LLFSELog   10
#define OffFSELog   9
#define MAX(a,b) ((a)<(b)?(b):(a))
#define MaxSeq MAX(MaxLL, MaxML)

#define LITERAL_NOENTROPY 63
#define COMMAND_NOENTROPY 7   /* to remove */

static const size_t ZSTD_blockHeaderSize = 3;
static const size_t ZSTD_frameHeaderSize = 4;


/* *******************************************************
*  Memory operations
**********************************************************/
static void ZSTD_copy4(void* dst, const void* src) { memcpy(dst, src, 4); }


/* **************************************
*  Local structures
****************************************/
void ZSTD_resetSeqStore(seqStore_t* ssPtr)
{
    ssPtr->offset = ssPtr->offsetStart;
    ssPtr->lit = ssPtr->litStart;
    ssPtr->litLength = ssPtr->litLengthStart;
    ssPtr->matchLength = ssPtr->matchLengthStart;
    ssPtr->dumps = ssPtr->dumpsStart;
}

struct ZSTD_CCtx_s
{
    const BYTE* base;
    U32 current;
    U32 nextUpdate;
    seqStore_t seqStore;
#ifdef __AVX2__
    __m256i hashTable[HASH_TABLESIZE>>3];
#else
    U32 hashTable[HASH_TABLESIZE];
#endif
    BYTE buffer[WORKPLACESIZE];
};


void ZSTD_resetCCtx(ZSTD_CCtx* ctx)
{
    ctx->base = NULL;
    ctx->seqStore.buffer = ctx->buffer;
    ctx->seqStore.offsetStart = (U32*) (ctx->seqStore.buffer);
    ctx->seqStore.offCodeStart = (BYTE*) (ctx->seqStore.offsetStart + (BLOCKSIZE>>2));
    ctx->seqStore.litStart = ctx->seqStore.offCodeStart + (BLOCKSIZE>>2);
    ctx->seqStore.litLengthStart =  ctx->seqStore.litStart + BLOCKSIZE;
    ctx->seqStore.matchLengthStart = ctx->seqStore.litLengthStart + (BLOCKSIZE>>2);
    ctx->seqStore.dumpsStart = ctx->seqStore.matchLengthStart + (BLOCKSIZE>>2);
    memset(ctx->hashTable, 0, sizeof(ctx->hashTable));
}

ZSTD_CCtx* ZSTD_createCCtx(void)
{
    ZSTD_CCtx* ctx = (ZSTD_CCtx*) malloc( sizeof(ZSTD_CCtx) );
    if (ctx==NULL) return NULL;
    ZSTD_resetCCtx(ctx);
    return ctx;
}

size_t ZSTD_freeCCtx(ZSTD_CCtx* ctx)
{
    free(ctx);
    return 0;
}


/* *************************************
*  Error Management
***************************************/
/*! ZSTD_isError
*   tells if a return value is an error code */
unsigned ZSTD_isError(size_t code) { return ERR_isError(code); }

/*! ZSTD_getErrorName
*   provides error code string (useful for debugging) */
const char* ZSTD_getErrorName(size_t code) { return ERR_getErrorName(code); }


/* *************************************
*  Tool functions
***************************************/
unsigned ZSTD_versionNumber (void) { return ZSTD_VERSION_NUMBER; }


/* *******************************************************
*  Compression
*********************************************************/
size_t ZSTD_compressBound(size_t srcSize)   /* maximum compressed size */
{
    return FSE_compressBound(srcSize) + 12;
}


size_t ZSTD_noCompressBlock (void* dst, size_t maxDstSize, const void* src, size_t srcSize)
{
    BYTE* const ostart = (BYTE* const)dst;

    if (srcSize + ZSTD_blockHeaderSize > maxDstSize) return ERROR(dstSize_tooSmall);
    memcpy(ostart + ZSTD_blockHeaderSize, src, srcSize);

    /* Build header */
    ostart[0]  = (BYTE)(srcSize>>16);
    ostart[1]  = (BYTE)(srcSize>>8);
    ostart[2]  = (BYTE) srcSize;
    ostart[0] += (BYTE)(bt_raw<<6);   /* is a raw (uncompressed) block */

    return ZSTD_blockHeaderSize+srcSize;
}


static size_t ZSTD_compressRawLiteralsBlock (void* dst, size_t maxDstSize, const void* src, size_t srcSize)
{
    BYTE* const ostart = (BYTE* const)dst;

    if (srcSize + 3 > maxDstSize) return ERROR(dstSize_tooSmall);

    MEM_writeLE32(dst, ((U32)srcSize << 2) | IS_RAW);
    memcpy(ostart + 3, src, srcSize);
    return srcSize + 3;
}

static size_t ZSTD_compressRleLiteralsBlock (void* dst, size_t maxDstSize, const void* src, size_t srcSize)
{
    BYTE* const ostart = (BYTE* const)dst;

    (void)maxDstSize;
    MEM_writeLE32(dst, ((U32)srcSize << 2) | IS_RLE);  /* note : maxDstSize > litHeaderSize > 4 */
    ostart[3] = *(const BYTE*)src;
    return 4;
}

size_t ZSTD_minGain(size_t srcSize) { return (srcSize >> 6) + 1; }

static size_t ZSTD_compressLiterals (void* dst, size_t maxDstSize,
                               const void* src, size_t srcSize)
{
    const size_t minGain = ZSTD_minGain(srcSize);
    BYTE* const ostart = (BYTE*)dst;
    size_t hsize;
    static const size_t litHeaderSize = 5;

    if (maxDstSize < litHeaderSize+1) return ERROR(dstSize_tooSmall);   /* not enough space for compression */

    hsize = HUF_compress(ostart+litHeaderSize, maxDstSize-litHeaderSize, src, srcSize);

    if ((hsize==0) || (hsize >= srcSize - minGain)) return ZSTD_compressRawLiteralsBlock(dst, maxDstSize, src, srcSize);
    if (hsize==1) return ZSTD_compressRleLiteralsBlock(dst, maxDstSize, src, srcSize);

    /* Build header */
    {
        ostart[0]  = (BYTE)(srcSize << 2); /* is a block, is compressed */
        ostart[1]  = (BYTE)(srcSize >> 6);
        ostart[2]  = (BYTE)(srcSize >>14);
        ostart[2] += (BYTE)(hsize << 5);
        ostart[3]  = (BYTE)(hsize >> 3);
        ostart[4]  = (BYTE)(hsize >>11);
    }

    return hsize+litHeaderSize;
}


size_t ZSTD_compressSequences(BYTE* dst, size_t maxDstSize,
                        const seqStore_t* seqStorePtr,
                              size_t srcSize)
{
    U32 count[MaxSeq+1];
    S16 norm[MaxSeq+1];
    size_t mostFrequent;
    U32 max = 255;
    U32 tableLog = 11;
    U32 CTable_LitLength  [FSE_CTABLE_SIZE_U32(LLFSELog, MaxLL )];
    U32 CTable_OffsetBits [FSE_CTABLE_SIZE_U32(OffFSELog,MaxOff)];
    U32 CTable_MatchLength[FSE_CTABLE_SIZE_U32(MLFSELog, MaxML )];
    U32 LLtype, Offtype, MLtype;   /* compressed, raw or rle */
    const BYTE* const op_lit_start = seqStorePtr->litStart;
    const BYTE* const llTable = seqStorePtr->litLengthStart;
    const BYTE* const llPtr = seqStorePtr->litLength;
    const BYTE* const mlTable = seqStorePtr->matchLengthStart;
    const U32*  const offsetTable = seqStorePtr->offsetStart;
    BYTE* const offCodeTable = seqStorePtr->offCodeStart;
    BYTE* op = dst;
    BYTE* const oend = dst + maxDstSize;
    const size_t nbSeq = llPtr - llTable;
    const size_t minGain = ZSTD_minGain(srcSize);
    const size_t maxCSize = srcSize - minGain;
    BYTE* seqHead;


    /* Compress literals */
    {
        size_t cSize;
        size_t litSize = seqStorePtr->lit - op_lit_start;

        if (litSize <= LITERAL_NOENTROPY)
            cSize = ZSTD_compressRawLiteralsBlock(op, maxDstSize, op_lit_start, litSize);
        else
            cSize = ZSTD_compressLiterals(op, maxDstSize, op_lit_start, litSize);
        if (ZSTD_isError(cSize)) return cSize;
        op += cSize;
    }

    /* Sequences Header */
    if ((oend-op) < MIN_SEQUENCES_SIZE)
        return ERROR(dstSize_tooSmall);
    MEM_writeLE16(op, (U16)nbSeq); op+=2;
    seqHead = op;

    /* dumps : contains too large lengths */
    {
        size_t dumpsLength = seqStorePtr->dumps - seqStorePtr->dumpsStart;
        if (dumpsLength < 512)
        {
            op[0] = (BYTE)(dumpsLength >> 8);
            op[1] = (BYTE)(dumpsLength);
            op += 2;
        }
        else
        {
            op[0] = 2;
            op[1] = (BYTE)(dumpsLength>>8);
            op[2] = (BYTE)(dumpsLength);
            op += 3;
        }
        if ((size_t)(oend-op) < dumpsLength+6) return ERROR(dstSize_tooSmall);
        memcpy(op, seqStorePtr->dumpsStart, dumpsLength);
        op += dumpsLength;
    }

    /* CTable for Literal Lengths */
    max = MaxLL;
    mostFrequent = FSE_countFast(count, &max, seqStorePtr->litLengthStart, nbSeq);
    if ((mostFrequent == nbSeq) && (nbSeq > 2))
    {
        *op++ = *(seqStorePtr->litLengthStart);
        FSE_buildCTable_rle(CTable_LitLength, (BYTE)max);
        LLtype = bt_rle;
    }
    else if ((nbSeq < 64) || (mostFrequent < (nbSeq >> (LLbits-1))))
    {
        FSE_buildCTable_raw(CTable_LitLength, LLbits);
        LLtype = bt_raw;
    }
    else
    {
        size_t NCountSize;
        tableLog = FSE_optimalTableLog(LLFSELog, nbSeq, max);
        FSE_normalizeCount(norm, tableLog, count, nbSeq, max);
        NCountSize = FSE_writeNCount(op, oend-op, norm, max, tableLog);   /* overflow protected */
        if (FSE_isError(NCountSize)) return ERROR(GENERIC);
        op += NCountSize;
        FSE_buildCTable(CTable_LitLength, norm, max, tableLog);
        LLtype = bt_compressed;
    }

    /* CTable for Offsets codes */
    {
        /* create Offset codes */
        size_t i;
        max = MaxOff;
        for (i=0; i<nbSeq; i++)
        {
            offCodeTable[i] = (BYTE)ZSTD_highbit(offsetTable[i]) + 1;
            if (offsetTable[i]==0) offCodeTable[i]=0;
        }
        mostFrequent = FSE_countFast(count, &max, offCodeTable, nbSeq);
    }
    if ((mostFrequent == nbSeq) && (nbSeq > 2))
    {
        *op++ = *offCodeTable;
        FSE_buildCTable_rle(CTable_OffsetBits, (BYTE)max);
        Offtype = bt_rle;
    }
    else if ((nbSeq < 64) || (mostFrequent < (nbSeq >> (Offbits-1))))
    {
        FSE_buildCTable_raw(CTable_OffsetBits, Offbits);
        Offtype = bt_raw;
    }
    else
    {
        size_t NCountSize;
        tableLog = FSE_optimalTableLog(OffFSELog, nbSeq, max);
        FSE_normalizeCount(norm, tableLog, count, nbSeq, max);
        NCountSize = FSE_writeNCount(op, oend-op, norm, max, tableLog);   /* overflow protected */
        if (FSE_isError(NCountSize)) return ERROR(GENERIC);
        op += NCountSize;
        FSE_buildCTable(CTable_OffsetBits, norm, max, tableLog);
        Offtype = bt_compressed;
    }

    /* CTable for MatchLengths */
    max = MaxML;
    mostFrequent = FSE_countFast(count, &max, seqStorePtr->matchLengthStart, nbSeq);
    if ((mostFrequent == nbSeq) && (nbSeq > 2))
    {
        *op++ = *seqStorePtr->matchLengthStart;
        FSE_buildCTable_rle(CTable_MatchLength, (BYTE)max);
        MLtype = bt_rle;
    }
    else if ((nbSeq < 64) || (mostFrequent < (nbSeq >> (MLbits-1))))
    {
        FSE_buildCTable_raw(CTable_MatchLength, MLbits);
        MLtype = bt_raw;
    }
    else
    {
        size_t NCountSize;
        tableLog = FSE_optimalTableLog(MLFSELog, nbSeq, max);
        FSE_normalizeCount(norm, tableLog, count, nbSeq, max);
        NCountSize = FSE_writeNCount(op, oend-op, norm, max, tableLog);   /* overflow protected */
        if (FSE_isError(NCountSize)) return ERROR(GENERIC);
        op += NCountSize;
        FSE_buildCTable(CTable_MatchLength, norm, max, tableLog);
        MLtype = bt_compressed;
    }

    seqHead[0] += (BYTE)((LLtype<<6) + (Offtype<<4) + (MLtype<<2));

    /* Encoding Sequences */
    {
        size_t streamSize, errorCode;
        BIT_CStream_t blockStream;
        FSE_CState_t stateMatchLength;
        FSE_CState_t stateOffsetBits;
        FSE_CState_t stateLitLength;
        int i;

        errorCode = BIT_initCStream(&blockStream, op, oend-op);
        if (ERR_isError(errorCode)) return ERROR(dstSize_tooSmall);   /* not enough space remaining */
        FSE_initCState(&stateMatchLength, CTable_MatchLength);
        FSE_initCState(&stateOffsetBits, CTable_OffsetBits);
        FSE_initCState(&stateLitLength, CTable_LitLength);

        for (i=(int)nbSeq-1; i>=0; i--)
        {
            BYTE matchLength = mlTable[i];
            U32  offset = offsetTable[i];
            BYTE offCode = offCodeTable[i];                                 /* 32b*/  /* 64b*/
            U32 nbBits = (offCode-1) * (!!offCode);
            BYTE litLength = llTable[i];                                    /* (7)*/  /* (7)*/
            FSE_encodeSymbol(&blockStream, &stateMatchLength, matchLength); /* 17 */  /* 17 */
            if (MEM_32bits()) BIT_flushBits(&blockStream);                 /*  7 */
            BIT_addBits(&blockStream, offset, nbBits);                      /* 32 */  /* 42 */
            if (MEM_32bits()) BIT_flushBits(&blockStream);                 /*  7 */
            FSE_encodeSymbol(&blockStream, &stateOffsetBits, offCode);      /* 16 */  /* 51 */
            FSE_encodeSymbol(&blockStream, &stateLitLength, litLength);     /* 26 */  /* 61 */
            BIT_flushBits(&blockStream);                                    /*  7 */  /*  7 */
        }

        FSE_flushCState(&blockStream, &stateMatchLength);
        FSE_flushCState(&blockStream, &stateOffsetBits);
        FSE_flushCState(&blockStream, &stateLitLength);

        streamSize = BIT_closeCStream(&blockStream);
        if (streamSize==0) return ERROR(dstSize_tooSmall);   /* not enough space */
        op += streamSize;
    }

    /* check compressibility */
    if ((size_t)(op-dst) >= maxCSize) return 0;

    return op - dst;
}


//static const U32 hashMask = (1<<HASH_LOG)-1;

//static U32   ZSTD_hashPtr(const void* p) { return (U32) _bextr_u64(*(U64*)p * prime7bytes, (56-HASH_LOG), HASH_LOG); }
//static U32   ZSTD_hashPtr(const void* p) { return ( (*(U64*)p * prime7bytes) << 8 >> (64-HASH_LOG)); }
//static U32   ZSTD_hashPtr(const void* p) { return ( (*(U64*)p * prime7bytes) >> (56-HASH_LOG)) & ((1<<HASH_LOG)-1); }
//static U32   ZSTD_hashPtr(const void* p) { return ( ((*(U64*)p & 0xFFFFFFFFFFFFFF) * prime7bytes) >> (64-HASH_LOG)); }

//static const U64 prime8bytes = 14923729446516375013ULL;
//static U32   ZSTD_hashPtr(const void* p) { return ( (*(U64*)p * prime8bytes) >> (64-HASH_LOG)); }

static const U64 prime7bytes =    58295818150454627ULL;
static U32   ZSTD_hashPtr(const void* p) { return ( (MEM_read64(p) * prime7bytes) >> (56-HASH_LOG)) & HASH_MASK; }

//static const U64 prime6bytes =      227718039650203ULL;
//static U32   ZSTD_hashPtr(const void* p) { return ( (MEM_read64(p) * prime6bytes) >> (48-HASH_LOG)) & HASH_MASK; }

//static const U64 prime5bytes =         889523592379ULL;
//static U32   ZSTD_hashPtr(const void* p) { return ( (*(U64*)p * prime5bytes) >> (40-HASH_LOG)) & HASH_MASK; }

//static U32   ZSTD_hashPtr(const void* p) { return ( (*(U32*)p * KNUTH) >> (32-HASH_LOG)); }

static const BYTE* ZSTD_updateMatch(U32* table, const BYTE* p, const BYTE* start)
{
    U32 h = ZSTD_hashPtr(p);
    const BYTE* r;
    r = table[h] + start;
    table[h] = (U32)(p-start);
    return r;
}

static int ZSTD_checkMatch(const BYTE* match, const BYTE* ip)
{
    return MEM_read32(match) == MEM_read32(ip);
}

static void  ZSTD_addPtr(U32* table, const BYTE* p, const BYTE* start) { table[ZSTD_hashPtr(p)] = (U32)(p-start); }


static size_t ZSTD_compressBlock(ZSTD_CCtx* ctx, void* dst, size_t maxDstSize, const void* src, size_t srcSize)
{
    U32*  HashTable = (U32*)(ctx->hashTable);
    seqStore_t* seqStorePtr = &(ctx->seqStore);
    const BYTE* const base = ctx->base;

    const BYTE* const istart = (const BYTE*)src;
    const BYTE* ip = istart + 1;
    const BYTE* anchor = istart;
    const BYTE* const iend = istart + srcSize;
    const BYTE* const ilimit = iend - 8;

    size_t offset_2=4, offset_1=4;


    /* init */
    if (ip-base < 4)
    {
        ZSTD_addPtr(HashTable, ip+0, base);
        ZSTD_addPtr(HashTable, ip+1, base);
        ZSTD_addPtr(HashTable, ip+2, base);
        ZSTD_addPtr(HashTable, ip+3, base);
        ip += 4;
    }
    ZSTD_resetSeqStore(seqStorePtr);

    /* Main Search Loop */
    while (ip < ilimit)  /* < instead of <=, because unconditionnal ZSTD_addPtr(ip+1) */
    {
        const BYTE* match = ZSTD_updateMatch(HashTable, ip, base);

        if (ZSTD_checkMatch(ip-offset_2,ip)) match = ip-offset_2;
        if (!ZSTD_checkMatch(match,ip)) { ip += ((ip-anchor) >> g_searchStrength) + 1; offset_2 = offset_1; continue; }
        while ((ip>anchor) && (match>base) && (ip[-1] == match[-1])) { ip--; match--; }  /* catch up */

        {
            size_t litLength = ip-anchor;
            size_t matchLength = ZSTD_count(ip+MINMATCH, match+MINMATCH, iend);
            size_t offsetCode = ip-match;
            if (offsetCode == offset_2) offsetCode = 0;
            offset_2 = offset_1;
            offset_1 = ip-match;
            ZSTD_storeSeq(seqStorePtr, litLength, anchor, offsetCode, matchLength);

            /* Fill Table */
            ZSTD_addPtr(HashTable, ip+1, base);
            ip += matchLength + MINMATCH;
            anchor = ip;
            if (ip < ilimit) /* same test as loop, for speed */
                ZSTD_addPtr(HashTable, ip-2, base);
        }
    }

    /* Last Literals */
    {
        size_t lastLLSize = iend - anchor;
        memcpy(seqStorePtr->lit, anchor, lastLLSize);
        seqStorePtr->lit += lastLLSize;
    }

    /* Finale compression stage */
    return ZSTD_compressSequences((BYTE*)dst, maxDstSize,
                                  seqStorePtr, srcSize);
}


size_t ZSTD_compressBegin(ZSTD_CCtx* ctx, void* dst, size_t maxDstSize)
{
    /* Sanity check */
    if (maxDstSize < ZSTD_frameHeaderSize) return ERROR(dstSize_tooSmall);

    /* Init */
    ZSTD_resetCCtx(ctx);

    /* Write Header */
    MEM_writeLE32(dst, ZSTD_magicNumber);

    return ZSTD_frameHeaderSize;
}


static void ZSTD_scaleDownCtx(ZSTD_CCtx* ctx, const U32 limit)
{
    int i;

#if defined(__AVX2__)
    /* AVX2 version */
    __m256i* h = ctx->hashTable;
    const __m256i limit8 = _mm256_set1_epi32(limit);
    for (i=0; i<(HASH_TABLESIZE>>3); i++)
    {
        __m256i src =_mm256_loadu_si256((const __m256i*)(h+i));
  const __m256i dec = _mm256_min_epu32(src, limit8);
                src = _mm256_sub_epi32(src, dec);
        _mm256_storeu_si256((__m256i*)(h+i), src);
    }
#else
    /* this should be auto-vectorized by compiler */
    U32* h = ctx->hashTable;
    for (i=0; i<HASH_TABLESIZE; ++i)
    {
        U32 dec;
        if (h[i] > limit) dec = limit; else dec = h[i];
        h[i] -= dec;
    }
#endif
}


static void ZSTD_limitCtx(ZSTD_CCtx* ctx, const U32 limit)
{
    int i;

    if (limit > g_maxLimit)
    {
        ZSTD_scaleDownCtx(ctx, limit);
        ctx->base += limit;
        ctx->current -= limit;
        ctx->nextUpdate -= limit;
        return;
    }

#if defined(__AVX2__)
    /* AVX2 version */
    {
        __m256i* h = ctx->hashTable;
        const __m256i limit8 = _mm256_set1_epi32(limit);
        //printf("Address h : %0X\n", (U32)h);    // address test
        for (i=0; i<(HASH_TABLESIZE>>3); i++)
        {
            __m256i src =_mm256_loadu_si256((const __m256i*)(h+i));   // Unfortunately, clang doesn't guarantee 32-bytes alignment
                    src = _mm256_max_epu32(src, limit8);
            _mm256_storeu_si256((__m256i*)(h+i), src);
        }
    }
#else
    /* this should be auto-vectorized by compiler */
    {
        U32* h = (U32*)(ctx->hashTable);
        for (i=0; i<HASH_TABLESIZE; ++i)
        {
            if (h[i] < limit) h[i] = limit;
        }
    }
#endif
}


size_t ZSTD_compressContinue(ZSTD_CCtx* ctx, void* dst, size_t maxDstSize, const void* src, size_t srcSize)
{
    const BYTE* const istart = (const BYTE* const)src;
    const BYTE* ip = istart;
    BYTE* const ostart = (BYTE* const)dst;
    BYTE* op = ostart;
    const U32 updateRate = 2 * BLOCKSIZE;

    /*  Init */
    if (ctx->base==NULL)
        ctx->base = (const BYTE*)src, ctx->current=0, ctx->nextUpdate = g_maxDistance;
    if (src != ctx->base + ctx->current)   /* not contiguous */
    {
        ZSTD_resetCCtx(ctx);
        ctx->base = (const BYTE*)src;
        ctx->current = 0;
    }
    ctx->current += (U32)srcSize;

    while (srcSize)
    {
        size_t cSize;
        size_t blockSize = BLOCKSIZE;
        if (blockSize > srcSize) blockSize = srcSize;

        if (maxDstSize < 2*ZSTD_blockHeaderSize+1)  /* one RLE block + endMark */
            return ERROR(dstSize_tooSmall);

        /* update hash table */
        if (g_maxDistance <= BLOCKSIZE)   /* static test ; yes == blocks are independent */
        {
            ZSTD_resetCCtx(ctx);
            ctx->base = ip;
            ctx->current=0;
        }
        else if (ip >= ctx->base + ctx->nextUpdate)
        {
            ctx->nextUpdate += updateRate;
            ZSTD_limitCtx(ctx, ctx->nextUpdate - g_maxDistance);
        }

        /* compress */
        cSize = ZSTD_compressBlock(ctx, op+ZSTD_blockHeaderSize, maxDstSize-ZSTD_blockHeaderSize, ip, blockSize);
        if (cSize == 0)
        {
            cSize = ZSTD_noCompressBlock(op, maxDstSize, ip, blockSize);   /* block is not compressible */
            if (ZSTD_isError(cSize)) return cSize;
        }
        else
        {
            if (ZSTD_isError(cSize)) return cSize;
            op[0] = (BYTE)(cSize>>16);
            op[1] = (BYTE)(cSize>>8);
            op[2] = (BYTE)cSize;
            op[0] += (BYTE)(bt_compressed << 6); /* is a compressed block */
            cSize += 3;
        }
        op += cSize;
        maxDstSize -= cSize;
        ip += blockSize;
        srcSize -= blockSize;
    }

    return op-ostart;
}


size_t ZSTD_compressEnd(ZSTD_CCtx*  ctx, void* dst, size_t maxDstSize)
{
    BYTE* op = (BYTE*)dst;

    /* Sanity check */
    (void)ctx;
    if (maxDstSize < ZSTD_blockHeaderSize) return ERROR(dstSize_tooSmall);

    /* End of frame */
    op[0] = (BYTE)(bt_end << 6);
    op[1] = 0;
    op[2] = 0;

    return 3;
}


size_t ZSTD_compressCCtx(ZSTD_CCtx* ctx, void* dst, size_t maxDstSize, const void* src, size_t srcSize)
{
    BYTE* const ostart = (BYTE* const)dst;
    BYTE* op = ostart;
    size_t oSize;

    /* Header */
    oSize = ZSTD_compressBegin(ctx, dst, maxDstSize);
    if(ZSTD_isError(oSize)) return oSize;
    op += oSize;
    maxDstSize -= oSize;

    /* Compression */
    oSize = ZSTD_compressContinue(ctx, op, maxDstSize, src, srcSize);
    if (ZSTD_isError(oSize)) return oSize;
    op += oSize;
    maxDstSize -= oSize;

    /* Close frame */
    oSize = ZSTD_compressEnd(ctx, op, maxDstSize);
    if(ZSTD_isError(oSize)) return oSize;
    op += oSize;

    return (op - ostart);
}


size_t ZSTD_compress(void* dst, size_t maxDstSize, const void* src, size_t srcSize)
{
    size_t r;
#if defined(ZSTD_HEAPMODE) && (ZSTD_HEAPMODE==1)
    ZSTD_CCtx* ctx;
    ctx = ZSTD_createCCtx();
    if (ctx==NULL) return ERROR(GENERIC);
# else
    ZSTD_CCtx ctxBody;
    ZSTD_CCtx* const ctx = &ctxBody;
# endif

    r = ZSTD_compressCCtx(ctx, dst, maxDstSize, src, srcSize);

#if defined(ZSTD_HEAPMODE) && (ZSTD_HEAPMODE==1)
    ZSTD_freeCCtx(ctx);
#endif

    return r;
}


/* *************************************************************
*   Decompression section
***************************************************************/
struct ZSTD_DCtx_s
{
    U32 LLTable[FSE_DTABLE_SIZE_U32(LLFSELog)];
    U32 OffTable[FSE_DTABLE_SIZE_U32(OffFSELog)];
    U32 MLTable[FSE_DTABLE_SIZE_U32(MLFSELog)];
    void* previousDstEnd;
    void* base;
    size_t expected;
    blockType_t bType;
    U32 phase;
    const BYTE* litPtr;
    size_t litBufSize;
    size_t litSize;
    BYTE litBuffer[BLOCKSIZE + 8 /* margin for wildcopy */];
};   /* typedef'd to ZSTD_Dctx within "zstd_static.h" */


size_t ZSTD_getcBlockSize(const void* src, size_t srcSize, blockProperties_t* bpPtr)
{
    const BYTE* const in = (const BYTE* const)src;
    BYTE headerFlags;
    U32 cSize;

    if (srcSize < 3) return ERROR(srcSize_wrong);

    headerFlags = *in;
    cSize = in[2] + (in[1]<<8) + ((in[0] & 7)<<16);

    bpPtr->blockType = (blockType_t)(headerFlags >> 6);
    bpPtr->origSize = (bpPtr->blockType == bt_rle) ? cSize : 0;

    if (bpPtr->blockType == bt_end) return 0;
    if (bpPtr->blockType == bt_rle) return 1;
    return cSize;
}

static size_t ZSTD_copyUncompressedBlock(void* dst, size_t maxDstSize, const void* src, size_t srcSize)
{
    if (srcSize > maxDstSize) return ERROR(dstSize_tooSmall);
    memcpy(dst, src, srcSize);
    return srcSize;
}


/** ZSTD_decompressLiterals
    @return : nb of bytes read from src, or an error code*/
static size_t ZSTD_decompressLiterals(void* dst, size_t* maxDstSizePtr,
                                const void* src, size_t srcSize)
{
    const BYTE* ip = (const BYTE*)src;

    const size_t litSize = (MEM_readLE32(src) & 0x1FFFFF) >> 2;   /* no buffer issue : srcSize >= MIN_CBLOCK_SIZE */
    const size_t litCSize = (MEM_readLE32(ip+2) & 0xFFFFFF) >> 5;   /* no buffer issue : srcSize >= MIN_CBLOCK_SIZE */

    if (litSize > *maxDstSizePtr) return ERROR(corruption_detected);
    if (litCSize + 5 > srcSize) return ERROR(corruption_detected);

    if (HUF_isError(HUF_decompress(dst, litSize, ip+5, litCSize))) return ERROR(corruption_detected);

    *maxDstSizePtr = litSize;
    return litCSize + 5;
}


/** ZSTD_decodeLiteralsBlock
    @return : nb of bytes read from src (< srcSize )*/
size_t ZSTD_decodeLiteralsBlock(void* ctx,
                          const void* src, size_t srcSize)   /* note : srcSize < BLOCKSIZE */
{
    ZSTD_DCtx* dctx = (ZSTD_DCtx*)ctx;
    const BYTE* const istart = (const BYTE*) src;

    /* any compressed block with literals segment must be at least this size */
    if (srcSize < MIN_CBLOCK_SIZE) return ERROR(corruption_detected);

    switch(*istart & 3)
    {
    /* compressed */
    case 0:
        {
            size_t litSize = BLOCKSIZE;
            const size_t readSize = ZSTD_decompressLiterals(dctx->litBuffer, &litSize, src, srcSize);
            dctx->litPtr = dctx->litBuffer;
            dctx->litBufSize = BLOCKSIZE+8;
            dctx->litSize = litSize;
            return readSize;   /* works if it's an error too */
        }
    case IS_RAW:
        {
            const size_t litSize = (MEM_readLE32(istart) & 0xFFFFFF) >> 2;   /* no buffer issue : srcSize >= MIN_CBLOCK_SIZE */
            if (litSize > srcSize-11)   /* risk of reading too far with wildcopy */
            {
                if (litSize > srcSize-3) return ERROR(corruption_detected);
                memcpy(dctx->litBuffer, istart, litSize);
                dctx->litPtr = dctx->litBuffer;
                dctx->litBufSize = BLOCKSIZE+8;
                dctx->litSize = litSize;
                return litSize+3;
            }
            /* direct reference into compressed stream */
            dctx->litPtr = istart+3;
            dctx->litBufSize = srcSize-3;
            dctx->litSize = litSize;
            return litSize+3;        }
    case IS_RLE:
        {
            const size_t litSize = (MEM_readLE32(istart) & 0xFFFFFF) >> 2;   /* no buffer issue : srcSize >= MIN_CBLOCK_SIZE */
            if (litSize > BLOCKSIZE) return ERROR(corruption_detected);
            memset(dctx->litBuffer, istart[3], litSize);
            dctx->litPtr = dctx->litBuffer;
            dctx->litBufSize = BLOCKSIZE+8;
            dctx->litSize = litSize;
            return 4;
        }
    default:
        return ERROR(corruption_detected);   /* forbidden nominal case */
    }
}


size_t ZSTD_decodeSeqHeaders(int* nbSeq, const BYTE** dumpsPtr, size_t* dumpsLengthPtr,
                         FSE_DTable* DTableLL, FSE_DTable* DTableML, FSE_DTable* DTableOffb,
                         const void* src, size_t srcSize)
{
    const BYTE* const istart = (const BYTE* const)src;
    const BYTE* ip = istart;
    const BYTE* const iend = istart + srcSize;
    U32 LLtype, Offtype, MLtype;
    U32 LLlog, Offlog, MLlog;
    size_t dumpsLength;

    /* check */
    if (srcSize < 5) return ERROR(srcSize_wrong);

    /* SeqHead */
    *nbSeq = MEM_readLE16(ip); ip+=2;
    LLtype  = *ip >> 6;
    Offtype = (*ip >> 4) & 3;
    MLtype  = (*ip >> 2) & 3;
    if (*ip & 2)
    {
        dumpsLength  = ip[2];
        dumpsLength += ip[1] << 8;
        ip += 3;
    }
    else
    {
        dumpsLength  = ip[1];
        dumpsLength += (ip[0] & 1) << 8;
        ip += 2;
    }
    *dumpsPtr = ip;
    ip += dumpsLength;
    *dumpsLengthPtr = dumpsLength;

    /* check */
    if (ip > iend-3) return ERROR(srcSize_wrong); /* min : all 3 are "raw", hence no header, but at least xxLog bits per type */

    /* sequences */
    {
        S16 norm[MaxML+1];    /* assumption : MaxML >= MaxLL and MaxOff */
        size_t headerSize;

        /* Build DTables */
        switch(LLtype)
        {
        U32 max;
        case bt_rle :
            LLlog = 0;
            FSE_buildDTable_rle(DTableLL, *ip++); break;
        case bt_raw :
            LLlog = LLbits;
            FSE_buildDTable_raw(DTableLL, LLbits); break;
        default :
            max = MaxLL;
            headerSize = FSE_readNCount(norm, &max, &LLlog, ip, iend-ip);
            if (FSE_isError(headerSize)) return ERROR(GENERIC);
            if (LLlog > LLFSELog) return ERROR(corruption_detected);
            ip += headerSize;
            FSE_buildDTable(DTableLL, norm, max, LLlog);
        }

        switch(Offtype)
        {
        U32 max;
        case bt_rle :
            Offlog = 0;
            if (ip > iend-2) return ERROR(srcSize_wrong);   /* min : "raw", hence no header, but at least xxLog bits */
            FSE_buildDTable_rle(DTableOffb, *ip++ & MaxOff); /* if *ip > MaxOff, data is corrupted */
            break;
        case bt_raw :
            Offlog = Offbits;
            FSE_buildDTable_raw(DTableOffb, Offbits); break;
        default :
            max = MaxOff;
            headerSize = FSE_readNCount(norm, &max, &Offlog, ip, iend-ip);
            if (FSE_isError(headerSize)) return ERROR(GENERIC);
            if (Offlog > OffFSELog) return ERROR(corruption_detected);
            ip += headerSize;
            FSE_buildDTable(DTableOffb, norm, max, Offlog);
        }

        switch(MLtype)
        {
        U32 max;
        case bt_rle :
            MLlog = 0;
            if (ip > iend-2) return ERROR(srcSize_wrong); /* min : "raw", hence no header, but at least xxLog bits */
            FSE_buildDTable_rle(DTableML, *ip++); break;
        case bt_raw :
            MLlog = MLbits;
            FSE_buildDTable_raw(DTableML, MLbits); break;
        default :
            max = MaxML;
            headerSize = FSE_readNCount(norm, &max, &MLlog, ip, iend-ip);
            if (FSE_isError(headerSize)) return ERROR(GENERIC);
            if (MLlog > MLFSELog) return ERROR(corruption_detected);
            ip += headerSize;
            FSE_buildDTable(DTableML, norm, max, MLlog);
        }
    }

    return ip-istart;
}


typedef struct {
    size_t litLength;
    size_t offset;
    size_t matchLength;
} seq_t;

typedef struct {
    BIT_DStream_t DStream;
    FSE_DState_t stateLL;
    FSE_DState_t stateOffb;
    FSE_DState_t stateML;
    size_t prevOffset;
    const BYTE* dumps;
    const BYTE* dumpsEnd;
} seqState_t;


static void ZSTD_decodeSequence(seq_t* seq, seqState_t* seqState)
{
    size_t litLength;
    size_t prevOffset;
    size_t offset;
    size_t matchLength;
    const BYTE* dumps = seqState->dumps;
    const BYTE* const de = seqState->dumpsEnd;

    /* Literal length */
    litLength = FSE_decodeSymbol(&(seqState->stateLL), &(seqState->DStream));
    prevOffset = litLength ? seq->offset : seqState->prevOffset;
    seqState->prevOffset = seq->offset;
    if (litLength == MaxLL)
    {
        U32 add = *dumps++;
        if (add < 255) litLength += add;
        else
        {
            litLength = MEM_readLE32(dumps) & 0xFFFFFF;  /* no pb : dumps is always followed by seq tables > 1 byte */
            dumps += 3;
        }
        if (dumps >= de) dumps = de-1;   /* late correction, to avoid read overflow (data is now corrupted anyway) */
    }

    /* Offset */
    {
        static const U32 offsetPrefix[MaxOff+1] = {
                1 /*fake*/, 1, 2, 4, 8, 16, 32, 64, 128, 256,
                512, 1024, 2048, 4096, 8192, 16384, 32768, 65536, 131072, 262144,
                524288, 1048576, 2097152, 4194304, 8388608, 16777216, 33554432, /*fake*/ 1, 1, 1, 1, 1 };
        U32 offsetCode, nbBits;
        offsetCode = FSE_decodeSymbol(&(seqState->stateOffb), &(seqState->DStream));   /* <= maxOff, by table construction */
        if (MEM_32bits()) BIT_reloadDStream(&(seqState->DStream));
        nbBits = offsetCode - 1;
        if (offsetCode==0) nbBits = 0;   /* cmove */
        offset = offsetPrefix[offsetCode] + BIT_readBits(&(seqState->DStream), nbBits);
        if (MEM_32bits()) BIT_reloadDStream(&(seqState->DStream));
        if (offsetCode==0) offset = prevOffset;   /* cmove */
    }

    /* MatchLength */
    matchLength = FSE_decodeSymbol(&(seqState->stateML), &(seqState->DStream));
    if (matchLength == MaxML)
    {
        U32 add = *dumps++;
        if (add < 255) matchLength += add;
        else
        {
            matchLength = MEM_readLE32(dumps) & 0xFFFFFF;  /* no pb : dumps is always followed by seq tables > 1 byte */
            dumps += 3;
        }
        if (dumps >= de) dumps = de-1;   /* late correction, to avoid read overflow (data is now corrupted anyway) */
    }
    matchLength += MINMATCH;

    /* save result */
    seq->litLength = litLength;
    seq->offset = offset;
    seq->matchLength = matchLength;
    seqState->dumps = dumps;
}


static size_t ZSTD_execSequence(BYTE* op,
                                seq_t sequence,
                                const BYTE** litPtr, const BYTE* const litLimit_8,
                                BYTE* const base, BYTE* const oend)
{
    static const int dec32table[] = {0, 1, 2, 1, 4, 4, 4, 4};   /* added */
    static const int dec64table[] = {8, 8, 8, 7, 8, 9,10,11};   /* substracted */
    const BYTE* const ostart = op;
    BYTE* const oLitEnd = op + sequence.litLength;
    BYTE* const oMatchEnd = op + sequence.litLength + sequence.matchLength;   /* risk : address space overflow (32-bits) */
    BYTE* const oend_8 = oend-8;
    const BYTE* const litEnd = *litPtr + sequence.litLength;

    /* check */
    if (oLitEnd > oend_8) return ERROR(dstSize_tooSmall);   /* last match must start at a minimum distance of 8 from oend */
    if (oMatchEnd > oend) return ERROR(dstSize_tooSmall);   /* overwrite beyond dst buffer */
    if (litEnd > litLimit_8) return ERROR(corruption_detected);   /* risk read beyond lit buffer */

    /* copy Literals */
    ZSTD_wildcopy(op, *litPtr, sequence.litLength);   /* note : oLitEnd <= oend-8 : no risk of overwrite beyond oend */
    op = oLitEnd;
    *litPtr = litEnd;   /* update for next sequence */

    /* copy Match */
    {
        const BYTE* match = op - sequence.offset;

        /* check */
        //if (match > op) return ERROR(corruption_detected);   /* address space overflow test (is clang optimizer removing this test ?) */
        if (sequence.offset > (size_t)op) return ERROR(corruption_detected);   /* address space overflow test (this test seems kept by clang optimizer) */
        if (match < base) return ERROR(corruption_detected);

        /* close range match, overlap */
        if (sequence.offset < 8)
        {
            const int dec64 = dec64table[sequence.offset];
            op[0] = match[0];
            op[1] = match[1];
            op[2] = match[2];
            op[3] = match[3];
            match += dec32table[sequence.offset];
            ZSTD_copy4(op+4, match);
            match -= dec64;
        }
        else
        {
            ZSTD_copy8(op, match);
        }
        op += 8; match += 8;

        if (oMatchEnd > oend-12)
        {
            if (op < oend_8)
            {
                ZSTD_wildcopy(op, match, oend_8 - op);
                match += oend_8 - op;
                op = oend_8;
            }
            while (op < oMatchEnd) *op++ = *match++;
        }
        else
        {
            ZSTD_wildcopy(op, match, sequence.matchLength-8);   /* works even if matchLength < 8 */
        }
    }

    return oMatchEnd - ostart;
}

static size_t ZSTD_decompressSequences(
                               void* ctx,
                               void* dst, size_t maxDstSize,
                         const void* seqStart, size_t seqSize)
{
    ZSTD_DCtx* dctx = (ZSTD_DCtx*)ctx;
    const BYTE* ip = (const BYTE*)seqStart;
    const BYTE* const iend = ip + seqSize;
    BYTE* const ostart = (BYTE* const)dst;
    BYTE* op = ostart;
    BYTE* const oend = ostart + maxDstSize;
    size_t errorCode, dumpsLength;
    const BYTE* litPtr = dctx->litPtr;
    const BYTE* const litLimit_8 = litPtr + dctx->litBufSize - 8;
    const BYTE* const litEnd = litPtr + dctx->litSize;
    int nbSeq;
    const BYTE* dumps;
    U32* DTableLL = dctx->LLTable;
    U32* DTableML = dctx->MLTable;
    U32* DTableOffb = dctx->OffTable;
    BYTE* const base = (BYTE*) (dctx->base);

    /* Build Decoding Tables */
    errorCode = ZSTD_decodeSeqHeaders(&nbSeq, &dumps, &dumpsLength,
                                      DTableLL, DTableML, DTableOffb,
                                      ip, iend-ip);
    if (ZSTD_isError(errorCode)) return errorCode;
    ip += errorCode;

    /* Regen sequences */
    {
        seq_t sequence;
        seqState_t seqState;

        memset(&sequence, 0, sizeof(sequence));
        sequence.offset = 4;
        seqState.dumps = dumps;
        seqState.dumpsEnd = dumps + dumpsLength;
        seqState.prevOffset = 4;
        errorCode = BIT_initDStream(&(seqState.DStream), ip, iend-ip);
        if (ERR_isError(errorCode)) return ERROR(corruption_detected);
        FSE_initDState(&(seqState.stateLL), &(seqState.DStream), DTableLL);
        FSE_initDState(&(seqState.stateOffb), &(seqState.DStream), DTableOffb);
        FSE_initDState(&(seqState.stateML), &(seqState.DStream), DTableML);

        for ( ; (BIT_reloadDStream(&(seqState.DStream)) <= BIT_DStream_completed) && (nbSeq>0) ; )
        {
            size_t oneSeqSize;
            nbSeq--;
            ZSTD_decodeSequence(&sequence, &seqState);
            oneSeqSize = ZSTD_execSequence(op, sequence, &litPtr, litLimit_8, base, oend);
            if (ZSTD_isError(oneSeqSize)) return oneSeqSize;
            op += oneSeqSize;
        }

        /* check if reached exact end */
        if ( !BIT_endOfDStream(&(seqState.DStream)) ) return ERROR(corruption_detected);   /* requested too much : data is corrupted */
        if (nbSeq<0) return ERROR(corruption_detected);   /* requested too many sequences : data is corrupted */

        /* last literal segment */
        {
            size_t lastLLSize = litEnd - litPtr;
            if (litPtr > litEnd) return ERROR(corruption_detected);
            if (op+lastLLSize > oend) return ERROR(dstSize_tooSmall);
            if (op != litPtr) memcpy(op, litPtr, lastLLSize);
            op += lastLLSize;
        }
    }

    return op-ostart;
}


static size_t ZSTD_decompressBlock(
                            void* ctx,
                            void* dst, size_t maxDstSize,
                      const void* src, size_t srcSize)
{
    /* blockType == blockCompressed */
    const BYTE* ip = (const BYTE*)src;

    /* Decode literals sub-block */
    size_t litCSize = ZSTD_decodeLiteralsBlock(ctx, src, srcSize);
    if (ZSTD_isError(litCSize)) return litCSize;
    ip += litCSize;
    srcSize -= litCSize;

    return ZSTD_decompressSequences(ctx, dst, maxDstSize, ip, srcSize);
}


static size_t ZSTD_decompressDCtx(void* ctx, void* dst, size_t maxDstSize, const void* src, size_t srcSize)
{
    const BYTE* ip = (const BYTE*)src;
    const BYTE* iend = ip + srcSize;
    BYTE* const ostart = (BYTE* const)dst;
    BYTE* op = ostart;
    BYTE* const oend = ostart + maxDstSize;
    size_t remainingSize = srcSize;
    U32 magicNumber;
    blockProperties_t blockProperties;

    /* Frame Header */
    if (srcSize < ZSTD_frameHeaderSize+ZSTD_blockHeaderSize) return ERROR(srcSize_wrong);
    magicNumber = MEM_readLE32(src);
#if defined(ZSTD_LEGACY_SUPPORT) && (ZSTD_LEGACY_SUPPORT==1)
    if (ZSTD_isLegacy(magicNumber))
        return ZSTD_decompressLegacy(dst, maxDstSize, src, srcSize, magicNumber);
#endif
    if (magicNumber != ZSTD_magicNumber) return ERROR(prefix_unknown);
    ip += ZSTD_frameHeaderSize; remainingSize -= ZSTD_frameHeaderSize;

    /* Loop on each block */
    while (1)
    {
        size_t decodedSize=0;
        size_t cBlockSize = ZSTD_getcBlockSize(ip, iend-ip, &blockProperties);
        if (ZSTD_isError(cBlockSize)) return cBlockSize;

        ip += ZSTD_blockHeaderSize;
        remainingSize -= ZSTD_blockHeaderSize;
        if (cBlockSize > remainingSize) return ERROR(srcSize_wrong);

        switch(blockProperties.blockType)
        {
        case bt_compressed:
            decodedSize = ZSTD_decompressBlock(ctx, op, oend-op, ip, cBlockSize);
            break;
        case bt_raw :
            decodedSize = ZSTD_copyUncompressedBlock(op, oend-op, ip, cBlockSize);
            break;
        case bt_rle :
            return ERROR(GENERIC);   /* not yet supported */
            break;
        case bt_end :
            /* end of frame */
            if (remainingSize) return ERROR(srcSize_wrong);
            break;
        default:
            return ERROR(GENERIC);   /* impossible */
        }
        if (cBlockSize == 0) break;   /* bt_end */

        if (ZSTD_isError(decodedSize)) return decodedSize;
        op += decodedSize;
        ip += cBlockSize;
        remainingSize -= cBlockSize;
    }

    return op-ostart;
}

size_t ZSTD_decompress(void* dst, size_t maxDstSize, const void* src, size_t srcSize)
{
    ZSTD_DCtx ctx;
    ctx.base = dst;
    return ZSTD_decompressDCtx(&ctx, dst, maxDstSize, src, srcSize);
}


/* ******************************
*  Streaming Decompression API
********************************/

size_t ZSTD_resetDCtx(ZSTD_DCtx* dctx)
{
    dctx->expected = ZSTD_frameHeaderSize;
    dctx->phase = 0;
    dctx->previousDstEnd = NULL;
    dctx->base = NULL;
    return 0;
}

ZSTD_DCtx* ZSTD_createDCtx(void)
{
    ZSTD_DCtx* dctx = (ZSTD_DCtx*)malloc(sizeof(ZSTD_DCtx));
    if (dctx==NULL) return NULL;
    ZSTD_resetDCtx(dctx);
    return dctx;
}

size_t ZSTD_freeDCtx(ZSTD_DCtx* dctx)
{
    free(dctx);
    return 0;
}

size_t ZSTD_nextSrcSizeToDecompress(ZSTD_DCtx* dctx)
{
    return dctx->expected;
}

size_t ZSTD_decompressContinue(ZSTD_DCtx* ctx, void* dst, size_t maxDstSize, const void* src, size_t srcSize)
{
    /* Sanity check */
    if (srcSize != ctx->expected) return ERROR(srcSize_wrong);
    if (dst != ctx->previousDstEnd)  /* not contiguous */
        ctx->base = dst;

    /* Decompress : frame header */
    if (ctx->phase == 0)
    {
        /* Check frame magic header */
        U32 magicNumber = MEM_readLE32(src);
        if (magicNumber != ZSTD_magicNumber) return ERROR(prefix_unknown);
        ctx->phase = 1;
        ctx->expected = ZSTD_blockHeaderSize;
        return 0;
    }

    /* Decompress : block header */
    if (ctx->phase == 1)
    {
        blockProperties_t bp;
        size_t blockSize = ZSTD_getcBlockSize(src, ZSTD_blockHeaderSize, &bp);
        if (ZSTD_isError(blockSize)) return blockSize;
        if (bp.blockType == bt_end)
        {
            ctx->expected = 0;
            ctx->phase = 0;
        }
        else
        {
            ctx->expected = blockSize;
            ctx->bType = bp.blockType;
            ctx->phase = 2;
        }

        return 0;
    }

    /* Decompress : block content */
    {
        size_t rSize;
        switch(ctx->bType)
        {
        case bt_compressed:
            rSize = ZSTD_decompressBlock(ctx, dst, maxDstSize, src, srcSize);
            break;
        case bt_raw :
            rSize = ZSTD_copyUncompressedBlock(dst, maxDstSize, src, srcSize);
            break;
        case bt_rle :
            return ERROR(GENERIC);   /* not yet handled */
            break;
        case bt_end :   /* should never happen (filtered at phase 1) */
            rSize = 0;
            break;
        default:
            return ERROR(GENERIC);
        }
        ctx->phase = 1;
        ctx->expected = ZSTD_blockHeaderSize;
        ctx->previousDstEnd = (void*)( ((char*)dst) + rSize);
        return rSize;
    }

}


