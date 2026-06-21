// WebGL2 (GLES3) renderer for RocketSim.
//   * Batched 2D geometry in screen-pixel space (parts, UI, particles, text).
//   * A dedicated SDF shader for celestial bodies (smooth surface shading +
//     day/night terminator + atmospheric halo), so planets look good at any zoom.
//   * Thick anti-aliased polylines for orbit/trajectory prediction.
// All world->screen projection is done by the caller in double precision; the
// renderer only ever sees floats in pixel space, which keeps it precise far
// from the origin.
#pragma once
#include <GLES3/gl3.h>
#include <emscripten.h>
#include <emscripten/html5.h>
#include <vector>
#include <cstdio>
#include <cstring>
#include <string>
#include "math.h"

struct Vert { float x, y, u, v, r, g, b, a, tex; };

class Renderer {
public:
    int W = 1280, H = 720;
    float pixelRatio = 1.0f;
    // proportional font metrics (atlas built from the system UI font)
    int fontAW = 0, fontAH = 0, fontCols = 16;
    float fontCell = 64, fontPad = 4, fontAsc = 1, fontDesc = 1, fontBand = 1;
    float fontAdv[96] = {0};
    static constexpr float FONT_LINE = 11.0f;   // on-screen line/band height at scale 1

    bool init() {
        EmscriptenWebGLContextAttributes attr;
        emscripten_webgl_init_context_attributes(&attr);
        attr.majorVersion = 2;
        attr.minorVersion = 0;
        attr.antialias = EM_TRUE;
        attr.alpha = EM_FALSE;
        attr.depth = EM_FALSE;
        attr.stencil = EM_FALSE;
        attr.premultipliedAlpha = EM_TRUE;
        attr.powerPreference = EM_WEBGL_POWER_PREFERENCE_HIGH_PERFORMANCE;
        ctx_ = emscripten_webgl_create_context("#canvas", &attr);
        if (ctx_ <= 0) { printf("[gl] context creation failed: %d\n", (int)ctx_); return false; }
        emscripten_webgl_make_context_current(ctx_);

        prog2d_ = link(VS2D, FS2D);
        progBody_ = link(VSBODY, FSBODY);
        if (!prog2d_ || !progBody_) return false;
        uRes2d_  = glGetUniformLocation(prog2d_, "uRes");
        uFont2d_ = glGetUniformLocation(prog2d_, "uFont");
        bRes_   = glGetUniformLocation(progBody_, "uRes");
        bCenter_= glGetUniformLocation(progBody_, "uCenter");
        bRad_   = glGetUniformLocation(progBody_, "uRadius");
        bAtmo_  = glGetUniformLocation(progBody_, "uAtmo");
        bCol1_  = glGetUniformLocation(progBody_, "uCol1");
        bCol2_  = glGetUniformLocation(progBody_, "uCol2");
        bAtmoC_ = glGetUniformLocation(progBody_, "uAtmoCol");
        bSun_   = glGetUniformLocation(progBody_, "uSunDir");
        bSeed_  = glGetUniformLocation(progBody_, "uSeed");
        bFlags_ = glGetUniformLocation(progBody_, "uFlags");

        glGenVertexArrays(1, &vao_);
        glGenBuffers(1, &vbo_);
        glBindVertexArray(vao_);
        glBindBuffer(GL_ARRAY_BUFFER, vbo_);
        const GLsizei stride = sizeof(Vert);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride, (void*)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride, (void*)8);
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, stride, (void*)16);
        glEnableVertexAttribArray(3);
        glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, stride, (void*)32);
        glBindVertexArray(0);

        buildFontAtlas();

        // Unit quad for body shader.
        glGenVertexArrays(1, &bodyVao_);
        glGenBuffers(1, &bodyVbo_);
        glBindVertexArray(bodyVao_);
        glBindBuffer(GL_ARRAY_BUFFER, bodyVbo_);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
        glBindVertexArray(0);

        glDisable(GL_DEPTH_TEST);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        return true;
    }

    void resize(int w, int h, float ratio) {
        W = w; H = h; pixelRatio = ratio;
    }

    void beginFrame(const Color& clear) {
        emscripten_webgl_make_context_current(ctx_);
        glViewport(0, 0, (int)(W * pixelRatio), (int)(H * pixelRatio));
        glClearColor(clear.r, clear.g, clear.b, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        normal_.clear();
        add_.clear();
    }

    // ---- celestial body (drawn immediately, before batched 2D) ----
    void body(float cx, float cy, float radius, float atmo,
              Color col1, Color col2, Color atmoCol, Vec2 sunDir,
              float seed, bool isStar) {
        // Bounding quad in pixel space.
        float ext = atmo;
        float x0 = cx - ext, y0 = cy - ext, x1 = cx + ext, y1 = cy + ext;
        float quad[12] = { x0,y0, x1,y0, x1,y1, x0,y0, x1,y1, x0,y1 };
        glUseProgram(progBody_);
        glUniform2f(bRes_, (float)W, (float)H);
        glUniform2f(bCenter_, cx, cy);
        glUniform1f(bRad_, radius);
        glUniform1f(bAtmo_, atmo);
        glUniform4f(bCol1_, col1.r, col1.g, col1.b, col1.a);
        glUniform4f(bCol2_, col2.r, col2.g, col2.b, col2.a);
        glUniform4f(bAtmoC_, atmoCol.r, atmoCol.g, atmoCol.b, atmoCol.a);
        Vec2 sd = sunDir.norm();
        glUniform2f(bSun_, (float)sd.x, (float)sd.y);
        glUniform1f(bSeed_, seed);
        glUniform1f(bFlags_, isStar ? 1.0f : 0.0f);
        glBindVertexArray(bodyVao_);
        glBindBuffer(GL_ARRAY_BUFFER, bodyVbo_);
        glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_DYNAMIC_DRAW);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glBindVertexArray(0);
    }

    // ---- 2D primitives (screen-pixel space) ----
    void tri(Vec2 a, Vec2 b, Vec2 c, Color col, bool additive = false) {
        auto& buf = additive ? add_ : normal_;
        pushV(buf, a, col); pushV(buf, b, col); pushV(buf, c, col);
    }
    void quad(Vec2 a, Vec2 b, Vec2 c, Vec2 d, Color col, bool additive = false) {
        tri(a, b, c, col, additive); tri(a, c, d, col, additive);
    }
    void rect(float x, float y, float w, float h, Color col, bool additive = false) {
        quad({x, y}, {x + w, y}, {x + w, y + h}, {x, y + h}, col, additive);
    }
    void rectRound(float x, float y, float w, float h, float r, Color col) {
        r = std::min(r, std::min(w, h) * 0.5f);
        rect(x + r, y, w - 2 * r, h, col);
        rect(x, y + r, r, h - 2 * r, col);
        rect(x + w - r, y + r, r, h - 2 * r, col);
        const int seg = 5;
        Vec2 corners[4] = {{x + r, y + r}, {x + w - r, y + r}, {x + w - r, y + h - r}, {x + r, y + h - r}};
        float base[4] = {(float)PI, (float)(1.5 * PI), 0.0f, (float)(0.5 * PI)};
        for (int c = 0; c < 4; c++)
            fan(corners[c], r, base[c], base[c] + PI / 2, seg, col);
    }
    void fan(Vec2 center, float r, double a0, double a1, int seg, Color col, bool additive = false) {
        for (int i = 0; i < seg; i++) {
            double t0 = a0 + (a1 - a0) * i / seg;
            double t1 = a0 + (a1 - a0) * (i + 1) / seg;
            tri(center, center + fromAngle(t0, r), center + fromAngle(t1, r), col, additive);
        }
    }
    void circle(Vec2 c, float r, Color col, bool additive = false, int seg = 22) {
        fan(c, r, 0, TAU, seg, col, additive);
    }
    void ring(Vec2 c, float r, float thickness, Color col, int seg = 48) {
        float ro = r + thickness * 0.5f, ri = r - thickness * 0.5f;
        for (int i = 0; i < seg; i++) {
            double t0 = TAU * i / seg, t1 = TAU * (i + 1) / seg;
            Vec2 o0 = c + fromAngle(t0, ro), o1 = c + fromAngle(t1, ro);
            Vec2 i0 = c + fromAngle(t0, ri), i1 = c + fromAngle(t1, ri);
            quad(i0, o0, o1, i1, col);
        }
    }
    void line(Vec2 a, Vec2 b, float width, Color col, bool additive = false) {
        Vec2 d = (b - a).norm();
        Vec2 n = d.perp() * (width * 0.5);
        quad(a - n, b - n, b + n, a + n, col, additive);
    }
    // Polyline with mitre-less round-ish joins (good for smooth orbit curves).
    void polyline(const std::vector<Vec2>& pts, float width, Color col, bool closed = false, bool additive = false) {
        int n = (int)pts.size();
        if (n < 2) return;
        int last = closed ? n : n - 1;
        for (int i = 0; i < last; i++) {
            const Vec2& a = pts[i];
            const Vec2& b = pts[(i + 1) % n];
            line(a, b, width, col, additive);
            if (width > 2.5f) circle(b, width * 0.5f, col, additive, 8);
        }
    }
    void convex(const std::vector<Vec2>& pts, Color col, bool additive = false) {
        for (size_t i = 1; i + 1 < pts.size(); i++)
            tri(pts[0], pts[i], pts[i + 1], col, additive);
    }

    // ---- text (proportional, anti-aliased system font) ----
    float pxScaleFor(float scale) const { return FONT_LINE * scale / fontBand; }   // atlas px -> screen px
    float charH(float scale) const { return FONT_LINE * scale; }
    float charW(float scale) const { return fontAdv['n' - 32] * pxScaleFor(scale); }  // representative width
    float textWidth(const std::string& s, float scale) const {
        float ps = pxScaleFor(scale), w = 0;
        for (char ch : s) { int idx = (unsigned char)ch - 32; w += (idx >= 0 && idx < 96 ? fontAdv[idx] : fontAdv[0]) * ps; }
        return w;
    }
    void text(const std::string& s, float x, float y, float scale, Color col, bool additive = false) {
        auto& buf = additive ? add_ : normal_;
        float ps = pxScaleFor(scale);
        float bandH = fontBand * ps;
        auto emit = [&](float ox, float oy, Color c) {
            float penX = x + ox;
            for (char ch : s) {
                int idx = (unsigned char)ch - 32;
                if (idx < 0 || idx >= 96) idx = 0;
                float aw = fontAdv[idx], gw = aw * ps;
                if (ch != ' ' && aw > 0.1f) {
                    int gc = idx % fontCols, gr = idx / fontCols;
                    float bx = gc * fontCell + fontPad, by = gr * fontCell + fontPad;
                    float u0 = bx / fontAW, u1 = (bx + aw) / fontAW;
                    float v0 = by / fontAH, v1 = (by + fontBand) / fontAH;
                    float X = penX, Y = y + oy;
                    pushVuv(buf, {X, Y},          {u0, v0}, c);
                    pushVuv(buf, {X + gw, Y},     {u1, v0}, c);
                    pushVuv(buf, {X + gw, Y + bandH}, {u1, v1}, c);
                    pushVuv(buf, {X, Y},          {u0, v0}, c);
                    pushVuv(buf, {X + gw, Y + bandH}, {u1, v1}, c);
                    pushVuv(buf, {X, Y + bandH},  {u0, v1}, c);
                }
                penX += gw;
            }
        };
        if (!additive) { float so = std::max(1.0f, scale * 0.9f); emit(so, so, Color(0, 0, 0, 0.6f * col.a)); }
        emit(0, 0, col);
    }
    void textC(const std::string& s, float cx, float y, float scale, Color col) {
        text(s, cx - textWidth(s, scale) * 0.5f, y, scale, col);
    }

    void flush() {
        glUseProgram(prog2d_);
        glUniform2f(uRes2d_, (float)W, (float)H);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, fontTex_);
        glUniform1i(uFont2d_, 0);
        glBindVertexArray(vao_);
        glBindBuffer(GL_ARRAY_BUFFER, vbo_);

        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        drawBuf(normal_);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE);
        drawBuf(add_);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glBindVertexArray(0);
    }

