#include "token_bucket.h"
#include "ccp.h"

token_bucket *NewTokenBucket() {
    token_bucket *bucket;
    bucket = malloc(sizeof(token_bucket));
    bucket->rate = 0;
    bucket->burst = 28960;
    bucket->tokens = bucket->burst;
    bucket->last_fill_t = _dp_now();
    return bucket;
}

void _refill_bucket(token_bucket *bucket) {
    uint32_t elapsed = _dp_since_usecs(bucket->last_fill_t);
    double new_tokens = (bucket->rate / 1000000) * elapsed;
    bucket->tokens = MIN(bucket->burst, bucket->tokens + new_tokens);
    bucket->last_fill_t = _dp_now();
}

int SufficientTokens(token_bucket *bucket, uint64_t new_bits) {
    double new_bytes = (new_bits / 8.0);

    _refill_bucket(bucket);

    if (bucket->tokens >= new_bytes) {
        bucket->tokens -= new_bytes;
        return 0;
    }
    return -1;
}

void PrintBucket(token_bucket *bucket) {
    fprintf(stderr, "[rate=%u tokens=%f last=%u]\n",
            bucket->rate,
            bucket->tokens,
            bucket->last_fill_t);
}
