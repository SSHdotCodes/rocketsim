import { Fragment, useCallback, useEffect, useMemo, useRef, useState, type ReactNode } from 'react';
import {
  AlertTriangle,
  Anchor,
  Box,
  Camera,
  Car,
  CircleDollarSign,
  Cpu,
  Crosshair,
  Flame,
  FlaskConical,
  FolderOpen,
  Fuel,
  Gauge,
  GripVertical,
  Hammer,
  LocateFixed,
  Map,
  Package,
  Play,
  Plus,
  Rocket,
  RotateCcw,
  Satellite,
  Save,
  Send,
  Settings,
  Shield,
  ShoppingCart,
  Timer,
  Trash2,
  Users,
  Zap,
} from 'lucide-react';
import './App.css';
import { SpaceScene } from './SpaceScene';
import {
  BODY_BY_ID,
  CELESTIAL_BODIES,
  COMPONENT_CATEGORIES,
  COMPONENTS,
  MARKET_UNLOCKS,
  REQUIRED_TEXTURE_SOURCES,
} from './gameData';
import {
  atmosphereTop,
  bodyDisplayName,
  calculateStats,
  canAccessBody,
  componentById,
  createInitialEconomy,
  createInitialFlight,
  defaultContract,
  defaultDesign,
  deployRover,
  loadJson,
  makeDesign,
  orbitAltitude,
  orbitalSpeed,
  saveJson,
  seededRocks,
  transferCostKm,
} from './sim';
import { useMultiplayer } from './useMultiplayer';
import type {
  BodyId,
  CelestialBody,
  ComponentCategory,
  ContractState,
  EconomyState,
  FlightState,
  RocketComponent,
  RocketDesign,
  RocketStats,
  RoverState,
  View,
} from './types';

type CameraMode = 'third' | 'cockpit' | 'orbit' | 'builder';

const STORAGE = {
  designs: 'rocket-sim.designs.v1',
  activeDesign: 'rocket-sim.active-design.v1',
  economy: 'rocket-sim.economy.v1',
  rovers: 'rocket-sim.rovers.v1',
  contract: 'rocket-sim.contract.v1',
};

const timeScales = [1, 5, 25, 100];

function clamp(value: number, min: number, max: number) {
  return Math.max(min, Math.min(max, value));
}

function meters(value: number) {
  if (value >= 1000000) return `${(value / 1000000).toFixed(2)} Mm`;
  if (value >= 1000) return `${(value / 1000).toFixed(1)} km`;
  return `${Math.max(0, value).toFixed(0)} m`;
}

function money(value: number) {
  return `${Math.round(value).toLocaleString()} AB`;
}

function stat(value: number, digits = 0) {
  return Number.isFinite(value) ? value.toFixed(digits) : '0';
}

function initialDesigns() {
  const loaded = loadJson<RocketDesign[]>(STORAGE.designs, [defaultDesign]);
  return loaded.length ? loaded : [defaultDesign];
}

function availableComponents(economy: EconomyState) {
  return COMPONENTS.filter((component) => !component.unlockCost || economy.unlockedParts.includes(component.id));
}

function missingUnlock(component: RocketComponent, economy: EconomyState) {
  return Boolean(component.unlockCost && !economy.unlockedParts.includes(component.id));
}

// Tuning constants for the arcade-but-time-based flight model.
const ASCENT_BOOST = 3.0; // thrust multiplier so a balanced rocket reaches orbit in ~1.5-2.5 min
const FUEL_BURN = 0.025; // fuel units consumed per kN of thrust per second
const ORBIT_INSERT_FRACTION = 0.8; // fraction of orbital velocity needed to capture