private:
    EMSCRIPTEN_WEBGL_CONTEXT_HANDLE ctx_ = 0;
    GLuint prog2d_ = 0, progBody_ = 0, vao_ = 0, vbo_ = 0, fontTex_ = 0;
    GLuint bodyVao_ = 0, bodyVbo_ = 0;
    GLint uRes2d_, uFont2d_;
    GLint bRes_, bCenter_, bRad_, bAtmo_, bCol1_, bCol2_, bAtmoC_, bSun_, bSeed_, bFlags_;
    std::vector<Vert> normal_, add_;

    static void pushV(std::vector<Vert>& b, Vec2 p, Color c) {
        b.push_back({(float)p.x, (float)p.y, 0, 0, c.r, c.g, c.b, c.a, 0});
    }
    static void pushVuv(std::vector<Vert>& b, Vec2 p, Vec2 uv, Color c) {
        b.push_back({(float)p.x, (float)p.y, (float)uv.x, (float)uv.y, c.r, c.g, c.b, c.a, 1});
    }
    void drawBuf(std::vector<Vert>& buf) {
        if (buf.empty()) return;
        glBufferData(GL_ARRAY_BUFFER, buf.size() * sizeof(Vert), buf.data(), GL_DYNAMIC_DRAW);
        glDrawArrays(GL_TRIANGLES, 0, (GLsizei)buf.size());
    }

    void buildFontAtlas() {
        // Rasterise printable ASCII (32..127) from the system UI font into an
        // anti-aliased coverage atlas via an offscreen Canvas 2D, then sample it
        // with LINEAR filtering. This gives normal, smooth, proportional text.
        EM_ASM({
            var S = 64; var F = 46;
            var cvs = document.createElement('canvas');
            cvs.width = 16 * S; cvs.height = 6 * S;
            var ctx = cvs.getContext('2d');
            ctx.font = '500 ' + F + 'px system-ui,-apple-system,"Segoe UI",Roboto,"Helvetica Neue",Arial,sans-serif';
            ctx.textBaseline = 'alphabetic'; ctx.textAlign = 'left'; ctx.fillStyle = '#ffffff';
            var mm = ctx.measureText('MbdfhklABCXgjpqy');
            var asc = mm.actualBoundingBoxAscent || F * 0.78;
            var desc = mm.actualBoundingBoxDescent || F * 0.22;
            var pad = 4; var baseY = pad + asc;
            var adv = new Float32Array(96);
            for (var i = 0; i < 96; i++) {
                var ch = String.fromCharCode(32 + i);
                var cx = (i % 16) * S; var cy = ((i / 16) | 0) * S;
                adv[i] = ctx.measureText(ch).width;
                ctx.fillText(ch, cx + pad, cy + baseY);
            }
            window.__rsfont = ({ AW: cvs.width, AH: cvs.height, S: S, pad: pad, asc: asc, desc: desc, adv: adv, data: ctx.getImageData(0, 0, cvs.width, cvs.height).data });
        });
        fontAW = EM_ASM_INT({ return window.__rsfont.AW; });
        fontAH = EM_ASM_INT({ return window.__rsfont.AH; });
        fontCell = (float)EM_ASM_DOUBLE({ return window.__rsfont.S; });
        fontPad = (float)EM_ASM_DOUBLE({ return window.__rsfont.pad; });
        fontAsc = (float)EM_ASM_DOUBLE({ return window.__rsfont.asc; });
        fontDesc = (float)EM_ASM_DOUBLE({ return window.__rsfont.desc; });
        fontBand = fontAsc + fontDesc;
        EM_ASM({ var a = window.__rsfont.adv; for (var i = 0; i < 96; i++) HEAPF32[($0 >> 2) + i] = a[i]; }, fontAdv);
        std::vector<uint8_t> px((size_t)fontAW * fontAH);
        EM_ASM({ var d = window.__rsfont.data; var p = $0; var n = d.length >> 2; for (var i = 0; i < n; i++) HEAPU8[p + i] = d[(i << 2) + 3]; }, px.data());
        glGenTextures(1, &fontTex_);
        glBindTexture(GL_TEXTURE_2D, fontTex_);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, fontAW, fontAH, 0, GL_RED, GL_UNSIGNED_BYTE, px.data());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }

    GLuint compile(GLenum type, const char* src) {
        GLuint s = glCreateShader(type);
        glShaderSource(s, 1, &src, nullptr);
        glCompileShader(s);
        GLint ok = 0; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
        if (!ok) { char log[1024]; glGetShaderInfoLog(s, 1024, nullptr, log); printf("[shader] %s\n", log); }
        return s;
    }
    GLuint link(const char* vs, const char* fs) {
        GLuint v = compile(GL_VERTEX_SHADER, vs), f = compile(GL_FRAGMENT_SHADER, fs);
        GLuint p = glCreateProgram();
        glAttachShader(p, v); glAttachShader(p, f); glLinkProgram(p);
        GLint ok = 0; glGetProgramiv(p, GL_LINK_STATUS, &ok);
        if (!ok) { char log[1024]; glGetProgramInfoLog(p, 1024, nullptr, log); printf("[link] %s\n", log); return 0; }
        glDeleteShader(v); glDeleteShader(f);
        return p;
    }

    static constexpr const char* VS2D = R"(#version 300 es
