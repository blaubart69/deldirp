///////////////////////////////////////////////////////////////////////////////////////////////////
//
//               D E L D I R P
//
//
//  FILE:        deldirp.c
//
//  PURPOSE:     delete a directory tree in parallel
//
//
//               Copyright (c) 2018 UBIS-Austria, Vienna (AT)
//
///////////////////////////////////////////////////////////////////////////////////////////////////
 
#define  UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN  // Exclude rarely-used
#define STRICT
#include <windows.h>
#include "common.h"
#include "delpmsg.h"
 
// defines
 
#define NO___OUTPUT_DEBUG_STRING
#define NUMBER_OF_CONCURRENT 0
 
#define CACHE_LINE  64
#define CACHE_ALIGN __declspec(align(CACHE_LINE))
 
#define ERROR_FAILED_REPORT_LIMIT 10
#define MAX_U32 0xFFFFFFFFUL
 
// .INC files
 
#include <cmdline.c>
#include <message.c>
 
// global data
 
CACHE_ALIGN HANDLE        _hCompletionPort;
CACHE_ALIGN HANDLE        _hExit;
CACHE_ALIGN LONG volatile _QueuedCount;
CACHE_ALIGN LONG volatile _DirEnuCount;
CACHE_ALIGN LONG volatile _FailedCount;
CACHE_ALIGN LONG volatile _DeleteCount;
 
// new types
 
typedef struct _ELEMENT {
    LONG volatile lNumberOfChildren;
    struct _ELEMENT *pParent;
    DWORD dwFileAttributes;
    DWORD dwFileNameLength;
    WCHAR wcFileName[ 1 ]; // must be the last
} ELEMENT;
 
// function prototypes
 
DWORD WINAPI PoolThread( LPVOID lpParam );
PRIVATE VOID ProcessDirectory( ELEMENT *pElement );
PRIVATE ELEMENT* AllocElementAndInit( ELEMENT *pParent, DWORD dwFileAttr, LPWSTR cFileName );
PRIVATE ELEMENT* FreeElementAndGetParent( ELEMENT* pElement );
PRIVATE BOOL IsDotDir( WIN32_FIND_DATA *FindBuffer );
PRIVATE DWORD GetEnvOpt( LPWSTR Opt, DWORD Def );
PRIVATE BOOL GetU32( LPWSTR s, ULONG* pResult );
 
// logging
 
#ifdef OUTPUT_DEBUG_STRING
#define LOG(n) Log n
PRIVATE VOID Log( char *msg, ... );
#else
#define LOG(n)
#endif
 
