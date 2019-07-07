#include "ss.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

static bool testMatch( ss_Context* ctx, ss_Format fmt, char const* p, char const* s ) {
    ss_Pattern* pat = ss_compile( ctx, fmt, p, strlen( p ) );
    if( ss_error( ctx ) )
        goto fail;
    
    ss_Scanner* scanner = ss_start( ctx, fmt, pat, s, strlen( s ) );
    if( ss_error( ctx ) )
        goto fail;
    
    ss_Match* match = ss_match( scanner );
    if( !match )
        goto fail;
    
    if( ss_loc( match ) != s )
        goto fail;
    if( ss_end( match ) != s + strlen( s ) )
        goto fail;
    
    ss_release( scanner );
    ss_release( match );
    ss_release( pat );
    return true;
    
fail:
    if( scanner )
        ss_release( scanner );
    if( match )
        ss_release( match );
    if( pat )
        ss_release( pat );
    return false;
}

static bool test1( void ) {
    ss_Context* ctx = ss_init();
    bool result = testMatch( ctx, ss_BYTES,
        "Literal text, ( 'not literal' ).",
        "Literal text, not literal."
    );
    ss_release( ctx );
    return result;
}

static bool test2( void ) {
    ss_Context* ctx = ss_init();
    
    char const* p = "I have an ( 'apple' | 'orange' | 'almond' ).";
    bool result = true;
    result &= testMatch( ctx, ss_BYTES, p, "I have an apple." );
    result &= testMatch( ctx, ss_BYTES, p, "I have an orange." );
    result &= testMatch( ctx, ss_BYTES, p, "I have an almond." );
    
    ss_release( ctx );
    return result;
}

static bool test3( void ) {
    ss_Context* ctx = ss_init();
    
    char const* p = "I have ( 'two ' 'apples' | 'three oranges' ).";
    bool result = true;
    result &= testMatch( ctx, ss_BYTES, p, "I have two apples." );
    result &= testMatch( ctx, ss_BYTES, p, "I have three oranges." );
    
    ss_release( ctx );
    return result;
}

static bool test4( void ) {
    ss_Context* ctx = ss_init();
    
    char const* p = "I eat [ 'blueberry ' ]pancakes.";
    bool result = true;
    result &= testMatch( ctx, ss_BYTES, p, "I eat pancakes." );
    result &= testMatch( ctx, ss_BYTES, p, "I eat blueberry pancakes." );
    
    ss_release( ctx );
    return result;
}

static bool test5( void ) {
    ss_Context* ctx = ss_init();
    
    char const* p = "I < 'love ' >food!";
    bool result = true;
    result &= testMatch( ctx, ss_BYTES, p, "I love food!" );
    result &= testMatch( ctx, ss_BYTES, p, "I love love food!" );
    result &= testMatch( ctx, ss_BYTES, p, "I love love love food!" );
    
    ss_release( ctx );
    return result;
}

static bool test6( void ) {
    ss_Context* ctx = ss_init();
    
    char const* p = "I sleep{ ' a' | ' lot' | ' very' | ' little' }.";
    bool result = true;
    result &= testMatch( ctx, ss_BYTES, p, "I sleep." );
    result &= testMatch( ctx, ss_BYTES, p, "I sleep a lot." );
    result &= testMatch( ctx, ss_BYTES, p, "I sleep a little." );
    result &= testMatch( ctx, ss_BYTES, p, "I sleep very little." );
    
    ss_release( ctx );
    return result;
}

static bool test7( void ) {
    ss_Context* ctx = ss_init();
    
    char const* p = "I drink~( ' wine' )[ ' water' | ' beer' ].";
    bool result = true;
    result &= testMatch( ctx, ss_BYTES, p, "I drink water." );
    result &= testMatch( ctx, ss_BYTES, p, "I drink beer." );
    result &= !testMatch( ctx, ss_BYTES, p, "I drink wine." );
    
    ss_release( ctx );
    return result;
}

static bool test8( void ) {
    ss_Context* ctx = ss_init();
    
    char const* p = "I eat ^( 't' )( 'tacos' | 'enchiladas' | 'fries' ).";
    bool result = true;
    result &= testMatch( ctx, ss_BYTES, p, "I eat tacos." );
    result &= !testMatch( ctx, ss_BYTES, p, "I eat enchiladas." );
    result &= !testMatch( ctx, ss_BYTES, p, "I eat fries." );
    
    ss_release( ctx );
    return result;
}

static bool test9( void ) {
    ss_Context* ctx = ss_init();
    
    char const* p = "I ate ( digit ) tacos.";
    bool result = true;
    result &= testMatch( ctx, ss_BYTES, p, "I ate 1 tacos." );
    result &= testMatch( ctx, ss_BYTES, p, "I ate 2 tacos." );
    result &= testMatch( ctx, ss_BYTES, p, "I ate 3 tacos." );
    result &= testMatch( ctx, ss_BYTES, p, "I ate 9 tacos." );
    result &= !testMatch( ctx, ss_BYTES, p, "I ate N tacos." );
    
    ss_release( ctx );
    return result;
}

