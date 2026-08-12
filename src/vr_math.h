#pragma once
// Minimal vector/matrix helpers for OpenVR types. Raw tracking space is the
// working frame everywhere in this app: +Y up, velocities in m/s.

#include <openvr.h>
#include <cmath>

struct Vec3
{
    double x = 0.0, y = 0.0, z = 0.0;

    Vec3 operator+( const Vec3& o ) const { return { x + o.x, y + o.y, z + o.z }; }
    Vec3 operator-( const Vec3& o ) const { return { x - o.x, y - o.y, z - o.z }; }
    Vec3 operator*( double s ) const { return { x * s, y * s, z * s }; }
    Vec3& operator+=( const Vec3& o )
    {
        x += o.x;
        y += o.y;
        z += o.z;
        return *this;
    }

    double length() const { return std::sqrt( x * x + y * y + z * z ); }

    Vec3 horizontal() const { return { x, 0.0, z }; }

    Vec3 normalizedOrZero() const
    {
        double l = length();
        if ( l < 1e-9 )
            return {};
        return { x / l, y / l, z / l };
    }

    static double dot( const Vec3& a, const Vec3& b )
    {
        return a.x * b.x + a.y * b.y + a.z * b.z;
    }
};

inline Vec3 positionOf( const vr::HmdMatrix34_t& m )
{
    return { m.m[0][3], m.m[1][3], m.m[2][3] };
}

// Third basis column = device local +Z in world. Device "forward" is -Z.
inline Vec3 forwardOf( const vr::HmdMatrix34_t& m )
{
    return { -m.m[0][2], -m.m[1][2], -m.m[2][2] };
}

inline Vec3 velocityOf( const vr::TrackedDevicePose_t& pose )
{
    return { pose.vVelocity.v[0], pose.vVelocity.v[1], pose.vVelocity.v[2] };
}

// Rotate a vector by the rotation part of a 3x4 pose matrix (no translation).
inline Vec3 rotate( const vr::HmdMatrix34_t& m, const Vec3& v )
{
    return { m.m[0][0] * v.x + m.m[0][1] * v.y + m.m[0][2] * v.z,
             m.m[1][0] * v.x + m.m[1][1] * v.y + m.m[1][2] * v.z,
             m.m[2][0] * v.x + m.m[2][1] * v.y + m.m[2][2] * v.z };
}

// Rotate by the transpose (= inverse for pure rotations).
inline Vec3 rotateInverse( const vr::HmdMatrix34_t& m, const Vec3& v )
{
    return { m.m[0][0] * v.x + m.m[1][0] * v.y + m.m[2][0] * v.z,
             m.m[0][1] * v.x + m.m[1][1] * v.y + m.m[2][1] * v.z,
             m.m[0][2] * v.x + m.m[1][2] * v.y + m.m[2][2] * v.z };
}

template <typename T> T clamp( T v, T lo, T hi )
{
    return v < lo ? lo : ( v > hi ? hi : v );
}

// Full 3x4 point transform: M * [p, 1]
inline Vec3 transformPoint( const vr::HmdMatrix34_t& m, const Vec3& p )
{
    Vec3 r = rotate( m, p );
    return { r.x + m.m[0][3], r.y + m.m[1][3], r.z + m.m[2][3] };
}

// Inverse of a rigid 3x4 transform (rotation transpose, back-rotated offset).
inline vr::HmdMatrix34_t invertRigid( const vr::HmdMatrix34_t& m )
{
    vr::HmdMatrix34_t r = {};
    for ( int i = 0; i < 3; ++i )
        for ( int j = 0; j < 3; ++j )
            r.m[i][j] = m.m[j][i];
    Vec3 t = { m.m[0][3], m.m[1][3], m.m[2][3] };
    Vec3 it = rotateInverse( m, t );
    r.m[0][3] = static_cast<float>( -it.x );
    r.m[1][3] = static_cast<float>( -it.y );
    r.m[2][3] = static_cast<float>( -it.z );
    return r;
}

// a * b for 3x4 rigid transforms (b applied first).
inline vr::HmdMatrix34_t matMul34( const vr::HmdMatrix34_t& a,
                                   const vr::HmdMatrix34_t& b )
{
    vr::HmdMatrix34_t r = {};
    for ( int i = 0; i < 3; ++i )
    {
        for ( int j = 0; j < 3; ++j )
        {
            r.m[i][j] = a.m[i][0] * b.m[0][j] + a.m[i][1] * b.m[1][j]
                        + a.m[i][2] * b.m[2][j];
        }
        r.m[i][3] = a.m[i][0] * b.m[0][3] + a.m[i][1] * b.m[1][3]
                    + a.m[i][2] * b.m[2][3] + a.m[i][3];
    }
    return r;
}
