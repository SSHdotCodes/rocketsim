import { useEffect, useRef } from 'react';
import * as THREE from 'three';
import { RoomEnvironment } from 'three/examples/jsm/environments/RoomEnvironment.js';
import { EffectComposer } from 'three/examples/jsm/postprocessing/EffectComposer.js';
import { RenderPass } from 'three/examples/jsm/postprocessing/RenderPass.js';
import { UnrealBloomPass } from 'three/examples/jsm/postprocessing/UnrealBloomPass.js';
import { OutputPass } from 'three/examples/jsm/postprocessing/OutputPass.js';
import { BODY_BY_ID, CELESTIAL_BODIES } from './gameData';
import { componentById } from './sim';
import type { BodyId, FlightState, RocketDesign, RoverState, View } from './types';

type CameraMode = 'third' | 'cockpit' | 'orbit' | 'builder';

interface SpaceSceneProps {
  view: View;
  design: RocketDesign;
  flight: FlightState;
  selectedBody: BodyId;
  cameraMode: CameraMode;
  rover?: RoverState;
  showOtherRockets: boolean;
  peerCount: number;
}

interface Runtime {
  renderer: THREE.WebGLRenderer;
  composer: EffectComposer | null;
  scene: THREE.Scene;
  camera: THREE.PerspectiveCamera;
  sun: THREE.DirectionalLight;
  startedAt: number;
  groups: Record<string, THREE.Group>;
  planetMeshes: Partial<Record<BodyId, THREE.Mesh>>;
  lastRocketKey: string;
  lastRoverKey: string;
  flightBodyKey: string;
  flightAngle: number;
  rocketHeight: number;
  shake: number;
  sunMaterial: THREE.ShaderMaterial;
  plumeMaterial: THREE.ShaderMaterial;
}

// Display radius of the planet you are currently flying around. Kept large so the
// rocket reads as a small craft against a big world rather than a moon-sized object.
const FLIGHT_R = 180;

const textureLoader = new THREE.TextureLoader();
const textureCache = new Map<string, THREE.Texture>();

function getTexture(path: string) {
  if (!textureCache.has(path)) {
    const texture = textureLoader.load(path);
    texture.colorSpace = THREE.SRGBColorSpace;
    texture.anisotropy = 8;
    textureCache.set(path, texture);
  }
  return textureCache.get(path)!;
}

// ---------------------------------------------------------------------------
// Map layout: spread the planets out so they never overlap on the system map.
// ---------------------------------------------------------------------------
const MAP_ORDER: BodyId[] = ['mercury', 'venus', 'earth', 'mars', 'jupiter', 'saturn', 'uranus', 'neptune', 'pluto'];

function mapDistance(bodyId: BodyId) {
  const index = MAP_ORDER.indexOf(bodyId);
  if (index < 0) return 0;
  // Generous, even-ish spacing so nothing crowds.
  return 16 + index * 13;
}

function mapPlanetRadius(bodyId: BodyId) {
  const body = BODY_BY_ID[bodyId];
  if (body.kind === 'star') return 5.4;
  if (body.kind === 'dwarf') return 0.7;
  // Gentle log scale keeps gas giants big without swallowing neighbours.
  return THREE.MathUtils.clamp(Math.log10(body.radiusKm) * 0.62 - 0.7, 0.7, 2.9);
}

function moonDistance(bodyId: BodyId) {
  const body = BODY_BY_ID[bodyId];
  return mapPlanetRadius(body.parent ?? 'earth') + 1.6 + body.orbitRadius * 0.55;
}

// ---------------------------------------------------------------------------
// Materials
// ---------------------------------------------------------------------------
function mat(color: string, metalness: number, roughness: number, extra: THREE.MeshStandardMaterialParameters = {}) {
  return new THREE.MeshStandardMaterial({ color, metalness, roughness, envMapIntensity: 1.1, ...extra });
}

function createSunMaterial() {
  return new THREE.ShaderMaterial({
    uniforms: {
      time: { value: 0 },
      colorA: { value: new THREE.Color('#fff1b0') },
      colorB: { value: new THREE.Color('#ff7a18') },
    },
    vertexShader: `
      varying vec2 vUv;
      varying vec3 vNormal;
      void main() {
        vUv = uv;
        vNormal = normalize(normalMatrix * normal);
        gl_Position = projectionMatrix * modelViewMatrix * vec4(position, 1.0);
      }
    `,
    fragmentShader: `
      uniform float time;
      uniform vec3 colorA;
      uniform vec3 colorB;
      varying vec2 vUv;
      varying vec3 vNormal;
      float hash(vec2 p){ return fract(sin(dot(p, vec2(127.1,311.7)))*43758.5453123); }
      float noise(vec2 p){
        vec2 i=floor(p), f=fract(p);
        float a=hash(i), b=hash(i+vec2(1.,0.)), c=hash(i+vec2(0.,1.)), d=hash(i+vec2(1.,1.));
        vec2 u=f*f*(3.-2.*f);
        return mix(mix(a,b,u.x),mix(c,d,u.x),u.y);
      }
      void main(){
        float n = noise(vUv*9.0 + time*0.12) * 0.5 + noise(vUv*22.0 - time*0.08)*0.5;
        float rim = pow(1.0 - abs(vNormal.z), 2.4);
        vec3 color = mix(colorB, colorA, 0.35 + n*0.6);
        color += vec3(1.0,0.5,0.12) * rim * 1.6;
        gl_FragColor = vec4(color, 1.0);
      }
    `,
  });
}

function createAtmosphereMaterial(color: string, opacity = 0.6, power = 2.6) {
  return new THREE.ShaderMaterial({
    transparent: true,
    side: THREE.BackSide,
    blending: THREE.AdditiveBlending,
    depthWrite: false,
    uniforms: {
      glowColor: { value: new THREE.Color(color) },
      opacity: { value: opacity },
      power: { value: power },
    },
    vertexShader: `
      varying vec3 vNormal;
      void main(){
        vNormal = normalize(normalMatrix * normal);
        gl_Position = projectionMatrix * modelViewMatrix * vec4(position,1.0);
      }
    `,
    fragmentShader: `
      uniform vec3 glowColor; uniform float opacity; uniform float power;
      varying vec3 vNormal;
      void main(){
        float intensity = pow(0.78 - dot(vNormal, vec3(0.0,0.0,1.0)), power);
        gl_FragColor = vec4(glowColor, clamp(intensity,0.0,1.0) * opacity);
      }
    `,
  });
}

