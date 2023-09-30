
#include <stdint.h>

#define BIT(X) (1 << X)

#define BIT_COMPARE(src, cmp) ((src & cmp) == cmp)
#define BIT_N_COMPARE(src, cmp) ((src & cmp) != cmp)

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef int8_t  i8;
typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;

typedef uintptr_t uptr;
typedef intptr_t iptr;

typedef int32_t bool32;
typedef bool32 b32;

typedef float f32;
typedef double f64;

typedef uint8_t byte;

typedef size_t memory_index;

const f64 PI64 = 3.1415926535897931;
const f32 PI32 = (f32)PI64;

#define megabytes *1024*1024

union v2u
{
    struct
    {
        u32 x, y;
    };
    struct
    {
        u32 Width, Height;
    };
    u32 E[2];
};