//=================================================================================================
VOID rawmain( VOID ) {
//=================================================================================================
 
    LPWSTR  *szArglist = NULL;
    INT     nArgs;
    DWORD   rc = 999, dwAttr, i, nThreads;
    ELEMENT *pRoot;
    HANDLE  *hThreads;
 
    _hCompletionPort = NULL;
    _hExit = NULL;
 
    SetErrorMode( SEM_NOOPENFILEERRORBOX );
 
    if (( szArglist = CommandLineToArgv2( GetCommandLine(), &nArgs )) == NULL ) {
        Message( MSGID_ERROR_WINAPI_S, TEXT( __FILE__ ), __LINE__
               , L"CommandLineToArgv2", L"", GetLastError());
        rc = 998;
    } else if ( nArgs != 2 ) {
        Message( MSGID_USAGE, TEXT( FILE_VERSION_DELDIRP ), TEXT( __DATE__ ), TEXT( COPYRIGHTTEXT ));
    } else if (( dwAttr = GetFileAttributes( szArglist[ 1 ] )) == INVALID_FILE_ATTRIBUTES ) {
        Message( MSGID_ERROR_WINAPI_S, TEXT( __FILE__ ), __LINE__
               , L"GetFileAttributes", szArglist[ 1 ], GetLastError());
        rc = 1;
    } else if (( _hExit = CreateEvent( NULL, TRUE, FALSE, NULL )) == NULL ) {
        Message( MSGID_ERROR_WINAPI_S, TEXT( __FILE__ ), __LINE__
               , L"CreateEvent", L"Manual", GetLastError());
        rc = 2;
    } else if (( _hCompletionPort = CreateIoCompletionPort( INVALID_HANDLE_VALUE
                                                          , NULL
                                                          , 0
                                                          , NUMBER_OF_CONCURRENT )) == NULL ) {
        Message( MSGID_ERROR_WINAPI_S, TEXT( __FILE__ ), __LINE__
               , L"CreateIoCompletionPort", L"NULL", GetLastError());
        rc = 3;
    } else if (( pRoot = AllocElementAndInit( NULL
                                            , dwAttr
                                            , szArglist[ 1 ] )) == NULL ) {
        rc = 4;
    } else {
 
        nThreads = GetEnvOpt( L"DELDIRP_OPT_THREADS", 64 );
 
        hThreads = _alloca( nThreads * sizeof( hThreads[ 0 ] ));
 
        // --- let's create a number of working threads ---
 
        for ( i = 0; i < nThreads; i++ ) {
            DWORD dwThreadId;
            if (( hThreads[ i ] = CreateThread( NULL
                                              , 0
                                              , PoolThread
                                              , NULL
                                              , 0
                                              , &dwThreadId )) == NULL ) {
                Message( MSGID_ERROR_WINAPI_U, TEXT( __FILE__ ), __LINE__
                       , L"CreateThread", i, GetLastError());
            } /* endif */
        } /* endfor */
 
        // --- queue the root entry ---
 
        _QueuedCount = 1;
        _DirEnuCount = 0;
        _FailedCount = 0;
        _DeleteCount = 0;
 
        if ( ! PostQueuedCompletionStatus( _hCompletionPort, 0, (ULONG_PTR)pRoot, NULL )) {
            Message( MSGID_ERROR_WINAPI_S, TEXT( __FILE__ ), __LINE__
                   , L"PostQueuedCompletionStatus", L"", GetLastError());
            _FailedCount = 1;
        } else {
 
            DWORD dwMilliseconds = GetEnvOpt( L"DELDIRP_OPT_REPORT_PERIOD", 1000 );
 
            // --- wait and report performance ---
 
            while ( WaitForSingleObject( _hExit, dwMilliseconds ) == WAIT_TIMEOUT ) {
                Message( MSGID_STATUS, _DeleteCount, _QueuedCount, _DirEnuCount, _FailedCount );
            } /* endwhile */
 
        } /* endif */
 
        // --- send exit command to all worker threads ---
 
        for ( i = 0; i < nThreads; i++ ) {
            _InterlockedIncrement( &_QueuedCount );
            PostQueuedCompletionStatus( _hCompletionPort, 0, 0, NULL );
        } /* endfor */
 
        // --- wait for all worker threads (but not more than a half second) ---
 
        for ( i = 0; i < nThreads; i++ ) {
            if ( hThreads[ i ] != NULL ) {
                WaitForSingleObject( hThreads[ i ], 500L );
                CloseHandle( hThreads[ i ] );
            } /* endif */
        } /* endfor */
 
        Message( MSGID_STATUS, _DeleteCount, _QueuedCount, _DirEnuCount, _FailedCount );
 
        rc = ( _FailedCount > 0 ) ? 5 : 0;
 
    } /* endif */
 
    // --- cleanup ---
 
    if ( _hCompletionPort != NULL ) CloseHandle( _hCompletionPort );
    if ( _hExit           != NULL ) CloseHandle( _hExit );
    if ( szArglist != NULL ) HeapFree( GetProcessHeap(), 0, szArglist );
 
    // --- bye bye ---
 
    ExitProcess( rc );
}
 
