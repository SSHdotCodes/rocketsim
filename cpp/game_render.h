// Rendering for RocketSim. All members of Game are public, so these are free
// functions operating on a Game&. Two coordinate paths: build (no rotation) and
// flight (rocket frame -> world -> screen). Planets use the renderer's SDF body
// shader; everything else is batched 2D triangles + bitmap text.
#pragma once
#include <functional>
#include "game.h"

namespace gr {

inline Vec2 w2s(Game& g, Vec2 w, Vec2 cam, double scale) {
    return { g.r->W*0.5 + (w.x-cam.x)*scale, g.r->H*0.5 - (w.y-cam.y)*scale };
}

inline std::string fmt(double v, int dec=0){ char b[48]; snprintf(b,48,"%.*f",dec,v); return b; }
inline std::string fmtKM(double m){
    double a=std::fabs(m);
    if (a>1e7) return fmt(m/1e6,0)+" Mm";
    if (a>=1000) return fmt(m/1000,1)+" km";
    return fmt(m,0)+" m";
}
inline std::string fmtTime(double s){
    if (s<90) return fmt(s,0)+" s";
    if (s<7200) return fmt(s/60.0,1)+" min";
    return fmt(s/3600.0,1)+" h";
}
inline std::string stageTitle(int s){
    return s==0 ? "STEP 1  LIFTOFF" : "STEP "+fmt((double)(s+1),0)+"  SPACE #"+fmt((double)s,0);
}
inline std::string stageShort(int s){
    return s==0 ? "Launch" : "Space "+fmt((double)s,0);
}

inline bool button(Game& g, float x,float y,float w,float h,const std::string& label,bool active=false,float ts=1.3f){
    bool hover = g.in.mx>=x&&g.in.mx<=x+w&&g.in.my>=y&&g.in.my<=y+h;
    Color base = active? rgb(0x2f7fd6) : (hover? rgb(0x394452):rgb(0x222a34));
    g.r->rectRound(x,y,w,h,5,base);
    g.r->text(label, x+10, y+h*0.5f-g.r->charH(ts)*0.4f, ts, active?rgb(0xffffff):rgb(0xd6dde6));
    return hover && g.in.mouseReleased;
}

// ---------- part artwork (rocket frame -> screen via tf) ----------
inline void drawPart(Game& g, int type, Vec2 c, std::function<Vec2(Vec2)> tf, double scale, bool firing, bool deployed=false){
    const PartSpec& s = spec(type);
    double w=s.w, h=s.h;
    auto P=[&](double x,double y){ return tf(c+Vec2{x,y}); };
    auto quad=[&](Vec2 a,Vec2 b,Vec2 c2,Vec2 d,Color col){ g.r->quad(a,b,c2,d,col); };
    Color steel=rgb(0xc9d2dc), steelD=rgb(0x8f99a6), white=rgb(0xeef2f6);
    switch(type){
        case PT_POD: {
            Color base=rgb(0xdfe6ee), dark=rgb(0x9aa6b4);
            // capsule trapezoid
            quad(P(-w*0.28,h*0.5),P(w*0.28,h*0.5),P(w*0.5,-h*0.5),P(-w*0.5,-h*0.5),base);
            quad(P(0,h*0.5),P(w*0.28,h*0.5),P(w*0.16,-h*0.5),P(0,-h*0.5),dark);
            // window
            g.r->circle(P(0,h*0.05), w*0.16*scale, rgb(0x2a3340));
            g.r->circle(P(-w*0.04,h*0.09), w*0.07*scale, rgb(0x86c5ff),true);
        } break;
        case PT_NOSE: {
            Vec2 tip=P(0,h*0.5);
            g.r->tri(tip,P(-w*0.5,-h*0.5),P(w*0.5,-h*0.5),rgb(0xe2e7ee));
            g.r->tri(tip,P(0,-h*0.5),P(w*0.5,-h*0.5),rgb(0xb9c2cd));
        } break;
        case PT_TANK_S: case PT_TANK_L: {
            quad(P(-w*0.5,h*0.5),P(w*0.5,h*0.5),P(w*0.5,-h*0.5),P(-w*0.5,-h*0.5),white);
            quad(P(0,h*0.5),P(w*0.5,h*0.5),P(w*0.5,-h*0.5),P(0,-h*0.5),steel);
            quad(P(w*0.18,h*0.5),P(w*0.5,h*0.5),P(w*0.5,-h*0.5),P(w*0.18,-h*0.5),steelD.withA(0.5f));
            // bands
            for (double by=-0.3; by<=0.31; by+=0.3)
                g.r->line(P(-w*0.5,h*by),P(w*0.5,h*by),std::max(1.0,scale*0.08),rgb(0x7c8794));
        } break;
        case PT_ENGINE_S: case PT_ENGINE_L: {
            Color m=rgb(0x4b525c), m2=rgb(0x2c3138);
            quad(P(-w*0.3,h*0.5),P(w*0.3,h*0.5),P(w*0.28,h*0.1),P(-w*0.28,h*0.1),rgb(0x6b7480));
            // bell nozzle flares out at bottom
            quad(P(-w*0.28,h*0.1),P(w*0.28,h*0.1),P(w*0.5,-h*0.5),P(-w*0.5,-h*0.5),m);
            quad(P(0,h*0.1),P(w*0.28,h*0.1),P(w*0.5,-h*0.5),P(0,-h*0.5),m2);
            if (firing){
                Vec2 a=P(-w*0.42,-h*0.5), b=P(w*0.42,-h*0.5), tip=P(0,-h*0.5-h*frand(1.6,2.4));
                g.r->tri(a,b,tip,Color(1,0.85f,0.4f,0.8f),true);
                g.r->tri(P(-w*0.22,-h*0.5),P(w*0.22,-h*0.5),P(0,-h*0.5-h*frand(2.2,3.2)),Color(1,1,0.8f,0.8f),true);
            }
        } break;
        case PT_SRB: {
            Color body=rgb(0xeceff3);
            quad(P(-w*0.5,h*0.42),P(w*0.5,h*0.42),P(w*0.5,-h*0.4),P(-w*0.5,-h*0.4),body);
            quad(P(0,h*0.42),P(w*0.5,h*0.42),P(w*0.5,-h*0.4),P(0,-h*0.4),rgb(0xc2c8d0));
            g.r->tri(P(-w*0.5,h*0.42),P(w*0.5,h*0.42),P(0,h*0.5),rgb(0xd23b32)); // nose
            quad(P(-w*0.32,-h*0.4),P(w*0.32,-h*0.4),P(w*0.42,-h*0.5),P(-w*0.42,-h*0.5),rgb(0x3a3f47));
            for (double by=-0.25; by<=0.26; by+=0.25) g.r->line(P(-w*0.5,h*by),P(w*0.5,h*by),std::max(1.0,scale*0.06),rgb(0xb0202a).withA(0.6f));
            if (firing){ g.r->tri(P(-w*0.34,-h*0.5),P(w*0.34,-h*0.5),P(0,-h*0.5-h*frand(1.4,2.0)),Color(1,0.8f,0.35f,0.8f),true); }
        } break;
        case PT_DECOUPLER:
            quad(P(-w*0.5,h*0.5),P(w*0.5,h*0.5),P(w*0.5,-h*0.5),P(-w*0.5,-h*0.5),rgb(0x33373d));
            g.r->line(P(-w*0.5,0),P(w*0.5,0),std::max(1.0,scale*0.12),rgb(0xe2a33a));
            break;
        case PT_SEP: {
            Color body=rgb(0x565f6b), plate=rgb(0x2f3742), stripe=rgb(0xe2a33a);
            double dir = c.x>=0?1:-1;
            double inner = -dir*w*0.5, outer = dir*w*0.5;
            quad(P(inner,h*0.38),P(outer,h*0.38),P(outer,-h*0.38),P(inner,-h*0.38),body);
            g.r->line(P(inner,h*0.35),P(inner,-h*0.35),std::max(1.2,scale*0.10),plate);
            g.r->line(P(outer,h*0.28),P(outer,-h*0.28),std::max(1.2,scale*0.10),plate);
            g.r->line(P(inner,0),P(outer,0),std::max(1.3,scale*0.12),stripe);
            g.r->circle(P(inner,h*0.18),std::max(1.2,scale*0.055),rgb(0xc9d2dc));
            g.r->circle(P(inner,-h*0.18),std::max(1.2,scale*0.055),rgb(0xc9d2dc));
        } break;
        case PT_CHUTE:
            quad(P(-w*0.5,h*0.5),P(w*0.5,h*0.5),P(w*0.5,-h*0.5),P(-w*0.5,-h*0.5),rgb(0x9aa6b4));
            g.r->rectRound(tf(c).x-3,tf(c).y-3,6,6,2,rgb(0xd23b32));
            break;
        case PT_LEGS: {
            Color leg=rgb(0x6b7480), dark=rgb(0x3a424c), foot=rgb(0x9aa6b4);
            if (std::fabs(c.x)>0.1){
                double dir = c.x>=0?1:-1;
                double inner = -dir*w*0.5, outer = dir*w*0.5;
                quad(P(inner,h*0.34),P(inner+dir*w*0.16,h*0.34),P(inner+dir*w*0.16,-h*0.34),P(inner,-h*0.34),dark);
                if (deployed){
                    g.r->line(P(inner+dir*w*0.08,h*0.22),P(outer,-h*0.46),std::max(2.0,scale*0.16),leg);
                    g.r->line(P(inner+dir*w*0.08,-h*0.10),P(outer,-h*0.46),std::max(2.0,scale*0.13),leg);
                    g.r->line(P(outer,-h*0.46),P(outer-dir*w*0.22,-h*0.46),std::max(2.2,scale*0.18),foot);
                } else {
                    g.r->line(P(inner+dir*w*0.10,h*0.24),P(outer-dir*w*0.10,-h*0.24),std::max(2.0,scale*0.13),leg);
                    g.r->line(P(inner+dir*w*0.10,-h*0.24),P(outer-dir*w*0.10,-h*0.24),std::max(2.0,scale*0.13),leg);
                }
            } else {
                if (!deployed){
                    quad(P(-w*0.42,h*0.16),P(w*0.42,h*0.16),P(w*0.42,-h*0.16),P(-w*0.42,-h*0.16),dark);
                    break;
                }
                g.r->line(P(-w*0.16,h*0.4),P(-w*0.5,-h*0.5),std::max(2.0,scale*0.18),leg);
                g.r->line(P( w*0.16,h*0.4),P( w*0.5,-h*0.5),std::max(2.0,scale*0.18),leg);
                g.r->line(P(-w*0.5,-h*0.5),P(-w*0.34,-h*0.5),std::max(2.0,scale*0.2),foot);
                g.r->line(P( w*0.5,-h*0.5),P( w*0.34,-h*0.5),std::max(2.0,scale*0.2),foot);
            }
        } break;
        case PT_FIN: {
            Color fin=rgb(0x57606c), fin2=rgb(0x3c434c);
            double dir = c.x>=0?1:-1;
            double inner = -dir*w*0.5, outer = dir*w*0.5;
            g.r->tri(P(inner,h*0.48),P(inner,-h*0.48),P(outer,-h*0.48),fin);
            g.r->tri(P(inner,h*0.48),P(outer,-h*0.48),P(outer,-h*0.16),fin2);
            g.r->line(P(inner,h*0.42),P(inner,-h*0.42),std::max(1.4,scale*0.11),rgb(0x7a8490));
        } break;
        case PT_RCS:
            quad(P(-w*0.4,h*0.5),P(w*0.4,h*0.5),P(w*0.4,-h*0.5),P(-w*0.4,-h*0.5),rgb(0x4b525c));
            g.r->circle(P(-w*0.5,0),scale*0.12,rgb(0x9aa6b4)); g.r->circle(P(w*0.5,0),scale*0.12,rgb(0x9aa6b4));
            break;
    }
}

inline void drawInterstageShroud(Game& g, Vec2 engineC, const PartSpec& engine,
                                 Vec2 decC, const PartSpec& dec,
                                 double upperW, double lowerW,
                                 std::function<Vec2(Vec2)> tf, double scale){
    double topW = std::max(0.45, std::min(upperW, engine.w)) + 0.05;
    double bottomW = std::max(lowerW, topW) + 0.05;
    if (bottomW <= topW + 0.12) return;

    double topY = engineC.y + engine.h*0.5 - 0.04;
    double bottomY = decC.y - dec.h*0.5 + 0.03;
    if (topY <= bottomY + 0.15) return;

    auto P=[&](double x,double y){ return tf(Vec2{x,y}); };
    auto quad=[&](Vec2 a,Vec2 b,Vec2 c,Vec2 d,Color col){ g.r->quad(a,b,c,d,col); };

    Color skin=rgb(0xdfe6ee), skinD=rgb(0xaeb8c4), edge=rgb(0x74808d);
    quad(P(-topW*0.5,topY), P(topW*0.5,topY), P(bottomW*0.5,bottomY), P(-bottomW*0.5,bottomY), skin);
    quad(P(0,topY), P(topW*0.5,topY), P(bottomW*0.5,bottomY), P(0,bottomY), skinD.withA(0.55f));
    g.r->line(P(-topW*0.5,topY), P(-bottomW*0.5,bottomY), std::max(1.0,scale*0.055), edge);
    g.r->line(P(topW*0.5,topY), P(bottomW*0.5,bottomY), std::max(1.0,scale*0.055), edge);

    double bandH = std::max(0.16, dec.h*0.58);
    double by1 = decC.y + bandH*0.5, by0 = decC.y - bandH*0.5;
    quad(P(-bottomW*0.5,by1), P(bottomW*0.5,by1), P(bottomW*0.5,by0), P(-bottomW*0.5,by0), rgb(0x33373d));
    g.r->line(P(-bottomW*0.5,decC.y), P(bottomW*0.5,decC.y), std::max(1.3,scale*0.12), rgb(0xe2a33a));
}

inline bool touches(double a, double b, double tol=0.18){ return std::fabs(a-b) <= tol; }

inline void drawBuildInterstageShrouds(Game& g, std::function<Vec2(Vec2)> tf){
    for (int di=0; di<(int)g.design.size(); di++){
        const PlacedPart& decPart = g.design[di];
        const PartSpec& dec = spec(decPart.type);
        if (!dec.isDecoupler || std::fabs(decPart.pos.x)>0.08) continue;

        double decTop = decPart.pos.y + dec.h*0.5;
        double decBottom = decPart.pos.y - dec.h*0.5;
        int engineIdx=-1, upperIdx=-1, lowerIdx=-1;

        for (int i=0;i<(int)g.design.size();i++){
            if (i==di || std::fabs(g.design[i].pos.x)>0.08 || Game::isRadialPart(g.design[i].type)) continue;
            const PartSpec& s=spec(g.design[i].type);
            if (s.isEngine && touches(g.design[i].pos.y - s.h*0.5, decTop)){ engineIdx=i; break; }
        }
        if (engineIdx<0) continue;

        const PlacedPart& engPart = g.design[engineIdx];
        const PartSpec& engine = spec(engPart.type);
        double engineTop = engPart.pos.y + engine.h*0.5;
        for (int i=0;i<(int)g.design.size();i++){
            if (i==di || i==engineIdx || std::fabs(g.design[i].pos.x)>0.08 || Game::isRadialPart(g.design[i].type)) continue;
            const PartSpec& s=spec(g.design[i].type);
            if (touches(g.design[i].pos.y - s.h*0.5, engineTop)){ upperIdx=i; break; }
        }
        for (int i=0;i<(int)g.design.size();i++){
            if (i==di || i==engineIdx || std::fabs(g.design[i].pos.x)>0.08 || Game::isRadialPart(g.design[i].type)) continue;
            const PartSpec& s=spec(g.design[i].type);
            if (touches(g.design[i].pos.y + s.h*0.5, decBottom)){ lowerIdx=i; break; }
        }
        if (lowerIdx<0) continue;

        double upperW = upperIdx>=0 ? spec(g.design[upperIdx].type).w : engine.w;
        double lowerW = spec(g.design[lowerIdx].type).w;
        drawInterstageShroud(g, engPart.pos, engine, decPart.pos, dec, upperW, lowerW, tf, g.buildScale);
    }
}

inline void drawFlightInterstageShrouds(Game& g, std::function<Vec2(Vec2)> tf){
    for (int di=0; di<(int)g.parts.size(); di++){
        const RPart& decPart = g.parts[di];
        if (!decPart.alive) continue;
        const PartSpec& dec = spec(decPart.type);
        if (!dec.isDecoupler || std::fabs(decPart.local.x)>0.08) continue;

        double decTop = decPart.local.y + dec.h*0.5;
        double decBottom = decPart.local.y - dec.h*0.5;
        int engineIdx=-1, upperIdx=-1, lowerIdx=-1;

        for (int i=0;i<(int)g.parts.size();i++){
            if (i==di || !g.parts[i].alive || std::fabs(g.parts[i].local.x)>0.08 || Game::isRadialPart(g.parts[i].type)) continue;
            const PartSpec& s=spec(g.parts[i].type);
            if (s.isEngine && touches(g.parts[i].local.y - s.h*0.5, decTop)){ engineIdx=i; break; }
        }
        if (engineIdx<0) continue;

        const RPart& engPart = g.parts[engineIdx];
        const PartSpec& engine = spec(engPart.type);
        double engineTop = engPart.local.y + engine.h*0.5;
        for (int i=0;i<(int)g.parts.size();i++){
            if (i==di || i==engineIdx || !g.parts[i].alive || std::fabs(g.parts[i].local.x)>0.08 || Game::isRadialPart(g.parts[i].type)) continue;
            const PartSpec& s=spec(g.parts[i].type);
            if (touches(g.parts[i].local.y - s.h*0.5, engineTop)){ upperIdx=i; break; }
        }
        for (int i=0;i<(int)g.parts.size();i++){
            if (i==di || i==engineIdx || !g.parts[i].alive || std::fabs(g.parts[i].local.x)>0.08 || Game::isRadialPart(g.parts[i].type)) continue;
            const PartSpec& s=spec(g.parts[i].type);
            if (touches(g.parts[i].local.y + s.h*0.5, decBottom)){ lowerIdx=i; break; }
        }
        if (lowerIdx<0) continue;

        double upperW = upperIdx>=0 ? spec(g.parts[upperIdx].type).w : engine.w;
        double lowerW = spec(g.parts[lowerIdx].type).w;
        drawInterstageShroud(g, engPart.local, engine, decPart.local, dec, upperW, lowerW, tf, g.flightScale);
    }
}

inline void drawParticles(Game& g, Vec2 cam, double scale){
    for (auto& p : g.particles){
        float a=(float)clampd(p.life/p.maxLife,0,1);
        Vec2 s=w2s(g,p.pos,cam,scale);
        Color c=p.col; c.a*=a;
        g.r->circle(s, std::max(1.5,p.size*scale*0.25), c, p.additive, 8);
    }
}

inline void drawReentryPlasma(Game& g){
    if (g.reentryHeat < 0.04 || g.exploded) return;
    const Body& B=g.world.bodies[g.domBody];
    Vec2 vrel=g.vel-B.vel;
    if (vrel.len()<1) return;
    Vec2 vd=vrel.norm();
    Vec2 center=w2s(g,g.l2w(g.comLocal()),g.flightCam,g.flightScale);
    Vec2 nose=w2s(g,g.l2w(g.comLocal()) + vd*(5.0+g.reentryHeat*7.0),g.flightCam,g.flightScale);
    Vec2 tail=w2s(g,g.l2w(g.comLocal()) - vd*(12.0+g.reentryHeat*18.0),g.flightCam,g.flightScale);
    Vec2 axis=(tail-nose).norm();
    Vec2 side=axis.perp();
    double w=12.0 + g.reentryHeat*32.0;
    Color orange=Color(1.0f,0.38f,0.06f,(float)(0.26+g.reentryHeat*0.34));
    Color gold=Color(1.0f,0.88f,0.32f,(float)(0.20+g.reentryHeat*0.30));
    Color blue=Color(0.45f,0.86f,1.0f,(float)(g.reentryHeat*0.22));
    g.r->tri(nose, tail+side*w, tail-side*w, orange, true);
    g.r->tri(nose+side*w*0.24, center, nose-side*w*0.24, gold, true);
    if (g.reentryHeat>0.55) g.r->ring(center, (float)(w*0.62), 2.2f, blue, 32);
}

inline void drawStars(Game& g, float alpha){
    if (alpha<=0.02f) return;
    Vec2 cam = g.mapView? g.mapCam : g.flightCam;
    double px=std::fmod(cam.x*0.02,2400.0), py=std::fmod(cam.y*0.02,2400.0);
    for (auto& st : g.stars){
        float x=std::fmod(st.x - (float)px + 4800.0f, 2400.0f) - 600.0f;
        float y=std::fmod(st.y + (float)py + 4800.0f, 2400.0f) - 600.0f;
        if (x<-20||x>g.r->W+20||y<-20||y>g.r->H+20) continue;
        g.r->circle({x,y}, st.b*1.3f, Color(1,1,1,st.b*alpha), true, 6);
    }
}

inline void drawBodies(Game& g, Vec2 cam, double scale){
    for (int i=0;i<(int)g.world.bodies.size();i++){
        Body& b=g.world.bodies[i];
        Vec2 s=w2s(g,b.pos,cam,scale);
        float rad=(float)(b.radius*scale);
        float atmo=(float)((b.radius+ (b.atmoH>0?b.atmoH:b.radius*0.06))*scale);
        // cull if entirely offscreen
        if (s.x+atmo<0||s.x-atmo>g.r->W||s.y+atmo<0||s.y-atmo>g.r->H) {
            if (rad<2) continue;
        }
        Vec2 sun = g.world.sunDir; Vec2 sunS{sun.x,-sun.y};
        g.r->body(s.x,s.y,rad,atmo,b.col1,b.col2,b.atmoCol,sunS,b.seed,b.isStar);
    }
}

inline void drawRocketFlight(Game& g){
    auto tf=[&](Vec2 lr){ return w2s(g,g.l2w(lr),g.flightCam,g.flightScale); };
    // draw radial parts first (behind), then core
    for (int pass=0;pass<2;pass++)
        for (auto& p : g.parts){ if(!p.alive) continue; const PartSpec& s=spec(p.type);
            bool radial=Game::isRadialPart(p.type);
            if ((pass==0)!=radial) continue;
            bool firing = (p.engineOn) && ((s.isSRB&&p.srbFuel>0)||(s.isEngine&&p.section<g.nSections&&g.sectionFuel[p.section]>0&&g.throttle>0.02));
            drawPart(g,p.type,p.local,tf,g.flightScale,firing,s.isLeg&&g.legsDeployed);
        }
    drawFlightInterstageShrouds(g, tf);
    // deployed parachute canopy
    if (g.chuteDeployed && g.hasChute() && !g.exploded){
        double press=g.world.pressure(g.domBody,g.pos);
        if (press>0.001){
            for (auto& p:g.parts) if(p.alive&&spec(p.type).isChute){
                Vec2 top=g.l2w(p.local+Vec2{0,spec(p.type).h*0.5});
                Vec2 ctop=top+g.noseDir()*8.0;
                Vec2 a=w2s(g,ctop+g.noseDir().perp()*6.0,g.flightCam,g.flightScale);
                Vec2 b=w2s(g,ctop-g.noseDir().perp()*6.0,g.flightCam,g.flightScale);
                Vec2 apex=w2s(g,ctop+g.noseDir()*4.0,g.flightCam,g.flightScale);
                Vec2 base=w2s(g,top,g.flightCam,g.flightScale);
                g.r->tri(a,b,apex,rgb(0xe8643a));
                g.r->tri(a,base,apex,rgb(0xff8a4a)); g.r->tri(b,base,apex,rgb(0xd2502a));
                g.r->line(a,base,1.5,rgb(0xcccccc)); g.r->line(b,base,1.5,rgb(0xcccccc));
            }
        }
    }
}

// small attitude indicator (navball-lite)
inline void drawNavball(Game& g, float cx,float cy,float R){
    Vec2 up=(g.pos-g.world.bodies[g.domBody].pos).norm(); Vec2 upS{up.x,-up.y};
    g.r->circle({cx,cy},R+3,rgb(0x10151c));
    g.r->circle({cx,cy},R,rgb(0x20303f));
    // horizon (perpendicular to up): sky above, ground below
    Vec2 right=upS.perp();
    Vec2 a={cx-(float)right.x*R, cy-(float)right.y*R};
    Vec2 b={cx+(float)right.x*R, cy+(float)right.y*R};
    Vec2 down={ -(float)upS.x, -(float)upS.y};
    g.r->tri(a,b,{cx+(float)down.x*R,cy+(float)down.y*R},rgb(0x5a3a22));
    g.r->tri(a,b,{cx-(float)down.x*R,cy-(float)down.y*R},rgb(0x2f6da0));
    g.r->line(a,b,2,rgb(0xe7eef5));
    // nose marker
    Vec2 nose={(float)std::cos(g.heading),-(float)std::sin(g.heading)};
    g.r->circle({cx+nose.x*R*0.7f,cy+nose.y*R*0.7f},5,rgb(0xffd24a));
    // prograde marker
    Vec2 vrel=g.vel-g.world.bodies[g.domBody].vel;
    if (vrel.len()>2){ Vec2 vd=vrel.norm(); Vec2 vS{vd.x,-vd.y};
        g.r->ring({cx+(float)vS.x*R*0.7f,cy+(float)vS.y*R*0.7f},5,2,rgb(0x4ad0a0)); }
    g.r->ring({cx,cy},R,2,rgb(0x3a4654));
}

// small always-on minimap: current body, the rocket, its orbit, nearby bodies
inline void drawMinimap(Game& g, float cx, float cy, float R){
    Renderer* r=g.r;
    const Body& B=g.world.bodies[g.domBody];
    double rr=(g.pos-B.pos).len();
    double view=std::max(rr*1.35, B.radius*1.5);
    double sc=R/view;
    r->circle({cx,cy},R+3,rgb(0x0a0e14));
    r->circle({cx,cy},R,rgb(0x0b121b));
    auto plot=[&](Vec2 w){ Vec2 rel=(w-B.pos)*sc; return Vec2{cx+rel.x, cy-rel.y}; };
    // body
    r->circle({cx,cy}, std::max(3.0f,(float)(B.radius*sc)), mix(B.col2,B.col1,0.45f));
    // other bodies in view
    for (int i=0;i<(int)g.world.bodies.size();i++){ if(i==g.domBody || !g.bodyUnlocked(i)) continue;
        Vec2 s=plot(g.world.bodies[i].pos); if(std::hypot(s.x-cx,s.y-cy)<R-2){
            r->circle(s, std::max(2.0f,(float)(g.world.bodies[i].radius*sc)), mix(g.world.bodies[i].col2,g.world.bodies[i].col1,0.5f));
            if (i==g.targetBody) r->ring(s,6,2,rgb(0xffd24a)); } }
    // orbit
    if (g.showTrajectory && g.trajectory.size()>2){
        for (size_t i=0;i+1<g.trajectory.size();i++){ Vec2 a=plot(g.trajectory[i]), b=plot(g.trajectory[i+1]);
            if (std::hypot(a.x-cx,a.y-cy)<R && std::hypot(b.x-cx,b.y-cy)<R) r->line(a,b,1.2,rgb(0x59d6ff).withA(0.8f),true); }
    }
    // rocket
    Vec2 s=plot(g.pos); r->circle(s,2.6f,rgb(0xffd24a),true,8);
    r->ring({cx,cy},R,2,rgb(0x2b3a4d));
    r->textC(B.name, cx, cy-R-13, 1.0, rgb(0x9fb0c4));
}

// analytic Hohmann transfer ellipse around `P` with periapsis r1 at `periArg`
inline void drawTransferEllipse(Game& g, const Body& P, double r1, double r2, double periArg, Vec2 cam, double sc, Color col){
    double e=(r2-r1)/(r1+r2), a=(r1+r2)*0.5, p=a*(1-e*e);
    std::vector<Vec2> pts;
    for (int i=0;i<=140;i++){ double th=TAU*i/140.0; double rr=p/(1+e*std::cos(th));
        Vec2 w=P.pos+fromAngle(periArg+th, rr); pts.push_back(w2s(g,w,cam,sc)); }
    g.r->polyline(pts, 1.6f, col, true, true);
}

inline bool finitePoint(Vec2 p){
    return std::isfinite(p.x) && std::isfinite(p.y);
}

inline bool nearViewport(Game& g, Vec2 p, double margin){
    return p.x >= -margin && p.x <= g.r->W + margin && p.y >= -margin && p.y <= g.r->H + margin;
}

inline void drawTrajectorySegments(Game& g, Vec2 cam, double sc, float width, Color col){
    if (g.trajectory.size() < 2) return;
    const double margin = std::max(g.r->W, g.r->H) * 0.65 + 80.0;
    const double maxSeg = std::hypot((double)g.r->W, (double)g.r->H) * 0.8;
    for (size_t i=0;i+1<g.trajectory.size();i++){
        Vec2 a=w2s(g,g.trajectory[i],cam,sc), b=w2s(g,g.trajectory[i+1],cam,sc);
        if (!finitePoint(a) || !finitePoint(b)) continue;
        if ((b-a).len() > maxSeg) continue;
        if (!nearViewport(g,a,margin) && !nearViewport(g,b,margin)) continue;
        g.r->line(a,b,width,col,true);
        if (width > 2.5f) g.r->circle(b,width*0.5f,col,true,8);
    }
}

inline void drawHUD(Game& g){
    Renderer* r=g.r;
    const Body& B=g.world.bodies[g.domBody];
    double alt=(g.pos-B.pos).len()-B.radius;
    Vec2 vrel=g.vel-B.vel; double spd=vrel.len();
    Vec2 up=(g.pos-B.pos).norm(); double vspd=vrel.dot(up);
    // top bar
    r->rect(0,0,r->W,52,rgb(0x0c1118).withA(0.82f));
    auto stat=[&](float x,const std::string&label,const std::string&val,Color c=rgb(0xffffff)){
        r->text(label,x,9,1.0,rgb(0x8794a4)); r->text(val,x,24,1.7,c); };
    stat(14,"BODY",B.name,rgb(0x9fd2ff));
    stat(140,"ALTITUDE",fmtKM(alt));
    stat(300,"SPEED",fmt(spd,0)+" m/s");
    stat(440,"VERT SPD",fmt(vspd,1)+" m/s", vspd<-1?rgb(0xff9a6a):rgb(0xbfeac0));
    if (!g.landed){
        stat(600,"APOAPSIS", g.hyperbolic?"escape":fmtKM(g.apo));
        stat(760,"PERIAPSIS", fmtKM(g.peri), g.peri<0?rgb(0xff7a6a):rgb(0xffffff));
    } else stat(600,"STATUS","LANDED",rgb(0x8effa0));

    // throttle slider (drag to set) + master engine cutoff
    float bx=14, by=58;
    r->text("THROTTLE  "+fmt(g.throttle*100,0)+"%", bx, by, 1.0, g.enginesEnabled?rgb(0x9fe0b0):rgb(0xff9a6a));
    float sx=bx, sy=by+14, sw=150, sh=15;
    if (g.in.mousePressed && g.in.mx>=sx-5 && g.in.mx<=sx+sw+10 && g.in.my>=sy-8 && g.in.my<=sy+sh+8) g.draggingThrottle=true;
    if (!g.in.mouseDown) g.draggingThrottle=false;
    if (g.draggingThrottle) g.throttle=clampd((g.in.mx-sx)/sw,0,1);
    r->rectRound(sx,sy,sw,sh,4,rgb(0x1a2230));
    r->rectRound(sx,sy,(float)(sw*g.throttle),sh,4, g.enginesEnabled?rgb(0x39c06a):rgb(0x55606e));
    float hx=sx+(float)(sw*g.throttle);
    r->rectRound(hx-3,sy-3,6,sh+6,3,rgb(0xeaf0f6));
    if (button(g, sx+sw+12, sy-3, 104, sh+6, g.enginesEnabled?"ENGINES ON":"ENGINES OFF", g.enginesEnabled)){
        g.enginesEnabled=!g.enginesEnabled;
        bool srb=false; for(auto&p:g.parts) if(p.alive&&spec(p.type).isSRB&&p.srbFuel>0&&p.engineOn) srb=true;
        g.setToast(g.enginesEnabled?"Engines on":(srb?"Engines cut (boosters keep firing)":"Engines cut"));
    }
    by = sy+sh+12;
    for (int s=g.nSections-1;s>=0;s--){
        if (g.sectionFuelMax[s]<=0) continue;
        double f=g.sectionFuel[s]/g.sectionFuelMax[s];
        r->rectRound(bx,by,120,9,2,rgb(0x1a2230));
        r->rectRound(bx,by,(float)(120*f),9,2, s<g.currentStage?rgb(0xf2b24a):rgb(0x5a6678));
        r->text("S"+fmt(s),bx+124,by-1,0.9,rgb(0x8794a4));
        by+=13;
    }
    // SRB fuel
    double srbMax=0,srbCur=0; for(auto&p:g.parts) if(p.alive&&spec(p.type).isSRB){srbMax+=spec(p.type).srbFuel;srbCur+=p.srbFuel;}
    if (srbMax>0){ r->rectRound(bx,by,120,9,2,rgb(0x1a2230)); r->rectRound(bx,by,(float)(120*srbCur/srbMax),9,2,rgb(0xd2503a)); r->text("SRB",bx+124,by-1,0.9,rgb(0x8794a4)); by+=13; }
    if (g.reentryHeat>0.04){
        r->rectRound(bx,by,120,9,2,rgb(0x1a2230));
        r->rectRound(bx,by,(float)(120*clampd(g.reentryHeat,0,1)),9,2, g.reentryHeat>0.75?rgb(0xffe06a):rgb(0xff6a2a));
        r->text("HEAT",bx+124,by-1,0.9,g.reentryHeat>0.75?rgb(0xffe06a):rgb(0xff9a6a));
        by+=13;
    }

    // ---- interactive controls (clickable; keyboard shortcuts still work) ----
    float ux = r->W-150, uw=138;
    r->text("TIME WARP", ux, 56, 0.9, rgb(0x8794a4));
    if (button(g, ux, 68, 30, 22, "<<")) g.warpDown();
    r->rectRound(ux+33, 68, uw-66, 22, 4, rgb(0x161e2a));
    r->textC("x"+fmt(g.warps[g.warpIdx],0), ux+uw/2, 74, 1.3, g.warpIdx>0?rgb(0x9fd2ff):rgb(0xd6dde6));
    if (button(g, ux+uw-30, 68, 30, 22, ">>")) g.warpUp();
    if (button(g, ux, 96, uw, 24, g.mapView?"CLOSE MAP":"MAP VIEW", g.mapView)) g.mapView=!g.mapView;
    if (button(g, ux, 124, uw, 24, "MENU / SETTINGS", g.showSettings)) g.showSettings=!g.showSettings;
    if (button(g, ux, 152, 86, 24, g.sas?"SAS ON":"SAS OFF", g.sas)) g.sas=!g.sas;
    r->text("NEXT: "+stageShort(g.currentStage), ux+92, 158, 1.0, rgb(0x9fb0c4));

    drawNavball(g, r->W-78, r->H-82, 56);
    if (g.showMinimap && !g.mapView) drawMinimap(g, 96, r->H-100, 70);

    // primary actions, bottom-centre
    if (button(g, r->W/2-150, r->H-48, 150, 38, "STAGE  (SPACE)")) g.doStage();
    if (g.hasChute()){
        if (button(g, r->W/2+6, r->H-48, 150, 38, g.chuteDeployed?"CUT CHUTE (P)":"PARACHUTE (P)")){
            g.toggleParachute();
        }
    }

    // toast
    if (g.toastT>0){
        float a=(float)clampd(g.toastT,0,1);
        float tw=r->textWidth(g.toast,1.8f);
        r->rectRound(r->W/2-tw/2-14, 92, tw+28, 30, 6, rgb(0x0c1118).withA(0.8f*a));
        r->textC(g.toast, r->W/2, 100, 1.8, rgb(0xffffff).withA(a));
    }
}

inline void drawSettings(Game& g){
    Renderer* r=g.r;
    r->rect(0,0,r->W,r->H, rgb(0x05080d).withA(0.55f));
    float pw=440, ph=312, ox=r->W/2-pw/2, oy=r->H/2-ph/2;
    r->rectRound(ox,oy,pw,ph,10, rgb(0x0c121b).withA(0.98f));
    r->text("SETTINGS", ox+22, oy+18, 1.9, rgb(0x9fd2ff));
    float y=oy+52;
    auto toggle=[&](const std::string& label, bool& val){
        r->text(label, ox+24, y+7, 1.3, rgb(0xc6cfda));
        if (button(g, ox+pw-104, y, 80, 26, val?"ON":"OFF", val)) val=!val;
        y+=36;
    };
    toggle("Minimap", g.showMinimap);
    toggle("Orbit trajectory overlay", g.showTrajectory);
    toggle("Engine & explosion particles", g.showParticles);
    toggle("SAS stability assist", g.sas);
    r->text("CONTROLS", ox+24, y+2, 1.0, rgb(0x6b7888)); y+=17;
    const char* c[]={
        "A / D  steer        Shift / Ctrl  throttle",
        "Z / X  full / cut throttle      Space  stage",
        "M  map     T  SAS     P  deploy / cut parachute",
        ", / .  time warp        R  revert to hangar",
        "On the map, click a moon/planet to plan a transfer."};
    for (int i=0;i<5;i++){ r->text(c[i], ox+24, y, 1.0, rgb(0x9fb0c4)); y+=15; }
    if (button(g, ox+24, oy+ph-44, 170, 32, "RETURN TO HANGAR")){ g.mode=MODE_BUILD; g.showSettings=false; g.mapView=false; g.clearNavigation(); g.setToast("Reverted to hangar"); }
    if (button(g, ox+pw-104, oy+ph-44, 80, 32, "RESUME", true)) g.showSettings=false;
}

inline void drawMap(Game& g){
    Renderer* r=g.r;
    Vec2 cam=g.mapCam; double sc=g.mapScale;
    drawStars(g,0.9f);
    // orbit guide rings for every unlocked moon/planet that orbits another body
    for (int i=0;i<(int)g.world.bodies.size();i++){
        if (!g.bodyUnlocked(i)) continue;
        Body& b=g.world.bodies[i];
        if (b.parentIdx<0) continue;
        const Body& par=g.world.bodies[b.parentIdx];
        Vec2 pc=w2s(g,par.pos,cam,sc); float orx=(float)(b.orbitR*sc);
        if (orx>6 && orx<8000) r->ring(pc, orx, 1.0f, rgb(0x35506b).withA(0.6f), 96);
    }
    // planned transfer
    if (g.transferValid && g.targetBody>=0){
        const Body& P=g.world.bodies[g.transferParent];
        const Body& T=g.world.bodies[g.targetBody];
        double r1=(g.pos-P.pos).len(), r2=T.parentIdx>=0 ? T.orbitR : (T.pos-P.pos).len();
        double burnAngle=(g.transferNode-P.pos).angle();
        drawTransferEllipse(g, P, std::min(r1,r2), std::max(r1,r2), r1<r2?burnAngle:burnAngle+PI, cam, sc, rgb(0xffb347).withA(0.9f));
        // burn node marker
        Vec2 nodeS=w2s(g,g.transferNode,cam,sc);
        bool aligned=std::fabs(wrapAngle(g.transferPhaseNow-g.transferPhaseReq))<0.12;
        Color nc=aligned?rgb(0x5ad08a):rgb(0xffb347);
        r->ring(nodeS,9,2.5f,nc); r->circle(nodeS,3,nc,true);
        r->textC("BURN", nodeS.x, nodeS.y-22, 1.0, nc);
    }
    // bodies
    for (int i=0;i<(int)g.world.bodies.size();i++){
        if (!g.bodyUnlocked(i)) continue;
        Body& b=g.world.bodies[i];
        Vec2 s=w2s(g,b.pos,cam,sc);
        float rad=std::max(2.5f,(float)(b.radius*sc));
        g.r->body(s.x,s.y,rad,std::max(rad*1.08f,rad+6),b.col1,b.col2,b.atmoCol,{g.world.sunDir.x,-g.world.sunDir.y},b.seed,b.isStar);
        r->textC(b.name, s.x, s.y-rad-16, 1.2, rgb(0xbcc6d2));
        if (i==g.targetBody) r->ring(s, rad+7, 2, rgb(0xffd24a), 48);
        else if (i!=g.domBody) r->textC("[ navigate ]", s.x, s.y+rad+8, 0.95, rgb(0x6f8196));
    }
    // trajectory
    drawTrajectorySegments(g, cam, sc, 2.0f, rgb(0x59d6ff).withA(0.85f));
    if (g.targetBody>=0 && g.closestValid){
        Vec2 cp=w2s(g,g.closestPoint,cam,sc), tp=w2s(g,g.closestTargetPoint,cam,sc);
        r->line(cp,tp,1.2f,rgb(0xffd24a).withA(0.55f),true);
        r->ring(cp,7,2,rgb(0x59d6ff),24);
        r->ring(tp,8,2,rgb(0xffd24a),24);
        r->textC("closest "+fmtKM(g.closestApproach), (cp.x+tp.x)*0.5, (cp.y+tp.y)*0.5-14, 1.0, rgb(0xffd24a));
    }
    // rocket marker
    Vec2 s=w2s(g,g.pos,cam,sc);
    Vec2 nd={(float)std::cos(g.heading),-(float)std::sin(g.heading)};
    r->tri({s.x+nd.x*9,s.y+nd.y*9},{s.x-nd.x*5-nd.y*5,s.y-nd.y*5+nd.x*5},{s.x-nd.x*5+nd.y*5,s.y-nd.y*5-nd.x*5},rgb(0xffd24a));

    drawHUD(g);
    r->textC("MAP VIEW  —  scroll to zoom  ·  click a body to navigate  ·  M to exit", r->W/2, 60, 1.1, rgb(0x9fd2ff));

    // transfer guidance panel
    if (g.targetBody>=0){
        const std::string& tn=g.world.bodies[g.targetBody].name;
        bool fixedTarget = g.world.bodies[g.targetBody].parentIdx<0;
        float px=14, py=r->H-178, pw=360;
        r->rectRound(px-4,py-6,pw,148,8,rgb(0x0c1118).withA(0.85f));
        r->text("NAVIGATE -> "+tn, px, py, 1.5, rgb(0xffd24a)); py+=22;
        if (g.transferValid){
            bool aligned=std::fabs(g.transferPhaseError)<0.12;
            r->text(std::string(fixedTarget?"Transfer burn: ":(g.transferDv>=0?"Prograde burn: ":"Retrograde burn: "))+fmt(std::fabs(g.transferDv),0)+" m/s  (~"+fmtTime(g.transferBurn)+")", px, py, 1.15, rgb(0xd6dde6)); py+=18;
            if (g.closestValid){ r->text("Projected closest: "+fmtKM(g.closestApproach)+" in "+fmtTime(g.closestApproachTime), px, py, 1.1, rgb(0xd6dde6)); py+=17; }
            r->text(aligned?"START BURN AT THE MARKER":"WAIT: coast until marker turns green", px, py, 1.18, aligned?rgb(0x5ad08a):rgb(0xffb347)); py+=18;
            r->text(fixedTarget?"Burn until projected closest moves onto target":"Burn "+std::string(g.transferDv>=0?"prograde":"retrograde")+"; stop when closest shrinks", px, py, 1.02, rgb(0x9fb0c4)); py+=15;
            r->text(fixedTarget?"Fine tune with short correction burns.":"Fine tune with short burns near apoapsis.", px, py, 1.02, rgb(0x9fb0c4));
        } else {
            if (g.closestValid){
                r->text("Projected closest: "+fmtKM(g.closestApproach)+" in "+fmtTime(g.closestApproachTime), px, py, 1.15, rgb(0xd6dde6)); py+=18;
                r->text("Adjust prograde/retrograde until the", px, py, 1.05, rgb(0x9fb0c4)); py+=15;
                r->text("closest marker lands on "+tn+".", px, py, 1.05, rgb(0x9fb0c4)); py+=18;
                r->text("No exact Hohmann plan from this orbit.", px, py, 1.0, rgb(0x6f8196));
            } else {
                r->text("Get into orbit around "+g.world.bodies[g.transferParent>=0?g.transferParent:g.domBody].name, px, py, 1.1, rgb(0x9fb0c4)); py+=15;
                r->text("first, then a transfer can be planned.", px, py, 1.1, rgb(0x9fb0c4));
            }
        }
    }
}

// ---------------- marketplace ----------------
inline void drawMarket(Game& g){
    Renderer* r=g.r;
    r->rect(0,0,r->W,r->H, rgb(0x05080d).withA(0.6f));
    float pw=620, ph=620, ox=r->W/2-pw/2, oy=r->H/2-ph/2;
    r->rectRound(ox,oy,pw,ph,12, rgb(0x0c121b).withA(0.99f));
    r->text("MARKETPLACE", ox+22, oy+18, 2.0, rgb(0x9fd2ff));
    r->text("Earn Astrobucks by reaching space, landing, and visiting new worlds.", ox+22, oy+44, 1.0, rgb(0x6f8196));
    std::string bal=fmt((double)g.money,0)+" AB";
    r->text(bal, ox+pw-28-r->textWidth(bal,1.7f), oy+20, 1.7, rgb(0xf2c14a));
    float y=oy+68;
    r->text("PARTS", ox+24, y, 0.9, rgb(0x6b7888)); y+=16;
    for (int t=0;t<PT_COUNT;t++){
        if (spec(t).cost<=0) continue;
        bool owned=g.unlocked[t];
        r->rectRound(ox+20, y, pw-40, 34, 6, rgb(0x141b24));
        r->text(spec(t).name, ox+32, y+6, 1.3, owned?rgb(0x6fe09a):rgb(0xeaf0f6));
        r->text(spec(t).desc, ox+32, y+22, 0.85, rgb(0x6f8196));
        if (owned) r->text("OWNED", ox+pw-104, y+13, 1.2, rgb(0x6fe09a));
        else { bool afford=g.money>=spec(t).cost;
            if (button(g, ox+pw-138, y+6, 116, 24, fmt((double)spec(t).cost,0)+" AB", afford) && afford) g.buyPart(t); }
        y+=38;
    }
    y+=8; r->text("DEEP SPACE NAVIGATION", ox+24, y, 0.9, rgb(0x6b7888)); y+=16;
    for (int i=0;i<(int)g.world.bodies.size();i++){
        if (g.world.bodies[i].cost<=0) continue;
        bool owned=g.bodyUnlocked(i);
        r->rectRound(ox+20, y, pw-40, 34, 6, rgb(0x141b24));
        r->text(g.world.bodies[i].name, ox+32, y+6, 1.3, owned?rgb(0x6fe09a):rgb(0xeaf0f6));
        r->text(g.world.bodies[i].desc, ox+32, y+22, 0.85, rgb(0x6f8196));
        if (owned) r->text("NAV DATA", ox+pw-124, y+13, 1.15, rgb(0x6fe09a));
        else { bool afford=g.money>=g.world.bodies[i].cost;
            if (button(g, ox+pw-138, y+6, 116, 24, fmt((double)g.world.bodies[i].cost,0)+" AB", afford) && afford) g.buyBody(i); }
        y+=38;
    }
    if (button(g, ox+pw-120, oy+ph-44, 100, 32, "CLOSE", true)) g.showMarket=false;
}

// ---------------- staging editor ----------------
inline void drawStaging(Game& g){
    Renderer* r=g.r;
    r->rect(0,0,r->W,r->H, rgb(0x05080d).withA(0.6f));
    if (g.autoStage && g.stagingDragPart<0) g.assignDefaultStages();
    float pw=560, ph=560, ox=r->W/2-pw/2, oy=r->H/2-ph/2;
    r->rectRound(ox,oy,pw,ph,12, rgb(0x0c121b).withA(0.99f));
    r->text("STAGING", ox+22, oy+18, 2.0, rgb(0x9fd2ff));
    r->text(g.autoStage?"AUTO: boosters, stack stages, legs, chute":"MANUAL: drag rows up/down or use - / +", ox+22, oy+44, 1.0, g.autoStage?rgb(0x6f8196):rgb(0xf2b24a));
    if (button(g, ox+pw-210, oy+12, 80, 26, "AUTO", g.autoStage)){ g.autoStage=true; g.assignDefaultStages(); }
    if (button(g, ox+pw-116, oy+12, 92, 26, "MANUAL", !g.autoStage)) g.autoStage=false;

    std::vector<int> rows;
    for (int i=0;i<(int)g.design.size();i++) if (Game::isActivatable(g.design[i].type)) rows.push_back(i);
    std::sort(rows.begin(), rows.end(), [&](int a,int b){
        int sa=g.design[a].stage<0?0:g.design[a].stage;
        int sb=g.design[b].stage<0?0:g.design[b].stage;
        if (sa!=sb) return sa<sb;
        return g.design[a].pos.y<g.design[b].pos.y;
    });
    int n=std::max(1,g.stageCount());
    float y=oy+66;
    r->text("FIRING / SEPARATION ORDER", ox+24, y, 0.9, rgb(0x6b7888)); y+=18;
    for (int s=0;s<n && y<oy+ph-338;s++){
        std::string names;
        for (auto& p : g.design) if (Game::isActivatable(p.type) && p.stage==s){ if(!names.empty()) names+=", "; names+=spec(p.type).name; }
        r->text(stageTitle(s), ox+26, y, 1.15, rgb(0xf2b24a));
        r->text(names.empty()?"(empty)":names, ox+176, y, 1.0, rgb(0xc6cfda));
        y+=22;
    }

    float listY=std::max(oy+206, y+24), rowH=28;
    r->text("DRAG PART ROWS  (top = launch, lower = later space presses)", ox+24, listY-18, 0.9, rgb(0x6b7888));
    int maxDrop = std::max(n+2, (int)rows.size()+1);
    if (g.stagingDragPart>=0 && !g.in.mouseDown){
        int st=(int)std::floor((g.in.my-listY)/rowH);
        st=std::max(0,std::min(maxDrop,st));
        if (g.stagingDragPart<(int)g.design.size()) g.design[g.stagingDragPart].stage=st;
        g.stagingDragPart=-1;
        g.autoStage=false;
    }
    int visible=0;
    for (int ri=0;ri<(int)rows.size();ri++){
        int i=rows[ri];
        if (listY+visible*rowH>oy+ph-58) break;
        float ry=listY+visible*rowH;
        int st=g.design[i].stage<0?0:g.design[i].stage;
        bool dragging=(g.stagingDragPart==i);
        bool hover=g.in.mx>=ox+22&&g.in.mx<=ox+pw-24&&g.in.my>=ry&&g.in.my<=ry+24;
        if (g.in.mousePressed && hover && g.in.mx<ox+pw-116){ g.stagingDragPart=i; g.autoStage=false; dragging=true; }
        if (!dragging){
            r->rectRound(ox+22, ry, pw-46, 24, 5, hover?rgb(0x233044):rgb(0x141b24));
            r->text("DRAG", ox+32, ry+7, 0.85, rgb(0x6f8196));
            r->text(std::string(spec(g.design[i].type).name)+"  ->  "+stageShort(st), ox+76, ry+5, 1.0, rgb(0xd6dde6));
            if (button(g, ox+pw-102, ry+2, 28, 20, "-") && st>0){ g.autoStage=false; g.design[i].stage=st-1; }
            if (button(g, ox+pw-68, ry+2, 28, 20, "+")){ g.autoStage=false; g.design[i].stage=st+1; }
        }
        visible++;
    }
    if (g.stagingDragPart>=0 && g.stagingDragPart<(int)g.design.size()){
        int target=(int)std::floor((g.in.my-listY)/rowH);
        target=std::max(0,std::min(maxDrop,target));
        float gy=(float)g.in.my-rowH*0.45f;
        r->rectRound(ox+22, gy, pw-46, 26, 5, rgb(0x2f7fd6));
        r->text(std::string(spec(g.design[g.stagingDragPart].type).name)+"  ->  "+stageShort(target), ox+34, gy+7, 1.0, rgb(0xffffff));
    }
    if (button(g, ox+pw-120, oy+ph-44, 100, 32, "CLOSE", true)) g.showStaging=false;
}

// ---------------- build ----------------
inline void drawBuild(Game& g){
    Renderer* r=g.r;
    // sky gradient backdrop
    r->rect(0,0,r->W,r->H,rgb(0x0a0f16));
    for (int i=0;i<6;i++) r->rect(0, r->H-120+i*20, r->W, 20, mix(rgb(0x101826),rgb(0x0a0f16),i/6.0f));
    drawStars(g,0.5f);
    // ground strip
    r->rect(0,r->H-46,r->W,46,rgb(0x14202a));

    // grid in build area
    auto b2s=[&](Vec2 w){ return w2s(g,w,g.buildCam,g.buildScale); };
    Color grid=rgb(0x1b2533);
    for (double gx=-12; gx<=12; gx+=1){ Vec2 a=b2s({gx,-4}),bb=b2s({gx,28}); if(a.x>150&&a.x<r->W) r->line(a,bb,gx==0?2.0:1.0, gx==0?rgb(0x2b3a4d):grid); }
    for (double gy=-4; gy<=28; gy+=1){ Vec2 a=b2s({-12,gy}),bb=b2s({12,gy}); r->line({std::max(150.0,a.x),a.y},bb,1.0,grid); }

    // rocket preview
    auto tf=[&](Vec2 l){ return b2s(l); };
    for (int pass=0;pass<2;pass++)
        for (auto& p : g.design){ bool radial=Game::isRadialPart(p.type);
            if ((pass==0)!=radial) continue; drawPart(g,p.type,p.pos,tf,g.buildScale,false,spec(p.type).isLeg); }
    drawBuildInterstageShrouds(g, tf);

    // ghost of selected part at the snapped attachment node
    if (g.in.mx>150 && g.in.my<r->H-70){
        Vec2 w=g.screenToBuild(g.in.mx,g.in.my), c;
        const PartSpec& s=spec(g.palette);
        if (g.findSnap(w,g.palette,c)){
            Vec2 a=b2s(c+Vec2{-s.w/2,s.h/2}), d=b2s(c+Vec2{s.w/2,-s.h/2});
            r->rectRound(a.x,a.y,d.x-a.x,d.y-a.y,3, rgb(0x39c06a).withA(0.28f));
            r->ring(b2s(c),6,2, rgb(0x39c06a));
            bool radial=Game::isRadialPart(g.palette);
            if (g.symmetry && radial && std::fabs(c.x)>0.1){ Vec2 m={-c.x,c.y};
                Vec2 a2=b2s(m+Vec2{-s.w/2,s.h/2}), d2=b2s(m+Vec2{s.w/2,-s.h/2});
                r->rectRound(a2.x,a2.y,d2.x-a2.x,d2.y-a2.y,3, rgb(0x39c06a).withA(0.2f)); }
        } else {
            r->ring(b2s(g.snapPos(w,g.palette)),6,2, rgb(0xd2503a));
        }
    }

    // ---- palette panel ----
    r->rect(0,0,150,r->H,rgb(0x0c121b).withA(0.96f));
    r->text("ROCKETSIM",12,12,1.7,rgb(0x9fd2ff));
    r->text("ASSEMBLY",12,32,1.1,rgb(0x6b7888));
    const char* cats[5]={"COMMAND","FUEL","ENGINES","AERO","UTILITY"};
    PartCat cc[5]={CAT_COMMAND,CAT_FUEL,CAT_ENGINE,CAT_AERO,CAT_UTILITY};
    float py=54;
    for (int ci=0; ci<5; ci++){
        r->text(cats[ci],12,py,1.0,rgb(0x6b7888)); py+=15;
        for (int t=0;t<PT_COUNT;t++) if (spec(t).cat==cc[ci]){
            bool lock=!g.unlocked[t];
            bool hover=g.in.mx>=10&&g.in.mx<=140&&g.in.my>=py&&g.in.my<=py+22;
            Color base=g.palette==t? rgb(0x2f7fd6):(lock?rgb(0x171c24):(hover?rgb(0x394452):rgb(0x222a34)));
            r->rectRound(10,py,130,22,5,base);
            r->text(spec(t).name,18,py+7,1.0, lock?rgb(0x596373):(g.palette==t?rgb(0xffffff):rgb(0xd6dde6)));
            if (lock){ std::string pr=fmt((double)spec(t).cost,0); r->text(pr, 137-r->textWidth(pr,0.9f), py+7, 0.9, rgb(0xf2c14a)); }
            if (hover && g.in.mouseReleased){ if(lock) g.showMarket=true; else g.palette=t; }
            py+=25;
        }
        py+=4;
    }

    // ---- stats ----
    double mass=0,fuelMass=0,thrust=0,thrustSea=0; int npart=0;
    for (auto& p : g.design){ const PartSpec& s=spec(p.type); mass+=s.dryMass+s.fuel+s.srbFuel; fuelMass+=s.fuel+s.srbFuel; npart++;
        // liftoff thrust = engines/SRB in the bottom section (no decoupler below)
    }
    // bottom-section thrust estimate
    std::vector<double> decY; for(auto&p:g.design) if(spec(p.type).isDecoupler) decY.push_back(p.pos.y);
    std::sort(decY.begin(),decY.end());
    double firstDec = decY.empty()? 1e18 : decY[0];
    for (auto& p : g.design){ const PartSpec& s=spec(p.type); if((s.isEngine||s.isSRB)&&p.pos.y<firstDec){ thrust+=s.thrust; thrustSea+=s.thrust*(s.ispSea/std::max(1.0,s.ispVac)); } }
    double twr = mass>0? thrustSea/(mass*G0) : 0;

    float sx=r->W-210, sy=12;
    r->rectRound(sx-10,sy-6,210,86,8,rgb(0x0c121b).withA(0.9f));
    r->text("MASS  "+fmt(mass/1000,1)+" t",sx,sy,1.2,rgb(0xd6dde6)); sy+=18;
    r->text("PARTS "+fmt((double)npart),sx,sy,1.2,rgb(0xd6dde6)); sy+=18;
    r->text("LIFTOFF TWR "+fmt(twr,2),sx,sy,1.2, twr>=1.2?rgb(0x6fe09a):rgb(0xff8a6a)); sy+=18;
    r->text(twr>=1.2?"Ready to fly":"Add thrust / drop mass",sx,sy,1.0, twr>=1.2?rgb(0x6fe09a):rgb(0xff8a6a));

    // ---- Astrobucks balance ----
    r->rectRound(r->W/2-80, 10, 160, 26, 6, rgb(0x0c121b).withA(0.92f));
    r->textC(fmt((double)g.money,0)+" ASTROBUCKS", r->W/2, 18, 1.2, rgb(0xf2c14a));

    // ---- bottom buttons ----
    float bh=40, by=r->H-bh-6, x=160;
    if (button(g,x,by,116,bh,"LAUNCH",true,1.6f)) g.launch(); x+=124;
    if (button(g,x,by,124,bh,"MARKETPLACE",g.showMarket,1.1f)){ g.showMarket=true; g.showStaging=false; } x+=132;
    if (button(g,x,by,100,bh,"STAGING",g.showStaging,1.2f)){ g.showStaging=true; g.showMarket=false; if(g.autoStage) g.assignDefaultStages(); } x+=108;
    if (button(g,x,by,104,bh, g.symmetry?"SYM: ON":"SYM: OFF",g.symmetry,1.1f)) g.symmetry=!g.symmetry; x+=112;
    if (button(g,x,by,80,bh,"CLEAR",false,1.2f)){ g.design.clear(); g.clearNavigation(); } x+=88;
    if (button(g,x,by,104,bh,"STOCK",false,1.2f)) g.loadStockRocket();
    r->text("Left-click places (snaps to ship)  -  right-click removes  -  scroll to zoom",
            165, by-18, 1.0, rgb(0x71808f));

    // help overlay
    if (g.showHelp){
        float pw=560,ph=246,ox=r->W/2-pw/2,oy=r->H/2-ph/2;
        r->rectRound(ox,oy,pw,ph,10,rgb(0x0c121b).withA(0.96f));
        r->text("WELCOME, FLIGHT DIRECTOR",ox+20,oy+18,1.8,rgb(0x9fd2ff));
        const char* lines[]={
            "Build from the parts on the left, then press LAUNCH.",
            "A starter ship is on the pad - just hit LAUNCH to fly it.",
            "",
            "FLIGHT: drag the THROTTLE, steer with A/D, SPACE to stage.",
            "Climb and curve east to fall into orbit. Open the MAP (M) and",
            "click a moon/planet to plan a transfer; time-warp while coasting.",
            "",
            "Earn ASTROBUCKS by reaching space, orbit, Luna & Duna, then",
            "unlock boosters, separators & legs in the MARKETPLACE.",
            "Set the firing order in STAGING.   Press H to toggle help."};
        for (int i=0;i<10;i++) r->text(lines[i],ox+20,oy+48+i*17,1.1,rgb(0xc6cfda));
        if (button(g,ox+pw-130,oy+ph-42,110,32,"GOT IT",true,1.3f)) g.showHelp=false;
    }

    if (g.showMarket) drawMarket(g);
    if (g.showStaging) drawStaging(g);
}

inline void drawCrashPanel(Game& g){
    Renderer* r=g.r;
    r->rect(0,0,r->W,r->H, rgb(0x140406).withA(0.35f));
    float pw=400, ph=140, ox=r->W/2-pw/2, oy=r->H/2-ph/2;
    r->rectRound(ox,oy,pw,ph,12, rgb(0x1c0e10).withA(0.97f));
    r->textC("ROCKET DESTROYED", r->W/2, oy+22, 1.9, rgb(0xff6a5a));
    if (button(g, ox+22, oy+66, 168, 46, "REVERT 30s", true, 1.5f)) g.revert30();
    if (button(g, ox+pw-190, oy+66, 168, 46, "TO HANGAR (R)", false, 1.3f)){ g.mode=MODE_BUILD; g.mapView=false; g.clearNavigation(); }
}

inline void render(Game& g){
    Renderer* r=g.r;
    if (g.mode==MODE_BUILD){ r->beginFrame(rgb(0x05080d)); drawBuild(g); r->flush(); return; }

    // flight background sky color by altitude
    const Body& B=g.world.bodies[g.domBody];
    double alt=(g.pos-B.pos).len()-B.radius;
    float dayf=0;
    Color sky=rgb(0x04060b);
    if (B.atmoH>0 && alt<B.atmoH){
        float t=(float)clampd(alt/B.atmoH,0,1);
        Color day=mix(B.atmoCol,rgb(0x0a1830),0.2f); day.a=1;
        sky=mix(day,rgb(0x04060b),t*t*0.6f+0.2f);
        dayf=1-t;
    }
    r->beginFrame(sky);

    if (g.mapView){ drawMap(g); if (g.showSettings) drawSettings(g); if (g.exploded) drawCrashPanel(g); r->flush(); return; }

    drawStars(g, 1.0f-dayf*0.9f);
    drawBodies(g, g.flightCam, g.flightScale);
    // faint orbit overlay in the world view
    if (g.showTrajectory) drawTrajectorySegments(g, g.flightCam, g.flightScale, 1.5f, rgb(0x59d6ff).withA(0.4f));
    if (g.showParticles) drawParticles(g, g.flightCam, g.flightScale);
    drawReentryPlasma(g);
    if (!g.exploded) drawRocketFlight(g);
    drawHUD(g);
    if (g.showSettings) drawSettings(g);
    if (g.exploded) drawCrashPanel(g);
    r->flush();
}

} // namespace gr
