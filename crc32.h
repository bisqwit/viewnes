#ifndef bqtCrc32HH
#define bqtCrc32HH

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

typedef uint_least32_t crc32_t;

enum { crc32_startvalue = 0xFFFFFFFFUL };

/* The second parameter type of crc32_update() is really 'unsigned char',
 * but if we specify it so, the compiler will explicitly "and 0xFF" the
 * value when it passes the parameter, and in this particular implementation,
 * we don't need that happening.
 */
extern uint_fast32_t crc32_update(uint_fast32_t c, unsigned/* char*/b)
#ifdef __GNUC__
               __attribute__((pure))
#endif
    ;
extern crc32_t crc32_calc(const unsigned char* buf, unsigned long size);
extern crc32_t crc32_calc_upd(crc32_t c, const unsigned char* buf, unsigned long size);

#ifdef __cplusplus
}
#endif

#endif