// One physics sub-step of powered flight (ascent AND descent). Mutates `next`.
// Returns true when a terminal transition (orbit/landed/crashed/burned) occurs.
function integratePowered(
  next: FlightState,
  step: number,
  keys: Set<string>,
  stats: RocketStats,
  contract: ContractState,
  body: CelestialBody,
  atmoTop: number,
  orbitTarget: number,
): boolean {
  const radius = body.radiusKm * 1000;
  const inAtmosphere = body.atmosphere && next.altitude < atmoTop;

  const turnRate = keys.has('Shift') ? 55 : 30;
  let manualPitch = false;
  if (keys.has('ArrowLeft') || keys.has('a') || keys.has('A')) next.heading -= turnRate * step;
  if (keys.has('ArrowRight') || keys.has('d') || keys.has('D')) next.heading += turnRate * step;
  if (keys.has('ArrowUp') || keys.has('w') || keys.has('W')) {
    next.pitch += turnRate * step;
    manualPitch = true;
  }
  if (keys.has('ArrowDown') || keys.has('s') || keys.has('S')) {
    next.pitch -= turnRate * step;
    manualPitch = true;
  }
  if (keys.has('=') || keys.has('+')) next.throttle = clamp(next.throttle + step * 0.7, 0, 1);
  if (keys.has('-') || keys.has('_')) next.throttle = clamp(next.throttle - step * 0.7, 0, 1);

  // Auto gravity turn: ease pitch from vertical toward the horizon as we climb.
  const turnTop = orbitAltitude(body);
  const turnProgress = clamp((next.altitude - 1200) / Math.max(1, turnTop - 1200), 0, 1);
  const targetPitch = clamp(90 - 88 * turnProgress, 3, 90);
  if (!manualPitch && next.verticalSpeed >= -2) {
    next.pitch += (targetPitch - next.pitch) * clamp(step * 0.5, 0, 0.4);
  }
  next.pitch = clamp(next.pitch, 0, 92);

  const gravity = body.gravity * Math.pow(radius / (radius + Math.max(0, next.altitude)), 2);
  const density = inAtmosphere ? Math.pow(clamp(1 - next.altitude / atmoTop, 0, 1), 1.6) : 0;
  const speed = Math.hypot(next.verticalSpeed, next.horizontalSpeed);
  const haveThrust = next.throttle > 0 && next.fuel > 0;
  const throttle = haveThrust ? next.throttle : 0;
  const thrustAcceleration = ((stats.thrust * 1000 * throttle) / Math.max(1000, stats.mass * 1000)) * ASCENT_BOOST;
  const pitchRadians = (next.pitch * Math.PI) / 180;

  const onFinalApproach =
    next.verticalSpeed < 0 &&
    next.horizontalSpeed < orbitTarget * 0.35 &&
    next.altitude < (body.atmosphere ? atmoTop * 0.5 : 30000);
  const canChute = body.atmosphere && density > 0.04 && stats.parachutes > 0;
  const canHover = haveThrust && next.throttle > 0.3;
  const guidedLanding = onFinalApproach && (canChute || canHover);

  if (guidedLanding) {
    const targetVs = -clamp(Math.sqrt(next.altitude) * 1.0, 3, 25);
    next.verticalSpeed += (targetVs - next.verticalSpeed) * clamp(step * 3, 0, 1);
    next.horizontalSpeed = Math.max(0, next.horizontalSpeed - next.horizontalSpeed * 0.8 * step);
  } else {
    next.verticalSpeed += (Math.sin(pitchRadians) * thrustAcceleration - gravity) * step;
    next.verticalSpeed -= Math.sign(next.verticalSpeed) * density * stats.drag * Math.abs(next.verticalSpeed) * 0.02 * step;
    next.horizontalSpeed += Math.cos(pitchRadians) * thrustAcceleration * step;
    next.horizontalSpeed = Math.max(0, next.horizontalSpeed - density * stats.drag * next.horizontalSpeed * 0.018 * step);
  }

  next.altitude = Math.max(0, next.altitude + next.verticalSpeed * step);
  next.speed = Math.hypot(next.verticalSpeed, next.horizontalSpeed);
  const burnRate = guidedLanding ? (canChute ? 0 : stats.thrust * FUEL_BURN * 0.18) : throttle * stats.thrust * FUEL_BURN;
  next.fuel = Math.max(0, next.fuel - burnRate * step);

  const thermal = density * Math.max(0, speed - 1600) * (next.verticalSpeed < -40 ? 1.6 : 0.5);
  if (thermal > 0) next.heat += (thermal * step) / (stats.heatShield ? 520 : 150);
  else next.heat = Math.max(0, next.heat - step * 4);

  next.apoapsis = Math.max(next.altitude, next.altitude + (next.verticalSpeed * Math.abs(next.verticalSpeed)) / (2 * Math.max(0.1, gravity)));
  next.periapsis = Math.max(0, next.altitude - Math.abs(next.verticalSpeed * Math.abs(next.verticalSpeed)) / (2 * Math.max(0.1, gravity)));

  if (next.heat >= 100 && !stats.heatShield) {
    next.status = 'burned';
    next.message = 'Vehicle burned up during atmospheric entry. Add a heat shield before trying that profile again.';
    return true;
  }

  const inSpace = next.altitude >= (body.atmosphere ? atmoTop * 0.96 : turnTop * 0.8);
  if (inSpace && next.altitude > 5000 && next.horizontalSpeed >= orbitTarget * ORBIT_INSERT_FRACTION && next.verticalSpeed > -120) {
    next.status = 'orbit';
    next.throttle = 0;
    next.verticalSpeed = 0;
    if (body.atmosphere) next.altitude = clamp(next.altitude, atmoTop * 1.9, atmoTop * 2.8);
    next.horizontalSpeed = Math.max(next.horizontalSpeed, orbitTarget * 0.86);
    next.speed = next.horizontalSpeed;
    const inBand =
      stats.satellite &&
      !contract.complete &&
      next.location === 'earth' &&
      Math.abs(next.altitude / 1000 - contract.desiredOrbitKm) <= contract.toleranceKm;
    next.contractComplete = inBand;
    next.message = inBand
      ? `${contract.company} satellite deployed inside the target orbit. Contract payout secured.`
      : `Stable orbit around ${bodyDisplayName(next.location)}. Open the Map to plan a transfer, or Land.`;
    return true;
  }

  if (next.altitude <= 0 && next.verticalSpeed < -0.5) {
    const vImpact = Math.abs(next.verticalSpeed);
    const hImpact = next.horizontalSpeed;
    const vLimit = stats.landingFeet ? 18 : stats.parachutes > 0 && body.atmosphere ? 11 : 7;
    const hLimit = stats.landingFeet ? 24 : 14;
    if (vImpact <= vLimit && hImpact <= hLimit) {
      next.status = 'landed';
      next.altitude = 0;
      next.verticalSpeed = 0;
      next.horizontalSpeed = 0;
      next.throttle = 0;
      next.timeScale = 1;
      next.message = `Touchdown on ${bodyDisplayName(next.location)}! Deploy a rover or EVA to collect samples.`;
    } else {
      next.status = 'crashed';
      next.altitude = 0;
      next.message = `Impact at ${stat(vImpact)} m/s vertical / ${stat(hImpact)} m/s lateral exceeded the gear margin. Vehicle destroyed.`;
    }
    return true;
  }

  // Guidance messages.
  if (inAtmosphere && next.verticalSpeed < -120 && !stats.heatShield && speed > 2200) {
    next.message = 'Re-entry plasma is building and this vehicle has no heat shield.';
  } else if (next.altitude >= atmoTop && next.horizontalSpeed < orbitTarget * ORBIT_INSERT_FRACTION && next.verticalSpeed > 0) {
    const pct = Math.round((next.horizontalSpeed / (orbitTarget * ORBIT_INSERT_FRACTION)) * 100);
    next.message = `In space — building orbital velocity (${clamp(pct, 0, 99)}%). Keep the nose near the horizon.`;
  } else if (!haveThrust && next.fuel <= 0 && next.verticalSpeed < 0) {
    next.message = 'Out of fuel and falling. Stage spent tanks earlier or add more delta-v next time.';
  } else if (next.altitude > 1200) {
    next.message = 'Ascent nominal. The autopilot is turning toward the horizon — ride it to orbit.';
  }

  return false;
}

function advanceFlight(
  previous: FlightState,
  dt: number,
  keys: Set<string>,
  stats: RocketStats,
  contract: ContractState,
) {
  dt = clamp(dt, 0, 0.075);
  const scale = Math.max(1, previous.timeScale);
  const next = { ...previous, elapsed: previous.elapsed + dt * scale };
  const body = BODY_BY_ID[next.location];
  const atmoTop = atmosphereTop(body);
  const orbitTarget = orbitalSpeed(body);

  if (next.status === 'countdown') {
    next.countdown = Math.max(0, next.countdown - dt);
    next.message =
      next.countdown > 0
        ? `T-${Math.ceil(next.countdown)}. Guidance, telemetry, and range are green.`
        : 'Liftoff! Hold throttle — the autopilot will start the gravity turn for you.';
    if (next.countdown <= 0) {
      next.status = 'ascent';
      next.throttle = Math.max(next.throttle, 1);
      next.verticalSpeed = Math.max(next.verticalSpeed, 4);
      next.pitch = 90;
    }
    return next;
  }

  if (next.status === 'eva') {
    next.oxygen = Math.max(0, next.oxygen - dt);
    if (next.oxygen <= 0) {
      next.status = 'crashed';
      next.message = 'Suit oxygen depleted. Mission reset from last safe state.';
    }
    return next;
  }

  if (next.status === 'transfer') {
    const target = next.target ?? next.location;
    const seconds = clamp(transferCostKm(target, next.location) / 5, 120, 900);
    next.transferProgress = clamp(next.transferProgress + (dt * scale) / seconds, 0, 1);
    next.speed = 4200 + Math.sin(next.transferProgress * Math.PI) * 16000;
    next.altitude = 600000 + Math.sin(next.transferProgress * Math.PI) * 12000000;
    const remaining = Math.ceil((1 - next.transferProgress) * seconds);
    next.message = `Coasting to ${bodyDisplayName(target)} — ${remaining}s of transfer left. Use time warp to speed it up.`;
    if (next.transferProgress >= 1) {
      const targetBody = BODY_BY_ID[target];
      next.location = target;
      next.target = undefined;
      next.status = 'orbit';
      next.altitude = orbitAltitude(targetBody);
      next.horizontalSpeed = orbitalSpeed(targetBody) * 0.86;
      next.verticalSpeed = 0;
      next.speed = next.horizontalSpeed;
      next.apoapsis = next.altitude + 18000;
      next.periapsis = Math.max(0, next.altitude - 12000);
      next.heat = 0;
      next.timeScale = 1;
      next.message = `Captured into ${bodyDisplayName(target)} orbit. Plan a landing, deploy payloads, or transfer onward.`;
    }
    return next;
  }

  if (next.status === 'orbit') {
    // Orbit is controllable: steer to reorient and burn the engine to raise/trim the orbit.
    const oStep = clamp(dt * scale, 0, 0.2);
    const turnRate = keys.has('Shift') ? 55 : 32;
    if (keys.has('ArrowLeft') || keys.has('a') || keys.has('A')) next.heading -= turnRate * oStep;
    if (keys.has('ArrowRight') || keys.has('d') || keys.has('D')) next.heading += turnRate * oStep;
    if (keys.has('ArrowUp') || keys.has('w') || keys.has('W')) next.pitch += turnRate * oStep;
    if (keys.has('ArrowDown') || keys.has('s') || keys.has('S')) next.pitch -= turnRate * oStep;
    next.pitch = clamp(next.pitch, 0, 92);
    if (keys.has('=') || keys.has('+')) next.throttle = clamp(next.throttle + oStep * 0.7, 0, 1);
    if (keys.has('-') || keys.has('_')) next.throttle = clamp(next.throttle - oStep * 0.7, 0, 1);

    const haveThrust = next.throttle > 0 && next.fuel > 0;
    if (haveThrust) {
      const accel = ((stats.thrust * 1000 * next.throttle) / Math.max(1000, stats.mass * 1000)) * ASCENT_BOOST;
      const pr = (next.pitch * Math.PI) / 180;
      next.horizontalSpeed += accel * Math.cos(pr) * oStep * 0.7;
      next.altitude += accel * (Math.sin(pr) * 26 + Math.cos(pr) * 9) * oStep;
      next.fuel = Math.max(0, next.fuel - next.throttle * stats.thrust * oStep * FUEL_BURN);
      next.message = 'Burning to adjust your orbit — cut the throttle to coast again.';
    }

    // Hold a stable orbit (never let a burn drop you back into the atmosphere here).
    next.altitude = Math.max(atmoTop > 0 ? atmoTop * 1.05 : 12000, next.altitude);
    next.speed = next.horizontalSpeed;
    const wobble = Math.sin(next.elapsed / 90) * Math.max(1500, next.altitude * 0.04);
    next.apoapsis = next.altitude + Math.max(8000, next.altitude * 0.08) + wobble;
    next.periapsis = Math.max(0, next.altitude - Math.max(6000, next.altitude * 0.06) - wobble * 0.4);
    next.heat = Math.max(0, next.heat - dt * scale * 4);
    return next;
  }

  if (next.status !== 'ascent') return next; // pad / landed / crashed / burned

  // Powered flight: sub-step over the (possibly warped) game-time for stability at any time scale.
  const totalDt = dt * scale;
  const subSteps = clamp(Math.ceil(totalDt / 0.3), 1, 64);
  const h = totalDt / subSteps;
  for (let i = 0; i < subSteps; i++) {
    if (integratePowered(next, h, keys, stats, contract, body, atmoTop, orbitTarget)) break;
  }
  return next;
}