static bool test10( void ) {
    ss_Context* ctx = ss_init();
    
    char const* p = "( 104 101 108 108 111 )";
    bool result = testMatch( ctx, ss_BYTES, p, "hello" );
    
    ss_release( ctx );
    return result;
}

static bool test11( void ) {
    ss_Context* ctx = ss_init();
    
    char const  splatSrc[] = "< ~'/' ~'.' char >";
    ss_Pattern* splatPat = ss_compile( ctx, ss_BYTES, splatSrc, strlen( splatSrc ) );
    ss_define( ctx, "splat", splatPat );
    ss_release( splatPat );
    
    char const  quarkSrc[] = "(char)";
    ss_Pattern* quarkPat = ss_compile( ctx, ss_BYTES, quarkSrc, strlen( quarkSrc ) );
    ss_define( ctx, "quark", quarkPat );
    ss_release( quarkPat );
    
    char const* p1 = "*/file.???";
    bool result = true;
    result &= testMatch( ctx, ss_BYTES, p1, "dir1/file.txt" );
    result &= testMatch( ctx, ss_BYTES, p1, "dir2/file.csv" );
    result &= testMatch( ctx, ss_BYTES, p1, "dir3/file.dat" );
    result &= !testMatch( ctx, ss_BYTES, p1, "dir1/dir2/file.txt" );
    
    char const* p2 = "*/*/*.txt";
    result &= testMatch( ctx, ss_BYTES, p2, "dir1/dir2/thing.txt" );
    
    ss_release( ctx );
    
    return result;
}

static bool test12( void ) {
    ss_Context* ctx = ss_init();
    
    char const* p = "I have two ( 'apples' | 'oranges' ):fruit.";
    char const* s = "I have two apples.";
    
    ss_Pattern* pat = ss_compile( ctx, ss_BYTES, p, strlen( p ) );
    if( ss_error( ctx ) )
        goto fail;
    
    ss_Scanner* scanner = ss_start( ctx, ss_BYTES, pat, s, strlen( s ) );
    if( ss_error( ctx ) )
        goto fail;
    
    ss_Match* match = ss_match( scanner );
    if( !match )
        goto fail;
    
    ss_Match* fruit = ss_get( match, "fruit" );
    if( !fruit )
        goto fail;
    if( ss_loc( fruit ) != s + 11 )
        goto fail;
    if( ss_end( fruit ) != s + 17 )
        goto fail;
    
    ss_release( fruit );
    ss_release( scanner );
    ss_release( match );
    ss_release( pat );
    ss_release( ctx );
    return true;

fail:
    if( fruit )
        ss_release( fruit );
    if( scanner )
        ss_release( scanner );
    if( match )
        ss_release( match );
    if( pat )
        ss_release( pat );
    ss_release( ctx );
    return false;
}

static bool test13( void ) {
    ss_Context* ctx = ss_init();
    
    char const* p = "( 'apple' | 'orange' | 'pear' )";
    char const* s = "I ate an apple.";
    
    ss_Pattern* pat = ss_compile( ctx, ss_BYTES, p, strlen( p ) );
    if( ss_error( ctx ) )
        goto fail;
    
    ss_Scanner* scanner = ss_start( ctx, ss_BYTES, pat, s, strlen( s ) );
    if( ss_error( ctx ) )
        goto fail;
    
    ss_Match* match = ss_find( scanner );
    if( !match )
        goto fail;
    if( ss_loc( match ) != s + 9 )
        goto fail;
    if( ss_end( match ) != s + 14 )
        goto fail;
    
    ss_release( scanner );
    ss_release( match );
    ss_release( pat );
    ss_release( ctx );
    return true;

fail:
    if( scanner )
        ss_release( scanner );
    if( match )
        ss_release( match );
    if( pat )
        ss_release( pat );
    ss_release( ctx );
    return false;
}

static bool test14( void ) {
    ss_Context* ctx = ss_init();
    
    char const* p = "( 20170 26085 12399 )";
    bool result = testMatch( ctx, ss_CHARS, p, "今日は" );
    
    ss_release( ctx );
    return result;
}

static bool test15( void ) {
    ss_Context* ctx = ss_init();
    
    char const* p = "This is ( '(' )not( ')' ) very interesting.";
    bool result = testMatch( ctx, ss_CHARS, p, "This is (not) very interesting." );
    
    ss_release( ctx );
    return result;
}

int main( void ) {
    bool passing = true;
    
    passing &= test1();
    passing &= test2();
    passing &= test3();
    passing &= test4();
    passing &= test5();
    passing &= test6();
    passing &= test7();
    passing &= test8();
    passing &= test9();
    passing &= test10();
    passing &= test11();
    passing &= test12();
    passing &= test13();
    passing &= test14();
    passing &= test15();
    
    if( passing ) {
        printf( "PASSED\n" );
        return 0;
    }
    else {
        printf( "FAILED\n" );
        return 1;
    }
}