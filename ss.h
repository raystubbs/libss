#ifndef ss_h
#define ss_h
#include <stddef.h>

typedef struct ss_Match   ss_Match;
typedef struct ss_Scanner ss_Scanner;
typedef struct ss_Pattern ss_Pattern;
typedef struct ss_Context ss_Context;

typedef enum {
    ss_BYTES,
    ss_UTF8,
    ss_UTF16,
    ss_UTF32
} ss_Format;

ss_Context* ss_init( void );

ss_Pattern* ss_compile( ss_Context* ctx, ss_Format fmt, char const* pat, size_t len );
void        ss_define( ss_Context* ctx, char const* name, ss_Pattern* pat );
char const* ss_error( ss_Context* ctx );

ss_Scanner* ss_start( ss_Context* ctx, ss_Format fmt, ss_Pattern* pat, char const* str, size_t len );
ss_Match*   ss_match( ss_Scanner* scanner );
ss_Match*   ss_find( ss_Scanner* scanner );
ss_Match*   ss_next( ss_Match* match );
char const* ss_loc( ss_Match* match );
char const* ss_end( ss_Match* match );
ss_Match*   ss_get( ss_Match* match, char const* binding );

void        ss_release( void* ptr );

#endif