function createPlumeMaterial() {
  return new THREE.ShaderMaterial({
    transparent: true,
    blending: THREE.AdditiveBlending,
    depthWrite: false,
    side: THREE.DoubleSide,
    uniforms: { time: { value: 0 }, throttle: { value: 0 } },
    vertexShader: `
      varying vec2 vUv;
      void main(){ vUv = uv; gl_Position = projectionMatrix * modelViewMatrix * vec4(position,1.0); }
    `,
    fragmentShader: `
      uniform float time; uniform float throttle;
      varying vec2 vUv;
      float hash(float n){ return fract(sin(n)*43758.5453); }
      void main(){
        float core = smoothstep(0.5, 0.0, abs(vUv.x - 0.5));
        float fade = smoothstep(1.0, 0.04, vUv.y);
        float shock = 0.78 + 0.22 * sin(time*46.0 + vUv.y*30.0);
        float spark = step(0.93, hash(floor(vUv.y*40.0) + floor(time*60.0)));
        vec3 hot = vec3(1.0, 0.95, 0.8);
        vec3 mid = vec3(1.0, 0.55, 0.12);
        vec3 cool = vec3(0.45, 0.7, 1.0);
        vec3 color = mix(hot, mix(mid, cool, vUv.y), smoothstep(0.0,0.6,vUv.y));
        float a = core * fade * throttle * shock + spark * core * throttle * 0.5;
        gl_FragColor = vec4(color, a);
      }
    `,
  });
}

// ---------------------------------------------------------------------------
// Procedural part geometry helpers
// ---------------------------------------------------------------------------
function ogiveGeometry(radius: number, height: number) {
  const points: THREE.Vector2[] = [];
  const n = 16;
  for (let i = 0; i <= n; i++) {
    const t = i / n;
    const r = radius * Math.sin(t * Math.PI * 0.5);
    points.push(new THREE.Vector2(Math.max(0.001, r), height * (1 - t)));
  }
  return new THREE.LatheGeometry(points, 40);
}

function bellGeometry(throat: number, bell: number, length: number) {
  const points: THREE.Vector2[] = [];
  const n = 14;
  for (let i = 0; i <= n; i++) {
    const t = i / n;
    const r = throat + (bell - throat) * Math.pow(t, 1.7);
    points.push(new THREE.Vector2(Math.max(0.01, r), -length * t));
  }
  return new THREE.LatheGeometry(points, 36);
}

function partHeight(partId: string) {
  const part = componentById[partId];
  if (!part) return 0.5;
  if (part.id.includes('nose')) return 1.15;
  if (part.id.includes('engine')) return 0.78;
  if (part.id.includes('separator')) return 0.16;
  if (part.id.includes('parachute')) return 0.34;
  if (part.id.includes('guidance')) return 0.32;
  if (part.id.includes('solar')) return 0.28;
  if (part.id.includes('satellite')) return 0.5;
  if (part.id.includes('rover')) return 0.9;
  if (part.id.includes('cargo')) return 0.86;
  if (part.id.includes('heat')) return 0.26;
  if (part.id.includes('feet') || part.id.includes('wheel')) return 0.3;
  if (part.size === 'large') return 1.6;
  if (part.size === 'small') return 0.7;
  return 1.12;
}

function partRadius(partId: string) {
  const part = componentById[partId];
  if (!part) return 0.48;
  if (part.size === 'large') return 0.72;
  if (part.size === 'small') return 0.4;
  return 0.54;
}

function tankColor(partId: string) {
  if (partId.includes('hydrolox')) return '#e7f7ff';
  if (partId.includes('heavy')) return '#e0e4e8';
  if (partId.includes('small')) return '#f1f4f7';
  return '#eef1f4';
}

