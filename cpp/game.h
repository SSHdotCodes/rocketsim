// RocketSim game: build editor, flight physics, map view, HUD. Holds all state
// and is driven by main.cpp (one step()/render() per animation frame).
#pragma once
#include <vector>
#include <string>
#include <cstdlib>
#include <cstdio>
#include <emscripten.h>
#include "math.h"
#include "renderer.h"
#include "parts.h"
#include "world.h"

inline double frand(double a, double b) { return a + (b - a) * (std::rand() / (double)RAND_MAX); }

enum Mode { MODE_BUILD, MODE_FLIGHT };

struct PlacedPart { int type; Vec2 pos; int stage = -1; };   // build design; pos = centre, y up; stage -1 = auto

struct RPart {
    int type;
    Vec2 local;          // centre in rocket frame, y up
    bool alive = true;
    bool engineOn = false;
    double srbFuel = 0;  // SRB internal fuel
    int section = 0;     // fuel section (decoupler depth)
    int stage = 0;       // firing stage in the staging sequence
};

struct Particle { Vec2 pos, vel; double life, maxLife, size; Color col; bool additive; };

// Rolling flight-state snapshot for the "revert 30s" rewind after a crash. Only
// the values that change during flight are stored; part types/layout/stages and
// section capacities are fixed at launch.
struct Snapshot {
    double simTime, heading, angVel, throttle;
    Vec2 pos, vel, landedRel;
    bool sas, enginesEnabled, chuteDeployed, legsDeployed, landed;
    int landedBody, currentStage, warpIdx;
    std::vector<double> sectionFuel, srbFuel;
    std::vector<char> alive, engineOn;
};

struct Star { float x, y, b; };

// raw input shared with main.cpp
struct Input {
    bool left=false, right=false, throttleUp=false, throttleDown=false;
    bool throttleMax=false, throttleZero=false;
    double mx=0, my=0;
    bool mouseDown=false, mousePressed=false, mouseReleased=false;
    double wheel=0;
    // edge-triggered actions (set true for one frame by main.cpp)
    bool stage=false, sasToggle=false, mapToggle=false, deployChute=false;
    bool warpUp=false, warpDown=false, launch=false, revert=false, help=false;
    bool rightPressed=false;     // right mouse button (build: remove part)
};

class Game {
public:
    Renderer* r = nullptr;
    World world;
    Mode mode = MODE_BUILD;
    Input in;

    // ---- build ----
    std::vector<PlacedPart> design;
    int palette = PT_POD;
    bool symmetry = true;
    Vec2 buildCam{0, 7};
    double buildScale = 24;
    bool showHelp = true;

    // ---- options / overlays ----
    bool showSettings = false, showMinimap = true, showTrajectory = true, showParticles = true;
    // ---- navigation / transfer guidance ----
    int targetBody = -1;
    bool transferValid = false;
    double transferDv = 0, transferBurn = 0, transferPhaseNow = 0, transferPhaseReq = 0, transferPhaseError = 0;
    bool closestValid = false;
    double closestApproach = 0, closestApproachTime = 0;
    Vec2 closestPoint{0,0}, closestTargetPoint{0,0};
    Vec2 transferNode{0,0};        // world point on the current orbit to start the burn
    int transferParent = 0;

    // ---- economy ----
    int money = 2000;
    bool unlocked[PT_COUNT];
    int bodyUnlocks = 0;
    int milestones = 0;              // bitmask of achieved milestones
    bool showMarket = false, showStaging = false, autoStage = true;
    int selectedPart = -1;          // build: part selected for staging tweaks
    int stagingDragPart = -1;

    // ---- flight ----
    std::vector<RPart> parts;
    int currentStage = 0;           // next stage to fire (0 fired automatically at launch)
    int nStages = 1;
    Vec2 pos{0,0}, vel{0,0};
    double heading = PI/2, angVel = 0;
    double throttle = 1.0;
    bool sas = true;
    bool enginesEnabled = true;       // master cutoff for throttleable (liquid) engines
    bool draggingThrottle = false;
    bool chuteDeployed = false;
    bool legsDeployed = false;
    bool landed = false, exploded = false, reachedSpace = false;
    int landedBody = 0; Vec2 landedRel{0,0};
    std::vector<double> sectionFuel, sectionFuelMax;
    int nSections = 1;
    double simTime = 0;
    bool mapView = false;
    int warpIdx = 0;
    static constexpr int WARP_COUNT = 9;
    const double warps[WARP_COUNT] = {1, 2, 3, 5, 25, 100, 1000, 10000, 100000};

    Vec2 flightCam{0,0};
    double flightScale = 9.0;
    double mapScale = 4e-4;
    bool followMap = true;
    Vec2 mapCam{0,0};

    std::vector<Particle> particles;
    std::vector<Star> stars;
    std::vector<Snapshot> history;   // rolling buffer for revert-30s
    double lastSnap = -100;

    std::string toast; double toastT = 0;
    std::vector<Vec2> trajectory; int trajTimer = 0;
    double apo=0, peri=0, ecc=0; bool hyperbolic=false; int domBody=0;
    double reentryHeat = 0;

    void init(Renderer* rr) {
        r = rr;
        world.build();
        std::srand(20260618u);
        stars.reserve(440);
        for (int i = 0; i < 440; i++)
            stars.push_back({(float)frand(0,2400), (float)frand(0,2400), (float)frand(0.25,1.0)});
        loadEconomy();
        loadStockRocket();
    }

