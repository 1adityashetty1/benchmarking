/*
    ZSTD HC - High Compression Mode of Zstandard
    Copyright (C) 2015, Yann Collet.

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
       - Zstd source repository : https://www.zstd.net
*/


/* *******************************************************
*  Compiler specifics
*********************************************************/
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


/* *************************************
*  Includes
***************************************/
#include <stdlib.h>   /* malloc */
#include <string.h>   /* memset */
#include "zstdhc_static.h"
#include "zstd_static.h"
#include "zstd_internal.h"
#include "mem.h"


/* *************************************
*  Local Constants
***************************************/
#define MINMATCH 4
#define MAXD_LOG 26

#define KB *1024
#define MB *1024*1024
#define GB *(1ULL << 30)

/* *************************************
*  Local Types
***************************************/
#define BLOCKSIZE (128 KB)                 /* define, for static allocation */
#define WORKPLACESIZE (BLOCKSIZE*3)

struct ZSTD_HC_CCtx_s
{
    const BYTE* end;        /* next block here to continue on current prefix */
    const BYTE* base;       /* All regular indexes relative to this position */
    const BYTE* dictBase;   /* extDict indexes relative to this position */
    U32   dictLimit;        /* below that point, need extDict */
    U32   lowLimit;         /* below that point, no more data */
    U32   nextToUpdate;     /* index from which to continue dictionary update */
    ZSTD_HC_parameters params;
    void* workSpace;
    size_t workSpaceSize;

    seqStore_t seqStore;    /* sequences storage ptrs */
    U32* hashTable;
    U32* chainTable;
};


ZSTD_HC_CCtx* ZSTD_HC_createCCtx(void)
{
    return (ZSTD_HC_CCtx*) calloc(1, sizeof(ZSTD_HC_CCtx));
}

size_t ZSTD_HC_freeCCtx(ZSTD_HC_CCtx* cctx)
{
    free(cctx->workSpace);
    free(cctx);
    return 0;
}

static size_t ZSTD_HC_resetCCtx_advanced (ZSTD_HC_CCtx* zc,
                                          ZSTD_HC_parameters params)
{
    /* validate params */
    if (params.windowLog   > ZSTD_HC_WINDOWLOG_MAX) params.windowLog = ZSTD_HC_WINDOWLOG_MAX;
    if (params.windowLog   < ZSTD_HC_WINDOWLOG_MIN) params.windowLog = ZSTD_HC_WINDOWLOG_MIN;
    if (params.chainLog    > params.windowLog) params.chainLog = params.windowLog;   /* <= ZSTD_HC_CHAINLOG_MAX */
    if (params.chainLog    < ZSTD_HC_CHAINLOG_MIN) params.chainLog = ZSTD_HC_CHAINLOG_MIN;
    if (params.hashLog     > ZSTD_HC_HASHLOG_MAX) params.hashLog = ZSTD_HC_HASHLOG_MAX;
    if (params.hashLog     < ZSTD_HC_HASHLOG_MIN) params.hashLog = ZSTD_HC_HASHLOG_MIN;
    if (params.searchLog   > ZSTD_HC_SEARCHLOG_MAX) params.searchLog = ZSTD_HC_SEARCHLOG_MAX;
    if (params.searchLog   < ZSTD_HC_SEARCHLOG_MIN) params.searchLog = ZSTD_HC_SEARCHLOG_MIN;
    if (params.searchLength> ZSTD_HC_SEARCHLENGTH_MAX) params.searchLength = ZSTD_HC_SEARCHLENGTH_MAX;
    if (params.searchLength< ZSTD_HC_SEARCHLENGTH_MIN) params.searchLength = ZSTD_HC_SEARCHLENGTH_MIN;

    /* reserve table memory */
    {
        const size_t tableSpace = ((1 << params.chainLog) + (1 << params.hashLog)) * sizeof(U32);
        const size_t neededSpace = tableSpace + WORKPLACESIZE;
        if (zc->workSpaceSize < neededSpace)
        {
            free(zc->workSpace);
            zc->workSpaceSize = neededSpace;
            zc->workSpace = malloc(neededSpace);
            if (zc->workSpace == NULL) return ERROR(memory_allocation);
        }
        zc->hashTable = (U32*)zc->workSpace;
        zc->chainTable = zc->hashTable + ((size_t)1 << params.hashLog);
        zc->seqStore.buffer = (void*) (zc->chainTable + ((size_t)1 << params.chainLog));
        memset(zc->hashTable, 0, tableSpace );
    }

    zc->nextToUpdate = 0;
    zc->end = NULL;
    zc->base = NULL;
    zc->dictBase = NULL;
    zc->dictLimit = 0;
    zc->lowLimit = 0;
    zc->params = params;
    zc->seqStore.offsetStart = (U32*) (zc->seqStore.buffer);
    zc->seqStore.offCodeStart = (BYTE*) (zc->seqStore.offsetStart + (BLOCKSIZE>>2));
    zc->seqStore.litStart = zc->seqStore.offCodeStart + (BLOCKSIZE>>2);
    zc->seqStore.litLengthStart =  zc->seqStore.litStart + BLOCKSIZE;
    zc->seqStore.matchLengthStart = zc->seqStore.litLengthStart + (BLOCKSIZE>>2);
    zc->seqStore.dumpsStart = zc->seqStore.matchLengthStart + (BLOCKSIZE>>2);

    return 0;
}