// Build one part as a group positioned with its base at y=0.
function buildPart(partId: string): THREE.Group {
  const g = new THREE.Group();
  const part = componentById[partId];
  const height = partHeight(partId);
  const radius = partRadius(partId);

  if (partId.includes('nose')) {
    const cone = new THREE.Mesh(ogiveGeometry(radius, height), mat('#f4f7fa', 0.4, 0.32));
    g.add(cone);
    const tip = new THREE.Mesh(new THREE.SphereGeometry(radius * 0.16, 16, 12), mat('#2b3440', 0.7, 0.4));
    tip.position.y = height;
    g.add(tip);
  } else if (partId.includes('parachute')) {
    const can = new THREE.Mesh(new THREE.CylinderGeometry(radius * 0.7, radius * 0.7, height, 28), mat('#cdd4db', 0.6, 0.35));
    can.position.y = height / 2;
    g.add(can);
    const cap = new THREE.Mesh(new THREE.CylinderGeometry(radius * 0.72, radius * 0.7, height * 0.18, 28), mat('#d2452f', 0.3, 0.5));
    cap.position.y = height * 0.9;
    g.add(cap);
  } else if (partId.includes('guidance')) {
    const ring = new THREE.Mesh(new THREE.CylinderGeometry(radius, radius, height, 36), mat('#39414c', 0.85, 0.3));
    ring.position.y = height / 2;
    g.add(ring);
    const band = new THREE.Mesh(new THREE.TorusGeometry(radius * 1.01, 0.05, 10, 36), mat('#37b6ff', 0.4, 0.4, { emissive: '#0a3d63', emissiveIntensity: 0.7 }));
    band.rotation.x = Math.PI / 2;
    band.position.y = height / 2;
    g.add(band);
  } else if (partId.includes('capsule')) {
    const pts: THREE.Vector2[] = [];
    const n = 12;
    for (let i = 0; i <= n; i++) {
      const t = i / n;
      pts.push(new THREE.Vector2(Math.max(0.05, radius * (0.55 + 0.45 * t)), height * (1 - t)));
    }
    const body = new THREE.Mesh(new THREE.LatheGeometry(pts, 36), mat('#dfe6ec', 0.55, 0.3));
    g.add(body);
    const window = new THREE.Mesh(new THREE.CircleGeometry(radius * 0.16, 20), mat('#0a141d', 0.9, 0.1, { emissive: '#13344d', emissiveIntensity: 0.5 }));
    window.position.set(0, height * 0.55, radius * 0.62);
    window.lookAt(0, height * 0.55, radius * 4);
    g.add(window);
  } else if (partId.includes('engine')) {
    const block = new THREE.Mesh(new THREE.CylinderGeometry(radius * 0.86, radius * 0.62, height * 0.42, 32), mat('#444d59', 0.9, 0.32));
    block.position.y = height - height * 0.21;
    g.add(block);
    const big = part?.size === 'large' || partId.includes('vector');
    const nozzles: Array<[number, number]> = big ? [[-radius * 0.42, 0], [radius * 0.42, 0], [0, radius * 0.42], [0, -radius * 0.42]] : [[0, 0]];
    const nozR = big ? radius * 0.34 : radius * 0.6;
    for (const [nx, nz] of nozzles) {
      const bell = new THREE.Mesh(bellGeometry(nozR * 0.4, nozR, height * 0.7), mat(partId.includes('nuclear') ? '#9a8f72' : '#1f242b', 0.95, 0.28));
      bell.position.set(nx, height * 0.58, nz);
      g.add(bell);
    }
    if (partId.includes('nuclear')) {
      const glow = new THREE.Mesh(new THREE.SphereGeometry(radius * 0.5, 20, 16), mat('#1b2a1f', 0.3, 0.5, { emissive: '#7dff9a', emissiveIntensity: 0.9 }));
      glow.position.y = height * 0.62;
      g.add(glow);
    }
  } else if (partId.includes('heat')) {
    const pts: THREE.Vector2[] = [];
    const n = 12;
    for (let i = 0; i <= n; i++) {
      const t = i / n;
      pts.push(new THREE.Vector2(radius * 1.12 * Math.sin((t * Math.PI) / 2), -height * Math.cos((t * Math.PI) / 2) * 0.0 - height * (1 - t)));
    }
    const dish = new THREE.Mesh(new THREE.SphereGeometry(radius * 1.16, 36, 18, 0, Math.PI * 2, Math.PI * 0.5, Math.PI * 0.5), mat('#6a4a32', 0.2, 0.85));
    dish.position.y = height;
    g.add(dish);
  } else if (partId.includes('solar')) {
    const hub = new THREE.Mesh(new THREE.CylinderGeometry(radius * 0.5, radius * 0.5, height, 20), mat('#cdd4db', 0.6, 0.35));
    hub.position.y = height / 2;
    g.add(hub);
    const panelMat = mat('#13356b', 0.45, 0.25, { emissive: '#06203f', emissiveIntensity: 0.6 });
    for (const dir of [-1, 1]) {
      const panel = new THREE.Mesh(new THREE.BoxGeometry(radius * 4.4, 0.03, height * 1.6), panelMat);
      panel.position.set(dir * radius * 2.7, height / 2, 0);
      g.add(panel);
    }
  } else if (partId.includes('rover')) {
    const bay = new THREE.Mesh(new THREE.CylinderGeometry(radius, radius, height, 36), mat('#c8a563', 0.5, 0.4));
    bay.position.y = height / 2;
    g.add(bay);
    const hatch = new THREE.Mesh(new THREE.BoxGeometry(radius * 1.2, height * 0.5, 0.04), mat('#3a2f1d', 0.4, 0.6));
    hatch.position.set(0, height * 0.5, radius);
    g.add(hatch);
  } else if (partId.includes('satellite')) {
    const bus = new THREE.Mesh(new THREE.BoxGeometry(radius * 1.3, height, radius * 1.3), mat('#caa84e', 0.8, 0.25, { emissive: '#3a2c08', emissiveIntensity: 0.4 }));
    bus.position.y = height / 2;
    g.add(bus);
    const dish = new THREE.Mesh(new THREE.SphereGeometry(radius * 0.7, 24, 12, 0, Math.PI * 2, 0, Math.PI / 2.2), mat('#e8edf2', 0.5, 0.3));
    dish.rotation.x = Math.PI;
    dish.position.set(radius * 0.7, height * 0.7, 0);
    g.add(dish);
  } else if (partId.includes('cargo')) {
    const lab = new THREE.Mesh(new THREE.CylinderGeometry(radius, radius, height, 36), mat('#d7dde3', 0.55, 0.3));
    lab.position.y = height / 2;
    g.add(lab);
    const stripe = new THREE.Mesh(new THREE.CylinderGeometry(radius * 1.005, radius * 1.005, height * 0.2, 36), mat('#37b6ff', 0.4, 0.4, { emissive: '#0a3d63', emissiveIntensity: 0.5 }));
    stripe.position.y = height * 0.5;
    g.add(stripe);
  } else if (partId.includes('separator')) {
    const ring = new THREE.Mesh(new THREE.CylinderGeometry(radius * 1.02, radius * 1.02, height, 36), mat('#2c333c', 0.85, 0.4));
    ring.position.y = height / 2;
    g.add(ring);
  } else if (partId.includes('feet') || partId.includes('wheel')) {
    const mount = new THREE.Mesh(new THREE.CylinderGeometry(radius, radius, height, 28), mat('#9aa3ad', 0.7, 0.35));
    mount.position.y = height / 2;
    g.add(mount);
  } else {
    // Fuel tanks (default): clean hull with banded ends and a guide stripe.
    const color = tankColor(partId);
    const tank = new THREE.Mesh(new THREE.CylinderGeometry(radius, radius, height, 40, 1), mat(color, 0.42, 0.32));
    tank.position.y = height / 2;
    g.add(tank);
    const bandMat = mat('#b9c0c8', 0.7, 0.3);
    for (const by of [height * 0.06, height * 0.94]) {
      const band = new THREE.Mesh(new THREE.CylinderGeometry(radius * 1.015, radius * 1.015, height * 0.05, 40), bandMat);
      band.position.y = by;
      g.add(band);
    }
    const stripe = new THREE.Mesh(new THREE.BoxGeometry(radius * 0.1, height * 0.7, 0.01), mat('#cf3b2a', 0.3, 0.5));
    stripe.position.set(0, height / 2, radius * 0.99);
    g.add(stripe);
  }

  g.traverse((obj) => {
    if (obj instanceof THREE.Mesh) {
      obj.castShadow = true;
      obj.receiveShadow = true;
    }
  });
  return g;
}