    // ---- economy (Astrobucks) persisted in localStorage ----
    void loadEconomy(){
        for (int i=0;i<PT_COUNT;i++) unlocked[i] = (spec(i).cost==0);
        money = EM_ASM_INT({ var s=localStorage.getItem('rs_money'); return s===null?2000:(parseInt(s)||0); });
        int um = EM_ASM_INT({ var s=localStorage.getItem('rs_unlocked'); return s===null?0:(parseInt(s)||0); });
        int bm = EM_ASM_INT({ var s=localStorage.getItem('rs_body_unlocked'); return s===null?0:(parseInt(s)||0); });
        milestones = EM_ASM_INT({ var s=localStorage.getItem('rs_miles'); return s===null?0:(parseInt(s)||0); });
        for (int i=0;i<PT_COUNT;i++) if (um & (1<<i)) unlocked[i]=true;
        bodyUnlocks = bm;
        for (int i=0;i<(int)world.bodies.size() && i<30;i++)
            if (world.bodies[i].cost<=0) bodyUnlocks |= (1<<i);
    }
    void saveEconomy(){
        int um=0; for(int i=0;i<PT_COUNT;i++) if(unlocked[i]) um|=(1<<i);
        EM_ASM({ try{ localStorage.setItem('rs_money',String($0)); localStorage.setItem('rs_unlocked',String($1)); localStorage.setItem('rs_miles',String($2)); localStorage.setItem('rs_body_unlocked',String($3));}catch(e){} }, money, um, milestones, bodyUnlocks);
    }
    bool buyPart(int t){
        if (unlocked[t]) return true;
        if (money < spec(t).cost){ setToast("Not enough Astrobucks"); return false; }
        money -= spec(t).cost; unlocked[t]=true; saveEconomy();
        setToast(std::string("Unlocked ")+spec(t).name); return true;
    }
    bool bodyUnlocked(int i) const { return i>=0 && i<(int)world.bodies.size() && i<30 && ((bodyUnlocks & (1<<i)) || world.bodies[i].cost<=0); }
    bool buyBody(int i){
        if (i<0 || i>=(int)world.bodies.size()) return false;
        if (bodyUnlocked(i)) return true;
        int cost = world.bodies[i].cost;
        if (money < cost){ setToast("Not enough Astrobucks"); return false; }
        money -= cost; bodyUnlocks |= (1<<i); saveEconomy();
        setToast("Navigation unlocked: "+world.bodies[i].name); return true;
    }
    void clearNavigation(){
        targetBody=-1; transferValid=false; closestValid=false;
        transferDv=transferBurn=transferPhaseNow=transferPhaseReq=transferPhaseError=0;
        closestApproach=closestApproachTime=0; transferParent=0;
    }
    void reward(int amt, const std::string& why){ money+=amt; saveEconomy(); setToast("+"+std::to_string(amt)+" AB - "+why); }
    void checkMilestones(){
        if (exploded) return;
        const Body& B = world.bodies[domBody];
        double alt = (pos-B.pos).len()-B.radius;
        if (domBody==0 && alt>70000 && !(milestones&1)){ milestones|=1; reward(1500,"reached space"); }
        if (domBody==0 && !landed && !hyperbolic && peri>70000 && apo>70000 && !(milestones&2)){ milestones|=2; reward(4000,"first orbit!"); }
        if (landed && landedBody==0 && (milestones&1) && !(milestones&4)){ milestones|=4; reward(2000,"safe landing"); }
        if (domBody==1 && !(milestones&8)){ milestones|=8; reward(6000,"reached Luna"); }
        if (landed && landedBody==1 && !(milestones&16)){ milestones|=16; reward(12000,"landed on Luna!"); }
        if (domBody==2 && !(milestones&32)){ milestones|=32; reward(10000,"reached Duna"); }
        if (landed && landedBody==2 && !(milestones&64)){ milestones|=64; reward(20000,"landed on Duna!"); }
        for (int i=3;i<(int)world.bodies.size() && i<12;i++){
            int reachBit = 1 << (7 + i*2);
            int landBit = 1 << (8 + i*2);
            if (domBody==i && bodyUnlocked(i) && !(milestones&reachBit)){
                milestones |= reachBit; reward(9000 + i*1800, "reached "+world.bodies[i].name);
            }
            if (landed && landedBody==i && bodyUnlocked(i) && !world.bodies[i].isStar && !(milestones&landBit)){
                milestones |= landBit; reward(18000 + i*3000, "landed on "+world.bodies[i].name);
            }
        }
    }

    // ============================ BUILD ============================
    void loadStockRocket() {
        // punchy single-stage starter (TWR ~2.5) using only unlocked parts; orbit-capable
        design.clear();
        clearNavigation();
        auto add = [&](int t, double x, double y){ design.push_back({t, {x,y}, -1}); };
        add(PT_ENGINE_S, 0, 0.8);
        add(PT_TANK_S,   0, 3.0);
        add(PT_TANK_S,   0, 5.8);
        add(PT_FIN,  1.45, 1.6); add(PT_FIN, -1.45, 1.6);
        add(PT_POD,      0, 7.95);
        add(PT_CHUTE,    0, 9.15);
        autoStage = true;
        frameBuildCam();
    }
    void frameBuildCam() {
        if (design.empty()) { buildCam = {0,7}; return; }
        double minY=1e9,maxY=-1e9;
        for (auto& p : design) { minY=std::min(minY,p.pos.y-spec(p.type).h/2); maxY=std::max(maxY,p.pos.y+spec(p.type).h/2); }
        buildCam = {0, (minY+maxY)/2};
        double span = std::max(8.0, maxY-minY+6);
        buildScale = clampd((r->H*0.8)/span, 6, 60);
    }

    Vec2 snapPos(Vec2 world, int type) {
        // snap centre to a 0.5 m grid; core parts lock to x=0 column
        const PartSpec& s = spec(type);
        bool radial = isRadialPart(type);
        double gx = std::round(world.x*2)/2.0;
        double gy = std::round(world.y*2)/2.0;
        if (!radial) gx = 0;
        return {gx, gy};
    }
    bool overlaps(Vec2 c, int type, int ignore=-1) {
        const PartSpec& s = spec(type);
        for (int i=0;i<(int)design.size();i++){
            if (i==ignore) continue;
            const PartSpec& o = spec(design[i].type);
            Vec2 d = c - design[i].pos;
            if (std::fabs(d.x) < (s.w+o.w)/2 - 0.15 && std::fabs(d.y) < (s.h+o.h)/2 - 0.15)
                return true;
        }
        return false;
    }
    bool connected(Vec2 c, int type) {
        if (design.empty()) return true;
        const PartSpec& s = spec(type);
        for (auto& p : design) {
            const PartSpec& o = spec(p.type);
            Vec2 d = c - p.pos;
            if (std::fabs(d.x) < (s.w+o.w)/2 + 0.3 && std::fabs(d.y) < (s.h+o.h)/2 + 0.3)
                return true;
        }
        return false;
    }
    int partAt(Vec2 world) {
        for (int i=(int)design.size()-1;i>=0;i--){
            const PartSpec& s = spec(design[i].type);
            Vec2 d = world - design[i].pos;
            if (std::fabs(d.x) < s.w/2 && std::fabs(d.y) < s.h/2) return i;
        }
        return -1;
    }

    // Snap a new part to the nearest free attachment node (stack above/below for
    // core parts, beside a core part for radial parts) instead of rejecting it
    // when the raw cursor position overlaps something. This makes building feel
    // like SFS/KSP: parts click into place against the rocket.
    bool findSnap(Vec2 w, int type, Vec2& out) {
        const PartSpec& s = spec(type);
        bool radial = isRadialPart(type);
        bool payload = isSeparatorPayload(type);
        if (design.empty()) { out = snapPos(w,type); return !overlaps(out,type); }
        std::vector<Vec2> cands;
        for (auto& p : design) {
            const PartSpec& o = spec(p.type);
            bool pradial = isRadialPart(p.type);
            if (!radial) {
                if (pradial) continue;
                cands.push_back({0.0, p.pos.y + o.h/2 + s.h/2});   // stack on top
                cands.push_back({0.0, p.pos.y - o.h/2 - s.h/2});   // stack below
            } else if (payload && p.type==PT_SEP && std::fabs(p.pos.x)>0.1) {
                double side = p.pos.x >= 0 ? 1.0 : -1.0;
                double dx = o.w/2 + s.w/2;
                cands.push_back({p.pos.x + side*dx, p.pos.y});      // attach outside separator
            } else if (!pradial) {
                double dx = o.w/2 + s.w/2;
                cands.push_back({p.pos.x + dx, p.pos.y});          // attach right
                cands.push_back({p.pos.x - dx, p.pos.y});          // attach left
            }
        }
        double bestD=1e18; bool found=false; Vec2 best;
        for (auto& c : cands) {
            if (overlaps(c,type)) continue;
            double d=(c-w).len2();
            if (d<bestD){ bestD=d; best=c; found=true; }
        }
        if (found) { out=best; return true; }
        Vec2 g=snapPos(w,type);                                    // fallback: free grid cell
        if (!overlaps(g,type)) { out=g; return true; }
        return false;
    }

