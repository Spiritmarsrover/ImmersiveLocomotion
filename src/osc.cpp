#include "osc.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <cstring>

namespace
{
// Append an OSC string: bytes + NUL, padded with NULs to a 4-byte boundary.
void putString( uint8_t*& p, const char* s )
{
    int len = static_cast<int>( std::strlen( s ) );
    std::memcpy( p, s, len );
    p += len;
    int pad = 4 - ( len % 4 ); // always at least one NUL
    std::memset( p, 0, pad );
    p += pad;
}

void putFloatBE( uint8_t*& p, float v )
{
    uint32_t bits;
    std::memcpy( &bits, &v, 4 );
    p[0] = static_cast<uint8_t>( bits >> 24 );
    p[1] = static_cast<uint8_t>( bits >> 16 );
    p[2] = static_cast<uint8_t>( bits >> 8 );
    p[3] = static_cast<uint8_t>( bits );
    p += 4;
}

float getFloatBE( const uint8_t* p )
{
    uint32_t bits = ( static_cast<uint32_t>( p[0] ) << 24 )
                    | ( static_cast<uint32_t>( p[1] ) << 16 )
                    | ( static_cast<uint32_t>( p[2] ) << 8 )
                    | static_cast<uint32_t>( p[3] );
    float v;
    std::memcpy( &v, &bits, 4 );
    return v;
}

// Advance past an OSC string (NUL-terminated, padded to 4). Returns nullptr on
// overrun.
const uint8_t* skipString( const uint8_t* p, const uint8_t* end )
{
    const uint8_t* s = p;
    while ( s < end && *s != 0 )
        ++s;
    if ( s >= end )
        return nullptr;
    size_t len = static_cast<size_t>( s - p );
    size_t padded = ( len / 4 + 1 ) * 4; // bytes incl. the NUL, up to 4-boundary
    return p + padded;
}
} // namespace

OscSender::~OscSender()
{
    if ( m_sock != static_cast<uintptr_t>( -1 ) )
        closesocket( static_cast<SOCKET>( m_sock ) );
    if ( m_wsaUp )
        WSACleanup();
}

bool OscSender::ensureSocket()
{
    if ( m_sock != static_cast<uintptr_t>( -1 ) )
        return true;
    if ( !m_wsaUp )
    {
        WSADATA wsa;
        if ( WSAStartup( MAKEWORD( 2, 2 ), &wsa ) != 0 )
            return false;
        m_wsaUp = true;
    }
    SOCKET s = socket( AF_INET, SOCK_DGRAM, IPPROTO_UDP );
    if ( s == INVALID_SOCKET )
        return false;
    m_sock = static_cast<uintptr_t>( s );
    return true;
}

bool OscSender::setDest( const std::string& ip, int port )
{
    in_addr a = {};
    if ( inet_pton( AF_INET, ip.c_str(), &a ) != 1 )
        return false;
    m_addr = a.s_addr;
    m_port = htons( static_cast<uint16_t>( port ) );
    return ensureSocket();
}

void OscSender::sendBuf( const uint8_t* data, int len )
{
    if ( m_sock == static_cast<uintptr_t>( -1 ) )
        return;
    sockaddr_in dst = {};
    dst.sin_family = AF_INET;
    dst.sin_addr.s_addr = m_addr;
    dst.sin_port = m_port;
    sendto( static_cast<SOCKET>( m_sock ),
            reinterpret_cast<const char*>( data ), len, 0,
            reinterpret_cast<sockaddr*>( &dst ), sizeof( dst ) );
}

void OscSender::sendBool( const char* address, bool v )
{
    uint8_t buf[512];
    uint8_t* p = buf;
    putString( p, address );
    putString( p, v ? ",T" : ",F" ); // OSC bool: no argument data
    sendBuf( buf, static_cast<int>( p - buf ) );
}

void OscSender::sendFloat( const char* address, float v )
{
    uint8_t buf[512];
    uint8_t* p = buf;
    putString( p, address );
    putString( p, ",f" );
    putFloatBE( p, v );
    sendBuf( buf, static_cast<int>( p - buf ) );
}