//=================================================================================================
DWORD WINAPI PoolThread( LPVOID lpParam ) {
//=================================================================================================
 
    BOOL bSuccess;
    DWORD dwSize;
    OVERLAPPED *pOverlapped;
    ELEMENT *pElement;
 
    UNREFERENCED_PARAMETER( lpParam );
 
    for (;;) {
 
        // --- wait for a job ---
 
        bSuccess = GetQueuedCompletionStatus( _hCompletionPort
                                            , &dwSize
                                            , (PULONG_PTR)&pElement
                                            , &pOverlapped
                                            , INFINITE );
 
        _InterlockedDecrement( &_QueuedCount );
 
        // --- check for termination ---
 
        if ( pElement == NULL ) {
            break;
        } /* endif */
 
        // --- check for read-only ---
 
        if ( pElement->dwFileAttributes & FILE_ATTRIBUTE_READONLY ) {
            SetFileAttributes( pElement->wcFileName
                             , pElement->dwFileAttributes & ~FILE_ATTRIBUTE_READONLY );
        } /* endif */
 
        // --- check if file or directory ---
 
        if ( pElement->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY ) {
 
            // --- DIRECTORY ---
 
            LOG(( "ENU:%.100lS\n", pElement->wcFileName ));
 
            _InterlockedIncrement( &_DirEnuCount );
 
            ProcessDirectory( pElement );
 
            _InterlockedDecrement( &_DirEnuCount );
 
        } else {
 
            // --- FILE ---
 
            if ( DeleteFile( pElement->wcFileName )) {
                LOG(( "FIL:%.100lS\n", pElement->wcFileName ));
                _InterlockedIncrement( &_DeleteCount );
            } else if ( _InterlockedIncrement( &_FailedCount ) < ERROR_FAILED_REPORT_LIMIT ) {
                Message( MSGID_ERROR_WINAPI_S, TEXT( __FILE__ ), __LINE__
                       , L"DeleteFile", pElement->wcFileName, GetLastError());
            } /* endif */
 
            pElement = FreeElementAndGetParent( pElement );
 
        } /* endif */
 
        // --- decrement children counter ---
 
        for (;;) {
 
            if ( pElement == NULL ) {
                SetEvent( _hExit );
                break;           
            } else if ( _InterlockedDecrement( &(( pElement )->lNumberOfChildren )) > 0 ) { // init with 1!
                break; // remove will be done by one of the children
            } else if ( RemoveDirectory( pElement->wcFileName )) {
                LOG(( "DIR:%.100lS\n", pElement->wcFileName ));
                _InterlockedIncrement( &_DeleteCount );
            } else if ( _InterlockedIncrement( &_FailedCount ) < ERROR_FAILED_REPORT_LIMIT ) {
                Message( MSGID_ERROR_WINAPI_S, TEXT( __FILE__ ), __LINE__
                       , L"RemoveDirectory", pElement->wcFileName, GetLastError());
            } /* endif */
 
            pElement = FreeElementAndGetParent( pElement );
 
        } /* endfor */
 
    } /* endfor */
 
    return 0;
}
 
//-------------------------------------------------------------------------------------------------
PRIVATE VOID ProcessDirectory( ELEMENT *pElement ) {
//-------------------------------------------------------------------------------------------------
 
    HANDLE hSearch;
    DWORD  dwError = NO_ERROR;
    WIN32_FIND_DATA FindBuffer;
    ELEMENT *pChild;
 
    // --- append \* to name ---
 
    lstrcpy( pElement->wcFileName + pElement->dwFileNameLength, L"\\*" );
 
    // --- looking for entries ---
 
    if (( hSearch = FindFirstFile( pElement->wcFileName
                                 , &FindBuffer )) == INVALID_HANDLE_VALUE ) {
        dwError = GetLastError();
    } /* endif */
 
    while ( dwError != ERROR_NO_MORE_FILES ) {
 
        if ( dwError != NO_ERROR ) {
 
            Message( MSGID_ERROR_WINAPI_S, TEXT( __FILE__ ), __LINE__
                   , L"FindFirst/NextFile", pElement->wcFileName, dwError );
            break;
 
        } else if ( IsDotDir( &FindBuffer )) {
 
            ; // nothing to do
 
        } else if (( pChild = AllocElementAndInit( pElement
                                                 , FindBuffer.dwFileAttributes
                                                 , FindBuffer.cFileName )) == NULL ) {
            break;
 
        } else {
 
            _InterlockedIncrement( &pElement->lNumberOfChildren );
            _InterlockedIncrement( &_QueuedCount );
            PostQueuedCompletionStatus( _hCompletionPort, 0, (ULONG_PTR)pChild, NULL );
 
        } /* endif */
 
        // --- search for next entry in this directory --------------------------------------------
 
        if ( ! FindNextFile( hSearch
                           , &FindBuffer )) {
            dwError = GetLastError();
        } /* endif */
 
    } /* endwhile */
 
    // --- local cleanup -------------------------------------------------------------------------------------
 
    if ( hSearch != INVALID_HANDLE_VALUE ) FindClose( hSearch );
 
    // --- remove \* from directory name ---
 
    pElement->wcFileName[ pElement->dwFileNameLength ] = L'\0';
}
 