function buildRocket(group: THREE.Group, parts: string[], plumeMaterial?: THREE.ShaderMaterial) {
  group.clear();
  let y = 0;
  let lowestTankY = -1;
  let lowestTankR = 0.54;
  let hasEngine = false;

  for (const partId of [...parts].reverse()) {
    const part = componentById[partId];
    if (!part) continue;
    const height = partHeight(partId);
    const radius = partRadius(partId);
    const piece = buildPart(partId);
    piece.position.y = y;
    group.add(piece);

    if (part.solar) {
      // panels already part of the piece
    }
    if (part.landingFeet) {
      const footMat = mat('#c6cbd0', 0.6, 0.3);
      for (let i = 0; i < 4; i++) {
        const angle = (i / 4) * Math.PI * 2 + Math.PI / 4;
        const leg = new THREE.Mesh(new THREE.CylinderGeometry(0.03, 0.03, 1.0, 10), footMat);
        leg.position.set(Math.cos(angle) * radius * 1.1, y + 0.18, Math.sin(angle) * radius * 1.1);
        leg.rotation.z = -Math.cos(angle) * 0.7;
        leg.rotation.x = Math.sin(angle) * 0.7;
        leg.castShadow = true;
        group.add(leg);
        const pad = new THREE.Mesh(new THREE.CylinderGeometry(0.1, 0.1, 0.04, 12), footMat);
        pad.position.set(Math.cos(angle) * radius * 1.55, y - 0.32, Math.sin(angle) * radius * 1.55);
        group.add(pad);
      }
    }
    if (partId.includes('engine')) hasEngine = true;
    if (partId.includes('tank') || partId.includes('engine')) {
      if (lowestTankY < 0) {
        lowestTankY = y;
        lowestTankR = radius;
      }
    }
    y += height;
  }

  // Fins on the lowest tank for launchers.
  if (hasEngine && lowestTankY >= 0) {
    const finMat = mat('#d7dde3', 0.45, 0.4);
    const finShape = new THREE.Shape();
    finShape.moveTo(0, 0);
    finShape.lineTo(0, 0.95);
    finShape.lineTo(0.62, 0);
    finShape.lineTo(0.62, -0.18);
    finShape.lineTo(0, -0.18);
    const finGeo = new THREE.ExtrudeGeometry(finShape, { depth: 0.05, bevelEnabled: false });
    for (let i = 0; i < 4; i++) {
      const angle = (i / 4) * Math.PI * 2;
      const fin = new THREE.Mesh(finGeo, finMat);
      fin.position.set(Math.cos(angle) * lowestTankR, lowestTankY + 0.1, Math.sin(angle) * lowestTankR);
      fin.rotation.y = -angle + Math.PI / 2;
      fin.castShadow = true;
      group.add(fin);
    }
  }

  group.position.y = -y / 2;

  if (plumeMaterial) {
    const plume = new THREE.Mesh(new THREE.ConeGeometry(0.38, 2.4, 32, 1, true), plumeMaterial);
    plume.name = 'engine-plume';
    plume.position.y = -0.95;
    plume.rotation.x = Math.PI;
    group.add(plume);
    const glow = new THREE.PointLight('#ff8a3c', 0, 6);
    glow.name = 'engine-glow';
    glow.position.y = -0.6;
    group.add(glow);
  }

  return y;
}

// ---------------------------------------------------------------------------
// Launch pad / mission scene
// ---------------------------------------------------------------------------
function buildMission(group: THREE.Group) {
  group.clear();

  const ground = new THREE.Mesh(
    new THREE.CircleGeometry(60, 64),
    mat('#1c2730', 0.0, 0.95),
  );
  ground.rotation.x = -Math.PI / 2;
  ground.position.y = -0.4;
  ground.receiveShadow = true;
  group.add(ground);

  const pad = new THREE.Mesh(new THREE.CylinderGeometry(4.6, 5.2, 0.5, 64), mat('#2a333c', 0.1, 0.85));
  pad.position.y = -0.18;
  pad.receiveShadow = true;
  group.add(pad);

  const trench = new THREE.Mesh(new THREE.CylinderGeometry(1.2, 1.5, 0.6, 32), mat('#0a0e12', 0.2, 0.9));
  trench.position.y = -0.1;
  group.add(trench);

  // Service tower
  const steel = mat('#7c8893', 0.85, 0.34);
  for (const sx of [-2.6, 2.6]) {
    const col = new THREE.Mesh(new THREE.BoxGeometry(0.18, 9, 0.18), steel);
    col.position.set(sx, 4.3, -2.4);
    col.castShadow = true;
    group.add(col);
  }
  for (let yy = 0.8; yy < 9; yy += 1.1) {
    const rail = new THREE.Mesh(new THREE.BoxGeometry(5.4, 0.08, 0.1), steel);
    rail.position.set(0, yy, -2.4);
    group.add(rail);
    const cross = new THREE.Mesh(new THREE.BoxGeometry(0.08, 1.3, 0.08), steel);
    cross.position.set(0, yy + 0.55, -2.4);
    cross.rotation.z = 0.7;
    group.add(cross);
  }
  const arm = new THREE.Mesh(new THREE.BoxGeometry(2.4, 0.18, 0.7), steel);
  arm.position.set(-1.2, 6.4, -1.4);
  group.add(arm);

  const flood = new THREE.PointLight('#ffe6b0', 14, 32, 1.8);
  flood.position.set(4.5, 6, 4);
  group.add(flood);
  const flood2 = new THREE.PointLight('#a9d8ff', 8, 28, 1.9);
  flood2.position.set(-4.5, 4.5, 3);
  group.add(flood2);

  group.traverse((o) => {
    if (o instanceof THREE.Mesh) o.castShadow = o.castShadow || false;
  });
}

// A compact launch pad that sits on the planet surface at the launch site,
// so liftoff reads as rising from the ground rather than starting in orbit.
function buildLaunchPad(group: THREE.Group) {
  group.clear();
  const steel = mat('#7c8893', 0.85, 0.34);

  const pad = new THREE.Mesh(new THREE.CylinderGeometry(2.4, 2.9, 0.4, 48), mat('#2a333c', 0.1, 0.85));
  pad.position.y = 0.05;
  pad.receiveShadow = true;
  group.add(pad);

  const trench = new THREE.Mesh(new THREE.CylinderGeometry(0.7, 0.9, 0.5, 24), mat('#0a0e12', 0.2, 0.9));
  trench.position.y = 0.1;
  group.add(trench);

  for (const sx of [-1.9, 1.9]) {
    const col = new THREE.Mesh(new THREE.BoxGeometry(0.14, 6.4, 0.14), steel);
    col.position.set(sx, 3.2, -1.4);
    col.castShadow = true;
    group.add(col);
  }
  for (let yy = 0.7; yy < 6.4; yy += 0.95) {
    const rail = new THREE.Mesh(new THREE.BoxGeometry(3.8, 0.07, 0.09), steel);
    rail.position.set(0, yy, -1.4);
    group.add(rail);
    const cross = new THREE.Mesh(new THREE.BoxGeometry(0.07, 1.2, 0.07), steel);
    cross.position.set(0, yy + 0.45, -1.4);
    cross.rotation.z = 0.7;
    group.add(cross);
  }
  const arm = new THREE.Mesh(new THREE.BoxGeometry(1.7, 0.14, 0.5), steel);
  arm.position.set(-0.85, 4.6, -0.8);
  group.add(arm);
}