function componentSummary(component: RocketComponent) {
  const details = [];
  if (component.fuel) details.push(`${component.fuel} fuel`);
  if (component.thrust) details.push(`${component.thrust} kN`);
  if (component.heatShield) details.push('heat shield');
  if (component.parachute) details.push('parachute');
  if (component.roverBay) details.push('rover bay');
  if (component.satellite) details.push('satellite');
  return details.join(' · ') || `${component.mass} t`;
}

// A little 2D silhouette of the actual part, so the builder is visual rather than text-only.
function PartGlyph({ part, size = 28 }: { part: RocketComponent; size?: number }) {
  const id = part.id;
  const hull = '#e3e9ef';
  const metal = '#aab2bb';
  const dark = '#3a414b';
  const gold = '#cdab53';
  const blue = '#3570b8';
  const heat = '#7a5234';
  const red = '#cf3b2a';
  let body: ReactNode;

  if (id.includes('nose')) {
    body = <path d="M18 3 Q29 24 29 40 L7 40 Q7 24 18 3 Z" fill={hull} stroke={metal} strokeWidth="1" />;
  } else if (id.includes('parachute')) {
    body = (
      <>
        <path d="M6 20 Q18 2 30 20 Z" fill={red} />
        <rect x="12" y="20" width="12" height="20" rx="2" fill={hull} stroke={metal} />
      </>
    );
  } else if (id.includes('capsule')) {
    body = (
      <>
        <path d="M13 6 L23 6 L29 40 L7 40 Z" fill={hull} stroke={metal} />
        <circle cx="18" cy="17" r="3.2" fill="#0b1722" />
      </>
    );
  } else if (id.includes('guidance')) {
    body = (
      <>
        <rect x="8" y="8" width="20" height="28" rx="2" fill={dark} />
        <ellipse cx="18" cy="22" rx="11" ry="3.4" fill="none" stroke={blue} strokeWidth="2" />
      </>
    );
  } else if (id.includes('engine')) {
    body = (
      <>
        <rect x="10" y="4" width="16" height="11" rx="1.5" fill={dark} />
        <path d="M8 15 L28 15 L23 41 L13 41 Z" fill="#262c34" stroke={metal} strokeWidth="0.8" />
      </>
    );
  } else if (id.includes('heat')) {
    body = <path d="M4 14 Q18 46 32 14 Q18 21 4 14 Z" fill={heat} stroke="#5a3c26" />;
  } else if (id.includes('solar')) {
    body = (
      <>
        <rect x="2" y="14" width="11" height="16" fill={blue} stroke="#10325e" />
        <rect x="23" y="14" width="11" height="16" fill={blue} stroke="#10325e" />
        <rect x="14" y="8" width="8" height="28" rx="2" fill={hull} stroke={metal} />
      </>
    );
  } else if (id.includes('rover')) {
    body = (
      <>
        <rect x="7" y="16" width="22" height="13" rx="2" fill={gold} />
        <circle cx="11" cy="31" r="3.4" fill={dark} />
        <circle cx="25" cy="31" r="3.4" fill={dark} />
      </>
    );
  } else if (id.includes('satellite')) {
    body = (
      <>
        <rect x="11" y="13" width="14" height="18" rx="1.5" fill={gold} />
        <path d="M25 16 Q34 22 25 28 Z" fill={hull} stroke={metal} />
      </>
    );
  } else if (id.includes('cargo')) {
    body = (
      <>
        <rect x="8" y="6" width="20" height="32" rx="2.5" fill={hull} stroke={metal} />
        <rect x="8" y="18" width="20" height="5" fill={blue} />
      </>
    );
  } else if (id.includes('separator')) {
    body = <rect x="6" y="17" width="24" height="9" rx="1.5" fill={dark} stroke={metal} />;
  } else if (id.includes('feet') || id.includes('wheel')) {
    body = (
      <>
        <rect x="11" y="12" width="14" height="14" rx="2" fill={metal} />
        <path d="M12 24 L5 38 M24 24 L31 38" stroke={metal} strokeWidth="2.4" fill="none" strokeLinecap="round" />
      </>
    );
  } else {
    const wide = part.size === 'large';
    const x = wide ? 5 : 8;
    const w = wide ? 26 : 20;
    body = (
      <>
        <rect x={x} y="4" width={w} height="36" rx="3" fill={hull} stroke={metal} />
        <rect x={x} y="9" width={w} height="3" fill={metal} opacity="0.7" />
        <rect x={x} y="32" width={w} height="3" fill={metal} opacity="0.7" />
        <rect x="17" y="6" width="2" height="32" fill={red} opacity="0.6" />
      </>
    );
  }

  return (
    <svg viewBox="0 0 36 44" width={size} height={(size * 44) / 36} aria-hidden="true">
      {body}
    </svg>
  );
}

function CategoryIcon({ category, size = 15 }: { category: ComponentCategory; size?: number }) {
  switch (category) {
    case 'Tanks':
      return <Fuel size={size} />;
    case 'Engines':
      return <Flame size={size} />;
    case 'Control':
      return <Cpu size={size} />;
    case 'Science':
      return <FlaskConical size={size} />;
    case 'Landing':
      return <Anchor size={size} />;
    case 'Payload':
      return <Package size={size} />;
    default:
      return <Box size={size} />;
  }
}