layout(location=0) in vec2 aPos;
layout(location=1) in vec2 aUV;
layout(location=2) in vec4 aColor;
layout(location=3) in float aTex;
uniform vec2 uRes;
out vec2 vUV; out vec4 vColor; out float vTex;
void main(){
  vec2 ndc = vec2(aPos.x/uRes.x*2.0-1.0, 1.0-aPos.y/uRes.y*2.0);
  gl_Position = vec4(ndc,0.0,1.0);
  vUV=aUV; vColor=aColor; vTex=aTex;
})";

    static constexpr const char* FS2D = R"(#version 300 es
precision highp float;
in vec2 vUV; in vec4 vColor; in float vTex;
uniform sampler2D uFont;
out vec4 frag;
void main(){
  vec4 c = vColor;
  if(vTex>0.5){ c.a *= texture(uFont, vUV).r; }
  if(c.a < 0.002) discard;
  frag = c;
})";

    static constexpr const char* VSBODY = R"(#version 300 es
layout(location=0) in vec2 aPos;
uniform vec2 uRes;
out vec2 vPix;
void main(){
  vPix = aPos;
  vec2 ndc = vec2(aPos.x/uRes.x*2.0-1.0, 1.0-aPos.y/uRes.y*2.0);
  gl_Position = vec4(ndc,0.0,1.0);
})";

    // SDF planet/star with surface bands, day/night terminator, atmosphere halo.
    static constexpr const char* FSBODY = R"(#version 300 es
