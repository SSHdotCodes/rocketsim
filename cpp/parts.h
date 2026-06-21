// Part catalogue. Masses in kg, thrust in N, Isp in seconds. Values are tuned
// so a sensible stack clears ~3.5 km/s of delta-v with TWR > 1.2 on a
// Kerbin-scale home planet (R=600 km), i.e. orbit is reachable but not trivial.
// `cost` is the Astrobucks unlock price (0 = unlocked from the start).
#pragma once
#include <string>
#include "math.h"

enum PartType {
    PT_POD = 0, PT_NOSE, PT_TANK_S, PT_TANK_L, PT_ENGINE_S, PT_ENGINE_L,
    PT_SRB, PT_DECOUPLER, PT_SEP, PT_CHUTE, PT_LEGS, PT_FIN, PT_RCS, PT_COUNT
};

enum PartCat { CAT_COMMAND, CAT_FUEL, CAT_ENGINE, CAT_AERO, CAT_UTILITY };

struct PartSpec {
    const char* name;
    const char* desc;
    PartCat cat;
    double w, h;          // bounding size, metres
    double dryMass;       // kg
    double fuel;          // liquid fuel capacity, kg (tanks only; engines draw from shared pool)
    double thrust;        // vacuum thrust, N (engines/SRB)
    double ispVac, ispSea;// s
    double gimbal;        // rad of thrust vectoring
    double srbFuel;       // kg of internal solid fuel (SRB only)
    double dragArea;      // m^2 reference area contribution
    double cd;            // drag coefficient
    double torque;        // reaction-wheel torque, N*m (pods)
    int cost;             // Astrobucks unlock price (0 = free/unlocked)
    bool isEngine, isSRB, isTank, isPod, isChute, isLeg, isDecoupler, isFin, isNose, isSep;
};

// Indexed by PartType.
static const PartSpec PARTS[PT_COUNT] = {
    // name        desc                          cat          w     h    dry    fuel  thrust   ispV ispS  gmbl  srb   dragA cd   torque  cost  E    SRB  Tnk  Pod  Cht  Leg  Dec  Fin  Nose Sep
    {"Command Pod","Crew capsule + reaction wheel",CAT_COMMAND,1.5, 1.5, 800,   0,    0,       0,   0,    0,    0,    1.6, 0.45,  42000, 0,    false,false,false,true ,false,false,false,false,false,false},
    {"Nose Cone", "Aerodynamic cap, cuts drag", CAT_AERO,    1.5,  1.6, 180,   0,    0,       0,   0,    0,    0,    0.4, 0.20,  0,     0,    false,false,false,false,false,false,false,false,true ,false},
    {"Fuel Tank S","Small liquid fuel tank",     CAT_FUEL,    1.5,  2.8, 600,   6200, 0,       0,   0,    0,    0,    1.6, 0.40,  0,     0,    false,false,true ,false,false,false,false,false,false,false},
    {"Fuel Tank L","Large liquid fuel tank",     CAT_FUEL,    2.0,  4.2, 1800,  16000,0,       0,   0,    0,    0,    2.2, 0.40,  0,     3000, false,false,true ,false,false,false,false,false,false,false},
    {"Aerospike",  "Efficient all-altitude engine",CAT_ENGINE, 1.5,  1.6, 600,   0,    320000,  340, 300,  0.07, 0,    1.6, 0.50,  0,     0,    true ,false,false,false,false,false,false,false,false,false},
    {"Heavy Engine","High-thrust booster engine",CAT_ENGINE,  2.0,  2.0, 1800,  0,    760000,  305, 265,  0.07, 0,    2.2, 0.50,  0,     5000, true ,false,false,false,false,false,false,false,false,false},
    {"Solid Booster","Big solid rocket, no throttle",CAT_ENGINE,1.3, 4.4, 1200,  0,    340000,  235, 200,  0,    9000, 1.4, 0.45,  0,     2500, false,true ,false,false,false,false,false,false,false,false},
    {"Decoupler",  "Separates the stage below",  CAT_UTILITY, 2.0,  0.5, 200,   0,    0,       0,   0,    0,    0,    2.0, 0.40,  0,     0,    false,false,false,false,false,false,true ,false,false,false},
    {"Separator",  "Drops side boosters/fins",   CAT_UTILITY, 0.6,  0.8, 80,    0,    0,       0,   0,    0,    0,    0.3, 0.40,  0,     800,  false,false,false,false,false,false,false,false,false,true },
    {"Parachute",  "Deploy to land in atmosphere",CAT_UTILITY,0.9,  0.9, 120,   0,    0,       0,   0,    0,    0,    0.5, 0.40,  0,     0,    false,false,false,false,true ,false,false,false,false,false},
    {"Landing Legs","Survive harder touchdowns", CAT_UTILITY, 2.6,  1.0, 160,   0,    0,       0,   0,    0,    0,    0.4, 0.40,  0,     1500, false,false,false,false,false,true ,false,false,false,false},
    {"Fins",       "Aero stability + steering",  CAT_AERO,    1.4,  1.3, 120,   0,    0,       0,   0,    0,    0,    1.2, 0.60,  0,     0,    false,false,false,false,false,false,false,true ,false,false},
    {"RCS Thruster","Fine attitude control",     CAT_UTILITY, 1.6,  0.6, 100,   0,    0,       0,   0,    0,    0,    0.3, 0.40,  6000,  2000, false,false,false,false,false,false,false,false,false,false},
};

inline const PartSpec& spec(int t) { return PARTS[t]; }

// Liquid-engine thrust scales with ambient pressure (0 = vacuum, 1 = sea level).
inline double engineIsp(const PartSpec& s, double press) {
    return lerpd(s.ispVac, s.ispSea, clampd(press, 0.0, 1.0));
}