function MiniMap({ flight, showOtherRockets }: { flight: FlightState; showOtherRockets: boolean }) {
  const body = BODY_BY_ID[flight.location];
  const target = orbitalSpeed(body);
  const orbitRatio = clamp(flight.horizontalSpeed / target, 0, 1);
  const cx = 90;
  const cy = 70;
  const planetR = 17;

  // The full orbit ring grows with altitude.
  const altRatio = clamp(flight.altitude / Math.max(120000, orbitAltitude(body) * 2.4), 0, 1);
  const rx = planetR + 9 + altRatio * 50;
  const ry = (planetR + 9 + altRatio * 50) * 0.58;

  // The craft rides the orbit; on ascent it spirals out from the surface.
  const orbiting = flight.status === 'orbit' || flight.status === 'transfer';
  const reach = orbiting ? 1 : clamp(flight.altitude / Math.max(1, orbitAltitude(body)), 0, 1);
  const theta = flight.elapsed * 0.5 - Math.PI / 2;
  const craftRx = planetR + reach * (rx - planetR);
  const craftRy = planetR * 0.58 + reach * (ry - planetR * 0.58);
  const ax = cx + Math.cos(theta) * craftRx;
  const ay = cy + Math.sin(theta) * craftRy;

  const label = orbiting ? 'In orbit' : flight.verticalSpeed < -2 ? 'Descending' : 'To orbit';

  return (
    <div className="minimap glass">
      <svg viewBox="0 0 180 140" role="img" aria-label="Orbital map">
        {/* full elliptical orbit path */}
        <ellipse cx={cx} cy={cy} rx={rx} ry={ry} className="orbit-line" />
        <ellipse cx={cx} cy={cy} rx={rx} ry={ry} className={`active-orbit ${orbiting ? 'live' : ''}`} />
        {/* planet */}
        <circle cx={cx} cy={cy} r={planetR + 3} className="planet-atmo" />
        <circle cx={cx} cy={cy} r={planetR} className="earth-disc" />
        {/* ascent spiral from the surface to the craft */}
        {!orbiting && reach > 0.02 && (
          <line x1={cx} y1={cy - planetR} x2={ax} y2={ay} className="ascent-trail" />
        )}
        {showOtherRockets &&
          [0.6, 2.4, 4.1].map((offset, index) => (
            <circle
              key={index}
              cx={cx + Math.cos(theta + offset) * rx}
              cy={cy + Math.sin(theta + offset) * ry}
              r="2.4"
              className="peer-dot"
            />
          ))}
        <circle cx={ax} cy={ay} r="4" className="craft-dot" />
      </svg>
      <div className="minimap-data">
        <span>{label}</span>
        <strong>{Math.round(orbitRatio * 100)}%</strong>
      </div>
    </div>
  );
}

function Telemetry({ flight, stats }: { flight: FlightState; stats: RocketStats }) {
  return (
    <div className="telemetry glass">
      <div>
        <span>Altitude</span>
        <strong>{meters(flight.altitude)}</strong>
      </div>
      <div>
        <span>Speed</span>
        <strong>{stat(flight.speed)} m/s</strong>
      </div>
      <div>
        <span>Vert Speed</span>
        <strong className={flight.verticalSpeed < -18 ? 'danger' : ''}>{stat(flight.verticalSpeed)} m/s</strong>
      </div>
      <div>
        <span>Apoapsis</span>
        <strong>{meters(flight.apoapsis)}</strong>
      </div>
      <div>
        <span>Fuel</span>
        <strong className={flight.fuel / Math.max(1, stats.fuel) < 0.12 ? 'danger' : ''}>
          {Math.round((flight.fuel / Math.max(1, stats.fuel)) * 100)}%
        </strong>
      </div>
      <div>
        <span>Heat</span>
        <strong className={flight.heat > 70 ? 'danger' : ''}>{stat(flight.heat)}%</strong>
      </div>
    </div>
  );
}

function StatusPill({ flight }: { flight: FlightState }) {
  const label =
    flight.status === 'countdown'
      ? `T-${Math.ceil(flight.countdown)}`
      : flight.status === 'ascent'
        ? flight.verticalSpeed < -2
          ? 'DESCENT'
          : 'ASCENT'
        : flight.status === 'transfer'
          ? `${Math.round(flight.transferProgress * 100)}% TRANSFER`
          : flight.status.toUpperCase();
  return <div className={`status-pill ${flight.status}`}>{label}</div>;
}