    void deletePart(int hit) {
        if (hit<0 || hit>=(int)design.size()) return;
        PlacedPart victim=design[hit];
        design.erase(design.begin()+hit);
        const PartSpec& vs=spec(victim.type);
        if (isRadialPart(victim.type) && std::fabs(victim.pos.x)>0.1) {     // remove mirror too
            for (int i=0;i<(int)design.size();i++)
                if (design[i].type==victim.type && std::fabs(design[i].pos.x+victim.pos.x)<0.2 && std::fabs(design[i].pos.y-victim.pos.y)<0.2){ design.erase(design.begin()+i); break; }
        }
    }

    // ============================ FLIGHT ============================
    static bool isRadialPart(int t){ const PartSpec& s=spec(t); return s.isSRB||s.isFin||s.isSep||s.isLeg||t==PT_RCS; }
    static bool isSeparatorPayload(int t){ const PartSpec& s=spec(t); return s.isSRB||s.isFin||t==PT_RCS; }
    static bool isActivatable(int t){ const PartSpec& s=spec(t); return s.isEngine||s.isSRB||s.isDecoupler||s.isSep||s.isLeg||s.isChute; }

    // Default staging from geometry: depth = #separation parts strictly below.
    // Liftoff ignites stage 0. Side boosters are jettisoned before stack
    // decouplers, then the next stack engine starts with its decoupler event.
    void assignDefaultStages(){
        std::vector<double> decY;
        for (auto& p : design) if (spec(p.type).isDecoupler) decY.push_back(p.pos.y);
        std::sort(decY.begin(), decY.end());
        auto depthAt = [&](double y){ int depth=0; for (double dy:decY) if (dy < y - 1e-6) depth++; return depth; };

        auto stackStage = [&](int depth, int stackBase){ return depth<=0 ? 0 : stackBase + depth - 1; };
        auto assignStack = [&](int stackBase){
            for (auto& p : design){
                if (!isActivatable(p.type)){ p.stage=-1; continue; }
                const PartSpec& s=spec(p.type);
                int depth = depthAt(p.pos.y);
                if (s.isEngine||s.isSRB) p.stage = stackStage(depth, stackBase);
                else if (s.isDecoupler) p.stage = stackBase + depth;
                else p.stage = -1;
            }
        };
        auto heldPayloadStage = [&](const PlacedPart& sep){
            if (std::fabs(sep.pos.x)<=0.1) return -1;
            double side = sep.pos.x>=0 ? 1.0 : -1.0;
            double best = 1e18; int stage = -1;
            for (auto& p : design){
                if (!isSeparatorPayload(p.type) || p.stage<0) continue;
                if (p.pos.x*side <= sep.pos.x*side + 0.05) continue;
                double reach = (spec(p.type).h + spec(sep.type).h)*0.5 + 0.75;
                double dy = std::fabs(p.pos.y - sep.pos.y);
                if (dy > reach) continue;
                double d = dy + std::fabs(p.pos.x - sep.pos.x)*0.2;
                if (d < best){ best=d; stage=p.stage; }
            }
            return stage;
        };
        auto assignSeparators = [&](int stackBase){
            for (auto& sep : design){
                if (!spec(sep.type).isSep) continue;
                int heldStage = heldPayloadStage(sep);
                sep.stage = heldStage>=0 ? heldStage+1 : stackStage(depthAt(sep.pos.y), stackBase)+1;
            }
        };

        assignStack(1);
        assignSeparators(1);
        bool firstEventIsBoosters = false;
        for (auto& p : design) if (spec(p.type).isSep && p.stage==1) firstEventIsBoosters = true;

        int stackBase = firstEventIsBoosters ? 2 : 1;
        assignStack(stackBase);
        assignSeparators(stackBase);

        int last = 0;
        for (auto& p : design) if (isActivatable(p.type) && !spec(p.type).isLeg && !spec(p.type).isChute && p.stage>=0) last = std::max(last, p.stage);
        bool hasLeg=false, hasChute=false;
        for (auto& p : design){ hasLeg = hasLeg || spec(p.type).isLeg; hasChute = hasChute || spec(p.type).isChute; }
        for (auto& p : design){
            if (spec(p.type).isLeg) p.stage = last + 1;
            if (spec(p.type).isChute) p.stage = last + (hasLeg ? 2 : 1);
        }
    }
    int stageCount(){ int n=1; for (auto& p : design) if (isActivatable(p.type)&&p.stage>=0) n=std::max(n,p.stage+1); return n; }

    void launch() {
        if (design.empty()) { setToast("Build a rocket first!"); return; }
        bool hasPod=false; for(auto&p:design) if(spec(p.type).isPod) hasPod=true;
        if (!hasPod) { setToast("Add a Command Pod!"); return; }
        if (autoStage) assignDefaultStages();
        clearNavigation();

        parts.clear();
        for (auto& p : design) { RPart rp; rp.type=p.type; rp.local=p.pos; rp.alive=true;
            rp.stage = p.stage<0 ? 0 : p.stage;
            if (spec(p.type).isSRB) rp.srbFuel = spec(p.type).srbFuel; parts.push_back(rp); }

        // fuel sections (decouplers only; separators are radial and don't split fuel)
        std::vector<double> decY;
        for (auto& rp : parts) if (spec(rp.type).isDecoupler) decY.push_back(rp.local.y);
        std::sort(decY.begin(), decY.end());
        nSections = (int)decY.size()+1;
        for (auto& rp : parts) { int s=0; for (double dy:decY) if (dy<rp.local.y-1e-6) s++; rp.section=s; }
        sectionFuelMax.assign(nSections,0); sectionFuel.assign(nSections,0);
        for (auto& rp : parts) if (spec(rp.type).isTank){
            sectionFuelMax[rp.section]+=spec(rp.type).fuel; sectionFuel[rp.section]+=spec(rp.type).fuel; }

        nStages = 1; for (auto& rp : parts) if (isActivatable(rp.type)) nStages = std::max(nStages, rp.stage+1);
        currentStage = 0; activateStage(0); currentStage = 1;   // ignite first stage at liftoff

        double bottom = 0; for (auto& rp : parts) bottom = std::min(bottom, rp.local.y - spec(rp.type).h/2);
        pos = world.bodies[0].pos + Vec2{0,1}*(world.bodies[0].radius - bottom);
        vel = {0,0}; heading = PI/2; angVel=0;
        throttle=1.0; sas=true; enginesEnabled=true; draggingThrottle=false; chuteDeployed=false; legsDeployed=false; landed=true; exploded=false; reachedSpace=false;
        landedBody=0; landedRel = pos - world.bodies[0].pos;
        warpIdx=0; mapView=false; showSettings=showMarket=showStaging=false; particles.clear(); trajectory.clear(); reentryHeat=0;
        history.clear(); lastSnap=-100;
        flightScale=9.0; mode=MODE_FLIGHT; simTime=0;
        setToast("LIFTOFF! Hold full throttle, steer east (D) past 8 km");
    }

    // ---- mass properties ----
    double partFuel(int i) {
        const PartSpec& s = spec(parts[i].type);
        if (s.isTank) { int sec=parts[i].section; double cap=sectionFuelMax[sec];
            return cap>0 ? sectionFuel[sec]*(s.fuel/cap) : 0; }
        if (s.isSRB) return parts[i].srbFuel;
        return 0;
    }
    double totalMass() { double m=0; for (int i=0;i<(int)parts.size();i++) if (parts[i].alive){ m+=spec(parts[i].type).dryMass+partFuel(i);} return m; }
    Vec2 comLocal() { double m=0; Vec2 c{0,0};
        for (int i=0;i<(int)parts.size();i++) if (parts[i].alive){ double pm=spec(parts[i].type).dryMass+partFuel(i); c+=parts[i].local*pm; m+=pm; }
        return m>0 ? c/m : Vec2{0,0}; }
    double inertia(Vec2 com,double m){ double I=0; for (int i=0;i<(int)parts.size();i++) if (parts[i].alive){
        double pm=spec(parts[i].type).dryMass+partFuel(i); double rr=(parts[i].local-com).len2();
        const PartSpec& s=spec(parts[i].type); I+=pm*(rr + (s.w*s.w+s.h*s.h)/12.0);} return std::max(I,1.0); }

