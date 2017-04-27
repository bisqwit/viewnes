/*** CRC32 calculation (CRC::update) ***/
#include "crc32.h"

#ifdef __GNUC__
# define likely(x)       __builtin_expect(!!(x), 1)
# define unlikely(x)     __builtin_expect(!!(x), 0)
#else
# define likely(x)   (x)
# define unlikely(x) (x)
#endif

namespace {
    /* This code constructs the CRC32 table at compile-time,
     * avoiding the need for a huge explicitly written table of magical numbers. */
    static const uint_least32_t crc32_poly = 0xEDB88320UL;
    
    /*
    template<uint_fast32_t crc> // One bit of a CRC32:
    struct b1 { enum { res = (crc >> 1) ^ (( crc&1) ? crc32_poly : 0UL) }; };
    template<uint_fast32_t i> // One byte of a CRC32 (eight bits):
    struct b8 { enum { res = b1<b1<b1<b1< b1<b1<b1<b1<i
        >::res>::res>::res>::res>::res>::res>::res>::res }; };
    */
    template<uint_fast32_t crc> // One byte of a CRC32 (eight bits):
    struct b8
    {
        enum { b1 = (crc & 1) ? (crc32_poly ^ (crc >> 1)) : (crc >> 1),
               b2 = (b1  & 1) ? (crc32_poly ^ (b1  >> 1)) : (b1  >> 1),
               b3 = (b2  & 1) ? (crc32_poly ^ (b2  >> 1)) : (b2  >> 1),
               b4 = (b3  & 1) ? (crc32_poly ^ (b3  >> 1)) : (b3  >> 1),
               b5 = (b4  & 1) ? (crc32_poly ^ (b4  >> 1)) : (b4  >> 1),
               b6 = (b5  & 1) ? (crc32_poly ^ (b5  >> 1)) : (b5  >> 1),
               b7 = (b6  & 1) ? (crc32_poly ^ (b6  >> 1)) : (b6  >> 1),
               res= (b7  & 1) ? (crc32_poly ^ (b7  >> 1)) : (b7  >> 1) };
               /*
        enum { b1 = (crc>> 1) ^ ((crc&1) ? crc32_poly : 0UL),
               b2 = (b1 >> 1) ^ (( b1&1) ? crc32_poly : 0UL),
               b3 = (b2 >> 1) ^ (( b2&1) ? crc32_poly : 0UL),
               b4 = (b3 >> 1) ^ (( b3&1) ? crc32_poly : 0UL),
               b5 = (b4 >> 1) ^ (( b4&1) ? crc32_poly : 0UL),
               b6 = (b5 >> 1) ^ (( b5&1) ? crc32_poly : 0UL),
               b7 = (b6 >> 1) ^ (( b6&1) ? crc32_poly : 0UL),
               res= (b7 >> 1) ^ (( b7&1) ? crc32_poly : 0UL),
             };*/
    };/**/
    // Four values of the table
    #define B4(n) b8<n>::res,b8<n+1>::res,b8<n+2>::res,b8<n+3>::res
    // Sixteen values of the table
    #define R(n) B4(n),B4(n+4),B4(n+8),B4(n+12)
    // The whole table, index by steps of 16
    static const uint_least32_t crctable[256] =
    { R(0x00),R(0x10),R(0x20),R(0x30), R(0x40),R(0x50),R(0x60),R(0x70),
      R(0x80),R(0x90),R(0xA0),R(0xB0), R(0xC0),R(0xD0),R(0xE0),R(0xF0) }; 
    #undef R
    #undef B4
}

uint_fast32_t crc32_update(uint_fast32_t crc, unsigned/* char */b) // __attribute__((pure))
{
    return ((crc >> 8) /* & 0x00FFFFFF*/) ^ crctable[/*(unsigned char)*/(crc^b)&0xFF];
}

crc32_t crc32_calc(const unsigned char* buf, unsigned long size)
{
    return crc32_calc_upd(crc32_startvalue, buf, size);
}
crc32_t crc32_calc_upd(crc32_t c, const unsigned char* buf, unsigned long size)
{
    uint_fast32_t value = c;

#if 0
    unsigned long pos = 0;
    while(size-- > 0) value = crc32_update(value, buf[pos++]);
#endif

#if 1
    for(unsigned long p=0; p<size; ++p) value = crc32_update(value, buf[p]);
#endif

#if 0
    unsigned unaligned_length = (4 - (unsigned long)(buf)) & 3;
    if(size < unaligned_length) unaligned_length = size;
    switch(unaligned_length)
    {
        case 3: value = crc32_update(value, *buf++);
        case 2: value = crc32_update(value, *buf++);
        case 1: value = crc32_update(value, *buf++);
                size -= unaligned_length;
        case 0: break;
    }
    for(; size >= 4; size -= 4, buf += 4)
        value = crc32_update(
                crc32_update(
                crc32_update(
                crc32_update(value, buf[0]),
                                    buf[1]),
                                    buf[2]),
                                    buf[3]);
    switch(size)
    {
        case 3: value = crc32_update(value, *buf++);
        case 2: value = crc32_update(value, *buf++);
        case 1: value = crc32_update(value, *buf++);
        case 0: break;
    }
#endif

#if 0 /* duff's device -- no gains observed over the simple loop above */
    if(__builtin_expect( (size!=0), 1l ))
    {
        { if(__builtin_expect( !(size&1), 1l )) goto case_0;
          --buf; goto case_1;
        }
        //switch(size % 2)
        {
            //default: 
                 do { size -= 2; buf += 2;
            case_0: value = crc32_update(value, buf[0]);
            case_1: value = crc32_update(value, buf[1]);
                    } while(size > 2);
        }
    }
#endif

#if 0
    while(size-- > 0) value = crc32_update(value, *buf++);
#endif
    
    return value;
}