/* *************************************
*  Inline functions and Macros
***************************************/

static const U32 prime4bytes = 2654435761U;
static U32 ZSTD_HC_hash4(U32 u, U32 h) { return (u * prime4bytes) >> (32-h) ; }
static size_t ZSTD_HC_hash4Ptr(const void* ptr, U32 h) { return ZSTD_HC_hash4(MEM_read32(ptr), h); }

static const U64 prime5bytes = 889523592379ULL;
static size_t ZSTD_HC_hash5(U64 u, U32 h) { return (size_t)((u * prime5bytes) << (64-40) >> (64-h)) ; }
static size_t ZSTD_HC_hash5Ptr(const void* p, U32 h) { return ZSTD_HC_hash5(MEM_read64(p), h); }

static const U64 prime6bytes = 227718039650203ULL;
static size_t ZSTD_HC_hash6(U64 u, U32 h) { return (size_t)((u * prime6bytes) << (64-48) >> (64-h)) ; }
static size_t ZSTD_HC_hash6Ptr(const void* p, U32 h) { return ZSTD_HC_hash6(MEM_read64(p), h); }

static size_t ZSTD_HC_hashPtr(const void* p, U32 h, U32 mls)
{
    switch(mls)
    {
    default:
    case 4: return ZSTD_HC_hash4Ptr(p,h);
    case 5: return ZSTD_HC_hash5Ptr(p,h);
    case 6: return ZSTD_HC_hash6Ptr(p,h);
    }
}

#define NEXT_IN_CHAIN(d)           chainTable[(d) & chainMask]   /* flexible, CHAINSIZE dependent */


/* *************************************
*  HC Compression
***************************************/
/* Update chains up to ip (excluded) */
static void ZSTD_HC_insert (ZSTD_HC_CCtx* zc, const BYTE* ip, U32 mls)
{
    U32* const hashTable  = zc->hashTable;
    const U32 hashLog = zc->params.hashLog;
    U32* const chainTable = zc->chainTable;
    const U32 chainMask = (1 << zc->params.chainLog) - 1;
    const BYTE* const base = zc->base;
    const U32 target = (U32)(ip - base);
    U32 idx = zc->nextToUpdate;

    while(idx < target)
    {
        size_t h = ZSTD_HC_hashPtr(base+idx, hashLog, mls);
        NEXT_IN_CHAIN(idx) = hashTable[h];
        hashTable[h] = idx;
        idx++;
    }

    zc->nextToUpdate = target;
}