// ---------------------------------------------------------------------------
// Local flight body (the planet you orbit while flying)
// ---------------------------------------------------------------------------
function buildFlightBody(group: THREE.Group, bodyId: BodyId) {
  group.clear();
  const body = BODY_BY_ID[bodyId];

  const planet = new THREE.Mesh(
    new THREE.SphereGeometry(FLIGHT_R, 64, 40),
    new THREE.MeshStandardMaterial({ map: getTexture(body.texture), color: '#ffffff', roughness: 0.92, metalness: 0.0, envMapIntensity: 0.4 }),
  );
  planet.name = 'flight-planet';
  planet.receiveShadow = true;
  group.add(planet);

  if (body.id === 'earth') {
    const clouds = new THREE.Mesh(
      new THREE.SphereGeometry(FLIGHT_R * 1.008, 48, 32),
      new THREE.MeshStandardMaterial({ map: getTexture('/textures/2k_earth_clouds.jpg'), transparent: true, opacity: 0.42, roughness: 1, depthWrite: false }),
    );
    clouds.name = 'flight-clouds';
    group.add(clouds);
  }
  if (body.id === 'venus') {
    const haze = new THREE.Mesh(
      new THREE.SphereGeometry(FLIGHT_R * 1.02, 48, 32),
      new THREE.MeshStandardMaterial({ map: getTexture('/textures/2k_venus_atmosphere.jpg'), transparent: true, opacity: 0.7, roughness: 1, depthWrite: false }),
    );
    haze.name = 'flight-clouds';
    group.add(haze);
  }

  if (body.atmosphere) {
    const glow = body.id === 'earth' ? '#5db4ff' : body.id === 'mars' ? '#ff9d6b' : body.id === 'titan' ? '#d9a14a' : body.color;
    const atmo = new THREE.Mesh(
      new THREE.SphereGeometry(FLIGHT_R * 1.06, 48, 32),
      createAtmosphereMaterial(glow, 0.9, 2.4),
    );
    group.add(atmo);
  }

  if (body.id === 'saturn') {
    const ring = new THREE.Mesh(
      new THREE.RingGeometry(FLIGHT_R * 1.4, FLIGHT_R * 2.3, 160),
      new THREE.MeshBasicMaterial({ map: getTexture('/textures/2k_saturn_ring_alpha.png'), transparent: true, opacity: 0.85, side: THREE.DoubleSide, depthWrite: false }),
    );
    ring.rotation.x = Math.PI / 2.4;
    group.add(ring);
  }
}

// ---------------------------------------------------------------------------
// Solar system map
// ---------------------------------------------------------------------------
function orbitRing(radius: number, color: string, opacity: number) {
  const curve = new THREE.EllipseCurve(0, 0, radius, radius, 0, Math.PI * 2, false, 0);
  const points = curve.getPoints(200).map((p) => new THREE.Vector3(p.x, 0, p.y));
  const geometry = new THREE.BufferGeometry().setFromPoints(points);
  const material = new THREE.LineBasicMaterial({ color, transparent: true, opacity });
  return new THREE.LineLoop(geometry, material);
}

function buildSolarSystem(runtime: Runtime) {
  const { groups, planetMeshes, sunMaterial } = runtime;
  const solar = groups.solar;
  solar.clear();

  const sun = new THREE.Mesh(new THREE.SphereGeometry(mapPlanetRadius('sun'), 64, 32), sunMaterial);
  sun.name = 'Sun';
  planetMeshes.sun = sun;
  solar.add(sun);
  const sunGlow = new THREE.PointLight('#fff0c2', 600, 700, 1.4);
  solar.add(sunGlow);

  for (const body of CELESTIAL_BODIES) {
    if (body.id === 'sun' || body.parent) continue;
    const radius = mapDistance(body.id);
    solar.add(orbitRing(radius, body.color, 0.22));

    const mesh = new THREE.Mesh(
      new THREE.SphereGeometry(mapPlanetRadius(body.id), 48, 32),
      new THREE.MeshStandardMaterial({ map: getTexture(body.texture), roughness: 0.85, metalness: 0, envMapIntensity: 0.5 }),
    );
    mesh.name = body.name;
    planetMeshes[body.id] = mesh;
    solar.add(mesh);

    if (body.atmosphere) {
      const atmo = new THREE.Mesh(
        new THREE.SphereGeometry(mapPlanetRadius(body.id) * 1.12, 32, 24),
        createAtmosphereMaterial(body.id === 'earth' ? '#5db4ff' : body.color, 0.6, 2.2),
      );
      mesh.add(atmo);
    }
    if (body.id === 'saturn') {
      const ring = new THREE.Mesh(
        new THREE.RingGeometry(mapPlanetRadius('saturn') * 1.4, mapPlanetRadius('saturn') * 2.3, 96),
        new THREE.MeshBasicMaterial({ map: getTexture('/textures/2k_saturn_ring_alpha.png'), transparent: true, opacity: 0.85, side: THREE.DoubleSide }),
      );
      ring.rotation.x = Math.PI / 2.3;
      mesh.add(ring);
    }
  }

  for (const body of CELESTIAL_BODIES) {
    if (!body.parent) continue;
    const mesh = new THREE.Mesh(
      new THREE.SphereGeometry(mapPlanetRadius(body.id), 28, 18),
      new THREE.MeshStandardMaterial({ map: getTexture(body.texture), roughness: 0.9, metalness: 0, envMapIntensity: 0.4 }),
    );
    mesh.name = body.name;
    planetMeshes[body.id] = mesh;
    solar.add(mesh);
  }
}

