// Lightweight 2D math for RocketSim. Positions use double precision because
// orbital distances span millions of metres; rendering casts to float late.
#pragma once
#include <cmath>
#include <algorithm>

constexpr double PI = 3.14159265358979323846;
constexpr double TAU = 2.0 * PI;
constexpr double DEG2RAD = PI / 180.0;
constexpr double RAD2DEG = 180.0 / PI;

inline double clampd(double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline float  clampf(float v, float lo, float hi)   { return v < lo ? lo : (v > hi ? hi : v); }
inline double lerpd(double a, double b, double t) { return a + (b - a) * t; }
inline double sign(double v) { return v < 0 ? -1.0 : (v > 0 ? 1.0 : 0.0); }
inline double wrapAngle(double a) { // -> (-PI, PI]
    a = std::fmod(a + PI, TAU);
    if (a < 0) a += TAU;
    return a - PI;
}
inline double approach(double cur, double target, double maxDelta) {
    double d = target - cur;
    if (std::fabs(d) <= maxDelta) return target;
    return cur + sign(d) * maxDelta;
}

struct Vec2 {
    double x = 0, y = 0;
    Vec2() {}
    Vec2(double x_, double y_) : x(x_), y(y_) {}
    Vec2 operator+(const Vec2& o) const { return {x + o.x, y + o.y}; }
    Vec2 operator-(const Vec2& o) const { return {x - o.x, y - o.y}; }
    Vec2 operator*(double s) const { return {x * s, y * s}; }
    Vec2 operator/(double s) const { return {x / s, y / s}; }
    Vec2& operator+=(const Vec2& o) { x += o.x; y += o.y; return *this; }
    Vec2& operator-=(const Vec2& o) { x -= o.x; y -= o.y; return *this; }
    Vec2& operator*=(double s) { x *= s; y *= s; return *this; }
    double len() const { return std::sqrt(x * x + y * y); }
    double len2() const { return x * x + y * y; }
    double dot(const Vec2& o) const { return x * o.x + y * o.y; }
    double cross(const Vec2& o) const { return x * o.y - y * o.x; }
    Vec2 norm() const { double l = len(); return l > 1e-12 ? Vec2{x / l, y / l} : Vec2{0, 0}; }
    Vec2 perp() const { return {-y, x}; }            // +90 deg
    double angle() const { return std::atan2(y, x); }
    Vec2 rotated(double a) const {
        double c = std::cos(a), s = std::sin(a);
        return {x * c - y * s, x * s + y * c};
    }
};
inline Vec2 operator*(double s, const Vec2& v) { return {v.x * s, v.y * s}; }
inline Vec2 fromAngle(double a, double r = 1.0) { return {std::cos(a) * r, std::sin(a) * r}; }

// sRGB-ish colour, components 0..1
struct Color {
    float r = 1, g = 1, b = 1, a = 1;
    Color() {}
    Color(float r_, float g_, float b_, float a_ = 1) : r(r_), g(g_), b(b_), a(a_) {}
    Color withA(float na) const { return {r, g, b, na}; }
};
inline Color mix(const Color& a, const Color& b, float t) {
    return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t,
            a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t};
}
inline Color rgb(int hex) {
    return {((hex >> 16) & 255) / 255.0f, ((hex >> 8) & 255) / 255.0f, (hex & 255) / 255.0f, 1.0f};
}