//-------------------------------------------------------------------------------------------------
PRIVATE ELEMENT* AllocElementAndInit( ELEMENT *pParent, DWORD dwFileAttr, LPWSTR cFileName ) {
//-------------------------------------------------------------------------------------------------
 
    ELEMENT *pElement;
    DWORD m, n;
 
    m = lstrlen( cFileName ) + (( pParent == NULL ) ? 0 : ( pParent->dwFileNameLength + 1 )); // + "\\"
    n = sizeof( *pElement ) + ( m + 2 ) * sizeof( WCHAR );                                    // + "\\*"
 
    if (( pElement = HeapAlloc( GetProcessHeap()
                              , 0
                              , n )) == NULL ) {
        Message( MSGID_ERROR_WINAPI_U, TEXT( __FILE__ ), __LINE__
               , L"HeapAlloc", n, ERROR_NOT_ENOUGH_MEMORY );
        SetEvent( _hExit ); // try to avoid more serious problems ...
    } else {
 
        pElement->lNumberOfChildren = 1;        // init with 1!
        pElement->pParent           = pParent;
        pElement->dwFileAttributes  = dwFileAttr;
        pElement->dwFileNameLength  = m;
 
        if ( pParent == NULL ) {
            lstrcpy( pElement->wcFileName, cFileName );
        } else {
            WCHAR *p = pElement->wcFileName;
            lstrcpy( p, pParent->wcFileName ); // lstrcpy( pElement->wcFileName, pParent->wcFileName );
            p += pParent->dwFileNameLength;
            *p++ = L'\\';                      // lstrcat( pElement->wcFileName, "\\" );
            lstrcpy( p, cFileName );           // lstrcat( pElement->wcFileName, cFileName );
        } /* endif */
 
    } /* endif */
 
    return pElement;
}
 
//-------------------------------------------------------------------------------------------------
PRIVATE ELEMENT* FreeElementAndGetParent( ELEMENT *pElement ) {
//-------------------------------------------------------------------------------------------------
 
    ELEMENT *pParent = pElement->pParent;
    HeapFree( GetProcessHeap(), 0, pElement );
    return pParent;
}
 
