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