    bool anyAlive(bool(*pred)(const PartSpec&)) { for (auto&p:parts) if(p.alive&&pred(spec(p.type))) return true; return false; }
    bool hasChute(){ for(auto&p:parts) if(p.alive&&spec(p.type).isChute) return true; return false; }
    bool hasLegs(){ for(auto&p:parts) if(p.alive&&spec(p.type).isLeg) return true; return false; }

    void deployLegs(){
        if (!hasLegs()){ setToast("No landing legs"); return; }
        legsDeployed=true;
        setToast("Landing legs deployed");
    }
    void deployParachute(){
        if (!hasChute()){ setToast("No parachute"); return; }
        chuteDeployed=true;
        setToast("Parachute deployed");
    }
    void cutParachutes(){
        bool cut=false;
        for (auto& p:parts) if (p.alive&&spec(p.type).isChute){ p.alive=false; cut=true; spawnSep(p); }
        chuteDeployed=false;
        setToast(cut?"Parachute cut":"No parachute");
    }
    void toggleParachute(){
        if (chuteDeployed) cutParachutes();
        else deployParachute();
    }

    void doStage() {
        if (currentStage >= nStages){
            if (hasLegs()&&!legsDeployed) deployLegs();
            else if (hasChute()&&!chuteDeployed) deployParachute();
            return;
        }
        activateStage(currentStage);
        currentStage++;
    }
    void activateStage(int s){
        bool ignited=false, deployLeg=false, deployChute=false;
        for (int i=0;i<(int)parts.size();i++){
            if (!parts[i].alive || parts[i].stage!=s) continue;
            const PartSpec& sp=spec(parts[i].type);
            if (sp.isEngine||sp.isSRB){ parts[i].engineOn=true; ignited=true; }
            else if (sp.isDecoupler) fireDecoupler(i);
            else if (sp.isSep) fireSeparator(i);
            else if (sp.isLeg) deployLeg=true;
            else if (sp.isChute) deployChute=true;
        }
        if (deployLeg) deployLegs();
        if (deployChute) deployParachute();
        if (ignited) enginesEnabled=true;
        for (int sec=0;sec<nSections && sec<(int)sectionFuel.size();sec++){ bool tank=false;
            for (auto& p : parts) if (p.alive && p.section==sec && spec(p.type).isTank) tank=true;
            if (!tank) sectionFuel[sec]=0; }
    }
    void fireDecoupler(int di){     // drop the whole stack below this point
        double dy = parts[di].local.y;
        for (auto& p : parts) if (p.alive && p.local.y < dy-1e-6){ p.alive=false; spawnSep(p); }
        parts[di].alive=false; spawnSep(parts[di]);
        setToast("Stage separated");
    }
    void fireSeparator(int si){     // drop the side boosters/fins it holds
        Vec2 sp = parts[si].local;
        double side = sp.x>=0 ? 1.0 : -1.0;
        bool radialSep = std::fabs(sp.x)>0.1;
        bool dropped = false;
        for (int i=0;i<(int)parts.size();i++){
            if (i==si || !parts[i].alive || !isSeparatorPayload(parts[i].type)) continue;
            bool held = false;
            if (radialSep){
                const PartSpec& ps=spec(parts[i].type);
                bool sameSide = parts[i].local.x*side > sp.x*side + 0.05;
                double reach = (ps.h + spec(parts[si].type).h)*0.5 + 0.75;
                held = sameSide && std::fabs(parts[i].local.y - sp.y) <= reach;
            } else {
                held = parts[i].local.y < sp.y + 2.5;
            }
            if (held){ parts[i].alive=false; spawnSep(parts[i]); dropped=true; }
        }
        parts[si].alive=false; spawnSep(parts[si]);
        setToast(dropped ? "Boosters jettisoned" : "Separator fired");
    }
    void spawnSep(RPart& p){ Vec2 w = l2w(p.local); for(int k=0;k<6;k++){ Particle pt; pt.pos=w; pt.vel=vel+fromAngle(frand(0,TAU),frand(2,12)); pt.life=pt.maxLife=frand(0.4,1.0); pt.size=frand(2,5); pt.col=Color(0.8f,0.8f,0.85f,1); pt.additive=false; particles.push_back(pt);} }

    Vec2 l2w(Vec2 l){ double s=std::sin(heading), c=std::cos(heading);
        return pos + Vec2{ l.x*s + l.y*c, -l.x*c + l.y*s }; }
    Vec2 noseDir(){ return fromAngle(heading); }

