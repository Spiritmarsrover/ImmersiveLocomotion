#pragma once
// Minimal OSC-over-UDP sender for driving VRChat avatar parameters.
// VRChat listens on 127.0.0.1:9000 and maps /avatar/parameters/<Name> to the
// avatar parameter <Name>. Winsock lives entirely in the .cpp so this header
// stays clear of windows.h ordering pitfalls.

#include <cstdint>
#include <string>

class OscSender
{
public:
    ~OscSender();

    // (Re)point at ip:port, creating the socket on first use. Cheap to call
    // repeatedly; only rebuilds when the destination actually changes.
    bool setDest( const std::string& ip, int port );

    void sendBool( const char* address, bool v );
    void sendFloat( const char* address, float v );

private:
    bool ensureSocket();
    void sendBuf( const uint8_t* data, int len );

    uintptr_t m_sock = static_cast<uintptr_t>( -1 );
    uint32_t m_addr = 0;  // network byte order
    uint16_t m_port = 0;  // network byte order
    bool m_wsaUp = false;
};