function buildRover(group: THREE.Group, rover?: RoverState) {
  group.clear();
  const bodyColor = rover ? BODY_BY_ID[rover.body].color : '#b4a38c';

  const terrain = new THREE.Mesh(new THREE.CircleGeometry(22, 96), mat(bodyColor, 0, 0.98));
  terrain.rotation.x = -Math.PI / 2;
  terrain.receiveShadow = true;
  group.add(terrain);

  const chassis = new THREE.Mesh(new THREE.BoxGeometry(1.3, 0.34, 0.86), mat('#d6b777', 0.6, 0.3));
  chassis.position.set(rover?.x ?? 0, 0.42, rover?.z ?? 0);
  chassis.castShadow = true;
  group.add(chassis);
  const mast = new THREE.Mesh(new THREE.CylinderGeometry(0.04, 0.04, 0.5, 10), mat('#c6cbd0', 0.7, 0.3));
  mast.position.set((rover?.x ?? 0) + 0.4, 0.72, rover?.z ?? 0);
  group.add(mast);
  const panel = new THREE.Mesh(new THREE.BoxGeometry(1.0, 0.03, 0.7), mat('#13356b', 0.4, 0.25, { emissive: '#06203f', emissiveIntensity: 0.5 }));
  panel.position.set(rover?.x ?? 0, 0.62, rover?.z ?? 0);
  group.add(panel);

  const wheelMat = mat('#15191e', 0.4, 0.5);
  for (const sx of [-0.55, 0.55]) {
    for (const sz of [-0.42, 0.42]) {
      const wheel = new THREE.Mesh(new THREE.TorusGeometry(0.19, 0.07, 12, 24), wheelMat);
      wheel.position.set((rover?.x ?? 0) + sx, 0.2, (rover?.z ?? 0) + sz);
      wheel.rotation.y = Math.PI / 2;
      wheel.castShadow = true;
      group.add(wheel);
    }
  }

  for (let i = 0; i < 16; i++) {
    const angle = i * 1.7;
    const distance = 3 + (i % 6) * 2.4;
    const rock = new THREE.Mesh(
      new THREE.DodecahedronGeometry(0.16 + (i % 3) * 0.08),
      mat('#cfc6b4', 0, 0.95),
    );
    rock.position.set(Math.cos(angle) * distance, 0.16, Math.sin(angle) * distance);
    rock.rotation.set(i * 0.2, i * 0.5, i * 0.7);
    rock.castShadow = true;
    group.add(rock);
  }
}

function updateMapPositions(runtime: Runtime, elapsed: number) {
  for (const body of CELESTIAL_BODIES) {
    const mesh = runtime.planetMeshes[body.id];
    if (!mesh) continue;
    mesh.rotation.y += body.kind === 'star' ? 0.0008 : 0.0022;
    if (body.id === 'sun') {
      mesh.position.set(0, 0, 0);
      continue;
    }
    if (body.parent) {
      const parent = runtime.planetMeshes[body.parent];
      const base = parent?.position ?? new THREE.Vector3();
      const angle = elapsed / Math.max(1, body.orbitDays * 0.1) + body.orbitRadius;
      const r = moonDistance(body.id);
      mesh.position.set(base.x + Math.cos(angle) * r, 0, base.z + Math.sin(angle) * r);
      continue;
    }
    const angle = elapsed / Math.max(1, body.orbitDays * 0.06) + MAP_ORDER.indexOf(body.id);
    const r = mapDistance(body.id);
    mesh.position.set(Math.cos(angle) * r, 0, Math.sin(angle) * r);
  }
}

function setCamera(runtime: Runtime, props: SpaceSceneProps, rocketGroup: THREE.Group, radial: THREE.Vector3) {
  const { camera } = runtime;
  const target = new THREE.Vector3();
  camera.up.set(0, 1, 0); // cockpit mode overrides this below

  if (props.view === 'mission') {
    camera.position.lerp(new THREE.Vector3(7.5, 4.4, 11.5), 0.05);
    target.set(0, 2.4, 0);
  } else if (props.view === 'builder') {
    camera.position.lerp(new THREE.Vector3(0.5, 2.2, 9), 0.1);
    target.set(0, 0.4, 0);
  } else if (props.view === 'map') {
    const focus = runtime.planetMeshes[props.selectedBody];
    const dist = mapDistance(BODY_BY_ID[props.selectedBody].parent ?? props.selectedBody) || 16;
    const camY = THREE.MathUtils.clamp(dist * 1.9 + 30, 70, 260);
    const fx = focus?.position.x ?? 0;
    const fz = focus?.position.z ?? 0;
    camera.position.lerp(new THREE.Vector3(fx * 0.3, camY, fz * 0.3 + 0.01), 0.06);
    target.set(fx * 0.3, 0, fz * 0.3);
  } else if (props.view === 'roverDrive' || props.view === 'rovers') {
    const rover = runtime.groups.rover.children[1];
    const pos = rover?.position ?? new THREE.Vector3();
    camera.position.lerp(new THREE.Vector3(pos.x + 5, 3.4, pos.z + 6), 0.1);
    target.copy(pos);
  } else {
    // Flight: a single continuous frame — zoomed in at the pad, smoothly pulling
    // back to reveal the planet and orbit as altitude climbs (no teleport).
    const rp = rocketGroup.position;
    const altFactor = THREE.MathUtils.clamp(props.flight.altitude / 90000, 0, 1);
    if (props.cameraMode === 'cockpit') {
      // True first-person: sit just past the nose and look along the flight (prograde)
      // direction, so you see straight up at liftoff and the horizon once you pitch over.
      const noseDir = new THREE.Vector3(0, 1, 0).applyQuaternion(rocketGroup.quaternion).normalize();
      const worldHeight = runtime.rocketHeight * rocketGroup.scale.y;
      const eye = rp.clone().add(noseDir.clone().multiplyScalar(worldHeight + 0.4));
      camera.position.lerp(eye, 0.45);

      const radialUp = radial.clone().normalize();
      const tangent = new THREE.Vector3(radialUp.y, -radialUp.x, 0).normalize();
      const prograde = radialUp
        .clone()
        .multiplyScalar(props.flight.verticalSpeed)
        .addScaledVector(tangent, Math.max(0, props.flight.horizontalSpeed));
      if (prograde.lengthSq() < 1) prograde.copy(noseDir);
      prograde.normalize();

      // Tilt the look slightly down toward the planet so the horizon stays framed.
      const lookDir = prograde.clone().addScaledVector(radialUp, -0.3).normalize();
      target.copy(camera.position).add(lookDir.multiplyScalar(60));
      // Level horizon (up = away from the planet); flip to a stable axis only when
      // the view points almost straight up (liftoff).
      camera.up.copy(Math.abs(lookDir.dot(radialUp)) > 0.985 ? tangent : radialUp);
    } else if (props.cameraMode === 'orbit') {
      const d = THREE.MathUtils.lerp(14, 52, altFactor);
      camera.position.lerp(new THREE.Vector3(rp.x + radial.x * d * 0.4 + 4, rp.y + radial.y * d * 0.4 + 6, rp.z + d), 0.05);
      target.copy(rp);
    } else {
      // Grounded launch (low camera, looking up the rocket) → orbital chase as you climb.
      const camDist = THREE.MathUtils.lerp(17, 46, altFactor);
      const radialComp = THREE.MathUtils.lerp(0.05, 0.3, altFactor);
      const yBias = THREE.MathUtils.lerp(0.02, 0.16, altFactor);
      const offset = radial
        .clone()
        .multiplyScalar(camDist * radialComp)
        .add(new THREE.Vector3(camDist * 0.12, camDist * yBias, camDist * 0.95));
      camera.position.lerp(rp.clone().add(offset), 0.08);
      target.copy(rp).addScaledVector(radial, THREE.MathUtils.lerp(4.5, 0, altFactor));
    }
    if (runtime.shake > 0.001) {
      camera.position.x += (Math.sin(performance.now() * 0.05) * runtime.shake) / 60;
      camera.position.y += (Math.cos(performance.now() * 0.07) * runtime.shake) / 60;
    }
  }
  camera.lookAt(target);
}