precision highp float;
in vec2 vPix;
uniform vec2 uCenter; uniform float uRadius; uniform float uAtmo;
uniform vec4 uCol1; uniform vec4 uCol2; uniform vec4 uAtmoCol;
uniform vec2 uSunDir; uniform float uSeed; uniform float uFlags;
out vec4 frag;

float hash(vec2 p){ p=fract(p*vec2(123.34,456.21)); p+=dot(p,p+45.32); return fract(p.x*p.y); }
float noise(vec2 p){
  vec2 i=floor(p), f=fract(p); f=f*f*(3.0-2.0*f);
  float a=hash(i), b=hash(i+vec2(1,0)), c=hash(i+vec2(0,1)), d=hash(i+vec2(1,1));
  return mix(mix(a,b,f.x), mix(c,d,f.x), f.y);
}
float fbm(vec2 p){ float s=0.0,a=0.5; for(int i=0;i<5;i++){ s+=a*noise(p); p*=2.0; a*=0.5; } return s; }

void main(){
  vec2 rel = vPix - uCenter;
  float d = length(rel);
  if(d > uAtmo){ discard; }
  vec2 n = rel / uRadius;            // disk coords -1..1
  float isStar = uFlags;

  if(d <= uRadius){
    float z = sqrt(max(0.0, 1.0 - dot(n,n)));
    vec3 sn = vec3(n, z);
    // surface texture: warped fbm bands
    vec2 sp = n*3.0 + vec2(uSeed*7.0, uSeed*3.0);
    float t = fbm(sp + fbm(sp*1.7)*0.6);
    vec3 base = mix(uCol2.rgb, uCol1.rgb, smoothstep(0.35,0.7,t));
    if(isStar > 0.5){
      float gran = fbm(n*8.0 + uSeed*11.0);
      vec3 col = mix(uCol2.rgb, uCol1.rgb, gran);
      col += pow(z, 1.5)*0.4;            // bright centre
      float aedge = 1.0 - smoothstep(uRadius-2.0, uRadius+1.0, d);
      frag = vec4(col, aedge);
      return;
    }
    float lambert = clamp(dot(sn, vec3(uSunDir, 0.35)), 0.0, 1.0);
    float night = 0.10;
    vec3 col = base * (night + (1.0-night)*smoothstep(0.0,0.55,lambert));
    // limb darkening + subtle rim light
    float limb = length(n);
    col *= mix(1.0, 0.62, smoothstep(0.55,1.0,limb));
    float rim = smoothstep(0.85,1.0,limb) * lambert;
    col += uAtmoCol.rgb * rim * 0.5;
    float aedge = 1.0 - smoothstep(uRadius-1.5, uRadius+0.5, d);
    frag = vec4(col, aedge);
  } else {
    // atmospheric halo / corona
    float t = (d - uRadius) / max(uAtmo - uRadius, 1.0);
    float glow = pow(1.0 - t, isStar>0.5 ? 1.6 : 2.6);
    vec2 nn = normalize(rel);
    float lit = isStar>0.5 ? 1.0 : clamp(dot(nn, uSunDir),0.0,1.0)*0.75 + 0.25;
    float a = glow * lit * uAtmoCol.a;
    frag = vec4(uAtmoCol.rgb, a);
  }
})";
};