#if 1
//-------------------------------------------------------------------------------------------------
PRIVATE BOOL IsDotDir( WIN32_FIND_DATA *FindBuffer ) {
//-------------------------------------------------------------------------------------------------
 
    if (( FindBuffer->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY ) == 0 ) return FALSE;
    if ( FindBuffer->cFileName[ 0 ] != L'.' ) return FALSE;
    if ( FindBuffer->cFileName[ 1 ] == L'\0' ) return TRUE;
    if ( FindBuffer->cFileName[ 1 ] != L'.' ) return FALSE;
    if ( FindBuffer->cFileName[ 2 ] == L'\0' ) return TRUE;
 
    return FALSE;
}
#else
//-------------------------------------------------------------------------------------------------
PRIVATE BOOL IsDotDir( WIN32_FIND_DATA *FindBuffer ) {
//-------------------------------------------------------------------------------------------------
 
    //   4   3   2   1   0   BIT
    // +---+---+---+---+---+
    // |DIR|[0]|[1]|[1]|[2]| DATA
    // +---+---+---+---+---+
    // | 1 | 1 | 1 | 1 | X | 0x1E 0x1F ... Not possible (cFileName[ 1 ] can not be '\0' and '.')
    // | 1 | 1 | 1 | 0 | X | 0x1C 0x1D ... Directory "."
    // | 1 | 1 | 0 | 1 | 1 | 0x1B      ... Directory ".."
    // +---+---+---+---+---+
    // | 1 | 1 | 0 | 1 | 0 | 0x1A      ... Directory "..?"
    // | 1 | 1 | 0 | 0 | X | 0x18 0x19 ... Directory ".?"
    // | 1 | 0 | X | X | X | 0x10-0x1F ... Directory with first char not '.'
    // | 0 | X | X | X | X | 0x00-0x0F ... Not a Directory
    // +---+---+---+---+---+
 
    INT i;
 
    i  = (( FindBuffer->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY ) ? 0x10 : 0 )
       + (( FindBuffer->cFileName[ 0 ] == L'.' )                      ? 0x08 : 0 )
       + (( FindBuffer->cFileName[ 1 ] == L'\0' )                     ? 0x04 : 0 )
       + (( FindBuffer->cFileName[ 1 ] == L'.' )                      ? 0x02 : 0 )
       + (( FindBuffer->cFileName[ 2 ] == L'\0' )                     ? 0x01 : 0 );
 
    return ( i >= 0x1B ) ? TRUE : FALSE;
}
#endif
//-------------------------------------------------------------------------------------------------
PRIVATE DWORD GetEnvOpt( LPWSTR Opt, DWORD Def ) {
//-------------------------------------------------------------------------------------------------
 
    DWORD n;
    WCHAR pBuffer[ 16 ];
 
    if (( n = GetEnvironmentVariable( Opt, pBuffer, ELEMENTS( pBuffer ))) == 0 ) {
        n = Def;
    } else if ( n >= ELEMENTS( pBuffer )) {
        n = Def;
    } else if ( ! GetU32( pBuffer, &n )) {
        n = Def;
    } /* endif */
 
    return n;
}
 
//-------------------------------------------------------------------------------------------------
PRIVATE BOOL GetU32( LPWSTR s, ULONG* pResult ) {
//-------------------------------------------------------------------------------------------------
 
    WCHAR c;
    ULONG rc = 0, Base;
 
    // this is a "poor man" strtoul.
 
    c = *s++;
 
    if ( c == L'\0' ) return FALSE;
 
    if ( c == L'0' && ( *s == L'x' || *s == L'X' )) {
        c = s[ 1 ];
        s += 2;
        Base = 16;
    } else {
        Base = ( c == L'0' ) ? 8 : 10;
    } /* endif */
 
    while ( c != L'\0' ) {
        if ( rc > ( MAX_U32 / Base )) return FALSE;
        else if ( c >= L'0' && c <= L'7' ) rc = rc * Base + ( c - L'0' );
        else if ( Base ==  8 ) return FALSE;
        else if ( c >= L'8' && c <= L'9' ) rc = rc * Base + ( c - L'0' );
        else if ( Base == 10 ) return FALSE;
        else if ( c >= L'A' && c <= L'F' ) rc = rc * Base + ( c - L'A' ) + 10;
        else if ( c >= L'a' && c <= L'f' ) rc = rc * Base + ( c - L'a' ) + 10;
        else return FALSE;
        c = *s++;
    } /* endfor */
 
    *pResult = rc;
 
    return TRUE;
}
 
//-------------------------------------------------------------------------------------------------
PRIVATE VOID Log( char *msg, ... ) {
//-------------------------------------------------------------------------------------------------
 
    CHAR buf[ 128 ];
    va_list vaList ;
 
    va_start( vaList, msg );
    wvsprintfA( buf, msg, vaList );
    va_end( vaList );
 
    OutputDebugStringA( buf );
}
 
// -=EOF=-