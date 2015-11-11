/*
    zstdhc - high compression variant
    Header File - Experimental API, static linking only
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
    - zstd source repository : http://www.zstd.net
*/
#pragma once

#if defined (__cplusplus)
extern "C" {
#endif

/* *************************************
*  Includes
***************************************/
#include "mem.h"
#include "zstdhc.h"


/* *************************************
*  Types
***************************************/
/** from faster to stronger */
typedef enum { ZSTD_HC_fast, ZSTD_HC_greedy, ZSTD_HC_lazy, ZSTD_HC_lazy2, ZSTD_HC_btlazy2 } ZSTD_HC_strategy;

typedef struct
{
    U32 windowLog;     /* largest match distance : impact decompression buffer size */
    U32 contentLog;    /* full search segment : larger == more compression, slower, more memory (useless for fast) */
    U32 hashLog;       /* dispatch table : larger == more memory, faster*/
    U32 searchLog;     /* nb of searches : larger == more compression, slower*/
    U32 searchLength;  /* size of matches : larger == faster decompression */
    ZSTD_HC_strategy strategy;
} ZSTD_HC_parameters;

/* parameters boundaries */
#define ZSTD_HC_WINDOWLOG_MAX 26
#define ZSTD_HC_WINDOWLOG_MIN 18
#define ZSTD_HC_CONTENTLOG_MAX (ZSTD_HC_WINDOWLOG_MAX+1)
#define ZSTD_HC_CONTENTLOG_MIN 4
#define ZSTD_HC_HASHLOG_MAX 28
#define ZSTD_HC_HASHLOG_MIN 4
#define ZSTD_HC_SEARCHLOG_MAX (ZSTD_HC_CONTENTLOG_MAX-1)
#define ZSTD_HC_SEARCHLOG_MIN 1
#define ZSTD_HC_SEARCHLENGTH_MAX 7
#define ZSTD_HC_SEARCHLENGTH_MIN 4


/* *************************************
*  Advanced function
***************************************/
/** ZSTD_HC_compress_advanced
*   Same as ZSTD_HC_compressCCtx(), with fine-tune control of each compression parameter */
size_t ZSTD_HC_compress_advanced (ZSTD_HC_CCtx* ctx,
                                 void* dst, size_t maxDstSize,
                           const void* src, size_t srcSize,
                                 ZSTD_HC_parameters params);

/** ZSTD_HC_validateParams
    correct params value to remain within authorized range
    srcSizeHint value is optional, select 0 if not known */
void ZSTD_HC_validateParams(ZSTD_HC_parameters* params, U64 srcSizeHint);


/* *************************************
*  Streaming functions
***************************************/
size_t ZSTD_HC_compressBegin(ZSTD_HC_CCtx* ctx, void* dst, size_t maxDstSize, int compressionLevel, U64 srcSizeHint);
size_t ZSTD_HC_compressContinue(ZSTD_HC_CCtx* ctx, void* dst, size_t maxDstSize, const void* src, size_t srcSize);
size_t ZSTD_HC_compressEnd(ZSTD_HC_CCtx* ctx, void* dst, size_t maxDstSize);


/* *************************************
*  Pre-defined compression levels
***************************************/
#define ZSTD_HC_MAX_CLEVEL 20
static const ZSTD_HC_parameters ZSTD_HC_defaultParameters[2][ZSTD_HC_MAX_CLEVEL+1] = {
{   /* for <= 128 KB */
    /* W,  C,  H,  S,  L, strat */
    { 17, 12, 12,  1,  4, ZSTD_HC_fast    },  /* level  0 - never used */
    { 17, 12, 13,  1,  6, ZSTD_HC_fast    },  /* level  1 */
    { 17, 15, 16,  1,  5, ZSTD_HC_fast    },  /* level  2 */
    { 17, 16, 17,  1,  5, ZSTD_HC_fast    },  /* level  3 */
    { 17, 13, 15,  2,  4, ZSTD_HC_greedy  },  /* level  4 */
    { 17, 15, 17,  3,  4, ZSTD_HC_greedy  },  /* level  5 */
    { 17, 14, 17,  3,  4, ZSTD_HC_lazy    },  /* level  6 */
    { 17, 16, 17,  4,  4, ZSTD_HC_lazy    },  /* level  7 */
    { 17, 16, 17,  4,  4, ZSTD_HC_lazy2   },  /* level  8 */
    { 17, 17, 16,  5,  4, ZSTD_HC_lazy2   },  /* level  9 */
    { 17, 17, 16,  6,  4, ZSTD_HC_lazy2   },  /* level 10 */
    { 17, 17, 16,  7,  4, ZSTD_HC_lazy2   },  /* level 11 */
    { 17, 17, 16,  8,  4, ZSTD_HC_lazy2   },  /* level 12 */
    { 17, 18, 16,  4,  4, ZSTD_HC_btlazy2 },  /* level 13 */
    { 17, 18, 16,  5,  4, ZSTD_HC_btlazy2 },  /* level 14 */
    { 17, 18, 16,  6,  4, ZSTD_HC_btlazy2 },  /* level 15 */
    { 17, 18, 16,  7,  4, ZSTD_HC_btlazy2 },  /* level 16 */
    { 17, 18, 16,  8,  4, ZSTD_HC_btlazy2 },  /* level 17 */
    { 17, 18, 16,  9,  4, ZSTD_HC_btlazy2 },  /* level 18 */
    { 17, 18, 16, 10,  4, ZSTD_HC_btlazy2 },  /* level 19 */
    { 17, 18, 18, 12,  4, ZSTD_HC_btlazy2 },  /* level 20 */
},
{   /* for > 128 KB */
    /* W,  C,  H,  S,  L, strat */
    { 18, 12, 12,  1,  4, ZSTD_HC_fast    },  /* level  0 - never used */
    { 18, 14, 14,  1,  7, ZSTD_HC_fast    },  /* level  1 - in fact redirected towards zstd fast */
    { 19, 15, 16,  1,  6, ZSTD_HC_fast    },  /* level  2 */
    { 20, 18, 20,  1,  6, ZSTD_HC_fast    },  /* level  3 */
    { 21, 19, 21,  1,  6, ZSTD_HC_fast    },  /* level  4 */
    { 20, 13, 18,  5,  5, ZSTD_HC_greedy  },  /* level  5 */
    { 20, 17, 19,  3,  5, ZSTD_HC_greedy  },  /* level  6 */
    { 21, 17, 20,  3,  5, ZSTD_HC_lazy    },  /* level  7 */
    { 21, 19, 20,  3,  5, ZSTD_HC_lazy    },  /* level  8 */
    { 21, 20, 20,  3,  5, ZSTD_HC_lazy2   },  /* level  9 */
    { 21, 19, 20,  4,  5, ZSTD_HC_lazy2   },  /* level 10 */
    { 22, 20, 22,  4,  5, ZSTD_HC_lazy2   },  /* level 11 */
    { 22, 20, 22,  5,  5, ZSTD_HC_lazy2   },  /* level 12 */
    { 22, 21, 22,  5,  5, ZSTD_HC_lazy2   },  /* level 13 */
    { 22, 22, 23,  5,  5, ZSTD_HC_lazy2   },  /* level 14 */
    { 23, 23, 23,  5,  5, ZSTD_HC_lazy2   },  /* level 15 */
    { 23, 21, 22,  5,  5, ZSTD_HC_btlazy2 },  /* level 16 */
    { 23, 24, 23,  4,  5, ZSTD_HC_btlazy2 },  /* level 17 */
    { 25, 24, 23,  5,  5, ZSTD_HC_btlazy2 },  /* level 18 */
    { 25, 26, 23,  5,  5, ZSTD_HC_btlazy2 },  /* level 19 */
    { 26, 27, 24,  6,  5, ZSTD_HC_btlazy2 },  /* level 20 */
}
};


#if defined (__cplusplus)
}
#endif
