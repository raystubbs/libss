#include "ss.h"
#include <stdbool.h>
#include <stdint.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>

/********************************* Core Types *********************************/
typedef struct ss_Map      ss_Map;
typedef struct ss_List     ss_List;
typedef struct ss_Iter     ss_Iter;
typedef struct ss_Stream   ss_Stream;
typedef struct ss_Buffer   ss_Buffer;
typedef struct ss_Object   ss_Object;
typedef enum   ss_Type     ss_Type;
typedef struct ss_Compiler ss_Compiler;

typedef ss_Match*  (*ss_Matcher)( ss_Context* ctx, ss_Pattern* pat, ss_Map* scope, ss_Stream* stream );
typedef void       (*ss_Cleaner)( ss_Context* ctx, ss_Pattern* pat );

struct ss_Pattern {
    ss_Matcher  match;
    ss_Cleaner  clean;
    char*       binding;
};

struct ss_Stream {
    ss_Context* ctx;
    char const* loc;
    char const* end;
    long       (*read)( ss_Context* ctx, ss_Stream* stream );
};

struct ss_Context {
    ss_Map*     patterns;
    ss_Error    errnum;
    char const* errmsg;
    
    size_t      tmpcap;
    size_t      tmptop;
    char const* tmpbuf;
};

struct ss_Scanner {
    ss_Pattern* pat;
    ss_Stream   stream;
};

struct ss_Match {
    ss_Map*     scope;
    ss_Match*   next;
    char const* loc;
    char const* end;
};

struct ss_Compiler {
    ss_List*    patterns;
    ss_Stream   stream;
    long        ch1;
    long        ch2;
};

enum ss_Type {
    TYPE_PATTERN,
    TYPE_SCANNER,
    TYPE_CONTEXT,
    TYPE_MATCH,
    TYPE_MAP,
    TYPE_LIST,
    TYPE_BUFFER,
    TYPE_COMPILER,
    TYPE_ITER,
    TYPE_LAST
};

struct ss_Object {
    ss_Type  type;
    unsigned refc;
    char     data[];
};

/********************************* Prototypes *********************************/


static void* ss_alloc( size_t sz, ss_Type type );
static void* ss_refer( void* ptr );
static void  ss_free( void* ptr );

static void ss_prelude( ss_Context* ctx );
static void ss_error( ss_Context* ctx, ss_Error err, char const* fmt, ... );

static ss_Map*  ss_mapNew( ss_Context* ctx );
static int      ss_mapPut( ss_Context* ctx, ss_Map* map, char const* key, void* val );
static void*    ss_mapGet( ss_Context* ctx, ss_Map* map, char const* key );
static int      ss_mapCommit( ss_Context* ctx, ss_Map* map );
static void     ss_mapCancel( ss_Context* ctx, ss_Map* map );

static ss_List* ss_listNew( ss_Context* ctx );
static int      ss_listAdd( ss_Context* ctx, ss_List* list, void* val );
static ss_Iter* ss_listIter( ss_Context* ctx, ss_List* list );

static void* ss_iterNext( ss_Context* ctx, ss_Iter* iter );

static ss_Buffer*  ss_bufferNew( ss_Context* ctx );
static int         ss_bufferPut( ss_Context* ctx, ss_Buffer* buf, long ch );
static long const* ss_bufferBuf( ss_Context* ctx, ss_Buffer* buf );
static size_t      ss_bufferLen( ss_Context* ctx, ss_Buffer* buf );

static ss_Pattern* ss_allOfPattern( ss_Context* ctx, ss_List* patterns );
static ss_Pattern* ss_oneOfPattern( ss_Context* ctx, ss_List* patterns );
static ss_Pattern* ss_hasNextPattern( ss_Context* ctx, ss_Pattern* pattern );
static ss_Pattern* ss_notNextPattern( ss_Context* ctx, ss_Pattern* pattern );
static ss_Pattern* ss_zeroOrOnePattern( ss_Context* ctx, ss_Pattern* pattern );
static ss_Pattern* ss_zeroOrMorePattern( ss_Context* ctx, ss_Pattern* pattern );
static ss_Pattern* ss_justOnePattern( ss_Context* ctx, ss_Pattern* pattern );
static ss_Pattern* ss_oneOrMorePattern( ss_Context* ctx, ss_Pattern* pattern );
static ss_Pattern* ss_literalPattern( ss_Context* ctx, long const* str, size_t len );

/****************************** Context Creation ******************************/
ss_Context* ss_init( void ) {
    ss_Context* ctx = ss_alloc( sizeof(ss_Context), TYPE_CONTEXT );
    if( !ctx )
        return NULL;
    
    ctx->patterns = ss_mapNew();
    ctx->errnum   = ss_ERR_NONE;
    ctx->errmsg   = NULL;
    
    ctx->tmpcap = 64;
    ctx->tmptop = 0;
    ctx->tmpbuf = malloc( 64 );
    if( !ctx->tmpbuf ) {
        ss_release( ctx );
        ss_error( ctx, ss_ERR_ALLOC, NULL );
        return NULL;
    }
    
    ss_prelude( ctx );
    return ctx;
}

static void freeContext( void* ptr ) {
    ss_Context* ctx = ptr;
    if( ctx->pattern )
        ss_release( ctx->patterns );
    if( ctx->tmpbuf )
        free( tmpbuf );
    ss_free( ctx );
}

/***************************** String Decoding ********************************/
#define ss_STREAM_END (-1)
#define ss_STREAM_ERR (-2)

static long readByte( ss_Context* ctx, ss_Stream* stream ) {
    if( stream->loc == stream->end )
        return ss_STREAM_END;
    else
        return *(stream->loc++);
}

#define isSingleChr( c ) ( (unsigned char)(c) >> 7 == 0  )
#define isDoubleChr( c ) ( (unsigned char)(c) >> 5 == 6  )
#define isTripleChr( c ) ( (unsigned char)(c) >> 4 == 14 )
#define isQuadChr( c )   ( (unsigned char)(c) >> 3 == 30 )
#define isAfterChr( c )  ( (unsigned char)(c) >> 6 == 2  )
static long readChar( ss_Context* ctx, ss_Stream* stream ) {
    
    if( stream->loc == stream->end )
        return ss_STREAM_END;
    
    long code = 0;
    int  byte = *( stream->loc++ );
    int  size = 0;
    if( isSingleChr( byte ) ) {
        size = 1;
        code = byte;
    }
    else
    if( isDoubleChr( byte ) ) {
        size = 2;
        code = byte & 0x1F;
    }
    else
    if( isTripleChr( byte ) ) {
        size = 3;
        code = byte & 0xF;
    }
    else
    if( isQuadChr( byte ) ) {
        size = 4;
        code = byte & 0x7;
    }
    else {
        ss_error( ctx, "Input is corrupted or not formated as UTF-8" );
        return ss_STREAM_ERROR;
    }
    
    for( int i = 1 ; i < size ; i++ ) {
        if( stream->loc == stream->end )
            return ss_STREAM_END;
        
        byte = *( stream->loc++ );
        code = ( code << 6 ) | ( byte & 0x3F );
    }
    
    return code;
}

