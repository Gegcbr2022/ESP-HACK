#pragma once
#include "mock_state.h"
struct mbedtls_sha256_context { std::vector<uint8_t> bytes; };
inline void mbedtls_sha256_init(mbedtls_sha256_context* hash){hash->bytes.clear();}
inline int mbedtls_sha256_starts_ret(mbedtls_sha256_context* hash,int) {
    hash->bytes.clear();return ++mock.starts==mock.failStart?-1:0;
}
inline int mbedtls_sha256_update_ret(mbedtls_sha256_context* hash,const uint8_t* data,size_t count) {
    hash->bytes.insert(hash->bytes.end(),data,data+count);return 0;
}
inline int mbedtls_sha256_finish_ret(mbedtls_sha256_context* hash,uint8_t digest[32]){mockDigest(hash->bytes,digest);return 0;}
inline void mbedtls_sha256_free(mbedtls_sha256_context*){}
