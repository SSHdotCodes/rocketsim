import { BODY_BY_ID, COMPONENTS, FIRST_ACCESS } from './gameData';
import type {
  BodyId,
  CelestialBody,
  ContractState,
  EconomyState,
  FlightState,
  RocketDesign,
  RocketStats,
  RoverState,
} from './types';

// Realistic-looking circular orbital velocity near the surface (m/s).
export function orbitalSpeed(body: CelestialBody) {
  return Math.sqrt(body.gravity * body.radiusKm * 1000);
}

// Edge of the (game) atmosphere in metres; 0 for airless worlds.
export function atmosphereTop(body: CelestialBody) {
  return body.atmosphere ? Math.max(60000, body.radiusKm * 12) : 0;
}

// Altitude where the gravity turn should be nearly horizontal / a parking orbit sits.
export function orbitAltitude(body: CelestialBody) {
  return body.atmosphere ? atmosphereTop(body) * 1.4 : Math.max(18000, body.radiusKm * 1000 * 0.012);
}

export const componentById = Object.fromEntries(
  COMPONENTS.map((component) => [component.id, component]),
);

export const defaultDesign: RocketDesign = {
  id: 'starter-atlas',
  name: 'Starter Atlas',
  parts: [
    'parachute',
    'nose-aero',
    'capsule-mk1',
    'satellite-bus',
    'rover-bay',
    'solar-panels',
    'tank-core',
    'separator-stage',
    'tank-core',
    'guidance-ring',
    'heat-shield',
    'landing-feet',
    'engine-orbiter',
  ],
  createdAt: Date.now(),
  updatedAt: Date.now(),
};

export const defaultContract: ContractState = {
  id: 'orbitnet-181',
  company: 'OrbitNet',
  desiredOrbitKm: 180,
  toleranceKm: 60,
  reward: 650,
  requiresSatellite: true,
  complete: false,
};

export function createInitialEconomy(): EconomyState {
  return {
    astrobucks: 900,
    unlockedBodies: FIRST_ACCESS,
    unlockedParts: COMPONENTS.filter((component) => !component.unlockCost).map((component) => component.id),
    landedBodies: ['earth'],
  };
}

export function createInitialFlight(stats: RocketStats): FlightState {
  return {
    status: 'pad',
    location: 'earth',
    countdown: 10,
    altitude: 0,
    verticalSpeed: 0,
    horizontalSpeed: 0,
    speed: 0,
    pitch: 90,
    heading: 90,
    throttle: 0,
    fuel: stats.fuel,
    heat: 0,
    apoapsis: 0,
    periapsis: 0,
    timeScale: 1,
    oxygen: 600,
    elapsed: 0,
    transferProgress: 0,
    message: 'Pad systems green. Press L or Launch to begin the 10 second countdown.',
    contractComplete: false,
  };
}

export function makeDesign(parts = defaultDesign.parts): RocketDesign {
  const now = Date.now();
  return {
    id: `rocket-${now}`,
    name: `Rocket ${new Date(now).toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' })}`,
    parts: [...parts],
    createdAt: now,
    updatedAt: now,
  };
}

export function calculateStats(design: RocketDesign): RocketStats {
  const components = design.parts.map((part) => componentById[part]).filter(Boolean);
  const dryMass = components.reduce((sum, part) => sum + part.mass, 0);
  const fuel = components.reduce((sum, part) => sum + (part.fuel ?? 0), 0);
  const fuelMass = fuel / 320;
  const mass = Math.max(1, dryMass + fuelMass);
  const thrust = components.reduce((sum, part) => sum + (part.thrust ?? 0), 0);
  const maxIsp =
    components.reduce((best, part) => Math.max(best, part.isp ?? 0), 0) || 285;
  const drag = Math.max(0.28, 0.78 + components.reduce((sum, part) => sum + (part.drag ?? 0), 0));
  const deltaV = maxIsp * 9.81 * Math.log((dryMass + fuelMass) / Math.max(dryMass, 0.5));
  const stages = Math.max(1, 1 + components.filter((part) => part.stageSeparator).length);

  return {
    mass,
    dryMass,
    fuel,
    thrust,
    thrustToWeight: thrust / Math.max(1, mass * 9.81),
    deltaV,
    drag,
    heatShield: components.some((part) => part.heatShield),
    parachutes: components.filter((part) => part.parachute).length,
    rover: components.some((part) => part.roverBay),
    satellite: components.some((part) => part.satellite),
    solar: components.some((part) => part.solar),
    landingFeet: components.some((part) => part.landingFeet),
    wheels: components.some((part) => part.wheels),
    stages,
    cost: components.reduce((sum, part) => sum + part.cost, 0),
  };
}

export function loadJson<T>(key: string, fallback: T): T {
  try {
    const raw = localStorage.getItem(key);
    return raw ? (JSON.parse(raw) as T) : fallback;
  } catch {
    return fallback;
  }
}

export function saveJson<T>(key: string, value: T) {
  localStorage.setItem(key, JSON.stringify(value));
}

export function canAccessBody(bodyId: BodyId, economy: EconomyState) {
  const body = BODY_BY_ID[bodyId];
  if (body.initialAccess || economy.unlockedBodies.includes(bodyId)) {
    return true;
  }
  if (body.parent && economy.unlockedBodies.includes(body.parent)) {
    return true;
  }
  return false;
}

export function transferCostKm(bodyId: BodyId, current: BodyId) {
  const body = BODY_BY_ID[bodyId];
  const here = BODY_BY_ID[current];
  const base = Math.abs(body.orbitRadius - here.orbitRadius) * 62 + 450;
  const moonBonus = body.parent ? 260 : 0;
  return Math.round(base + moonBonus);
}

export function seededRocks(bodyId: BodyId) {
  const seed = [...bodyId].reduce((sum, char) => sum + char.charCodeAt(0), 0);
  return Array.from({ length: 12 }, (_, index) => {
    const angle = (seed * 17 + index * 47) * (Math.PI / 180);
    const radius = 3.5 + ((seed + index * 11) % 18) / 2;
    return {
      id: `${bodyId}-rock-${index}`,
      x: Math.cos(angle) * radius,
      z: Math.sin(angle) * radius,
      value: 35 + ((seed + index * 13) % 5) * 10,
    };
  });
}

export function deployRover(body: BodyId, existing: RoverState[]): RoverState {
  const currentCount = existing.filter((rover) => rover.body === body).length;
  return {
    id: `rover-${body}-${Date.now()}`,
    body,
    name: `${BODY_BY_ID[body].name} Rover ${currentCount + 1}`,
    x: 0,
    z: 0,
    rocksCollected: 0,
  };
}

export function bodyDisplayName(id: BodyId) {
  return BODY_BY_ID[id]?.name ?? id;
}