function App() {
  const [view, setView] = useState<View>('mission');
  const [designs, setDesigns] = useState<RocketDesign[]>(initialDesigns);
  const [activeDesignId, setActiveDesignId] = useState(() =>
    loadJson<string>(STORAGE.activeDesign, defaultDesign.id),
  );
  const [economy, setEconomy] = useState<EconomyState>(() =>
    loadJson<EconomyState>(STORAGE.economy, createInitialEconomy()),
  );
  const [rovers, setRovers] = useState<RoverState[]>(() => loadJson<RoverState[]>(STORAGE.rovers, []));
  const [contract, setContract] = useState<ContractState>(() =>
    loadJson<ContractState>(STORAGE.contract, defaultContract),
  );
  const [selectedBody, setSelectedBody] = useState<BodyId>('moon');
  const [cameraMode, setCameraMode] = useState<CameraMode>('third');
  const [settingsOpen, setSettingsOpen] = useState(false);
  const [showOtherRockets, setShowOtherRockets] = useState(true);
  const [room, setRoom] = useState('apollo-room');
  const [multiplayerEnabled, setMultiplayerEnabled] = useState(false);
  const [activeRoverId, setActiveRoverId] = useState<string | null>(null);
  const [sampleCount, setSampleCount] = useState(0);
  const [drag, setDrag] = useState<{ kind: 'palette'; id: string } | { kind: 'move'; index: number } | null>(null);
  const [dropIndex, setDropIndex] = useState<number | null>(null);

  const activeDesign = useMemo(() => {
    return designs.find((design) => design.id === activeDesignId) ?? designs[0] ?? defaultDesign;
  }, [activeDesignId, designs]);

  const stats = useMemo(() => calculateStats(activeDesign), [activeDesign]);
  const [flight, setFlight] = useState<FlightState>(() => createInitialFlight(stats));
  const flightRef = useRef(flight);
  const statsRef = useRef(stats);
  const keysRef = useRef(new Set<string>());
  const awardedRef = useRef({ contract: contract.complete, landing: '' });

  useEffect(() => {
    flightRef.current = flight;
  }, [flight]);

  useEffect(() => {
    statsRef.current = stats;
  }, [stats]);

  useEffect(() => saveJson(STORAGE.designs, designs), [designs]);
  useEffect(() => saveJson(STORAGE.activeDesign, activeDesignId), [activeDesignId]);
  useEffect(() => saveJson(STORAGE.economy, economy), [economy]);
  useEffect(() => saveJson(STORAGE.rovers, rovers), [rovers]);
  useEffect(() => saveJson(STORAGE.contract, contract), [contract]);

  const activeRover = useMemo(
    () => rovers.find((rover) => rover.id === activeRoverId) ?? rovers[0],
    [activeRoverId, rovers],
  );

  const replaceDesignFromPeer = useCallback((design: RocketDesign) => {
    setDesigns((current) => {
      const next = { ...design, id: current.find((item) => item.id === design.id)?.id ?? design.id };
      const exists = current.some((item) => item.id === next.id);
      return exists ? current.map((item) => (item.id === next.id ? next : item)) : [next, ...current];
    });
    setActiveDesignId(design.id);
  }, []);

  const multiplayer = useMultiplayer({
    room,
    enabled: multiplayerEnabled,
    view,
    design: activeDesign,
    flight,
    onDesign: replaceDesignFromPeer,
  });

  const updateDesign = useCallback(
    (updater: (design: RocketDesign) => RocketDesign) => {
      setDesigns((current) => {
        const existing = current.find((design) => design.id === activeDesign.id) ?? activeDesign;
        const updated = { ...updater(existing), updatedAt: Date.now() };
        multiplayer.broadcastDesign(updated);
        return current.map((design) => (design.id === updated.id ? updated : design));
      });
    },
    [activeDesign, multiplayer],
  );

  const resetFlight = useCallback(() => {
    const next = createInitialFlight(calculateStats(activeDesign));
    setFlight(next);
    awardedRef.current = { contract: contract.complete, landing: '' };
  }, [activeDesign, contract.complete]);

  useEffect(() => {
    const onKeyDown = (event: KeyboardEvent) => {
      const target = event.target as HTMLElement | null;
      const typing =
        target?.tagName === 'INPUT' ||
        target?.tagName === 'TEXTAREA' ||
        target?.tagName === 'SELECT' ||
        Boolean(target?.isContentEditable);
      if (typing) return;
      keysRef.current.add(event.key);
      if (['ArrowUp', 'ArrowDown', 'ArrowLeft', 'ArrowRight', ' '].includes(event.key)) {
        event.preventDefault();
      }
      if (event.key === 'l' || event.key === 'L') startLaunch();
      if (event.key === 'm' || event.key === 'M') setView((current) => (current === 'map' ? 'flight' : 'map'));
      if (event.key === 'c' || event.key === 'C') cycleCamera();
    };
    const onKeyUp = (event: KeyboardEvent) => {
      keysRef.current.delete(event.key);
    };
    window.addEventListener('keydown', onKeyDown);
    window.addEventListener('keyup', onKeyUp);
    return () => {
      window.removeEventListener('keydown', onKeyDown);
      window.removeEventListener('keyup', onKeyUp);
    };
  });

  useEffect(() => {
    let raf = 0;
    let last = performance.now();
    const loop = (now: number) => {
      const dt = clamp((now - last) / 1000, 0, 0.075);
      last = now;
      setFlight((previous) => advanceFlight(previous, dt, keysRef.current, statsRef.current, contract));

      if (view === 'roverDrive' && activeRover) {
        setRovers((current) =>
          current.map((rover) => {
            if (rover.id !== activeRover.id) return rover;
            const speed = keysRef.current.has('Shift') ? 0.12 : 0.065;
            let x = rover.x;
            let z = rover.z;
            if (keysRef.current.has('ArrowLeft') || keysRef.current.has('a') || keysRef.current.has('A')) x -= speed;
            if (keysRef.current.has('ArrowRight') || keysRef.current.has('d') || keysRef.current.has('D')) x += speed;
            if (keysRef.current.has('ArrowUp') || keysRef.current.has('w') || keysRef.current.has('W')) z -= speed;
            if (keysRef.current.has('ArrowDown') || keysRef.current.has('s') || keysRef.current.has('S')) z += speed;
            return { ...rover, x: clamp(x, -15, 15), z: clamp(z, -15, 15) };
          }),
        );
      }
      raf = requestAnimationFrame(loop);
    };
    raf = requestAnimationFrame(loop);
    return () => cancelAnimationFrame(raf);
  }, [activeRover, contract, view]);

  useEffect(() => {
    if (flight.contractComplete && !awardedRef.current.contract) {
      awardedRef.current.contract = true;
      setContract((current) => ({ ...current, complete: true }));
      setEconomy((current) => ({ ...current, astrobucks: current.astrobucks + contract.reward }));
    }

    if (flight.status === 'landed') {
      const landingKey = `${flight.location}-${activeDesign.id}`;
      if (awardedRef.current.landing !== landingKey) {
        awardedRef.current.landing = landingKey;
        setEconomy((current) => ({
          ...current,
          landedBodies: Array.from(new Set([...current.landedBodies, flight.location])),
          astrobucks: current.astrobucks + (flight.location === 'earth' ? 0 : 180),
        }));
        if (stats.rover) {
          setRovers((current) => {
            const already = current.some((rover) => rover.body === flight.location);
            return already ? current : [...current, deployRover(flight.location, current)];
          });
        }
      }
    }
  }, [activeDesign.id, contract.reward, flight.contractComplete, flight.location, flight.status, stats.rover]);

  function startLaunch() {
    setView('flight');
    setFlight((current) => {
      if (['ascent', 'orbit', 'transfer'].includes(current.status)) return current;
      return {
        ...createInitialFlight(statsRef.current),
        status: 'countdown',
        countdown: 10,
        throttle: 1,
        message: 'Countdown started. Press arrow keys or WASD to steer after liftoff.',
      };
    });
  }

  function cycleCamera() {
    setCameraMode((current) => (current === 'third' ? 'cockpit' : current === 'cockpit' ? 'orbit' : 'third'));
  }

  function addPart(partId: string) {
    const part = COMPONENTS.find((item) => item.id === partId);
    if (!part || missingUnlock(part, economy)) return;
    updateDesign((design) => ({ ...design, parts: [...design.parts, partId] }));
  }

  function insertPart(partId: string, index: number) {
    const part = COMPONENTS.find((item) => item.id === partId);
    if (!part || missingUnlock(part, economy)) return;
    updateDesign((design) => {
      const parts = [...design.parts];
      parts.splice(clamp(index, 0, parts.length), 0, partId);
      return { ...design, parts };
    });
  }

  function movePart(from: number, to: number) {
    updateDesign((design) => {
      const parts = [...design.parts];
      if (from < 0 || from >= parts.length) return design;
      const [moved] = parts.splice(from, 1);
      const target = clamp(from < to ? to - 1 : to, 0, parts.length);
      parts.splice(target, 0, moved);
      return { ...design, parts };
    });
  }

  function removePart(index: number) {
    updateDesign((design) => ({ ...design, parts: design.parts.filter((_, itemIndex) => itemIndex !== index) }));
  }

  function saveNewDesign() {
    const next = makeDesign(activeDesign.parts);
    next.name = `${activeDesign.name} Copy`;
    setDesigns((current) => [next, ...current]);
    setActiveDesignId(next.id);
  }

  function createNewDesign() {
    const next = makeDesign(['parachute', 'capsule-mk1', 'tank-core', 'engine-orbiter']);
    next.name = 'New Rocket';
    setDesigns((current) => [next, ...current]);
    setActiveDesignId(next.id);
    setView('builder');
  }

  function deleteDesign(id: string) {
    setDesigns((current) => {
      const next = current.filter((design) => design.id !== id);
      if (id === activeDesignId) setActiveDesignId(next[0]?.id ?? defaultDesign.id);
      return next.length ? next : [defaultDesign];
    });
  }

  function setThrottle(value: number) {
    setFlight((current) => ({ ...current, throttle: clamp(value, 0, 1) }));
  }

  function setTimeScale(value: number) {
    setFlight((current) => ({ ...current, timeScale: value }));
  }

  function beginTransfer(bodyId: BodyId) {
    const body = BODY_BY_ID[bodyId];
    if (!canAccessBody(bodyId, economy)) {
      setFlight((current) => ({ ...current, message: `${body.name} is locked in Mission Market.` }));
      setView('market');
      return;
    }
    if (flight.status !== 'orbit') {
      setFlight((current) => ({ ...current, message: 'Reach a stable orbit before plotting an interplanetary burn.' }));
      return;
    }
    const cost = transferCostKm(bodyId, flight.location);
    if (flight.fuel < cost) {
      setFlight((current) => ({
        ...current,
        message: `Transfer needs ${cost} fuel. Add efficient engines or larger tanks.`,
      }));
      return;
    }
    setFlight((current) => ({
      ...current,
      status: 'transfer',
      target: bodyId,
      fuel: current.fuel - cost,
      transferProgress: 0,
      timeScale: Math.max(current.timeScale, 25),
      message: `Transfer burn committed for ${body.name}. Use time warp outside atmospheres.`,
    }));
    setView('flight');
  }

  function beginLanding() {
    if (flight.status !== 'orbit') return;
    const body = BODY_BY_ID[flight.location];
    // De-orbit + entry: drop to a final-approach altitude, then descend under guidance.
    setFlight((current) => ({
      ...current,
      status: 'ascent',
      altitude: body.atmosphere ? Math.min(current.altitude, 9000) : Math.min(current.altitude, 2200),
      verticalSpeed: body.atmosphere ? -40 : -8,
      horizontalSpeed: Math.min(current.horizontalSpeed, 50),
      speed: 50,
      throttle: body.atmosphere ? 0 : 0.45,
      pitch: 90,
      heat: 0,
      message: body.atmosphere
        ? `Entry complete at ${body.name}. Parachutes are out — use time warp to ride the descent down.`
        : `Starting powered descent at ${body.name}. Hold throttle to settle onto the surface.`,
    }));
  }

  function deployPayload() {
    if (!stats.satellite || flight.status !== 'orbit') {
      setFlight((current) => ({ ...current, message: 'You need a satellite bus and a stable orbit to deploy payloads.' }));
      return;
    }
    const inBand = Math.abs(flight.altitude / 1000 - contract.desiredOrbitKm) <= contract.toleranceKm;
    if (!contract.complete && inBand) {
      setContract((current) => ({ ...current, complete: true }));
      setEconomy((current) => ({ ...current, astrobucks: current.astrobucks + contract.reward }));
      setFlight((current) => ({
        ...current,
        message: `${contract.company} accepted the orbit. ${money(contract.reward)} received.`,
      }));
    } else {
      setFlight((current) => ({ ...current, message: 'Payload deployed, but the orbit missed the active contract band.' }));
    }
  }

  function startEva() {
    if (flight.status !== 'landed') return;
    setView('eva');
    setFlight((current) => ({ ...current, status: 'eva', oxygen: 600, message: 'EVA started. Collect samples and return before oxygen runs out.' }));
  }

  function collectEvaSample() {
    if (flight.status !== 'eva') return;
    const bonus = activeDesign.parts.includes('cargo-lab') ? 2 : 1;
    const payout = 55 * bonus;
    setSampleCount((current) => current + 1);
    setEconomy((current) => ({ ...current, astrobucks: current.astrobucks + payout }));
    setFlight((current) => ({
      ...current,
      oxygen: Math.max(0, current.oxygen - 18),
      message: `Collected a ${bodyDisplayName(current.location)} sample. ${money(payout)} earned.`,
    }));
  }

  function endEva() {
    setView('flight');
    setFlight((current) => ({ ...current, status: 'landed', oxygen: 600, message: 'Astronaut returned to the spacecraft.' }));
  }

  function scanRoverRock() {
    if (!activeRover) return;
    const rocks = seededRocks(activeRover.body);
    const nextRock = rocks[activeRover.rocksCollected % rocks.length];
    const distance = Math.hypot(activeRover.x - nextRock.x, activeRover.z - nextRock.z);
    if (distance > 3.2) {
      setFlight((current) => ({ ...current, message: 'Drive closer to the next highlighted sample field.' }));
      return;
    }
    const bonus = activeDesign.parts.includes('cargo-lab') ? 2 : 1;
    const payout = nextRock.value * bonus;
    setRovers((current) =>
      current.map((rover) =>
        rover.id === activeRover.id ? { ...rover, rocksCollected: rover.rocksCollected + 1 } : rover,
      ),
    );
    setEconomy((current) => ({ ...current, astrobucks: current.astrobucks + payout }));
  }

  function unlockBody(bodyId: BodyId) {
    const body = BODY_BY_ID[bodyId];
    const cost = body.unlockCost ?? 0;
    if (economy.unlockedBodies.includes(bodyId) || !cost || economy.astrobucks < cost) return;
    const related = CELESTIAL_BODIES.filter((candidate) => candidate.id === bodyId || candidate.parent === bodyId).map(
      (candidate) => candidate.id,
    );
    setEconomy((current) => ({
      ...current,
      astrobucks: current.astrobucks - cost,
      unlockedBodies: Array.from(new Set([...current.unlockedBodies, ...related])),
    }));
  }

  function unlockPart(partId: string) {
    const part = COMPONENTS.find((component) => component.id === partId);
    const cost = part?.unlockCost ?? 0;
    if (!part || economy.unlockedParts.includes(partId) || economy.astrobucks < cost) return;
    setEconomy((current) => ({
      ...current,
      astrobucks: current.astrobucks - cost,
      unlockedParts: [...current.unlockedParts, partId],
    }));
  }

  function nudge(key: string, down: boolean) {
    if (down) keysRef.current.add(key);
    else keysRef.current.delete(key);
  }

  const lockedBodies = MARKET_UNLOCKS.filter((body) => !economy.unlockedBodies.includes(body.id));
  const lockedParts = COMPONENTS.filter((component) => component.unlockCost && !economy.unlockedParts.includes(component.id));
  const components = availableComponents(economy);
  const activeBody = BODY_BY_ID[flight.location];

  return (
    <main className={`app-shell view-${view}`}>
      <SpaceScene
        view={view}
        design={activeDesign}
        flight={flight}
        selectedBody={selectedBody}
        cameraMode={cameraMode}
        rover={activeRover}
        showOtherRockets={showOtherRockets}
        peerCount={multiplayer.peers.length}
      />

      <header className="topbar">
        <button className="brand" type="button" onClick={() => setView('mission')}>
          <Rocket size={20} />
          <span>Rocket Sim</span>
        </button>
        <nav>
          <button
            type="button"
            className={view === 'builder' ? 'selected' : ''}
            aria-current={view === 'builder' ? 'page' : undefined}
            onClick={() => setView('builder')}
          >
            <Hammer size={16} /> Builder
          </button>
          <button
            type="button"
            className={view === 'flight' ? 'selected' : ''}
            aria-current={view === 'flight' ? 'page' : undefined}
            onClick={() => setView('flight')}
          >
            <Gauge size={16} /> Flight
          </button>
          <button
            type="button"
            className={view === 'map' ? 'selected' : ''}
            aria-current={view === 'map' ? 'page' : undefined}
            onClick={() => setView('map')}
          >
            <Map size={16} /> Map
          </button>
          <button
            type="button"
            className={view === 'market' ? 'selected' : ''}
            aria-current={view === 'market' ? 'page' : undefined}
            onClick={() => setView('market')}
          >
            <ShoppingCart size={16} /> Market
          </button>
        </nav>
        <button className="icon-button" type="button" aria-label="Settings" onClick={() => setSettingsOpen((open) => !open)}>
          <Settings size={20} />
        </button>
      </header>

      {view === 'mission' && (
        <section className="mission-panel glass">
          <div>
            <h1>Mission Control</h1>
            <p>Launch Pad Ready</p>
          </div>
          <button type="button" onClick={createNewDesign}>
            <Plus size={18} /> Create Rocket
          </button>
          <button type="button" onClick={() => setView('builder')}>
            <FolderOpen size={18} /> Load Rocket
          </button>
          <button type="button" onClick={() => setView('rovers')}>
            <Car size={18} /> Use Rovers
          </button>
          <button type="button" onClick={() => setView('multiplayer')}>
            <Users size={18} /> Multiplayer
          </button>
          <button className="primary" type="button" onClick={startLaunch}>
            <Play size={18} /> Launch
          </button>
          <div className="mission-readout">
            <span>Active design</span>
            <strong>{activeDesign.name}</strong>
            <span>Astrobucks</span>
            <strong>{money(economy.astrobucks)}</strong>
          </div>
        </section>
      )}

      {view === 'builder' && (
        <section className="builder-ui">
          <aside className="component-drawer glass">
            <h2>
              <Hammer size={18} /> Parts Catalog
            </h2>
            <p className="drawer-hint">Drag a part onto the rocket — or click to add it on top.</p>
            {COMPONENT_CATEGORIES.map((category) => (
              <div key={category} className="component-category">
                <h3>
                  <CategoryIcon category={category} size={13} /> {category}
                </h3>
                {components
                  .filter((component) => component.category === category)
                  .map((component) => (
                    <button
                      key={component.id}
                      className={`component-button ${drag?.kind === 'palette' && drag.id === component.id ? 'dragging' : ''}`}
                      type="button"
                      draggable
                      onDragStart={(event) => {
                        event.dataTransfer.effectAllowed = 'copy';
                        event.dataTransfer.setData('text/plain', component.id);
                        setDrag({ kind: 'palette', id: component.id });
                      }}
                      onDragEnd={() => {
                        setDrag(null);
                        setDropIndex(null);
                      }}
                      onClick={() => addPart(component.id)}
                    >
                      <span className="component-glyph">
                        <PartGlyph part={component} size={30} />
                      </span>
                      <span className="component-name">{component.name}</span>
                      <small>{componentSummary(component)}</small>
                    </button>
                  ))}
              </div>
            ))}
          </aside>

          <div
            className={`assembly glass ${drag ? 'drag-active' : ''}`}
            onDragOver={(event) => {
              event.preventDefault();
              event.dataTransfer.dropEffect = drag?.kind === 'move' ? 'move' : 'copy';
              if (dropIndex === null) setDropIndex(activeDesign.parts.length);
            }}
            onDragLeave={(event) => {
              if (event.currentTarget === event.target) setDropIndex(null);
            }}
            onDrop={(event) => {
              event.preventDefault();
              const target = dropIndex ?? activeDesign.parts.length;
              if (drag?.kind === 'palette') insertPart(drag.id, target);
              else if (drag?.kind === 'move') movePart(drag.index, target);
              else {
                const id = event.dataTransfer.getData('text/plain');
                if (id) insertPart(id, target);
              }
              setDrag(null);
              setDropIndex(null);
            }}
          >
            <div className="assembly-head">
              <h2>{activeDesign.name}</h2>
              <span>Nose at the top · engines at the base · drag to reorder</span>
            </div>
            <div className="rocket-stack">
              {activeDesign.parts.length === 0 && (
                <div className="empty-stack">
                  <Rocket size={26} />
                  <p>Drag parts from the catalog to start building.</p>
                </div>
              )}
              {dropIndex === 0 && activeDesign.parts.length > 0 && <div className="drop-line" />}
              {activeDesign.parts.map((partId, index) => {
                const component = componentById[partId];
                return (
                  <Fragment key={`${partId}-${index}`}>
                    <div
                      className={`stack-part ${drag?.kind === 'move' && drag.index === index ? 'dragging' : ''}`}
                      draggable
                      onDragStart={(event) => {
                        event.dataTransfer.effectAllowed = 'move';
                        event.dataTransfer.setData('text/plain', partId);
                        setDrag({ kind: 'move', index });
                      }}
                      onDragEnd={() => {
                        setDrag(null);
                        setDropIndex(null);
                      }}
                      onDragOver={(event) => {
                        event.preventDefault();
                        const rect = event.currentTarget.getBoundingClientRect();
                        const before = event.clientY < rect.top + rect.height / 2;
                        setDropIndex(before ? index : index + 1);
                      }}
                    >
                      <GripVertical className="grip" size={16} />
                      <span className="stack-glyph">
                        {component && <PartGlyph part={component} size={32} />}
                      </span>
                      <div className="stack-info">
                        <strong>{component?.name ?? partId}</strong>
                        <small>{component ? componentSummary(component) : ''}</small>
                      </div>
                      <button
                        type="button"
                        aria-label="Remove part"
                        className="stack-remove"
                        onClick={() => removePart(index)}
                      >
                        <Trash2 size={14} />
                      </button>
                    </div>
                    {dropIndex === index + 1 && <div className="drop-line" />}
                  </Fragment>
                );
              })}
            </div>
          </div>

          <aside className="builder-inspector glass">
            <label>
              Design name
              <input
                value={activeDesign.name}
                onChange={(event) =>
                  updateDesign((design) => ({ ...design, name: event.target.value || 'Untitled Rocket' }))
                }
              />
            </label>
            <div className="stat-grid">
              <span>Mass</span>
              <strong>{stat(stats.mass, 1)} t</strong>
              <span>Thrust</span>
              <strong>{stat(stats.thrust)} kN</strong>
              <span>TWR</span>
              <strong className={stats.thrustToWeight < 1.1 ? 'danger' : ''}>{stat(stats.thrustToWeight, 2)}</strong>
              <span>Delta-v</span>
              <strong>{stat(stats.deltaV)} m/s</strong>
              <span>Stages</span>
              <strong>{stats.stages}</strong>
              <span>Rover</span>
              <strong>{stats.rover ? 'Aboard' : 'No'}</strong>
            </div>
            <div className="builder-actions">
              <button type="button" onClick={saveNewDesign}>
                <Save size={16} /> Save Copy
              </button>
              <button type="button" onClick={createNewDesign}>
                <Plus size={16} /> New
              </button>
              <button className="primary" type="button" onClick={startLaunch}>
                <Play size={16} /> Launch
              </button>
            </div>
            <div className="saved-designs">
              <h3>Saved designs</h3>
              {designs.map((design) => (
                <button
                  type="button"
                  key={design.id}
                  className={design.id === activeDesign.id ? 'selected' : ''}
                  onClick={() => setActiveDesignId(design.id)}
                >
                  <span>{design.name}</span>
                  <small>{design.parts.length} parts</small>
                  {designs.length > 1 && (
                    <Trash2
                      size={14}
                      onClick={(event) => {
                        event.stopPropagation();
                        deleteDesign(design.id);
                      }}
                    />
                  )}
                </button>
              ))}
            </div>
          </aside>
        </section>
      )}

      {view === 'flight' && (
        <section className="flight-ui">
          <MiniMap flight={flight} showOtherRockets={showOtherRockets} />
          <StatusPill flight={flight} />
          <div className="flight-message glass">{flight.message}</div>
          <div className="contract-card glass">
            <Satellite size={17} />
            <div>
              <strong>{contract.company} satellite contract</strong>
              <span>
                {contract.complete
                  ? `Complete · ${money(contract.reward)} paid`
                  : `${contract.desiredOrbitKm} km ±${contract.toleranceKm} · ${money(contract.reward)}`}
              </span>
            </div>
          </div>
          <Telemetry flight={flight} stats={stats} />
          <div className="flight-controls glass">
            <div className="throttle">
              <span>Throttle</span>
              <input
                aria-label="Throttle"
                type="range"
                min="0"
                max="1"
                step="0.01"
                value={flight.throttle}
                onChange={(event) => setThrottle(Number(event.target.value))}
              />
              <strong>{Math.round(flight.throttle * 100)}%</strong>
            </div>
            <button type="button" onClick={startLaunch}>
              <Timer size={16} /> Launch
            </button>
            <button type="button" onClick={deployPayload}>
              <Satellite size={16} /> Deploy
            </button>
            <button type="button" onClick={beginLanding}>
              <LocateFixed size={16} /> Land
            </button>
            <button type="button" onClick={startEva} disabled={flight.status !== 'landed'}>
              <Shield size={16} /> EVA
            </button>
            <button type="button" onClick={cycleCamera}>
              <Camera size={16} /> {cameraMode}
            </button>
            <div className="timewarp">
              {timeScales.map((scale) => (
                <button
                  key={scale}
                  type="button"
                  className={flight.timeScale === scale ? 'selected' : ''}
                  onClick={() => setTimeScale(scale)}
                >
                  {scale}x
                </button>
              ))}
            </div>
          </div>
          <div className="mobile-controls">
            {['ArrowUp', 'ArrowLeft', 'ArrowDown', 'ArrowRight'].map((key) => (
              <button
                key={key}
                type="button"
                onPointerDown={() => nudge(key, true)}
                onPointerUp={() => nudge(key, false)}
                onPointerLeave={() => nudge(key, false)}
              >
                {key.replace('Arrow', '')}
              </button>
            ))}
          </div>
          {(flight.status === 'crashed' || flight.status === 'burned') && (
            <div className="failure glass">
              <AlertTriangle size={30} />
              <strong>{flight.status === 'burned' ? 'Vehicle burned up' : 'Vehicle destroyed'}</strong>
              <p>{flight.message}</p>
              <button type="button" onClick={resetFlight}>
                <RotateCcw size={16} /> Reset mission
              </button>
            </div>
          )}
        </section>
      )}

      {view === 'map' && (
        <section className="map-ui glass">
          <div className="map-header">
            <h2>Solar System Map</h2>
            <button type="button" onClick={() => setView('flight')}>
              Close Map
            </button>
          </div>
          <div className="body-grid">
            {CELESTIAL_BODIES.filter((body) => body.id !== 'sun').map((body) => {
              const accessible = canAccessBody(body.id, economy);
              return (
                <button
                  key={body.id}
                  type="button"
                  className={`${selectedBody === body.id ? 'selected' : ''} ${accessible ? '' : 'locked'}`}
                  onClick={() => setSelectedBody(body.id)}
                >
                  <span style={{ background: body.color }} />
                  <strong>{body.name}</strong>
                  <small>{accessible ? `${body.kind}${body.canLand ? ' · landable' : ''}` : 'locked'}</small>
                </button>
              );
            })}
          </div>
          <div className="map-detail">
            <h3>{BODY_BY_ID[selectedBody].name}</h3>
            <p>
              Gravity {BODY_BY_ID[selectedBody].gravity} m/s² ·{' '}
              {BODY_BY_ID[selectedBody].atmosphere ? 'atmosphere' : 'airless'} ·{' '}
              {canAccessBody(selectedBody, economy) ? 'access granted' : 'locked'}
            </p>
            <div className="map-actions">
              <button type="button" onClick={() => beginTransfer(selectedBody)}>
                <Send size={16} /> Transfer burn
              </button>
              <label className="toggle">
                <input
                  type="checkbox"
                  checked={showOtherRockets}
                  onChange={(event) => setShowOtherRockets(event.target.checked)}
                />
                Show other rockets
              </label>
            </div>
          </div>
        </section>
      )}

      {view === 'rovers' && (
        <section className="rovers-ui glass">
          <h2>Rover Garage</h2>
          {rovers.length === 0 ? (
            <p>Land a rocket with a Rover Bay on the Moon, Mars, Venus, or another unlocked world to add rovers here.</p>
          ) : (
            <div className="rover-list">
              {rovers.map((rover) => (
                <button
                  key={rover.id}
                  type="button"
                  className={activeRover?.id === rover.id ? 'selected' : ''}
                  onClick={() => {
                    setActiveRoverId(rover.id);
                    setView('roverDrive');
                  }}
                >
                  <Car size={18} />
                  <span>{rover.name}</span>
                  <small>
                    {bodyDisplayName(rover.body)} · {rover.rocksCollected} rocks
                  </small>
                </button>
              ))}
            </div>
          )}
        </section>
      )}

      {view === 'roverDrive' && (
        <section className="rover-hud">
          <div className="rover-card glass">
            <h2>{activeRover?.name ?? 'Rover'}</h2>
            <p>Use WASD or arrow keys to drive. Scan when close to a sample field.</p>
            <button type="button" onClick={scanRoverRock}>
              <Crosshair size={16} /> Scan rock
            </button>
            <button type="button" onClick={() => setView('rovers')}>
              Garage
            </button>
          </div>
        </section>
      )}

      {view === 'eva' && (
        <section className="eva-ui glass">
          <h2>EVA on {activeBody.name}</h2>
          <div className="oxygen">
            <span>Oxygen</span>
            <strong>{Math.floor(flight.oxygen / 60)}:{String(Math.floor(flight.oxygen % 60)).padStart(2, '0')}</strong>
          </div>
          <p>{flight.message}</p>
          <button type="button" onClick={collectEvaSample}>
            <Package size={16} /> Collect sample
          </button>
          <button type="button" onClick={endEva}>
            Return to craft
          </button>
          <small>{sampleCount} EVA samples collected</small>
        </section>
      )}

      {view === 'market' && (
        <section className="market-ui glass">
          <div className="market-header">
            <h2>Mission Market</h2>
            <strong>
              <CircleDollarSign size={18} /> {money(economy.astrobucks)}
            </strong>
          </div>
          <h3>Planetary access</h3>
          <div className="market-grid">
            {lockedBodies.map((body) => (
              <button
                key={body.id}
                type="button"
                disabled={economy.astrobucks < (body.unlockCost ?? 0)}
                onClick={() => unlockBody(body.id)}
              >
                <span style={{ background: body.color }} />
                <strong>{body.name}</strong>
                <small>
                  Unlocks {body.name}
                  {CELESTIAL_BODIES.some((candidate) => candidate.parent === body.id) ? ' system' : ''} ·{' '}
                  {money(body.unlockCost ?? 0)}
                </small>
              </button>
            ))}
            {lockedBodies.length === 0 && <p>All major destinations are unlocked.</p>}
          </div>
          <h3>Component upgrades</h3>
          <div className="market-grid">
            {lockedParts.map((part) => (
              <button
                key={part.id}
                type="button"
                disabled={economy.astrobucks < (part.unlockCost ?? 0)}
                onClick={() => unlockPart(part.id)}
              >
                <Zap size={17} />
                <strong>{part.name}</strong>
                <small>{money(part.unlockCost ?? 0)} · permanent upgrade</small>
              </button>
            ))}
          </div>
        </section>
      )}

      {view === 'multiplayer' && (
        <section className="multiplayer-ui glass">
          <h2>Multiplayer</h2>
          <p>Share a room code. Rocket edits and launch telemetry broadcast in realtime through the Rocket Sim WebSocket server.</p>
          <label>
            Room code
            <input value={room} onChange={(event) => setRoom(event.target.value)} />
          </label>
          <button
            className={multiplayerEnabled ? 'selected' : 'primary'}
            type="button"
            onClick={() => setMultiplayerEnabled((enabled) => !enabled)}
          >
            <Users size={16} /> {multiplayerEnabled ? 'Leave room' : 'Join room'}
          </button>
          <div className="multiplayer-status">
            <span>Status</span>
            <strong>{multiplayer.status}</strong>
            <span>Your pilot</span>
            <strong>{multiplayer.clientId}</strong>
          </div>
          <div className="peer-list">
            {multiplayer.peers.map((peer) => (
              <div key={peer.id}>
                <strong>{peer.name}</strong>
                <span>
                  {peer.view} · {peer.designName} · {meters(peer.altitude)}
                </span>
              </div>
            ))}
            {multiplayer.peers.length === 0 && <span>No peers visible yet.</span>}
          </div>
        </section>
      )}

      {settingsOpen && (
        <aside className="settings-drawer glass">
          <h2>Controls</h2>
          <dl>
            <dt>Launch</dt>
            <dd>L or Launch button</dd>
            <dt>Pitch / yaw</dt>
            <dd>Arrow keys or WASD</dd>
            <dt>Throttle</dt>
            <dd>Slider, +, -</dd>
            <dt>Map</dt>
            <dd>M</dd>
            <dt>Camera</dt>
            <dd>C</dd>
          </dl>
          <h3>Texture sources</h3>
          {REQUIRED_TEXTURE_SOURCES.map((source) => (
            <a key={source.url} href={source.url} target="_blank" rel="noreferrer">
              {source.label}
            </a>
          ))}
        </aside>
      )}
    </main>
  );
}

export default App;
