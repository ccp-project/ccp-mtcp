#ifndef __TOKEN_BUCKET_H_
#define __TOKEN_BUCKET_H_

#include "tcp_stream.h"

typedef struct token_bucket {
    double tokens;
    uint32_t rate;
    uint32_t burst;
    uint32_t last_fill_t;
} token_bucket;

token_bucket* NewTokenBucket();
int SufficientTokens(token_bucket *bucket, uint64_t new_bits);
void PrintBucket(token_bucket *bucket);

#endif