static ss_Stream ss_makeStream( ss_Format fmt, char const* loc, char const* end ) {
    ss_Stream stream = { .loc = loc, .end = end };
    
    switch( fmt ) {
        case ss_BYTES:
            stream.read = readByte;
        break;
        case ss_CHARS:
            stream.read = readChar;
        break;
        default:
            assert( false );
    }
    return stream;
}

/********************************* Compilation ********************************/

static int ss_advance( ss_Context* ctx, ss_Compiler* compiler ) {
    compiler->ch1 = compiler->ch2;
    compiler->ch2 = compiler->stream.read( &compiler->stream );
    if( compiler->ch2 == ss_STREAM_ERR )
        return ss_STREAM_ERR;
    else
        return 0;
}

static ss_Compiler* ss_compiler( ss_Context* ctx, ss_Format fmt, char const* str, size_t len ) {
    ss_Compiler* compiler = ss_alloc( sizeof(ss_Compiler), TYPE_COMPILER );
    if( !compiler ) {
        ss_error( ctx, ss_ERR_ALLOC, NULL );
        return NULL;
    }
    
    compiler->stream   = ss_makeStream( fmt, str, str + len );
    compiler->patterns = ss_listNew( ctx );
    if( !compiler->patterns ) {
        ss_release( compiler );
        return NULL;
    }
    
    if( ss_advance( ctx, compiler ) || ss_advance( ctx, compiler ) ) {
        ss_release( compiler );
        return NULL;
    }
    return compiler;
}

static void freeCompiler( void* ptr ) {
    ss_Compiler* compiler = ptr;
    if( compiler->patterns )
        ss_release( compiler->patterns );
    ss_free( compiler );
}

static void ss_whitespace( ss_Context* ctx, ss_Compiler* compiler ) {
    while( isspace( compiler->ch1 ) && !ss_advance( ctx, compiler ) )
        ;
}


static bool isopening( long ch ) {
    return ch == '(' ||
           ch == '{' ||
           ch == '[' ||
           ch == '<';
}
static bool isclosing( long ch ) {
    return ch == ')' ||
           ch == '}' ||
           ch == ']' ||
           ch == '>';
}

static bool isbreak( long ch1, long ch2 ) {
    if( ch1 == '^' ) {
        if( isopening( ch2 ) )
            return true;
        if( ch2 == '*' || ch2 == '?' )
            return true;
    }
    if( ch1 == '~' ) {
        if( isopening( ch2 ) )
            return true;
        if( ch2 == '*' || ch2 == '?' )
            return true;
    }
    if( isopening( ch1 ) )
        return true;
    if( ch1 == '*' || ch1 == '?' || ch1 == '\\' )
        return true;
    
    return false;
}

static bool isend( long ch ) {
  return ch < 0;
}

