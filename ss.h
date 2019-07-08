#ifndef ss_h
#define ss_h
#include <stddef.h>

typedef struct ss_Match   ss_Match;
typedef struct ss_Scanner ss_Scanner;
typedef struct ss_Pattern ss_Pattern;
typedef struct ss_Context ss_Context;
typedef struct ss_Text    ss_Text;

typedef enum {
    ss_BYTES,
    ss_CHARS
} ss_Format;

typedef enum {
    ss_ERR_NONE,
    ss_ERR_ALLOC,
    ss_ERR_FORMAT,
    ss_ERR_SYNTAX,
    ss_ERR_UNDEFINED
} ss_Error;

typedef struct {
    ss_Format   fmt;
    size_t      len;
    char const* str;
} ss_Slice;

ss_Context* ss_init( void );

ss_Pattern* ss_compile( ss_Context* ctx, ss_Text const* txt );
void        ss_define( ss_Context* ctx, char const* name, ss_Pattern* pat );
ss_Error    ss_errnum( ss_Context* ctx );
char const* ss_errmsg( ss_Context* ctx );
void        ss_errclr( ss_Context* ctx );


ss_Match*   ss_match( ss_Context* ctx, ss_Pattern* pat, ss_Text* txt );
ss_Scanner* ss_start( ss_Context* ctx, ss_Pattern* pat, ss_Text* txt );
ss_Match*   ss_find( ss_Context* ctx, ss_Scanner* scanner );
char const* ss_loc( ss_Context* ctx, ss_Match* match );
char const* ss_end( ss_Context* ctx, ss_Match* match );
ss_Match*   ss_get( ss_Context* ctx, ss_Match* match, char const* binding );

void        ss_release( void* ptr );

#endif