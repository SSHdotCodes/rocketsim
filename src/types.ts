export type View =
  | 'mission'
  | 'builder'
  | 'flight'
  | 'map'
  | 'rovers'
  | 'roverDrive'
  | 'eva'
  | 'market'
  | 'multiplayer';

export type ComponentCategory =
  | 'Structure'
  | 'Tanks'
  | 'Engines'
  | 'Control'
  | 'Science'
  | 'Landing'
  | 'Payload';

export type ComponentSize = 'small' | 'medium' | 'large';

export interface RocketComponent {
  id: string;
  name: string;
  category: ComponentCategory;
  description: string;
  mass: number;
  cost: number;
  size: ComponentSize;
  fuel?: number;
  thrust?: number;
  isp?: number;
  drag?: number;
  heatShield?: boolean;
  parachute?: boolean;
  roverBay?: boolean;
  satellite?: boolean;
  solar?: boolean;
  landingFeet?: boolean;
  stageSeparator?: boolean;
  boosterSeparator?: boolean;
  wheels?: boolean;
  unlockCost?: number;
}

export interface RocketDesign {
  id: string;
  name: string;
  parts: string[];
  createdAt: number;
  updatedAt: number;
}

export interface RocketStats {
  mass: number;
  dryMass: number;
  fuel: number;
  thrust: number;
  thrustToWeight: number;
  deltaV: number;
  drag: number;
  heatShield: boolean;
  parachutes: number;
  rover: boolean;
  satellite: boolean;
  solar: boolean;
  landingFeet: boolean;
  wheels: boolean;
  stages: number;
  cost: number;
}

export type BodyId =
  | 'sun'
  | 'mercury'
  | 'venus'
  | 'earth'
  | 'moon'
  | 'mars'
  | 'phobos'
  | 'deimos'
  | 'jupiter'
  | 'io'
  | 'europa'
  | 'ganymede'
  | 'callisto'
  | 'saturn'
  | 'titan'
  | 'enceladus'
  | 'rhea'
  | 'iapetus'
  | 'uranus'
  | 'neptune'
  | 'triton'
  | 'pluto';

export interface CelestialBody {
  id: BodyId;
  name: string;
  kind: 'star' | 'planet' | 'moon' | 'dwarf';
  texture: string;
  radiusKm: number;
  gravity: number;
  atmosphere: boolean;
  orbitRadius: number;
  orbitDays: number;
  color: string;
  parent?: BodyId;
  unlockCost?: number;
  initialAccess?: boolean;
  canLand?: boolean;
}

export type FlightStatus =
  | 'pad'
  | 'countdown'
  | 'ascent'
  | 'orbit'
  | 'transfer'
  | 'landed'
  | 'crashed'
  | 'burned'
  | 'eva';

export interface FlightState {
  status: FlightStatus;
  location: BodyId;
  target?: BodyId;
  countdown: number;
  altitude: number;
  verticalSpeed: number;
  horizontalSpeed: number;
  speed: number;
  pitch: number;
  heading: number;
  throttle: number;
  fuel: number;
  heat: number;
  apoapsis: number;
  periapsis: number;
  timeScale: number;
  oxygen: number;
  elapsed: number;
  transferProgress: number;
  message: string;
  contractComplete: boolean;
}

export interface EconomyState {
  astrobucks: number;
  unlockedBodies: BodyId[];
  unlockedParts: string[];
  landedBodies: BodyId[];
}

export interface RoverState {
  id: string;
  body: BodyId;
  name: string;
  x: number;
  z: number;
  rocksCollected: number;
}

export interface ContractState {
  id: string;
  company: string;
  desiredOrbitKm: number;
  toleranceKm: number;
  reward: number;
  requiresSatellite: boolean;
  complete: boolean;
}

export interface PeerState {
  id: string;
  name: string;
  view: View;
  designName: string;
  altitude: number;
  speed: number;
  updatedAt: number;
}