    // ---------------- physics ----------------
    void integrate(double dt) {
        world.update(simTime);
        domBody = world.dominant(pos);
        const Body& B = world.bodies[domBody];
        Vec2 toC = B.pos - pos; double r = toC.len();
        Vec2 radialOut = (pos - B.pos).norm();
        double alt = r - B.radius;
        if (alt > 70000) reachedSpace = true;

        double mass = totalMass();
        if (mass < 1) { explode(); return; }
        Vec2 com = comLocal();
        double I = inertia(com, mass);
        double press = world.pressure(domBody, pos);

        // ---- thrust + fuel ----
        Vec2 thrustDir = noseDir();
        double thrustMag = 0, torque = 0;
        // D/right tilts the nose right (east), A/left tilts it left (west)
        double ctl = (in.left?1.0:0.0) - (in.right?1.0:0.0);
        double gimbal = 0;
        for (int i=0;i<(int)parts.size();i++){
            if (!parts[i].alive || !parts[i].engineOn) continue;
            const PartSpec& s = spec(parts[i].type);
            if (s.isSRB){
                if (parts[i].srbFuel<=0) continue;
                double th = s.thrust;
                double mdot = th/(engineIsp(s,press)*G0);
                parts[i].srbFuel = std::max(0.0, parts[i].srbFuel - mdot*dt);
                thrustMag += th;
                torque += gimbalTorque(parts[i],com,th,ctl)*0.0; // SRBs don't gimbal
            } else if (s.isEngine){
                int sec=parts[i].section;
                if (!enginesEnabled || sec>=nSections || sectionFuel[sec]<=0) continue;
                double th = s.thrust*throttle;
                double mdot = th/(engineIsp(s,press)*G0);
                double used = std::min(sectionFuel[sec], mdot*dt);
                sectionFuel[sec]-=used;
                thrustMag += th*(mdot*dt>0? used/(mdot*dt):1.0);
                torque += gimbalTorque(parts[i],com,th,ctl);
            }
        }
        bool thrusting = thrustMag>1;

        // ---- accel ----
        Vec2 grav = world.gravityAt(pos);
        Vec2 acc = grav + thrustDir*(thrustMag/mass);
        // drag
        double rho = world.density(domBody, pos);
        Vec2 vair = vel - B.vel;           // atmosphere co-moving with body
        double sp = vair.len();
        if (rho>0 && sp>0.1){
            double area=0, cd=0; for(auto&p:parts) if(p.alive){ area+=spec(p.type).dragArea; }
            cd = 0.42;
            // each deployed parachute adds a large canopy; more chutes = heavier
            // payloads land softly (KSP/SFS-style)
            if (chuteDeployed && alt < B.atmoH){
                int nch=0; for(auto&p:parts) if(p.alive&&spec(p.type).isChute) nch++;
                if (nch>0){ area += 520.0*nch; cd = 1.5; }
            }
            double drag = 0.5*rho*sp*sp*cd*area;
            // soft cap so chute can't produce absurd spikes
            double maxA = (chuteDeployed?60.0:200.0)*mass;
            drag = std::min(drag, maxA);
            acc += (vair*(-1.0/sp))*(drag/mass);
        }

        // ---- rotation ----
        double rwTorque=0; for(auto&p:parts) if(p.alive) rwTorque+=spec(p.type).torque;
        double angAcc = torque/I + ctl*(rwTorque/I);
        // aero weathervane via fins
        if (rho>0 && sp>30){
            bool fins = anyAlive([](const PartSpec&s){return s.isFin;});
            double q = 0.5*rho*sp*sp;
            double cross = noseDir().cross(vair.norm());   // + => need + torque to align
            double finArea=0; for(auto&p:parts) if(p.alive&&spec(p.type).isFin) finArea+=spec(p.type).dragArea;
            double k = (fins? 0.9 : 0.18) * (finArea+0.4);
            angAcc += k*q*cross/I;
            angAcc -= angVel*std::min(1.0, q*0.0008+ (fins?0.02:0.004))*8.0/I*0.0; // mild damping handled below
        }
        // SAS
        if (sas && std::fabs(ctl)<0.01){
            double damp = (rwTorque/I) + (thrusting? 1.2:0.0);
            double target = -angVel/std::max(dt,1e-3);
            double maxAcc = damp;
            angAcc += clampd(target, -maxAcc, maxAcc);
        }
        angVel += angAcc*dt;
        angVel = clampd(angVel, -3.5, 3.5);
        angVel *= (1.0 - std::min(0.5, 0.6*dt)); // gentle global damping
        heading += angVel*dt;

        // ---- landed handling ----
        if (landed){
            // sit on body; relaunch if thrust overcomes gravity upward
            const Body& LB = world.bodies[landedBody];
            pos = LB.pos + landedRel; vel = LB.vel;
            Vec2 up = (pos-LB.pos).norm();
            double upThrust = thrustDir.dot(up)*(thrustMag/mass);
            double g = world.bodies[landedBody].mu/((pos-LB.pos).len2());
            if (thrusting && upThrust > g*1.001){ landed=false; }
            else { return; }
        }

        // velocity Verlet (gravity recomputed at new pos)
        Vec2 a0 = acc;
        Vec2 newPos = pos + vel*dt + a0*(0.5*dt*dt);
        Vec2 grav1 = world.gravityAt(newPos);
        Vec2 a1 = grav1 + thrustDir*(thrustMag/mass);
        // (drag/rotation already folded into a0; for coast steps a0≈a1 dominated by gravity)
        Vec2 newVel = vel + (a0+a1)*(0.5*dt);
        pos = newPos; vel = newVel;
        simTime += dt;

        checkGround();
    }

    double gimbalTorque(RPart& e, Vec2 com, double th, double ctl){
        const PartSpec& s = spec(e.type);
        if (s.gimbal<=0) return 0;
        double lever = (com.y - e.local.y);     // engine below com => lever>0
        // Same sign as the reaction-wheel term (ctl*rwTorque) so the gimbal ASSISTS
        // steering instead of fighting it — otherwise A/D invert when the engine fires.
        return ctl * th * std::sin(s.gimbal) * lever;
    }

    void checkGround(){
        for (int b=0;b<(int)world.bodies.size();b++){
            const Body& B = world.bodies[b];
            double r = (pos-B.pos).len();
            // lowest point of rocket
            double bottom = 0; for(auto&p:parts) if(p.alive) bottom=std::min(bottom,p.local.y);
            double off = -bottom + 0.4;
            if (r <= B.radius + off + 1.0){
                Vec2 up = (pos-B.pos).norm();
                Vec2 vrel = vel - B.vel;
                double vdown = -vrel.dot(up);
                double lateral = (vrel + up*vdown).len();
                double speed = vrel.len();
                // Only a descending (or fast) approach counts as touchdown — this
                // lets a thrusting rocket climb out of the contact band at liftoff
                // instead of being re-snapped to the pad every substep.
                if (vdown < 0.1 && speed < 30) return;
                bool upright = noseDir().dot(up) > 0.78;
                bool chuteLanding = chuteDeployed && hasChute() && B.atmoH > 0 && world.pressure(b,pos) > 0.01;
                bool legsReady = legsDeployed && hasLegs();
                double verticalLimit = legsReady? 18.0 : 8.0;
                double lateralLimit = legsReady? 14.0 : 6.0;
                double totalLimit = legsReady? 22.0 : 10.0;
                if (chuteLanding){
                    verticalLimit = std::max(verticalLimit, 28.0);
                    lateralLimit = std::max(lateralLimit, 20.0);
                    totalLimit = std::max(totalLimit, 34.0);
                }
                bool softEnough = vdown < verticalLimit && lateral < lateralLimit && speed < totalLimit;
                bool attitudeOk = upright || chuteLanding || speed < 4.0;
                if (softEnough && attitudeOk){
                    landed=true; landedBody=b;
                    pos = B.pos + up*(B.radius+off); vel = B.vel; angVel=0;
                    heading = up.angle();
                    landedRel = pos - B.pos;
                    if (!toastIs("LANDED")) {
                        if (b==0) setToast("LANDED on Terra");
                        else if (b==1) setToast("THE EAGLE HAS LANDED on Luna!");
                        else setToast("LANDED on Duna!");
                    }
                } else {
                    explode();
                }
                return;
            }
        }
    }

    void explode(){
        if (exploded) return;
        exploded=true; landed=false;
        for (int k=0;k<80;k++){ Particle p; p.pos=pos+fromAngle(frand(0,TAU),frand(0,8));
            p.vel=vel+fromAngle(frand(0,TAU),frand(8,90)); p.life=p.maxLife=frand(0.5,1.6);
            p.size=frand(3,9); double t=frand(0,1); p.col=mix(Color(1,0.9f,0.4f),Color(0.9f,0.25f,0.1f),t);
            p.additive=true; particles.push_back(p);}
        setToast("Rocket destroyed");
    }