FORCE_INLINE /* inlining is important to hardwire a hot branch (template emulation) */
size_t ZSTD_HC_insertAndFindBestMatch (
                        ZSTD_HC_CCtx* zc,   /* Index table will be updated */
                        const BYTE* ip, const BYTE* const iLimit,
                        const BYTE** matchpos,
                        const U32 maxNbAttempts, const U32 matchLengthSearch)
{
    U32* const hashTable = zc->hashTable;
    const U32 hashLog = zc->params.hashLog;
    U32* const chainTable = zc->chainTable;
    const U32 chainSize = (1 << zc->params.chainLog);
    const U32 chainMask = chainSize-1;
    const BYTE* const base = zc->base;
    const BYTE* const dictBase = zc->dictBase;
    const U32 dictLimit = zc->dictLimit;
    const U32 maxDistance = (1 << zc->params.windowLog);
    const U32 lowLimit = (zc->lowLimit + maxDistance > (U32)(ip-base)) ? zc->lowLimit : (U32)(ip - base) - (maxDistance - 1);
    U32 matchIndex;
    const BYTE* match;
    int nbAttempts=maxNbAttempts;
    size_t ml=0;

    /* HC4 match finder */
    ZSTD_HC_insert(zc, ip, matchLengthSearch);
    matchIndex = hashTable[ZSTD_HC_hashPtr(ip, hashLog, matchLengthSearch)];

    while ((matchIndex>=lowLimit) && (nbAttempts))
    {
        nbAttempts--;
        if (matchIndex >= dictLimit)
        {
            match = base + matchIndex;
            if (*(match+ml) == *(ip+ml)
                && (MEM_read32(match) == MEM_read32(ip)))
            {
                const size_t mlt = ZSTD_count(ip+MINMATCH, match+MINMATCH, iLimit) + MINMATCH;
                if (mlt > ml) { ml = mlt; *matchpos = match; if (ip+ml >= iLimit) break; }
            }
        }
        else
        {
            match = dictBase + matchIndex;
            if (MEM_read32(match) == MEM_read32(ip))
            {
                size_t mlt;
                const BYTE* vLimit = ip + (dictLimit - matchIndex);
                if (vLimit > iLimit) vLimit = iLimit;
                mlt = ZSTD_count(ip+MINMATCH, match+MINMATCH, vLimit) + MINMATCH;
                if ((ip+mlt == vLimit) && (vLimit < iLimit))
                    mlt += ZSTD_count(ip+mlt, base+dictLimit, iLimit);
                if (mlt > ml) { ml = mlt; *matchpos = base + matchIndex; }   /* virtual matchpos */
            }
        }

        if (base + matchIndex <= ip - chainSize) break;
        matchIndex = NEXT_IN_CHAIN(matchIndex);
    }

    return ml;
}


static size_t ZSTD_HC_insertAndFindBestMatch_selectMLS (
                        ZSTD_HC_CCtx* zc,   /* Index table will be updated */
                        const BYTE* ip, const BYTE* const iLimit,
                        const BYTE** matchpos,
                        const U32 maxNbAttempts, const U32 matchLengthSearch)
{
    switch(matchLengthSearch)
    {
    default :
    case 4 : return ZSTD_HC_insertAndFindBestMatch(zc, ip, iLimit, matchpos, maxNbAttempts, 4);
    case 5 : return ZSTD_HC_insertAndFindBestMatch(zc, ip, iLimit, matchpos, maxNbAttempts, 5);
    case 6 : return ZSTD_HC_insertAndFindBestMatch(zc, ip, iLimit, matchpos, maxNbAttempts, 6);
    }
}


