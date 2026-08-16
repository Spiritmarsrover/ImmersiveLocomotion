#pragma once
// Minimal OSC-over-UDP sender for driving VRChat avatar parameters.
// VRChat listens on 127.0.0.1:9000 and maps /avatar/parameters/<Name> to the
// avatar parameter <Name>. Winsock lives entirely in the .cpp so this header
// stays clear of windows.h ordering pitfalls.

#include <cstdint>
#include <string>
#include <unordered_map>

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

// Minimal OSC-over-UDP receiver. Listens on 127.0.0.1:port (VRChat OSC out =
// 9001) and keeps the latest value of every /avatar/parameters/<name> it sees
// (floats and bools), queryable by name.
class OscReceiver
{
public:
    ~OscReceiver();

    // (Re)bind to 127.0.0.1:port. Cheap to call repeatedly; only rebinds when
    // the port changes. Returns false if the socket could not be opened/bound.
    bool bind( int port );
    // Drain all pending datagrams into the parameter map. Non-blocking.
    void poll();

    // Latest value of /avatar/parameters/<name> (bools stored as 0/1). Returns
    // false if that parameter has never been received.
    bool getParam( const std::string& name, float& out ) const;

    // True once the listen socket is open/bound (false = port in use, etc.).
    bool bound() const { return m_sock != static_cast<uintptr_t>( -1 ); }

private:
    bool ensureWsa();

    uintptr_t m_sock = static_cast<uintptr_t>( -1 );
    int m_port = 0;
    bool m_wsaUp = false;
    std::unordered_map<std::string, float> m_params;
};