    // ---- revert / rewind ----
    void captureSnapshot(){
        Snapshot s;
        s.simTime=simTime; s.heading=heading; s.angVel=angVel; s.throttle=throttle;
        s.pos=pos; s.vel=vel; s.landedRel=landedRel;
        s.sas=sas; s.enginesEnabled=enginesEnabled; s.chuteDeployed=chuteDeployed; s.legsDeployed=legsDeployed; s.landed=landed;
        s.landedBody=landedBody; s.currentStage=currentStage; s.warpIdx=warpIdx;
        s.sectionFuel=sectionFuel;
        for (auto& p : parts){ s.alive.push_back(p.alive?1:0); s.engineOn.push_back(p.engineOn?1:0); s.srbFuel.push_back(p.srbFuel); }
        history.push_back(std::move(s));
        if (history.size()>140) history.erase(history.begin());   // ~70s at 0.5s spacing
    }
    void restoreSnapshot(const Snapshot& s){
        simTime=s.simTime; heading=s.heading; angVel=s.angVel; throttle=s.throttle;
        pos=s.pos; vel=s.vel; landedRel=s.landedRel;
        sas=s.sas; enginesEnabled=s.enginesEnabled; chuteDeployed=s.chuteDeployed; legsDeployed=s.legsDeployed; landed=s.landed;
        landedBody=s.landedBody; currentStage=s.currentStage; warpIdx=0;
        sectionFuel=s.sectionFuel;
        for (size_t i=0;i<parts.size() && i<s.alive.size();i++){
            parts[i].alive=s.alive[i]; parts[i].engineOn=s.engineOn[i]; parts[i].srbFuel=s.srbFuel[i]; }
        exploded=false; mapView=false; showSettings=false; particles.clear(); trajectory.clear(); reentryHeat=0; clearNavigation();
        world.update(simTime);
    }
    void revert30(){
        if (history.empty()){ launch(); return; }   // nothing recorded -> back to the pad
        double target = simTime - 30.0;
        int idx = 0;
        for (int i=0;i<(int)history.size();i++) if (history[i].simTime <= target) idx=i;
        for (int i=idx;i>=0;i--){
            double minAlt = 1e18;
            for (auto& b : world.bodies){
                Vec2 bp = bodyPosAt(b, history[i].simTime);
                minAlt = std::min(minAlt, (history[i].pos-bp).len()-b.radius);
            }
            if (history[i].landed || minAlt > 4500){ idx=i; break; }
        }
        restoreSnapshot(history[idx]);
        history.erase(history.begin()+idx+1, history.end());
        setToast("Reverted 30 seconds");
    }

    // ---------------- orbital readout ----------------
    void computeOrbit(){
        const Body& B = world.bodies[domBody];
        Vec2 rvec = pos - B.pos; double r = rvec.len();
        Vec2 vrel = vel - B.vel; double v = vrel.len();
        double energy = v*v*0.5 - B.mu/r;
        double h = rvec.cross(vrel);
        double e = std::sqrt(std::max(0.0, 1 + 2*energy*h*h/(B.mu*B.mu)));
        ecc=e;
        if (energy>=0){ hyperbolic=true; double a=-B.mu/(2*energy); peri=a*(1-e)-B.radius; apo=1e18; }
        else { hyperbolic=false; double a=-B.mu/(2*energy); apo=a*(1+e)-B.radius; peri=a*(1-e)-B.radius; }
    }

    // numerical trajectory prediction (gravity only, all bodies). For a bound
    // orbit the budget is one full period (using the true semi-major axis) so the
    // ellipse closes into a loop; for an escape trajectory it's a fixed arc.
    void predict(){
        trajectory.clear();
        closestValid=false; closestApproach=0; closestApproachTime=0;
        if (landed||exploded) return;
        Vec2 p=pos, v=vel; double t=simTime;
        int di = world.dominant(pos);
        const Body& B=world.bodies[di];
        bool trackTarget = targetBody>=0 && targetBody<(int)world.bodies.size() && bodyUnlocked(targetBody);
        double bestClose = 1e100;
        double r = (pos-B.pos).len();
        double vrel = (vel-B.vel).len();
        double energy = vrel*vrel*0.5 - B.mu/r;
        double drawTotal, searchTotal;
        if (energy < -1e-3){                 // bound: one full revolution
            double a = -B.mu/(2*energy);
            double period = TAU*std::sqrt(a*a*a/B.mu);
            drawTotal = clampd(period*1.02, 400, 400000);
        } else {                             // escape: a fixed look-ahead
            drawTotal = clampd(r/std::max(vrel,1.0)*6.0, 600, 120000);
        }
        searchTotal = drawTotal;
        if (trackTarget){
            double dist = (pos - world.bodies[targetBody].pos).len();
            double targetLook = dist / std::max(vrel, 500.0) * 1.45;
            searchTotal = std::max(searchTotal, clampd(targetLook, 1200, 400000));
            if (energy < -1e-3) drawTotal = std::min(drawTotal, searchTotal);
            else drawTotal = std::min(drawTotal, 120000.0);
        }
        // Draw the path in the frame of the body we're orbiting: store points
        // relative to the dominant body's position at time t, anchored to its
        // CURRENT position. For a moving body (Luna) this turns the drifting
        // world-frame spiral into a clean closed ellipse around the moon.
        Vec2 anchor = B.pos;
        auto stepPredict = [&](double dt, bool drawPoint){
            Vec2 a0 = gravAtTime(p,t);
            Vec2 np = p + v*dt + a0*(0.5*dt*dt);
            Vec2 a1 = gravAtTime(np,t+dt);
            v = v + (a0+a1)*(0.5*dt);
            p = np; t+=dt;
            Vec2 frameP = anchor + (p - bodyPosAt(B, t));
            if (drawPoint) trajectory.push_back(frameP);
            if (trackTarget){
                Vec2 tp = bodyPosAt(world.bodies[targetBody], t);
                double miss = (p - tp).len() - world.bodies[targetBody].radius;
                if (miss < bestClose){
                    bestClose = miss;
                    closestValid = true;
                    closestApproach = std::max(0.0, miss);
                    closestApproachTime = t - simTime;
                    closestPoint = frameP;
                    closestTargetPoint = anchor + (tp - bodyPosAt(B, t));
                }
            }
            for (auto& bd : world.bodies) if ((p-bodyPosAt(bd,t)).len() < bd.radius) return false;
            return true;
        };
        int drawN=700; double drawDt=drawTotal/drawN;
        bool hit=false;
        for (int i=0;i<drawN;i++){
            if (!stepPredict(drawDt, true)){ hit=true; break; }
        }
        if (!hit && searchTotal>drawTotal+1.0){
            int searchN=900; double searchDt=(searchTotal-drawTotal)/searchN;
            for (int i=0;i<searchN;i++){
                if (!stepPredict(searchDt, false)) break;
            }
        }
    }