static size_t ZSTD_HC_compressBlock(ZSTD_HC_CCtx* ctx, void* dst, size_t maxDstSize, const void* src, size_t srcSize)
{
    seqStore_t* seqStorePtr = &(ctx->seqStore);
    const BYTE* const istart = (const BYTE*)src;
    const BYTE* ip = istart;
    const BYTE* anchor = istart;
    const BYTE* const iend = istart + srcSize;
    const BYTE* const ilimit = iend - 8;
    const BYTE* match = istart;

    size_t offset_2=REPCODE_STARTVALUE, offset_1=REPCODE_STARTVALUE;
    const U32 maxSearches = 1 << ctx->params.searchLog;
    const U32 mls = ctx->params.searchLength;

    /* init */
    ZSTD_resetSeqStore(seqStorePtr);
    if (((ip-ctx->base) - ctx->dictLimit) < REPCODE_STARTVALUE) ip += REPCODE_STARTVALUE;

    /* Match Loop */
    while (ip < ilimit)
    {
        /* repcode */
        if (MEM_read32(ip) == MEM_read32(ip - offset_2))
        {
            /* store sequence */
            size_t matchLength = ZSTD_count(ip+MINMATCH, ip+MINMATCH-offset_2, iend);
            size_t litLength = ip-anchor;
            size_t offset = offset_2;
            offset_2 = offset_1;
            offset_1 = offset;
            ZSTD_storeSeq(seqStorePtr, litLength, anchor, 0, matchLength);
            ip += matchLength+MINMATCH;
            anchor = ip;
            continue;
        }

        offset_2 = offset_1;  /* failed once : necessarily offset_1 now */

        /* repcode at ip+1 */
        if (MEM_read32(ip+1) == MEM_read32(ip+1 - offset_1))
        {
            size_t matchLength = ZSTD_count(ip+1+MINMATCH, ip+1+MINMATCH-offset_1, iend);
            size_t litLength = ip+1-anchor;
            ZSTD_storeSeq(seqStorePtr, litLength, anchor, 0, matchLength);
            ip += 1+matchLength+MINMATCH;
            anchor = ip;
            continue;
        }

        /* search */
        {
            size_t matchLength = ZSTD_HC_insertAndFindBestMatch_selectMLS(ctx, ip, iend, &match, maxSearches, mls);
            if (!matchLength) { ip++; continue; }
            /* store sequence */
            {
                size_t litLength = ip-anchor;
                offset_1 = ip-match;
                ZSTD_storeSeq(seqStorePtr, litLength, anchor, offset_1, matchLength-MINMATCH);
                ip += matchLength;
                anchor = ip;
            }
        }
    }

    /* Last Literals */
    {
        size_t lastLLSize = iend - anchor;
        memcpy(seqStorePtr->lit, anchor, lastLLSize);
        seqStorePtr->lit += lastLLSize;
    }

    /* Final compression stage */
    return ZSTD_compressSequences((BYTE*)dst, maxDstSize,
                                  seqStorePtr, srcSize);
}

static size_t ZSTD_HC_compress_generic (ZSTD_HC_CCtx* ctxPtr,
                                        void* dst, size_t maxDstSize,
                                  const void* src, size_t srcSize)
{
    static const size_t blockSize = 128 KB;
    size_t remaining = srcSize;
    const BYTE* ip = (const BYTE*)src;
    BYTE* const ostart = (BYTE*)dst;
    BYTE* op = ostart;
    BYTE* const oend = op + maxDstSize;

    while (remaining > blockSize)
    {
        size_t cSize = ZSTD_HC_compressBlock(ctxPtr, op+3, oend-op, ip, blockSize);

        if (cSize == 0)
        {
            cSize = ZSTD_noCompressBlock(op, maxDstSize, ip, blockSize);   /* block is not compressible */
        }
        else
        {
            op[0] = (BYTE)(cSize>>16);
            op[1] = (BYTE)(cSize>>8);
            op[2] = (BYTE)cSize;
            op[0] += (BYTE)(bt_compressed << 6); /* is a compressed block */
            cSize += 3;
        }

        remaining -= blockSize;
        ip += blockSize;
        op += cSize;
        if (ZSTD_isError(cSize)) return cSize;
    }

    /* last block */
    {
        size_t cSize = ZSTD_HC_compressBlock(ctxPtr, op+3, oend-op, ip, remaining);

        if (cSize == 0)
        {
            cSize = ZSTD_noCompressBlock(op, maxDstSize, ip, remaining);   /* block is not compressible */
        }
        else
        {
            op[0] = (BYTE)(cSize>>16);
            op[1] = (BYTE)(cSize>>8);
            op[2] = (BYTE)cSize;
            op[0] += (BYTE)(bt_compressed << 6); /* is a compressed block */
            cSize += 3;
        }

        op += cSize;
        if (ZSTD_isError(cSize)) return cSize;
    }

    return op-ostart;
}


size_t ZSTD_HC_compressContinue (ZSTD_HC_CCtx* ctxPtr,
                                 void* dst, size_t dstSize,
                           const void* src, size_t srcSize)
{
    const BYTE* const ip = (const BYTE*) src;

    /* Check if blocks follow each other */
    if (ip != ctxPtr->end)
    {
        if (ctxPtr->end != NULL)
            ZSTD_HC_resetCCtx_advanced(ctxPtr, ctxPtr->params);   /* just reset, but no need to re-alloc */
        ctxPtr->base = ip;
    }

    ctxPtr->end = ip + srcSize;
    return ZSTD_HC_compress_generic (ctxPtr, dst, dstSize, src, srcSize);
}