void OscSender::sendFloat4( const char* address, float a, float b, float c,
                            float d )
{
    uint8_t buf[512];
    uint8_t* p = buf;
    putString( p, address );
    putString( p, ",ffff" );
    putFloatBE( p, a );
    putFloatBE( p, b );
    putFloatBE( p, c );
    putFloatBE( p, d );
    sendBuf( buf, static_cast<int>( p - buf ) );
}

void OscSender::sendEmpty( const char* address )
{
    uint8_t buf[512];
    uint8_t* p = buf;
    putString( p, address );
    putString( p, "," );
    sendBuf( buf, static_cast<int>( p - buf ) );
}

// ---- OscReceiver ----

OscReceiver::~OscReceiver()
{
    if ( m_sock != static_cast<uintptr_t>( -1 ) )
        closesocket( static_cast<SOCKET>( m_sock ) );
    if ( m_wsaUp )
        WSACleanup();
}

bool OscReceiver::ensureWsa()
{
    if ( m_wsaUp )
        return true;
    WSADATA wsa;
    if ( WSAStartup( MAKEWORD( 2, 2 ), &wsa ) != 0 )
        return false;
    m_wsaUp = true;
    return true;
}

bool OscReceiver::bind( int port )
{
    if ( m_sock != static_cast<uintptr_t>( -1 ) && port == m_port )
        return true;
    if ( m_sock != static_cast<uintptr_t>( -1 ) )
    {
        closesocket( static_cast<SOCKET>( m_sock ) );
        m_sock = static_cast<uintptr_t>( -1 );
    }
    if ( !ensureWsa() )
        return false;
    SOCKET s = socket( AF_INET, SOCK_DGRAM, IPPROTO_UDP );
    if ( s == INVALID_SOCKET )
        return false;
    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons( static_cast<uint16_t>( port ) );
    inet_pton( AF_INET, "127.0.0.1", &addr.sin_addr );
    if ( ::bind( s, reinterpret_cast<sockaddr*>( &addr ), sizeof( addr ) )
         == SOCKET_ERROR )
    {
        closesocket( s );
        return false;
    }
    u_long nonblock = 1;
    ioctlsocket( s, FIONBIO, &nonblock );
    m_sock = static_cast<uintptr_t>( s );
    m_port = port;
    return true;
}

bool OscReceiver::getParam( const std::string& name, float& out ) const
{
    auto it = m_params.find( name );
    if ( it == m_params.end() )
        return false;
    out = it->second;
    return true;
}

void OscReceiver::poll()
{
    if ( m_sock == static_cast<uintptr_t>( -1 ) )
        return;
    static const char kPrefix[] = "/avatar/parameters/";
    const size_t prefixLen = sizeof( kPrefix ) - 1;
    uint8_t buf[1024];
    for ( ;; )
    {
        // recvfrom (not recv): the socket is bound but not connect()ed, and on
        // Winsock recv() on an unconnected UDP socket returns nothing.
        sockaddr_in from = {};
        int fromLen = sizeof( from );
        int n = recvfrom( static_cast<SOCKET>( m_sock ),
                          reinterpret_cast<char*>( buf ), sizeof( buf ), 0,
                          reinterpret_cast<sockaddr*>( &from ), &fromLen );
        if ( n <= 0 )
            break; // WSAEWOULDBLOCK when drained
        const uint8_t* end = buf + n;
        const uint8_t* p = skipString( buf, end ); // past the address
        if ( !p )
            continue;
        const uint8_t* tags = p;
        p = skipString( p, end ); // past the type-tag string
        if ( !p )
            continue;
        const char* a = reinterpret_cast<const char*>( buf );
        const char* t = reinterpret_cast<const char*>( tags );

        // keep only /avatar/parameters/<name> with a single float or bool arg
        if ( std::strncmp( a, kPrefix, prefixLen ) != 0 )
            continue;
        float v;
        if ( std::strcmp( t, ",f" ) == 0 )
        {
            if ( p + 4 > end )
                continue;
            v = getFloatBE( p );
        }
        else if ( std::strcmp( t, ",T" ) == 0 ) // OSC bool: value in the tag
            v = 1.0f;
        else if ( std::strcmp( t, ",F" ) == 0 )
            v = 0.0f;
        else
            continue;
        m_params[std::string( a + prefixLen )] = v;
    }
}