    // Hohmann transfer guidance to an orbiting target body (e.g. Luna around
    // Terra): prograde Δv, burn time, and the phase angle that tells you WHERE on
    // your orbit to start the burn. Approximate (assumes near-circular start),
    // but enough to make moon trips reachable.
    void computeTransfer(){
        transferValid = false;
        transferPhaseError = 0;
        if (targetBody < 0 || targetBody >= (int)world.bodies.size() || !bodyUnlocked(targetBody)) return;
        if (landed || exploded) return;
        const Body& T = world.bodies[targetBody];
        int par = T.parentIdx;
        if (par < 0){
            if (targetBody==0 || world.dominant(pos)!=0) return;
            const Body& P = world.bodies[0];
            transferParent = 0;
            double r1 = (pos - P.pos).len();
            double r2 = (T.pos - P.pos).len();
            if (r1 < P.radius+10 || r2 < P.radius+1000) return;
            double mu = P.mu;
            double at = (r1 + r2) * 0.5;
            double vTrans = std::sqrt(std::max(0.0, mu*(2.0/r1 - 1.0/at)));
            Vec2 vrel = vel - P.vel;
            Vec2 radial = (pos - P.pos).norm();
            double vtang = std::fabs(vrel.cross(radial));
            transferDv = vTrans - vtang;
            double aTarget = (T.pos - P.pos).angle();
            double burnAngle = wrapAngle(aTarget - PI);
            transferNode = P.pos + fromAngle(burnAngle, r1);
            transferPhaseReq = 0;
            transferPhaseNow = wrapAngle((pos - P.pos).angle() - burnAngle);
            transferPhaseError = transferPhaseNow;
            double thrust=0, mass=totalMass();
            for (int i=0;i<(int)parts.size();i++) if (parts[i].alive){
                const PartSpec& s=spec(parts[i].type);
                if (s.isEngine && parts[i].section<nSections && sectionFuel[parts[i].section]>0) thrust+=s.thrust;
                else if (s.isSRB && parts[i].srbFuel>0) thrust+=s.thrust;
            }
            double accel = thrust>1 ? thrust/std::max(mass,1.0) : 0;
            transferBurn = accel>0 ? std::fabs(transferDv)/accel : 0;
            transferValid = true;
            return;
        }
        const Body& P = world.bodies[par];
        // only meaningful while orbiting that parent
        if (world.dominant(pos) != par) return;
        transferParent = par;
        double r1 = (pos - P.pos).len();
        double r2 = T.orbitR;
        double mu = P.mu;
        double at = (r1 + r2) * 0.5;
        double vTrans = std::sqrt(std::max(0.0, mu*(2.0/r1 - 1.0/at)));
        Vec2 vrel = vel - P.vel;
        Vec2 radial = (pos - P.pos).norm();
        double vtang = std::fabs(vrel.cross(radial));  // tangential speed magnitude
        transferDv = vTrans - vtang;
        double tTransfer = PI * std::sqrt(at*at*at/mu);
        // required phase: target must lead the burn point so it arrives at apoapsis
        double reqLead = PI - T.orbitOmega * tTransfer;     // radians
        transferPhaseReq = wrapAngle(reqLead);
        double aRocket = (pos - P.pos).angle();
        double aTarget = (T.pos - P.pos).angle();
        transferPhaseNow = wrapAngle(aTarget - aRocket);
        transferPhaseError = wrapAngle(transferPhaseNow-transferPhaseReq);
        // burn-node angle = where the rocket should be when aligned
        double burnAngle = wrapAngle(aTarget - transferPhaseReq);
        transferNode = P.pos + fromAngle(burnAngle, r1);
        // burn time from current thrust capability
        double thrust=0, mass=totalMass();
        for (int i=0;i<(int)parts.size();i++) if (parts[i].alive){
            const PartSpec& s=spec(parts[i].type);
            if (s.isEngine && parts[i].section<nSections && sectionFuel[parts[i].section]>0) thrust+=s.thrust;
            else if (s.isSRB && parts[i].srbFuel>0) thrust+=s.thrust;
        }
        double accel = thrust>1 ? thrust/std::max(mass,1.0) : 0;
        transferBurn = accel>0 ? std::fabs(transferDv)/accel : 0;
        transferValid = true;
    }

    void warpUp(){ warpIdx = std::min(warpIdx+1, WARP_COUNT-1); }
    void warpDown(){ warpIdx = std::max(warpIdx-1, 0); }
    Vec2 bodyPosAt(const Body& b, double t){
        if (b.parentIdx<0) return b.fixedPos;
        const Body& par=world.bodies[b.parentIdx];
        return bodyPosAt(par,t) + fromAngle(b.phase0+b.orbitOmega*t, b.orbitR);
    }
    Vec2 gravAtTime(Vec2 p,double t){ Vec2 a{0,0};
        for (auto& b : world.bodies){ Vec2 bp=bodyPosAt(b,t); Vec2 d=bp-p; double r2=d.len2(); double r=std::sqrt(r2);
            if (r<1) continue; a+=d*(b.mu/(r2*r)); } return a; }

    // ============================ UPDATE ============================
    void setToast(const std::string& s){ toast=s; toastT=4.0; }
    bool toastIs(const std::string& s){ return toast.find(s)!=std::string::npos && toastT>0; }
    bool toastStartsLanded(){ return toast.rfind("LANDED",0)==0; }

    void step(double dt){
        dt = std::min(dt, 0.05);
        if (toastT>0) toastT-=dt;
        // particles
        for (auto& p : particles){ p.pos+=p.vel*dt; p.vel*=(1-std::min(0.8,1.5*dt)); p.life-=dt; }
        particles.erase(std::remove_if(particles.begin(),particles.end(),
            [](const Particle&p){return p.life<=0;}),particles.end());

        if (mode==MODE_BUILD){ stepBuild(); return; }

        // ---- flight controls ----
        if (in.throttleUp) throttle=clampd(throttle+1.2*dt,0,1);
        if (in.throttleDown) throttle=clampd(throttle-1.2*dt,0,1);
        if (in.throttleMax) throttle=1;
        if (in.throttleZero) throttle=0;
        if (in.stage) doStage();
        if (in.sasToggle){ sas=!sas; setToast(sas?"SAS on":"SAS off"); }
        if (in.deployChute) toggleParachute();
        if (in.mapToggle) mapView=!mapView;
        if (in.revert) { mode=MODE_BUILD; clearNavigation(); mapView=false; showSettings=false; reentryHeat=0; setToast("Reverted to hangar"); return; }
        if (in.warpUp){ warpIdx=std::min(warpIdx+1,WARP_COUNT-1); }
        if (in.warpDown){ warpIdx=std::max(warpIdx-1,0); }

        // warp limits: powered flight is allowed up to x3; coasting in atmosphere
        // is capped but still fast enough for parachute descents.
        int maxWarp = WARP_COUNT-1;
        double press = world.pressure(world.dominant(pos),pos);
        bool thrusting = throttle>0.01 && enginesLive();
        if (thrusting) maxWarp = 2;                         // x3 powered/low-altitude warp
        else if (press>0.001) maxWarp = 5;                   // coasting/parachuting in air -> up to 100x
        if (warpIdx>maxWarp) warpIdx=maxWarp;
        double warp = warps[warpIdx];

        if (!exploded){
            double simAdvance = dt*warp;
            double sub = 1.0/120.0;
            int n = (int)std::ceil(simAdvance/sub);
            n = std::max(1,std::min(n,3000));
            double sdt = simAdvance/n;
            for (int i=0;i<n && !exploded;i++) integrate(sdt);
        }
        world.update(simTime);

        // exhaust/reentry particles
        if (!exploded) { spawnExhaust(dt); spawnReentryEffects(dt); }
        else reentryHeat = approach(reentryHeat, 0, dt*3.0);
        if (!exploded && !landed) computeOrbit();
        if (!exploded) checkMilestones();
        if (!exploded && simTime - lastSnap >= 0.5){ captureSnapshot(); lastSnap = simTime; }

        // trajectory + transfer guidance recompute (throttled); used by minimap,
        // the flight overlay and the map view.
        if (--trajTimer<=0){ predict(); computeTransfer(); trajTimer=8; }

        // pick a navigation target by clicking a body on the map
        if (mapView && in.mousePressed && !showSettings){
            for (int i=0;i<(int)world.bodies.size();i++){
                if (i==world.dominant(pos) || !bodyUnlocked(i)) continue;
                Vec2 s = mapToScreen(world.bodies[i].pos);
                double rpx = std::max(16.0, world.bodies[i].radius*mapScale);
                if (std::hypot(in.mx-s.x, in.my-s.y) < rpx+14){
                    targetBody = (targetBody==i? -1 : i);
                    transferValid=false; closestValid=false; trajTimer=0;
                    setToast(targetBody>=0? ("Navigate to "+world.bodies[i].name) : "Navigation cleared");
                    break;
                }
            }
        }

        // cameras — centre on the centre of mass so the ship stays framed even
        // when zoomed in or after a stage drops the lower parts (the origin/pos is
        // the build base, which can end up empty space after separation).
        Vec2 center = l2w(comLocal());
        flightCam = center;
        if (in.wheel!=0 && !mapView) flightScale = clampd(flightScale*std::pow(1.12, in.wheel),0.02,80);
        if (in.wheel!=0 && mapView) mapScale = clampd(mapScale*std::pow(1.12,in.wheel),1e-7,0.01);
        if (followMap) mapCam = center;

        if (in.mousePressed && !mapView && !showSettings && !exploded) handleFlightPartClick();
    }