export function SpaceScene(props: SpaceSceneProps) {
  const mountRef = useRef<HTMLDivElement | null>(null);
  const runtimeRef = useRef<Runtime | null>(null);
  const propsRef = useRef(props);
  propsRef.current = props;

  useEffect(() => {
    const mount = mountRef.current;
    if (!mount) return;

    const renderer = new THREE.WebGLRenderer({ antialias: true, alpha: false, powerPreference: 'high-performance' });
    renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2));
    renderer.setSize(mount.clientWidth, mount.clientHeight);
    renderer.shadowMap.enabled = true;
    renderer.shadowMap.type = THREE.PCFSoftShadowMap;
    renderer.toneMapping = THREE.ACESFilmicToneMapping;
    renderer.toneMappingExposure = 0.98;
    renderer.outputColorSpace = THREE.SRGBColorSpace;
    mount.appendChild(renderer.domElement);

    const scene = new THREE.Scene();
    scene.background = new THREE.Color('#01030a');

    // Image-based lighting for crisp metal.
    let environmentTexture: THREE.Texture | null = null;
    try {
      const pmrem = new THREE.PMREMGenerator(renderer);
      environmentTexture = pmrem.fromScene(new RoomEnvironment(), 0.04).texture;
      scene.environment = environmentTexture;
      pmrem.dispose();
    } catch {
      environmentTexture = null;
    }

    const camera = new THREE.PerspectiveCamera(50, mount.clientWidth / mount.clientHeight, 0.05, 4000);
    camera.position.set(8, 5, 12);

    // Starfield skybox (always behind everything).
    const stars = new THREE.Mesh(
      new THREE.SphereGeometry(1800, 64, 32),
      new THREE.MeshBasicMaterial({ map: getTexture('/textures/2k_stars_milky_way.jpg'), side: THREE.BackSide }),
    );
    scene.add(stars);

    const groups = {
      mission: new THREE.Group(),
      builder: new THREE.Group(),
      solar: new THREE.Group(),
      flight: new THREE.Group(),
      launchpad: new THREE.Group(),
      rocket: new THREE.Group(),
      rover: new THREE.Group(),
      peers: new THREE.Group(),
    };
    Object.values(groups).forEach((g) => scene.add(g));

    const sunMaterial = createSunMaterial();
    const plumeMaterial = createPlumeMaterial();

    const ambient = new THREE.AmbientLight('#54709f', 0.38);
    scene.add(ambient);
    const hemi = new THREE.HemisphereLight('#bcd4ff', '#1a1208', 0.4);
    scene.add(hemi);
    const sun = new THREE.DirectionalLight('#fff4e0', 2.9);
    sun.position.set(40, 60, 30);
    sun.castShadow = true;
    sun.shadow.mapSize.set(1024, 1024);
    sun.shadow.camera.near = 1;
    sun.shadow.camera.far = 200;
    sun.shadow.camera.left = -30;
    sun.shadow.camera.right = 30;
    sun.shadow.camera.top = 30;
    sun.shadow.camera.bottom = -30;
    scene.add(sun);
    scene.add(sun.target);

    let composer: EffectComposer | null = null;
    try {
      composer = new EffectComposer(renderer);
      composer.addPass(new RenderPass(scene, camera));
      const bloom = new UnrealBloomPass(new THREE.Vector2(mount.clientWidth, mount.clientHeight), 0.42, 0.5, 0.9);
      composer.addPass(bloom);
      composer.addPass(new OutputPass());
    } catch {
      composer = null;
    }

    const runtime: Runtime = {
      renderer,
      composer,
      scene,
      camera,
      sun,
      startedAt: performance.now(),
      groups,
      planetMeshes: {},
      lastRocketKey: '',
      lastRoverKey: '',
      flightBodyKey: '',
      flightAngle: 0,
      rocketHeight: 6,
      shake: 0,
      sunMaterial,
      plumeMaterial,
    };
    runtimeRef.current = runtime;

    buildMission(groups.mission);
    buildLaunchPad(groups.launchpad);
    groups.launchpad.position.set(0, FLIGHT_R, 0); // launch site sits at the planet's pole (theta = 0)
    buildSolarSystem(runtime);

    const builderGrid = new THREE.GridHelper(14, 28, '#2fd3ff', '#0e3247');
    const gridMaterial = builderGrid.material as THREE.Material;
    gridMaterial.transparent = true;
    gridMaterial.opacity = 0.32;
    groups.builder.add(builderGrid);

    let lastFrame = performance.now();
    const resize = () => {
      const width = mount.clientWidth;
      const height = mount.clientHeight;
      renderer.setSize(width, height);
      composer?.setSize(width, height);
      camera.aspect = width / Math.max(1, height);
      camera.updateProjectionMatrix();
    };
    window.addEventListener('resize', resize);

    let raf = 0;
    const animate = () => {
      const current = propsRef.current;
      const now = performance.now();
      const dt = Math.min(0.05, (now - lastFrame) / 1000);
      lastFrame = now;
      const time = (now - runtime.startedAt) / 1000;
      const flight = current.flight;

      sunMaterial.uniforms.time.value = time;
      plumeMaterial.uniforms.time.value = time;
      const thrusting =
        flight.throttle > 0 &&
        flight.fuel > 0 &&
        (flight.status === 'ascent' || flight.status === 'countdown' || flight.status === 'orbit');
      plumeMaterial.uniforms.throttle.value = thrusting ? Math.max(0.12, flight.throttle) : 0;

      // Rebuild rocket when the design changes.
      const rocketKey = current.design.parts.join('|');
      if (runtime.lastRocketKey !== rocketKey) {
        runtime.rocketHeight = buildRocket(groups.rocket, current.design.parts, plumeMaterial) || 6;
        buildRocket(groups.builder, current.design.parts);
        groups.builder.add(builderGrid);
        runtime.lastRocketKey = rocketKey;
      }

      // Rebuild the local planet when we change worlds / enter flight.
      const inFlight = current.view === 'flight' || current.view === 'eva';
      const flightBodyKey = `${flight.location}`;
      if (inFlight && runtime.flightBodyKey !== flightBodyKey) {
        buildFlightBody(groups.flight, flight.location);
        runtime.flightBodyKey = flightBodyKey;
      }

      if (current.view === 'map') updateMapPositions(runtime, time * 6 + flight.elapsed / 60);

      // The launch pad sits on the surface while we are still low over the launch site.
      const launching =
        inFlight &&
        flight.location === 'earth' &&
        flight.altitude < 6000 &&
        (flight.status === 'pad' ||
          flight.status === 'countdown' ||
          (flight.status === 'ascent' && flight.verticalSpeed > -8));

      // Visibility.
      groups.mission.visible = current.view === 'mission';
      groups.builder.visible = current.view === 'builder';
      groups.solar.visible = current.view === 'map';
      groups.flight.visible = inFlight;
      groups.launchpad.visible = launching;
      groups.rocket.visible = current.view === 'mission' || current.view === 'flight' || current.view === 'eva';
      groups.rover.visible = current.view === 'roverDrive' || current.view === 'rovers';
      groups.peers.visible = current.showOtherRockets && inFlight && flight.altitude > 8000;

      // Blue atmospheric sky near the ground that clears into black space as you climb,
      // so a launch reads as standing on a planet rather than floating beside one.
      const localBody = BODY_BY_ID[flight.location];
      if (inFlight && localBody.atmosphere) {
        const skyFactor = THREE.MathUtils.clamp(1 - flight.altitude / 55000, 0, 1);
        if (skyFactor > 0.01) {
          if (!scene.fog) scene.fog = new THREE.FogExp2(0x74a9e0, 0);
          (scene.fog as THREE.FogExp2).density = 0.0019 * skyFactor;
        } else {
          scene.fog = null;
        }
      } else {
        scene.fog = null;
      }

      // Rotate flight planet / clouds.
      if (groups.flight.visible) {
        const planet = groups.flight.getObjectByName('flight-planet');
        if (planet) planet.rotation.y += dt * 0.015;
        const clouds = groups.flight.getObjectByName('flight-clouds');
        if (clouds) clouds.rotation.y += dt * 0.024;
      }

      // Position the rocket.
      const radial = new THREE.Vector3(0, 1, 0);
      if (current.view === 'mission') {
        groups.rocket.position.set(0, 0.1, 0);
        groups.rocket.rotation.set(0, time * 0.06, 0);
        groups.rocket.scale.setScalar(0.62);
        runtime.shake = 0;
      } else if (inFlight) {
        // Advance the visual orbit angle — and let time warp visibly speed it up.
        const warp = Math.max(1, flight.timeScale);
        if (flight.status === 'pad' || flight.status === 'countdown') runtime.flightAngle = 0;
        else if (flight.status === 'orbit') runtime.flightAngle += dt * 0.12 * warp;
        else if (flight.status === 'ascent')
          runtime.flightAngle += dt * (0.015 + flight.horizontalSpeed / 90000) * (flight.altitude < 80000 ? 1 : warp);
        else if (flight.status === 'transfer') runtime.flightAngle += dt * 0.05 * warp;
        const theta = runtime.flightAngle;
        radial.set(Math.sin(theta), Math.cos(theta), 0).normalize();

        const altScene = flight.status === 'transfer' ? 130 : THREE.MathUtils.clamp(flight.altitude / 5200, 0, 60);
        const dist = FLIGHT_R + 0.4 + altScene;
        groups.rocket.position.set(radial.x * dist, radial.y * dist, radial.z * dist);

        const pitchRad = THREE.MathUtils.degToRad(flight.pitch);
        const lean = Math.PI / 2 - pitchRad;
        groups.rocket.rotation.set(0, 0, -theta - lean + Math.sin(time * 1.5) * 0.01);
        groups.rocket.scale.setScalar(0.85);

        // Camera shake on the pad ignition, in powered atmospheric flight, or on failure.
        const inAtmo = BODY_BY_ID[flight.location].atmosphere && flight.altitude < 70000;
        runtime.shake =
          flight.status === 'countdown' && flight.countdown < 1.2
            ? 0.5
            : thrusting && inAtmo
              ? 0.5 * flight.throttle
              : flight.status === 'crashed' || flight.status === 'burned'
                ? 1.4
                : 0;

        // Keep the sun lighting the rocket's side.
        runtime.sun.position.set(radial.x * dist + 60, radial.y * dist + 80, 60);
        runtime.sun.target.position.copy(groups.rocket.position);
      }

      // Engine glow intensity.
      const glow = groups.rocket.getObjectByName('engine-glow') as THREE.PointLight | undefined;
      if (glow) glow.intensity = thrusting ? 4 + flight.throttle * 6 : 0;

      // Rover.
      const roverKey = `${current.rover?.id ?? 'none'}-${(current.rover?.x ?? 0).toFixed(2)}-${(current.rover?.z ?? 0).toFixed(2)}`;
      if ((current.view === 'rovers' || current.view === 'roverDrive') && runtime.lastRoverKey !== roverKey) {
        buildRover(groups.rover, current.rover);
        runtime.lastRoverKey = roverKey;
      }

      // Peer markers in orbit.
      if (groups.peers.visible) {
        if (groups.peers.children.length === 0) {
          const peerMat = new THREE.MeshStandardMaterial({ color: '#ffcc66', emissive: '#ff8a3c', emissiveIntensity: 1.4, metalness: 0.4, roughness: 0.4 });
          for (let i = 0; i < Math.max(current.peerCount, 3); i++) {
            const marker = new THREE.Mesh(new THREE.ConeGeometry(0.5, 1.6, 16), peerMat);
            marker.name = `peer-${i}`;
            groups.peers.add(marker);
          }
        }
        groups.peers.children.forEach((marker, i) => {
          const angle = time * 0.12 + i * 1.7;
          const r = FLIGHT_R + 8 + i * 4;
          marker.position.set(Math.sin(angle) * r, Math.cos(angle) * r, Math.cos(angle * 0.7) * 6);
          marker.rotation.z = -angle;
        });
      } else {
        groups.peers.clear();
      }

      setCamera(runtime, current, groups.rocket, radial);

      if (composer) composer.render();
      else renderer.render(scene, camera);
      raf = requestAnimationFrame(animate);
    };
    raf = requestAnimationFrame(animate);

    return () => {
      cancelAnimationFrame(raf);
      window.removeEventListener('resize', resize);
      environmentTexture?.dispose();
      composer?.dispose();
      renderer.dispose();
      if (renderer.domElement.parentNode === mount) mount.removeChild(renderer.domElement);
      runtimeRef.current = null;
    };
  }, []);

  return <div className="space-scene" ref={mountRef} aria-hidden="true" />;
}
