#include "hash_functions.hpp"

u64 mix(u64 h) {
    (h) ^= (h) >> 23;
    (h) *= 0x2127599bf4325c37ULL;
    (h) ^= (h) >> 47;
    return h;
}

// From https://github.com/ztanml/fast-hash 
uint64_t fasthash64(const void* buf, size_t len, uint64_t seed)
{
    const uint64_t    m = 0x880355f21e6d1965ULL;
    const uint64_t* pos = (const uint64_t*)buf;
    const uint64_t* end = pos + (len / 8);
    const unsigned char* pos2;
    uint64_t h = seed ^ (len * m);
    uint64_t v;

    while (pos != end) {
        v = *pos++;
        h ^= mix(v);
        h *= m;
    }

    pos2 = (const unsigned char*)pos;
    v = 0;

    switch (len & 7) {
    case 7: v ^= (uint64_t)pos2[6] << 48;
    case 6: v ^= (uint64_t)pos2[5] << 40;
    case 5: v ^= (uint64_t)pos2[4] << 32;
    case 4: v ^= (uint64_t)pos2[3] << 24;
    case 3: v ^= (uint64_t)pos2[2] << 16;
    case 2: v ^= (uint64_t)pos2[1] << 8;
    case 1: v ^= (uint64_t)pos2[0];
        h ^= mix(v);
        h *= m;
    }

    return mix(h);
}

uint32_t fasthash32(const void* buf, size_t len, uint32_t seed)
{
    // the following trick converts the 64-bit hashcode to Fermat
    // residue, which shall retain information from both the higher
    // and lower parts of hashcode.
    uint64_t h = fasthash64(buf, len, seed);
    return h - (h >> 32);
}

#define FAST_HASH_SEED 271

u64 hash_memory(Array<byte> memory) {
    return fasthash64(memory.data, memory.size, FAST_HASH_SEED);
}

u64 hash_string(String* string) {
    return hash_memory(array_create_static((byte*)string->characters, string->size));
}

u64 hash_i32(i32* i) {
    return hash_memory(array_create_static<byte>((byte*)i, 4));
}

u64 hash_i64(i64* i) {
    return hash_memory(array_create_static<byte>((byte*)i, 8));
}

u64 hash_pointer(void* ptr) {
    i64 val = (i64)ptr;
    return hash_i64(&val);
}

bool equals_i32(i32* a, i32* b) { return *a == *b; }
bool equals_i64(i64* a, i64* b) { return *a == *b; }
bool equals_pointer(void** a, void** b) { return *a == *b; }