    Vec2 mapToScreen(Vec2 w){ return { r->W*0.5 + (w.x-mapCam.x)*mapScale, r->H*0.5 - (w.y-mapCam.y)*mapScale }; }

    Vec2 flightToScreen(Vec2 l){
        Vec2 w = l2w(l);
        return { r->W*0.5 + (w.x-flightCam.x)*flightScale, r->H*0.5 - (w.y-flightCam.y)*flightScale };
    }
    int flightPartAt(double sx,double sy){
        for (int i=(int)parts.size()-1;i>=0;i--){
            if (!parts[i].alive) continue;
            const PartSpec& s=spec(parts[i].type);
            Vec2 c=flightToScreen(parts[i].local);
            double hw=std::max(10.0, s.w*flightScale*0.55);
            double hh=std::max(10.0, s.h*flightScale*0.55);
            if (std::fabs(sx-c.x)<=hw && std::fabs(sy-c.y)<=hh) return i;
        }
        return -1;
    }
    void handleFlightPartClick(){
        if (in.my<190 && (in.mx<330 || in.mx>r->W-180)) return;
        if (in.my>r->H-82) return;
        int i=flightPartAt(in.mx,in.my);
        if (i<0 || i>=(int)parts.size() || !parts[i].alive) return;
        const PartSpec& s=spec(parts[i].type);
        if (s.isDecoupler) fireDecoupler(i);
        else if (s.isSep) fireSeparator(i);
        else if (s.isChute) {
            if (chuteDeployed) cutParachutes();
            else deployParachute();
        } else if (s.isLeg) deployLegs();
    }

    bool enginesLive(){ for(int i=0;i<(int)parts.size();i++){ if(!parts[i].alive||!parts[i].engineOn) continue;
        const PartSpec& s=spec(parts[i].type); if(s.isSRB&&parts[i].srbFuel>0) return true;
        if(s.isEngine&&enginesEnabled&&parts[i].section<nSections&&sectionFuel[parts[i].section]>0) return true; } return false; }

    double reentryIntensity(){
        if (landed || exploded) return 0;
        int bi = world.dominant(pos);
        const Body& B = world.bodies[bi];
        if (B.atmoH<=0) return 0;
        double rho = world.density(bi,pos);
        Vec2 vrel = vel - B.vel;
        double sp = vrel.len();
        Vec2 up = (pos - B.pos).norm();
        double radialSpeed = vrel.dot(up);
        if (radialSpeed > -25.0) return 0;
        if (rho<=1e-7 || sp<850) return 0;
        double q = 0.5*rho*sp*sp;
        double speedTerm = clampd((sp-850.0)/1800.0, 0, 1);
        double pressureTerm = clampd(std::sqrt(q/9000.0), 0, 1);
        double entryTerm = clampd((-radialSpeed-25.0)/450.0, 0, 1);
        return clampd((speedTerm*0.55 + speedTerm*pressureTerm*0.65) * entryTerm, 0, 1);
    }

    void spawnReentryEffects(double dt){
        double target = reentryIntensity();
        reentryHeat = lerpd(reentryHeat, target, clampd(dt*7.0, 0, 1));
        if (!showParticles || reentryHeat < 0.05) return;
        const Body& B = world.bodies[domBody];
        Vec2 vrel = vel - B.vel;
        if (vrel.len()<1) return;
        Vec2 vd = vrel.norm();
        Vec2 side = vd.perp();
        int cnt = (int)std::ceil(reentryHeat*8.0);
        Vec2 center = l2w(comLocal());
        for (int k=0;k<cnt;k++){
            Particle p;
            p.pos = center + vd*frand(0.8,4.5) + side*frand(-3.5,3.5);
            p.vel = vel - vd*frand(90,240) + side*frand(-80,80);
            p.life = p.maxLife = frand(0.12,0.34);
            p.size = frand(5,14) * (0.45 + reentryHeat);
            double t = frand(0,1);
            p.col = mix(Color(1.0f,0.95f,0.45f,0.95f), Color(1.0f,0.18f,0.05f,0.85f), (float)t);
            if (reentryHeat > 0.7 && frand(0,1)>0.55) p.col = Color(0.65f,0.88f,1.0f,0.65f);
            p.additive=true; particles.push_back(p);
        }
    }

    void spawnExhaust(double dt){
        double press = world.pressure(domBody,pos);
        for (int i=0;i<(int)parts.size();i++){
            if(!parts[i].alive||!parts[i].engineOn) continue;
            const PartSpec& s=spec(parts[i].type);
            bool live = s.isSRB? parts[i].srbFuel>0 : (enginesEnabled && parts[i].section<nSections && sectionFuel[parts[i].section]>0 && throttle>0.02);
            if(!live) continue;
            double pw = s.isSRB?1.0:throttle;
            int cnt = (int)std::ceil(pw*3);
            Vec2 nozzle = l2w(parts[i].local + Vec2{0,-s.h/2});
            Vec2 down = noseDir()*-1;
            for (int k=0;k<cnt;k++){
                Particle p; p.pos=nozzle+fromAngle(frand(0,TAU),frand(0,s.w*0.3));
                double spd=frand(40,90)*(0.6+0.6*pw);
                p.vel=vel+down*spd+noseDir().perp()*frand(-10,10);
                p.life=p.maxLife=frand(0.18,0.5)*(1.0+press); p.size=frand(s.w*2.0,s.w*4.0);
                p.col=mix(Color(1,1,0.7f),Color(1,0.4f,0.1f),frand(0,1)); p.additive=true;
                particles.push_back(p);
            }
        }
    }

    // ---------------- build interactions ----------------
    Vec2 screenToBuild(double sx,double sy){ return { buildCam.x+(sx-r->W/2)/buildScale, buildCam.y-(sy-r->H/2)/buildScale }; }
    void stepBuild(){
        if (in.wheel!=0) buildScale=clampd(buildScale*std::pow(1.12,in.wheel),5,80);
        if (in.help) showHelp=!showHelp;
        if (in.launch) { launch(); return; }
        if (showHelp || showMarket || showStaging) return;   // modal panels capture input
        double panelW=150;
        bool inArea = in.mx>panelW && in.my<r->H-70;
        // right-click removes the part under the cursor
        if (in.rightPressed && inArea){
            deletePart(partAt(screenToBuild(in.mx,in.my)));
            if (autoStage) assignDefaultStages();
        }
        // left-click places the selected part, snapping to the nearest node
        if (in.mousePressed && inArea){
            if (!unlocked[palette]){ showMarket=true; return; }    // locked -> open marketplace
            Vec2 w = screenToBuild(in.mx,in.my), c;
            if (findSnap(w,palette,c)){
                design.push_back({palette,c});
                const PartSpec& s=spec(palette);
                bool radial=isRadialPart(palette);
                if (symmetry && radial && std::fabs(c.x)>0.1){
                    Vec2 m={-c.x,c.y}; if(!overlaps(m,palette)) design.push_back({palette,m});
                }
                if (autoStage) assignDefaultStages();
            }
        }
    }
};