size_t ZSTD_HC_compressBegin_advanced(ZSTD_HC_CCtx* ctx,
                                      void* dst, size_t maxDstSize,
                                      const ZSTD_HC_parameters params)
{
    size_t errorCode;
    if (maxDstSize < 4) return ERROR(dstSize_tooSmall);
    errorCode = ZSTD_HC_resetCCtx_advanced(ctx, params);
    if (ZSTD_isError(errorCode)) return errorCode;
    MEM_writeLE32(dst, ZSTD_magicNumber); /* Write Header */
    return 4;
}


size_t ZSTD_HC_compressBegin(ZSTD_HC_CCtx* ctx, void* dst, size_t maxDstSize, int compressionLevel)
{
    if (compressionLevel<=0) compressionLevel = 1;
    if (compressionLevel > ZSTD_HC_MAX_CLEVEL) compressionLevel = ZSTD_HC_MAX_CLEVEL;
    return ZSTD_HC_compressBegin_advanced(ctx, dst, maxDstSize, ZSTD_HC_defaultParameters[compressionLevel]);
}


size_t ZSTD_HC_compressEnd(ZSTD_HC_CCtx* ctx, void* dst, size_t maxDstSize)
{
    BYTE* op = (BYTE*)dst;

    /* Sanity check */
    (void)ctx;
    if (maxDstSize < 3) return ERROR(dstSize_tooSmall);

    /* End of frame */
    op[0] = (BYTE)(bt_end << 6);
    op[1] = 0;
    op[2] = 0;

    return 3;
}

size_t ZSTD_HC_compress_advanced (ZSTD_HC_CCtx* ctx,
                                 void* dst, size_t maxDstSize,
                                 const void* src, size_t srcSize,
                                 ZSTD_HC_parameters params)
{
    BYTE* const ostart = (BYTE*)dst;
    BYTE* op = ostart;
    size_t oSize;

    /* correct params, to use less memory */
    U32 srcLog = ZSTD_highbit((U32)srcSize-1) + 1;
    if (params.windowLog > srcLog) params.windowLog = srcLog;
    if (params.chainLog > srcLog) params.chainLog = srcLog;

    /* Header */
    oSize = ZSTD_HC_compressBegin_advanced(ctx, dst, maxDstSize, params);
    if(ZSTD_isError(oSize)) return oSize;
    op += oSize;
    maxDstSize -= oSize;

    /* body (compression) */
    ctx->base = (const BYTE*)src;
    op += ZSTD_HC_compress_generic (ctx, op,  maxDstSize, src, srcSize);
    if(ZSTD_isError(oSize)) return oSize;
    op += oSize;
    maxDstSize -= oSize;

    /* Close frame */
    oSize = ZSTD_HC_compressEnd(ctx, op, maxDstSize);
    if(ZSTD_isError(oSize)) return oSize;
    op += oSize;

    return (op - ostart);
}

size_t ZSTD_HC_compressCCtx (ZSTD_HC_CCtx* ctx, void* dst, size_t maxDstSize, const void* src, size_t srcSize, int compressionLevel)
{
    if (compressionLevel<=1) return ZSTD_compress(dst, maxDstSize, src, srcSize);   /* fast mode */
    if (compressionLevel > ZSTD_HC_MAX_CLEVEL) compressionLevel = ZSTD_HC_MAX_CLEVEL;
    return ZSTD_HC_compress_advanced(ctx, dst, maxDstSize, src, srcSize, ZSTD_HC_defaultParameters[compressionLevel]);
}

size_t ZSTD_HC_compress(void* dst, size_t maxDstSize, const void* src, size_t srcSize, int compressionLevel)
{
    size_t result;
    ZSTD_HC_CCtx ctxBody;
    memset(&ctxBody, 0, sizeof(ctxBody));
    result = ZSTD_HC_compressCCtx(&ctxBody, dst, maxDstSize, src, srcSize, compressionLevel);
    free(ctxBody.workSpace);
    return result;
}