static ss_Pattern* ss_compileText( ss_Context* ctx, ss_Compiler* compiler ) {
    if( compiler->ch1 == ss_STREAM_END )
        return NULL;
    if( isbreak( compiler->ch1, compiler->ch2 ) )
        return NULL;
    
    ss_Buffer* buf = ss_bufferNew( ctx );
    if( !buf )
        return NULL;
    
    while( !isbreak( compiler->ch1, compiler->ch2 ) && !isend( compiler->ch1 ) ) {
        if( ss_bufferPut( buf, compiler->ch1 ) ) {
            ss_release( buf );
            return NULL;
        }
        if( ss_advance( ctx, compiler ) {
            ss_release( buf );
            return NULL;
        }
    }
    
    long const* str = ss_bufferBuf( buf );
    size_t      len = ss_bufferLen( buf );
    ss_Pattern* pat = ss_literalPattern( str, len );
    
    ss_release( buf );
    
    return pat;
}

static ss_Pattern* ss_compileString( ss_Context* ctx, ss_Compiler* compiler ) {
    long quote;
    if( compiler->ch1 == '"' || compiler->ch1 == '`' || compiler->ch1 == '\'' )
        quote = compiler->ch1;
    else
        return NULL;
    
    if( ss_advance( ctx, compiler ) )
        return NULL;
    
    ss_Buffer* buf = ss_bufferNew( ctx );
    if( !buf )
        return NULL;
    
    while( compiler->ch1 != quote ) {
        if( isend( compiler->ch1 ) ) {
            ss_error( ctx, "Unterminated string" );
            ss_release( buf );
            return NULL;
        }
        if( ss_bufferPut( buf, compiler->ch1 ) ) {
            ss_release( buf );
            return NULL;
        }
        
        if( ss_advance( ctx, compiler ) ) {
            ss_release( buf );
            return NULL;
        }
    }
    
    if( ss_advance( ctx, compiler ) ) {
        ss_release( buf );
        return NULL;
    }
    
    long const* str = ss_bufferBuf( buf );
    size_t      len = ss_bufferLen( buf );
    ss_Pattern* pat = ss_literalPattern( str, len );
    
    ss_release( buf );
    
    return pat;
}

static ss_Pattern* ss_compileChar( ss_Context* ctx, ss_Compiler* compiler ) {
    if( compiler->ch1 != '\\' )
        return NULL;
    if( ss_advance( ctx, compiler ) )
        return NULL;
    
    long chr = compiler->ch1;
    
    if( ss_advance( ctx, compiler ) )
        return NULL;
    
    return ss_literalPattern( &chr, 1 );
}

static ss_Pattern* ss_compileCode( ss_Context* ctx, ss_Compiler* compiler ) {
    if( !isdigit( compiler->ch1 ) )
        return NULL;
    
    long code = 0;
    while( isalnum( compiler->ch1 ) ) {
        if( !isdigit( compiler->ch1 ) ) {
            ss_error( ctx, ss_ERR_SYNTAX, "Non-digit at end of character code" );
            return NULL;
        }
        code = code*10 + (compiler->ch1 - '0');
        
        if( ss_advance( ctx, compiler ) )
            return NULL;
    }
    
    return ss_literalPattern( &code, 1 );
}

static char const* parseName( ss_Context* ctx, ss_Compiler* compiler ) {
    ctx->tmptop = 0;
    
    while( compiler->ch1 == '_' || isalnum( compiler->ch1 ) ) {
        if( ctx->tmptop >= ctx->tmpcap - 1 ) {
            void* rep = realloc( ctx->tmpbuf );
            if( !rep ) {
                ss_error( ctx, ss_ERR_ALLOC, NULL );
                return NULL;
            }
            ctx->tmpbuf = rep;
        }
        
        ctx->tmpbuf[ctx->tmptop++] = (char)compiler->ch1;
        if( ss_advance( ctx, compiler ) )
            return NULL;
    }
    ctx->tmpbuf[ctx->tmptop++] = '\0';
    return buf;
}

static ss_Pattern* ss_compileNamed( ss_Context* ctx, ss_Compiler* compiler ) {
    if( !isalpha( compiler->ch1 ) && compiler->ch1 != '_' && compiler->ch1 != '*' && compiler->ch1 != '?' )
        return NULL;
    
    
    char const* name = NULL;
    if( compiler->ch1 == '*' ) {
        name = "splat";
        if( ss_advance( ctx, compiler ) )
            return NULL;
    }
    else
    if( compiler->ch1 == '?' ) {
        name = "quark";
        if( ss_advance( ctx, compiler ) )
            return NULL;
    }
    else {
        name = parseName( compiler );
        if( !name )
            return NULL;
    }
    
    ss_Pattern* pat = ss_mapGet( compiler->ctx->patterns, name );
    if( !pat ) {
        ss_error( ctx, ss_ERR_ALLOC, NULL );
        return NULL;
    }
    return ss_refer( pat );
}

static ss_Pattern* ss_compilePattern( ss_Compiler* compiler );

static bool arematching( long open, long close ) {
    return (open == '(' && close == ')') ||
           (open == '{' && close == '}') ||
           (open == '[' && close == ']') ||
           (open == '<' && close == '>');
}
static ss_Pattern* ss_compileCompound( ss_Compiler* compiler ) {
    long open;
    if( isopening( compiler->ch1 ) )
        open = compiler->ch1;
    else
        return NULL;
    
    if( ss_advance( compiler ) || ss_whitespace( compiler ) )
        return NULL;
    
    ss_List* oneOfList = ss_listNew();
    while( !isclosing( compiler->ch1 ) ) {
        if( isend( compiler->ch1 ) ) {
            ss_error( ctx, ss_ERR_SYNTAX, "Unterminated pattern" );
            ss_release( oneOfList );
            return NULL;
        }
        
        ss_List* allOfList = ss_listNew();
        do {
            ss_Pattern* pat = ss_compilePattern( compiler );
            if( !pat ) {
                if( !ctx->errnum )
                    ss_error( ctx, ss_ERR_SYNTAX, "Expected sub-pattern" );
                ss_release( oneOfList );
                ss_release( allOfList );
                return NULL;
            }
            ss_listAdd( allOfList, pat );
            ss_whitespace( compiler );
            ss_release( pat );
        } while( compiler->ch1 != '|' && !isclosing( compiler->ch1 ) );
        
        if( compiler->ch1 == '|' )
            ss_advance( compiler );
        
        ss_Pattern* allOfPat = ss_allOfPattern( allOfList );
        ss_listAdd( oneOfList, allOfPat );
        ss_whitespace( compiler );
        ss_release( allOfList );
        ss_release( allOfPat );
    }
    if( !arematching( open, compiler->ch1 ) ) {
        compiler->ctx->error = "Mismatched brackets";
        ss_release( oneOfList );
        return NULL;
    }
    ss_advance( compiler );
    
    ss_Pattern* oneOfPat = ss_oneOfPattern( oneOfList );
    ss_release( oneOfList );
    
    ss_Pattern* compPat  = NULL;
    switch( open ) {
        case '(':
            compPat = ss_justOnePattern( oneOfPat );
            ss_release( oneOfPat );
        break;
        case '{':
            compPat = ss_zeroOrMorePattern( oneOfPat );
            ss_release( oneOfPat );
        break;
        case '[':
            compPat = ss_zeroOrOnePattern( oneOfPat );
            ss_release( oneOfPat );
        break;
        case '<':
            compPat = ss_oneOrMorePattern( oneOfPat );
            ss_release( oneOfPat );
        break;
        default:
            assert( false );
        break;
    }
    return compPat;
}

static ss_Pattern* ss_compilePrimitive( ss_Compiler* compiler ) {
    ss_Pattern* pat = NULL;
    pat = ss_compileString( compiler );
    if( pat )
        goto parsed;
    pat = ss_compileChar( compiler );
    if( pat )
        goto parsed;
    pat = ss_compileCode( compiler );
    if( pat )
        goto parsed;
    pat = ss_compileCompound( compiler );
    if( pat )
        goto parsed;
    return NULL;

parsed:

    if( compiler->ch1 != ':' )
        return pat;
    
    ss_advance( compiler );
    
    char const* binding = parseName( compiler );
    if( !binding ) {
        ss_release( pat );
        compiler->ctx->error = "Invalid binding name";
        return NULL;
    }
    
    size_t len = strlen( binding );
    char*  cpy = xmalloc( len + 1 );
    strcpy( cpy, binding );
    pat->binding = cpy;
    return pat;
}

static ss_Pattern* ss_compileNotNext( ss_Compiler* compiler ) {
    if( compiler->ch1 != '~' )
        return NULL;
    ss_advance( compiler );
    ss_whitespace( compiler );
    
    ss_Pattern* pat = ss_compilePrimitive( compiler );
    if( !pat ) {
        if( !compiler->ctx->error )
            compiler->ctx->error = "Expected sub-pattern";
        return NULL;
    }
    ss_Pattern* notNextPat = ss_notNextPattern( pat );
    ss_release( pat );
    return notNextPat;
}

static ss_Pattern* ss_compileHasNext( ss_Compiler* compiler ) {
    if( compiler->ch1 != '^' )
        return NULL;
    ss_advance( compiler );
    ss_whitespace( compiler );
    
    ss_Pattern* pat = ss_compilePrimitive( compiler );
    if( !pat ) {
        if( !compiler->ctx->error )
            compiler->ctx->error = "Expected sub-pattern";
        return NULL;
    }
    ss_Pattern* hasNextPat = ss_hasNextPattern( pat );
    ss_release( pat );
    return hasNextPat;
}

static ss_Pattern* ss_compilePattern( ss_Compiler* compiler ) {
    ss_Pattern* pat = ss_compilePrimitive( compiler );
    if( pat || compiler->ctx->error )
        return pat;
    pat = ss_compileNotNext( compiler );
    if( pat || compiler->ctx->error )
        return pat;
    pat = ss_compileHasNext( compiler );
    if( pat || compiler->ctx->error )
        return pat;
    pat = ss_compileNamed( compiler );
    if( pat || compiler->ctx->error )
        return pat;
    return NULL;
}

static ss_Pattern* ss_compileFull( ss_Compiler* compiler ) {
    ss_List* allOfList = ss_listNew();
    while( true ) {
        ss_Pattern* pat = NULL;
        
        pat = ss_compileText( compiler );
        if( !pat )
            pat = ss_compilePattern( compiler );
        if( !pat )
            break;
        
        ss_listAdd( allOfList, pat );
        ss_release( pat );
    }
    ss_Pattern* allOfPat = ss_allOfPattern( allOfList );
    ss_release( allOfList );
    
    return allOfPat;
}

ss_Pattern* ss_compile( ss_Context* ctx, ss_Format fmt, char const* pat, size_t len ) {
    ss_Compiler* compiler = ss_compiler( ctx, fmt, pat, len );
    ss_Pattern*  pattern  = ss_compileFull( compiler );
    ss_release( compiler );
    return pattern;
}

void ss_define( ss_Context* ctx, char const* name, ss_Pattern* pat ) {
    ss_mapPut( ctx->patterns, name, pat );
    ss_mapCommit( ctx->patterns );
}

/********************************** Matching **********************************/
ss_Scanner* ss_start( ss_Context* ctx, ss_Format fmt, ss_Pattern* pat, char const* str, size_t len ) {
    ss_Scanner* scanner = ss_alloc( sizeof(ss_Scanner), TYPE_SCANNER );
    scanner->pat = ss_refer( pat );
    scanner->stream = ss_makeStream( fmt, str, str + len );
    return scanner;
}

ss_Match* ss_match( ss_Scanner* scanner ) {
    ss_Stream stream = scanner->stream;
    
    ss_Pattern* pat   = scanner->pat;
    ss_Map*     scope = ss_mapNew();
    ss_Match*   match = pat->match( pat, scope, &stream );
    ss_mapCommit( scope );
    ss_release( scope );
    if( stream.end == scanner->stream.end )
        return match;
    
    ss_release( match );
    return NULL;
}

ss_Match* ss_find( ss_Scanner* scanner ) {
    ss_Match* m = NULL;
    while( !m && scanner->stream.loc != scanner->stream.end ) {
        ss_Stream stream = scanner->stream;
        
        ss_Pattern* pat   = scanner->pat;
        ss_Map*     scope = ss_mapNew();
        
        m = pat->match( pat, scope, &stream );
        ss_mapCommit( scope );
        ss_release( scope );
        
        scanner->stream.read( &scanner->stream );
    }
    
    if( !m )
        return NULL;
    
    scanner->stream.loc = m->end;
    return m;
}

ss_Match* ss_next( ss_Match* match ) {
    return ss_refer( match->next );
}


char const* ss_loc( ss_Match* match ) {
    return match->loc;
}

char const* ss_end( ss_Match* match ) {
    return match->end;
}

ss_Match* ss_get( ss_Match* match, char const* binding ) {
    ss_Match* m = ss_mapGet( match->scope, binding );
    if( m )
        return ss_refer( m );
    else
        return NULL;
}

static void freeScanner( void* ptr ) {
    ss_Scanner* scanner = ptr;
    ss_release( scanner->pat );
    ss_free( scanner );
}

/****************************** Object Allocation *****************************/
static void* xmalloc( size_t sz ) {
    void* ptr = malloc( sz );
    if( !ptr ) {
        fprintf( stderr, "Allocation error\n" );
        abort();
    }
    
    return ptr;
}

static void* xcalloc( size_t nmem, size_t size ) {
    void* ptr = calloc( nmem, size );
    if( !ptr ) {
        fprintf( stderr, "Allocation error\n" );
        abort();
    }
    return ptr;
}
static void* xrealloc( void* ptr, size_t sz ) {
    void* nptr = realloc( ptr, sz );
    if( !nptr ) {
        free( nptr );
        fprintf( stderr, "Allocation error\n" );
        abort();
    }
    return nptr;
}

#define ss_obj( PTR ) ((void*)(PTR) - sizeof(ss_Object))
static void* ss_alloc( size_t sz, ss_Type type ) {
    ss_Object* obj = xmalloc( sizeof(ss_Object) + sz );
    obj->type = type;
    obj->refc = 1;
    return obj->data;
}

static void* ss_refer( void* ptr ) {
    ss_Object* obj = ss_obj( ptr );
    obj->refc++;
    return ptr;
}

static void ss_free( void* ptr ) {
    ss_Object* obj = ss_obj( ptr );
    free( obj );
}

static void freePattern( void* ptr );
static void freeScanner( void* ptr );
static void freeContext( void* ptr );
static void freeMatch( void* ptr );
static void freeMap( void* ptr );
static void freeList( void* ptr );
static void freeBuffer( void* ptr );
static void freeCompiler( void* ptr );
static void freeIter( void* ptr );

static void (*freeFuns[])( void* ptr ) = {
    freePattern,
    freeScanner,
    freeContext,
    freeMatch,
    freeMap,
    freeList,
    freeBuffer,
    freeCompiler,
    freeIter
};

void ss_release( void* ptr ) {
    ss_Object* obj = ss_obj( ptr );
    assert( obj->type < TYPE_LAST );
    
    if( --obj->refc == 0 )
        freeFuns[obj->type]( ptr );
}

/***************************** Map Implementation *****************************/

typedef struct ss_MapNode ss_MapNode;

struct ss_MapNode {
    ss_MapNode* next;
    unsigned    hash;
    void*       value;
    char        key[];
};

struct ss_Map {
    ss_MapNode** buf;
    unsigned     cnt;
    unsigned     cap;
    
    ss_MapNode* staged;
};

static ss_Map* ss_mapNew( void ) {
    ss_Map* map = ss_alloc( sizeof(ss_Map), TYPE_MAP );
    map->cap = 21;
    map->cnt = 0;
    map->buf = xcalloc( map->cap, sizeof(ss_MapNode*) );
    map->staged = NULL;
    
    return map;
}

static unsigned hash( char const* key ) {
    unsigned h = 0;
    for( size_t i = 0 ; key[i] != '\0' ; i++ )
        h = h*37 + key[i];
    return h;
}

static void growMap( ss_Map* map, unsigned cap ) {
    size_t       ocap = map->cap;
    ss_MapNode** obuf = map->buf;
    
    map->cap = cap;
    map->buf = xcalloc( map->cap, sizeof(ss_MapNode*) );
    
    for( unsigned i = 0 ; i < ocap ; i++ ) {
        ss_MapNode* it = obuf[i];
        while( it ) {
            ss_MapNode* node = it;
            it = it->next;
            
            unsigned j = node->hash % map->cap;
            node->next = map->buf[j];
            map->buf[j] = node;
        }
    }
    
    free( obuf );
}

static void ss_mapPut( ss_Map* map, char const* key, void* val ) {
    unsigned h = hash( key );

    size_t keyLen = strlen( key );
    ss_MapNode* node = xmalloc( sizeof(ss_MapNode) + keyLen + 1 );
    node->next  = map->staged;
    node->hash  = h;
    node->value = ss_refer( val );
    strcpy( node->key, key );
    
    map->staged = node;
}

static void* ss_mapGet( ss_Map* map, char const* key ) {
    unsigned h = hash( key );
    unsigned i = h % map->cap;
    
    ss_MapNode* it = map->buf[i];
    while( it ) {
        if( it->hash == h && !strcmp( key, it->key ) ) {
            return it->value;
        }
        it = it->next;
    }
    return NULL;
}

static void ss_mapCommit( ss_Map* map ) {
    for( ss_MapNode* it = map->staged ; it ; it = it->next )
        map->cnt++;
    
    if( map->cnt*3 > map->cap )
        growMap( map, map->cnt*3 );

    ss_MapNode* it = map->staged;
    while( it ) {
        ss_MapNode* node = it;
        it = node->next;
        
        unsigned i = node->hash % map->cap;
        node->next  = map->buf[i];
        map->buf[i] = node;
    }
    
    map->staged = NULL;
}

static void ss_mapCancel( ss_Map* map ) {
    ss_MapNode* it = map->staged;
    while( it ) {
        ss_MapNode* node = it;
        it = node->next;
        
        ss_release( node->value );
        free( node );
    }
    
    map->staged = NULL;
}

static void freeMap( void* ptr ) {
    ss_Map* map = ptr;
    
    ss_mapCancel( map );
    
    for( unsigned i = 0 ; i < map->cap ; i++ ) {
        ss_MapNode* it = map->buf[i];
        while( it ) {
            ss_MapNode* node = it;
            it = it->next;
            
            ss_release( node->value );
            free( node );
        }
    }
    
    free( map->buf );
    ss_free( map );
}

/**************************** List Implementation *****************************/

typedef struct ss_ListNode ss_ListNode;

struct ss_ListNode {
    ss_ListNode* next;
    void*        value;
};

struct ss_List {
    ss_ListNode* first;
    ss_ListNode* last;
};

static ss_List* ss_listNew( void ) {
    ss_List* list = ss_alloc( sizeof(ss_List), TYPE_LIST );
    list->first  = NULL;
    list->last   = NULL;
    return list;
}

static void ss_listAdd( ss_List* list, void* val ) {
    ss_ListNode* node = xmalloc( sizeof(ss_ListNode) );
    node->value = ss_refer( val );
    node->next  = NULL;
    
    if( list->first ) {
        list->last->next = node;
        list->last       = node;
    }
    else {
        list->first  = node;
        list->last   = node;
    }
}

struct ss_Iter {
    ss_List*     list;
    ss_ListNode* next;
};

static ss_Iter* ss_listIter( ss_List* list ) {
    ss_Iter* iter = ss_alloc( sizeof(ss_Iter), TYPE_ITER );
    iter->list = ss_refer( list );
    iter->next = list->first;
    return iter;
}

static void* ss_iterNext( ss_Iter* iter ) {
    if( !iter->next )
        return NULL;
    ss_ListNode* node = iter->next;
    iter->next = node->next;
    
    return node->value;
}

static void freeList( void* dat ) {
    ss_List* list = dat;
    
    ss_ListNode* it = list->first;
    while( it ) {
        ss_ListNode* node = it;
        it = it->next;
        
        ss_release( node->value );
        free( node );
    }
    
    ss_free( list );
}

static void freeIter( void* dat ) {
    ss_Iter* iter = dat;
    ss_release( iter->list );
    ss_free( iter );
}

/**************************** Buffer Implementation ***************************/

struct ss_Buffer {
    size_t cap;
    size_t top;
    long*  buf;
};

static ss_Buffer* ss_bufferNew( void ) {
    ss_Buffer* buf = ss_alloc( sizeof(ss_Buffer), TYPE_BUFFER );
    buf->cap = 16;
    buf->top = 0;
    buf->buf = xmalloc( sizeof(long)*buf->cap );
    return buf;
}

static void ss_bufferPut( ss_Buffer* buf, long ch ) {
    if( buf->top >= buf->cap ) {
        buf->cap *= 2;
        buf->buf = xrealloc( buf->buf, sizeof(long)*buf->cap );
    }
    buf->buf[buf->top++] = ch;
}

static size_t ss_bufferLen( ss_Buffer* buf ) {
    return buf->top;
}

static long const* ss_bufferBuf( ss_Buffer* buf ) {
    return buf->buf;
}

static void freeBuffer( void* ptr ) {
    ss_Buffer* buf = ptr;
    free( buf->buf );
    ss_free( buf );
}


/**************************** Primitive Patterns ******************************/
static void freePattern( void* ptr ) {
    ss_Pattern* pat = ptr;
    if( pat->clean )
        pat->clean( pat );
    if( pat->binding )
        free( pat->binding );
    ss_free( pat );
}

static void freeMatch( void* ptr ) {
    ss_Match* match = ptr;
    if( match->scope )
        ss_release( match->scope );
    if( match->next )
        ss_release( match->next );
    ss_free( match );
}

typedef struct {
    ss_Pattern pat;
    ss_List*   patterns;
} AllOfPattern;

static ss_Match* allOfMatcher( ss_Pattern* p, ss_Map* scope, ss_Stream* stream ) {
    AllOfPattern* allOfPat = (AllOfPattern*)p;
    
    char const* loc = stream->loc;
    
    ss_Iter*    it  = ss_listIter( allOfPat->patterns );
    ss_Pattern* nxt = ss_iterNext( it );
    ss_Match*   sub = NULL;
    while( nxt ) {
        sub = nxt->match( nxt, scope, stream );
        if( !sub ) {
            ss_release( it );
            return NULL;
        }
        ss_release( sub );
        nxt = ss_iterNext( it );
    }
    ss_release( it );
    char const* end = stream->loc;
    
    ss_Match* mat = ss_alloc( sizeof(ss_Match), TYPE_MATCH );
    mat->scope = ss_refer( scope );
    mat->next  = NULL;
    mat->loc   = loc;
    mat->end   = end;
    return mat;
}

static void allOfCleaner( ss_Pattern* pat ) {
    AllOfPattern* allOfPat = (AllOfPattern*)pat;
    ss_release( allOfPat->patterns );
}

static ss_Pattern* ss_allOfPattern( ss_List* patterns ) {
    AllOfPattern* allOfPat = ss_alloc( sizeof(AllOfPattern), TYPE_PATTERN );
    allOfPat->pat.match   = allOfMatcher;
    allOfPat->pat.clean   = allOfCleaner;
    allOfPat->pat.binding = NULL;
    allOfPat->patterns    = ss_refer( patterns );
    return (ss_Pattern*)allOfPat;
}


typedef struct {
    ss_Pattern pat;
    ss_List*   patterns;
} OneOfPattern;

static ss_Match* oneOfMatcher( ss_Pattern* p, ss_Map* scope, ss_Stream* stream ) {
    OneOfPattern* oneOfPat = (OneOfPattern*)p;
    
    ss_Iter*    it  = ss_listIter( oneOfPat->patterns );
    ss_Pattern* nxt = ss_iterNext( it );
    ss_Match*   sub = NULL;
    while( nxt ) {
        ss_Stream saved = *stream;
        
        sub = nxt->match( nxt, scope, stream );
        if( sub ) {
            ss_release( it );
            return sub;
        }
        *stream = saved;
        ss_mapCancel( scope );
        nxt = ss_iterNext( it );
    }
    ss_release( it );
    return NULL;
}

static void oneOfCleaner( ss_Pattern* p ) {
    OneOfPattern* oneOfPat = (OneOfPattern*)p;
    ss_release( oneOfPat->patterns );
}

static ss_Pattern* ss_oneOfPattern( ss_List* patterns ) {
    OneOfPattern* oneOfPat = ss_alloc( sizeof(OneOfPattern), TYPE_PATTERN );
    oneOfPat->pat.match   = oneOfMatcher;
    oneOfPat->pat.clean   = oneOfCleaner;
    oneOfPat->pat.binding = NULL;
    oneOfPat->patterns    = ss_refer( patterns );
    return (ss_Pattern*)oneOfPat;
}


typedef struct {
    ss_Pattern  pat;
    ss_Pattern* wrapped;
} HasNextPattern;

static ss_Match* hasNextMatcher( ss_Pattern* p, ss_Map* scope, ss_Stream* stream ) {
    HasNextPattern* hasNextPat = (HasNextPattern*)p;
    
    ss_Stream saved = *stream;
    ss_Pattern* pat = hasNextPat->wrapped;
    ss_Match*   match = pat->match( pat, scope, stream );
    
    *stream = saved;
    
    return match;
}

static void hasNextCleaner( ss_Pattern* p ) {
    HasNextPattern* hasNextPat = (HasNextPattern*)p;
    ss_release( hasNextPat->wrapped );
}

static ss_Pattern* ss_hasNextPattern( ss_Pattern* pattern ) {
    HasNextPattern* hasNextPat = ss_alloc( sizeof(HasNextPattern), TYPE_PATTERN );
    hasNextPat->pat.match   = hasNextMatcher;
    hasNextPat->pat.clean   = hasNextCleaner;
    hasNextPat->pat.binding = NULL;
    hasNextPat->wrapped     = ss_refer( pattern );
    return (ss_Pattern*)hasNextPat;
}


typedef struct {
    ss_Pattern  pat;
    ss_Pattern* wrapped;
} NotNextPattern;

static ss_Match* notNextMatcher( ss_Pattern* p, ss_Map* scope, ss_Stream* stream ) {
    NotNextPattern* notNextPat = (NotNextPattern*)p;
    
    char const* loc = stream->loc;
    
    ss_Stream   saved = *stream;
    ss_Pattern* pat   = notNextPat->wrapped;
    ss_Match*   match = pat->match( pat, NULL, stream );
    
    *stream = saved;
    
    if( match ) {
        ss_release( match );
        return NULL;
    }
    
    match = ss_alloc( sizeof(ss_Match), TYPE_MATCH );
    match->scope = NULL;
    match->next  = NULL;
    match->loc   = loc;
    match->end   = loc;
    return match;
}

static void notNextCleaner( ss_Pattern* p ) {
    NotNextPattern* notNextPat = (NotNextPattern*)p;
    ss_release( notNextPat->wrapped );
}

static ss_Pattern* ss_notNextPattern( ss_Pattern* pattern ) {
    NotNextPattern* notNextPat = ss_alloc( sizeof(NotNextPattern), TYPE_PATTERN );
    notNextPat->pat.match   = notNextMatcher;
    notNextPat->pat.clean   = notNextCleaner;
    notNextPat->pat.binding = NULL;
    notNextPat->wrapped     = ss_refer( pattern );
    return (ss_Pattern*)notNextPat;
}


typedef struct {
    ss_Pattern  pat;
    ss_Pattern* wrapped;
} ZeroOrOnePattern;

static ss_Match* zeroOrOneMatcher( ss_Pattern* p, ss_Map* scope, ss_Stream* stream ) {
    ZeroOrOnePattern* zeroOrOnePat = (ZeroOrOnePattern*)p;
    
    char const* loc = stream->loc;
    
    ss_Stream   saved  = *stream;
    ss_Map*     sscope = ss_mapNew();
    ss_Pattern* pat    = zeroOrOnePat->wrapped;
    ss_Match*   match  = pat->match( pat, sscope, stream );
    ss_mapCommit( sscope );
    ss_release( sscope );
    
    if( !match ) {
        *stream = saved;
        match = ss_alloc( sizeof(ss_Match), TYPE_MATCH );
        match->scope = NULL;
        match->next  = NULL;
        match->loc   = loc;
        match->end   = loc;
    }
    if( zeroOrOnePat->pat.binding && scope )
        ss_mapPut( scope, zeroOrOnePat->pat.binding, match );
    return match;
}

static void zeroOrOneCleaner( ss_Pattern* p ) {
    ZeroOrOnePattern* zeroOrOnePat = (ZeroOrOnePattern*)p;
    ss_release( zeroOrOnePat->wrapped );
}

static ss_Pattern* ss_zeroOrOnePattern( ss_Pattern* pattern ) {
    ZeroOrOnePattern* zeroOrOnePat = ss_alloc( sizeof(ZeroOrOnePattern), TYPE_PATTERN );
    zeroOrOnePat->pat.match   = zeroOrOneMatcher;
    zeroOrOnePat->pat.clean   = zeroOrOneCleaner;
    zeroOrOnePat->pat.binding = NULL;
    zeroOrOnePat->wrapped     = ss_refer( pattern );
    return (ss_Pattern*)zeroOrOnePat;
}


typedef struct {
    ss_Pattern  pat;
    ss_Pattern* wrapped;
} ZeroOrMorePattern;

static ss_Match* zeroOrMoreMatcher( ss_Pattern* p, ss_Map* scope, ss_Stream* stream ) {
    ZeroOrMorePattern* zeroOrMorePat = (ZeroOrMorePattern*)p;
    
    char const* loc = stream->loc;
    
    ss_Stream   saved  = *stream;
    ss_Map*     sscope = ss_mapNew();
    ss_Pattern* pat    = zeroOrMorePat->wrapped;
    ss_Match*   first  = pat->match( pat, sscope, stream );
    ss_Match*   last   = first;
    ss_mapCommit( sscope );
    ss_release( sscope );
    if( !first ) {
        *stream = saved;
        ss_Match* match = ss_alloc( sizeof(ss_Match), TYPE_MATCH );
        match->scope = NULL;
        match->next  = NULL;
        match->loc   = loc;
        match->end   = loc;
        if( zeroOrMorePat->pat.binding && scope )
            ss_mapPut( scope, zeroOrMorePat->pat.binding, match );
        return match;
    }
    
    saved  = *stream;
    sscope = ss_mapNew();
    ss_Match* next = pat->match( pat, sscope, stream );
    ss_mapCommit( sscope );
    ss_release( sscope );
    while( next ) {
        last->next = next;
        last = next;
        
        saved  = *stream;
        sscope = ss_mapNew();
        next   = pat->match( pat, sscope, stream );
        ss_mapCommit( sscope );
        ss_release( sscope );
    }
    *stream = saved;
    
    if( zeroOrMorePat->pat.binding && scope )
        ss_mapPut( scope, zeroOrMorePat->pat.binding, first );
    return first;
}

static void zeroOrMoreCleaner( ss_Pattern* pat ) {
    ZeroOrMorePattern* zeroOrMorePat = (ZeroOrMorePattern*)pat;
    ss_release( zeroOrMorePat->wrapped );
}

static ss_Pattern* ss_zeroOrMorePattern( ss_Pattern* pattern ) {
    ZeroOrMorePattern* zeroOrMorePat = ss_alloc( sizeof(ZeroOrMorePattern), TYPE_PATTERN );
    zeroOrMorePat->pat.match   = zeroOrMoreMatcher;
    zeroOrMorePat->pat.clean   = zeroOrMoreCleaner;
    zeroOrMorePat->pat.binding = NULL;
    zeroOrMorePat->wrapped     = ss_refer( pattern );
    return (ss_Pattern*)zeroOrMorePat;
}


typedef struct {
    ss_Pattern  pat;
    ss_Pattern* wrapped;
} JustOnePattern;

static ss_Match* justOneMatcher( ss_Pattern* p, ss_Map* scope, ss_Stream* stream ) {
    JustOnePattern* justOnePat = (JustOnePattern*)p;
    
    ss_Map*     sscope = ss_mapNew();
    ss_Pattern* pat   = justOnePat->wrapped;
    ss_Match*   match = pat->match( pat, sscope, stream );
    ss_mapCommit( sscope );
    ss_release( sscope );
    if( justOnePat->pat.binding && scope )
        ss_mapPut( scope, justOnePat->pat.binding, match );
    return match;
}

static void justOneCleaner( ss_Pattern* p ) {
    JustOnePattern* justOnePat = (JustOnePattern*)p;
    ss_release( justOnePat->wrapped );
}

static ss_Pattern* ss_justOnePattern( ss_Pattern* pattern ) {
    JustOnePattern* justOnePat = ss_alloc( sizeof(JustOnePattern), TYPE_PATTERN );
    justOnePat->pat.match   = justOneMatcher;
    justOnePat->pat.clean   = justOneCleaner;
    justOnePat->pat.binding = NULL;
    justOnePat->wrapped     = ss_refer( pattern );
    return (ss_Pattern*)justOnePat;
}

typedef struct {
    ss_Pattern  pat;
    ss_Pattern* wrapped;
} OneOrMorePattern;

static ss_Match* oneOrMoreMatcher( ss_Pattern* p, ss_Map* scope, ss_Stream* stream ) {
    OneOrMorePattern* oneOrMorePat = (OneOrMorePattern*)p;
    
    ss_Stream   saved  = *stream;
    ss_Map*     sscope = ss_mapNew();
    ss_Pattern* pat    = oneOrMorePat->wrapped;
    ss_Match*   first  = pat->match( pat, sscope, stream );
    ss_Match*   last   = first;
    ss_mapCommit( sscope );
    ss_release( sscope );
    if( !first )
        return NULL;
    
    saved  = *stream;
    sscope = ss_mapNew();
    ss_Match* next = pat->match( pat, sscope, stream );
    ss_mapCommit( sscope );
    ss_release( sscope );
    
    while( next ) {
        last->next = next;
        last = next;
        
        saved  = *stream;
        sscope = ss_mapNew();
        next   = pat->match( pat, sscope, stream );
        ss_mapCommit( sscope );
        ss_release( sscope );
    }
    *stream = saved;
    
    if( oneOrMorePat->pat.binding && scope )
        ss_mapPut( scope, oneOrMorePat->pat.binding, first );
    return first;
}

static void oneOrMoreCleaner( ss_Pattern* pat ) {
    OneOrMorePattern* oneOrMorePat = (OneOrMorePattern*)pat;
    ss_release( oneOrMorePat->wrapped );
}

static ss_Pattern* ss_oneOrMorePattern( ss_Pattern* pattern ) {
    OneOrMorePattern* oneOrMorePat = ss_alloc( sizeof(OneOrMorePattern), TYPE_PATTERN );
    oneOrMorePat->pat.match   = oneOrMoreMatcher;
    oneOrMorePat->pat.clean   = oneOrMoreCleaner;
    oneOrMorePat->pat.binding = NULL;
    oneOrMorePat->wrapped     = ss_refer( pattern );
    return (ss_Pattern*)oneOrMorePat;
}


typedef struct {
    ss_Pattern  pat;
    size_t      len;
    long        str[];
} LiteralPattern;

static ss_Match* literalMatcher( ss_Pattern* p, ss_Map* scope, ss_Stream* stream ) {
    LiteralPattern* literalPat = (LiteralPattern*)p;
    
    char const* loc = stream->loc;
    for( unsigned i = 0 ; i < literalPat->len ; i++ ) {
        if( literalPat->str[i] != stream->read( stream ) )
            return NULL;
    }
    char const* end = stream->loc;
    
    ss_Match* match = ss_alloc( sizeof(ss_Match), TYPE_MATCH );
    match->scope = NULL;
    match->next  = NULL;
    match->loc   = loc;
    match->end   = end;
    
    if( literalPat->pat.binding && scope )
        ss_mapPut( scope, literalPat->pat.binding, match );
    return match;
}

static ss_Pattern* ss_literalPattern( long const* str, size_t len ) {
    LiteralPattern* literalPat = ss_alloc( sizeof(LiteralPattern) + sizeof(long)*len, TYPE_PATTERN );
    literalPat->pat.match   = literalMatcher;
    literalPat->pat.clean   = NULL;
    literalPat->pat.binding = NULL;
    memcpy( literalPat->str, str, sizeof(long)*len );
    literalPat->len = len;
    return (ss_Pattern*)literalPat;
}


/****************************** Named Patterns ********************************/

static ss_Match* charMatcher( ss_Pattern* p, ss_Map* scope, ss_Stream* stream ) {
    char const* loc = stream->loc;
    long        chr = stream->read( stream );
    char const* end = stream->loc;
    if( chr < 0 )
        return NULL;
    
    ss_Match* match = ss_alloc( sizeof(ss_Match), TYPE_MATCH );
    match->scope = NULL;
    match->next  = NULL;
    match->loc   = loc;
    match->end   = end;
    return match;
}

static ss_Pattern* ss_charPattern( void ) {
    ss_Pattern* pat = ss_alloc( sizeof(ss_Pattern), TYPE_PATTERN );
    pat->match   = charMatcher;
    pat->clean   = NULL;
    pat->binding = NULL;
    return pat;
}

static ss_Match* digitMatcher( ss_Pattern* p, ss_Map* scope, ss_Stream* stream ) {
    char const* loc = stream->loc;
    long        chr = stream->read( stream );
    char const* end = stream->loc;
    if( !isdigit( chr ) )
        return NULL;
    
    ss_Match* match = ss_alloc( sizeof(ss_Match), TYPE_MATCH );
    match->scope = NULL;
    match->next  = NULL;
    match->loc   = loc;
    match->end   = end;
    return match;
}

static ss_Pattern* ss_digitPattern( void ) {
    ss_Pattern* pat = ss_alloc( sizeof(ss_Pattern), TYPE_PATTERN );
    pat->match   = digitMatcher;
    pat->clean   = NULL;
    pat->binding = NULL;
    return pat;
}

static ss_Match* alphaMatcher( ss_Pattern* p, ss_Map* scope, ss_Stream* stream ) {
    char const* loc = stream->loc;
    long        chr = stream->read( stream );
    char const* end = stream->loc;
    if( !isalpha( chr ) )
        return NULL;
    
    ss_Match* match = ss_alloc( sizeof(ss_Match), TYPE_MATCH );
    match->scope = NULL;
    match->next  = NULL;
    match->loc   = loc;
    match->end   = end;
    return match;
}

static ss_Pattern* ss_alphaPattern( void ) {
    ss_Pattern* pat = ss_alloc( sizeof(ss_Pattern), TYPE_PATTERN );
    pat->match   = alphaMatcher;
    pat->clean   = NULL;
    pat->binding = NULL;
    return pat;
}

static ss_Match* alnumMatcher( ss_Pattern* p, ss_Map* scope, ss_Stream* stream ) {
    char const* loc = stream->loc;
    long        chr = stream->read( stream );
    char const* end = stream->loc;
    if( !isalnum( chr ) )
        return NULL;
    
    ss_Match* match = ss_alloc( sizeof(ss_Match), TYPE_MATCH );
    match->scope = NULL;
    match->next  = NULL;
    match->loc   = loc;
    match->end   = end;
    return match;
}

static ss_Pattern* ss_alnumPattern( void ) {
    ss_Pattern* pat = ss_alloc( sizeof(ss_Pattern), TYPE_PATTERN );
    pat->match   = alnumMatcher;
    pat->clean   = NULL;
    pat->binding = NULL;
    return pat;
}

static ss_Match* blankMatcher( ss_Pattern* p, ss_Map* scope, ss_Stream* stream ) {
    char const* loc = stream->loc;
    long        chr = stream->read( stream );
    char const* end = stream->loc;
    if( !isblank( chr ) )
        return NULL;
    
    ss_Match* match = ss_alloc( sizeof(ss_Match), TYPE_MATCH );
    match->scope = NULL;
    match->next  = NULL;
    match->loc   = loc;
    match->end   = end;
    return match;
}

static ss_Pattern* ss_blankPattern( void ) {
    ss_Pattern* pat = ss_alloc( sizeof(ss_Pattern), TYPE_PATTERN );
    pat->match   = blankMatcher;
    pat->clean   = NULL;
    pat->binding = NULL;
    return pat;
}

static ss_Match* spaceMatcher( ss_Pattern* p, ss_Map* scope, ss_Stream* stream ) {
    char const* loc = stream->loc;
    long        chr = stream->read( stream );
    char const* end = stream->loc;
    if( !isspace( chr ) )
        return NULL;
    
    ss_Match* match = ss_alloc( sizeof(ss_Match), TYPE_MATCH );
    match->scope = NULL;
    match->next  = NULL;
    match->loc   = loc;
    match->end   = end;
    return match;
}

static ss_Pattern* ss_spacePattern( void ) {
    ss_Pattern* pat = ss_alloc( sizeof(ss_Pattern), TYPE_PATTERN );
    pat->match   = spaceMatcher;
    pat->clean   = NULL;
    pat->binding = NULL;
    return pat;
}


static ss_Match* upperMatcher( ss_Pattern* p, ss_Map* scope, ss_Stream* stream ) {
    char const* loc = stream->loc;
    long        chr = stream->read( stream );
    char const* end = stream->loc;
    if( !isupper( chr ) )
        return NULL;
    
    ss_Match* match = ss_alloc( sizeof(ss_Match), TYPE_MATCH );
    match->scope = NULL;
    match->next  = NULL;
    match->loc   = loc;
    match->end   = end;
    return match;
}

static ss_Pattern* ss_upperPattern( void ) {
    ss_Pattern* pat = ss_alloc( sizeof(ss_Pattern), TYPE_PATTERN );
    pat->match   = upperMatcher;
    pat->clean   = NULL;
    pat->binding = NULL;
    return pat;
}

static ss_Match* lowerMatcher( ss_Pattern* p, ss_Map* scope, ss_Stream* stream ) {
    char const* loc = stream->loc;
    long        chr = stream->read( stream );
    char const* end = stream->loc;
    if( !islower( chr ) )
        return NULL;
    
    ss_Match* match = ss_alloc( sizeof(ss_Match), TYPE_MATCH );
    match->scope = NULL;
    match->next  = NULL;
    match->loc   = loc;
    match->end   = end;
    return match;
}

static ss_Pattern* ss_lowerPattern( void ) {
    ss_Pattern* pat = ss_alloc( sizeof(ss_Pattern), TYPE_PATTERN );
    pat->match   = lowerMatcher;
    pat->clean   = NULL;
    pat->binding = NULL;
    return pat;
}


static void ss_prelude( ss_Context* ctx ) {
    ss_Pattern* charPat = ss_charPattern();
    ss_mapPut( ctx->patterns, "char", charPat );
    ss_release( charPat );
    
    ss_Pattern* digitPat = ss_digitPattern();
    ss_mapPut( ctx->patterns, "digit", digitPat );
    ss_release( digitPat );
    
    ss_Pattern* alphaPat = ss_alphaPattern();
    ss_mapPut( ctx->patterns, "alpha", alphaPat );
    ss_release( alphaPat );
    
    ss_Pattern* alnumPat = ss_alnumPattern();
    ss_mapPut( ctx->patterns, "alnum", alnumPat );
    ss_release( alnumPat );
    
    ss_Pattern* blankPat = ss_blankPattern();
    ss_mapPut( ctx->patterns, "blank", blankPat );
    ss_release( blankPat );
    
    ss_Pattern* spacePat = ss_spacePattern();
    ss_mapPut( ctx->patterns, "space", spacePat );
    ss_release( spacePat );
    
    ss_Pattern* upperPat = ss_upperPattern();
    ss_mapPut( ctx->patterns, "upper", upperPat );
    ss_release( upperPat );
    
    ss_Pattern* lowerPat = ss_lowerPattern();
    ss_mapPut( ctx->patterns, "lower", lowerPat );
    ss_release( lowerPat );
    
    ss_mapCommit( ctx->patterns );
}
