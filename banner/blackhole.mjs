// Black hole banner, rendered from first principles. No dependencies.
//
//   node banner/blackhole.mjs            # writes assets/banner.png (1800x600)
//
// Units: G = c = M = 1, so the event horizon is at r = 2 and the photon sphere at r = 3.
//
// Physics
//   Light around a non-rotating black hole follows null geodesics of the Schwarzschild metric.
//   With u = 1/r and the angle phi measured inside the ray's orbital plane:
//
//         d^2u/dphi^2 = -u + 3 u^2
//
//   Each pixel's ray is integrated (RK4) from the camera until it
//     - falls in         (u > 1/2)          -> black shadow
//     - escapes          (u -> 0)           -> samples the starfield in the direction it left
//     - crosses the disk (y = 0 plane)      -> adds accretion-disk light
//   Rays can cross the disk more than once, which is what bends the far side of the
//   disk up and over the hole (the bright rings above the shadow).
//
//   Disk light is Doppler-shifted by Keplerian orbital speed, v = sqrt(1 / (r - 2)),
//   and gravitationally redshifted by sqrt(1 - 2/r).
//
// Post: radial "warp" blur around the hole, bloom, tone map, film grain.

import { Worker, isMainThread, parentPort, workerData } from 'node:worker_threads';
import { deflateSync, crc32 } from 'node:zlib';
import { writeFileSync, mkdirSync } from 'node:fs';
import { dirname } from 'node:path';
import os from 'node:os';

const DEFAULTS = {
  width: 1800, height: 600, ss: 3,   // output size, supersampling per axis
  fovx: 60,                          // horizontal field of view, degrees
  dist: 90,                          // camera distance from the hole (in M)
  elev: 13,                          // camera elevation above the disk plane, degrees
  cx: 0.5, cy: 0.47,                 // where the hole sits in the frame (0..1)
  rin: 6, rout: 220,                 // disk inner (ISCO) and outer radius
  spin: 1,                           // disk rotation direction, +1 / -1
  beam: 2.2,                         // Doppler beaming exponent
  warp: 0.12, warpFrom: 0.2, warpTo: 0.85, // radial blur: strength and ramp (in image heights)
  exposure: 0.9,
  seed: 11,
  out: 'assets/banner.png',
};

const TAU = Math.PI * 2;

// ---------- hashing / noise ----------------------------------------------------------------
function hash32(x) {
  x = Math.imul(x ^ (x >>> 16), 0x7feb352d);
  x = Math.imul(x ^ (x >>> 15), 0x846ca68b);
  x ^= x >>> 16;
  return (x >>> 0) / 4294967296;
}
const H2 = (a, b, s) => hash32((Math.imul(a, 374761393) + Math.imul(b, 668265263) + Math.imul(s, 1274126177)) | 0);
const H3 = (a, b, c, s) =>
  hash32((Math.imul(a, 374761393) + Math.imul(b, 668265263) + Math.imul(c, 1103515245) + Math.imul(s, 1274126177)) | 0);
const sstep = (a, b, x) => { const t = Math.min(1, Math.max(0, (x - a) / (b - a))); return t * t * (3 - 2 * t); };

// value noise, periodic in x with integer period P (used around the disk's azimuth)
function vn2p(x, y, P, s) {
  const xi = Math.floor(x), yi = Math.floor(y);
  let fx = x - xi, fy = y - yi;
  fx = fx * fx * (3 - 2 * fx); fy = fy * fy * (3 - 2 * fy);
  const x0 = ((xi % P) + P) % P, x1 = (x0 + 1) % P;
  const a = H2(x0, yi, s), b = H2(x1, yi, s), c = H2(x0, yi + 1, s), d = H2(x1, yi + 1, s);
  return a + (b - a) * fx + (c - a) * fy + (a - b - c + d) * fx * fy;
}
function vn3(x, y, z, s) {
  const xi = Math.floor(x), yi = Math.floor(y), zi = Math.floor(z);
  let fx = x - xi, fy = y - yi, fz = z - zi;
  fx = fx * fx * (3 - 2 * fx); fy = fy * fy * (3 - 2 * fy); fz = fz * fz * (3 - 2 * fz);
  const l = (dx, dy, dz) => H3(xi + dx, yi + dy, zi + dz, s);
  const x00 = l(0, 0, 0) + (l(1, 0, 0) - l(0, 0, 0)) * fx;
  const x10 = l(0, 1, 0) + (l(1, 1, 0) - l(0, 1, 0)) * fx;
  const x01 = l(0, 0, 1) + (l(1, 0, 1) - l(0, 0, 1)) * fx;
  const x11 = l(0, 1, 1) + (l(1, 1, 1) - l(0, 1, 1)) * fx;
  const y0 = x00 + (x10 - x00) * fy, y1 = x01 + (x11 - x01) * fy;
  return y0 + (y1 - y0) * fz;
}
function fbm3(x, y, z, oct, s) {
  let v = 0, a = 0.5;
  for (let i = 0; i < oct; i++) { v += a * vn3(x, y, z, s + i); x *= 2.03; y *= 2.03; z *= 2.03; a *= 0.5; }
  return v;
}

// ---------- scene ---------------------------------------------------------------------------
function makeScene(o) {
  const W = o.width * o.ss, H = o.height * o.ss;
  const e = (o.elev * Math.PI) / 180;
  const sE = Math.sin(e), cE = Math.cos(e);
  const pixAng = (o.fovx * Math.PI) / 180 / o.width; // radians per final pixel (at the centre)

  // star layers: cell size in final-image pixels, presence probability, brightness, sigma factor
  const layers = [
    { cell: 52, p: 0.22, b: 9, ex: 1.35, sg: 0.62, seed: 1 },
    { cell: 20, p: 0.30, b: 7, ex: 1.25, sg: 0.62, seed: 2 },
    { cell: 9, p: 0.34, b: 2.2, ex: 1.15, sg: 0.62, seed: 3 },
  ].map((l) => {
    const cs0 = l.cell * pixAng;
    const nLon = Math.max(8, Math.round(TAU / cs0));
    return { ...l, nLon, cs: TAU / nLon, sigma: pixAng * l.sg };
  });

  // Milky-Way band: great circle with normal g
  const gl = Math.hypot(0.38, 0.86, -0.34);
  return {
    W, H, o, layers,
    e1: [0, sE, cE],            // from the hole towards the camera
    fwd: [0, -sE, -cE],
    right: [1, 0, 0],
    up: [0, cE, -sE],
    r0: o.dist,
    tanH: Math.tan((o.fovx * Math.PI) / 360),
    g: [0.38 / gl, 0.86 / gl, -0.34 / gl],
    dphi: 0.012,
    maxPhi: 3.3 * Math.PI,
  };
}

// ---------- sky -----------------------------------------------------------------------------
function skyLum(x, y, z, S) {
  const lon = Math.atan2(z, x);
  const lat = Math.asin(Math.max(-1, Math.min(1, y)));
  const cl = Math.max(0.2, Math.cos(lat));

  // galaxy: soft band modulated by clumpy noise and dust lanes
  const gd = x * S.g[0] + y * S.g[1] + z * S.g[2];
  const band = Math.exp(-(gd * gd) / (2 * 0.16 * 0.16));
  const cloud = fbm3(x * 3.1, y * 3.1, z * 3.1, 5, 40);
  const dust = fbm3(x * 7.5 + 9, y * 7.5, z * 7.5, 4, 70);
  let L = 0.30 * band * Math.pow(cloud, 2.2) * (1.15 - 0.9 * sstep(0.35, 0.7, dust));
  const bandBoost = 1 + 2.2 * band * cloud;

  for (const ly of S.layers) {
    const gi = Math.floor(lon / ly.cs), gj = Math.floor(lat / ly.cs);
    for (let di = -1; di <= 1; di++) {
      for (let dj = -1; dj <= 1; dj++) {
        const ii = gi + di, jj = gj + dj;
        const iw = ((ii % ly.nLon) + ly.nLon) % ly.nLon;
        if (H2(iw, jj, ly.seed * 17) > ly.p * (ly.cell < 20 ? bandBoost : 1)) continue;
        const sl = (ii + H2(iw, jj, ly.seed * 17 + 1)) * ly.cs;
        const st = (jj + H2(iw, jj, ly.seed * 17 + 2)) * ly.cs;
        let dl = lon - sl; dl -= TAU * Math.round(dl / TAU);
        const dt = lat - st;
        const d2 = dl * dl * cl * cl + dt * dt;
        const s2 = 2 * ly.sigma * ly.sigma;
        if (d2 > 14 * s2) continue;
        const m = Math.min(1 / (0.03 + H2(iw, jj, ly.seed * 17 + 3)), 5); // cap: stars on the Einstein ring stretch into arcs and smear into slabs
        L += ly.b * Math.pow(m, ly.ex) * 0.04 * Math.exp(-d2 / s2);
      }
    }
  }
  return L;
}

// ---------- accretion disk ------------------------------------------------------------------
function diskLum(r, az, S) {
  const rin = S.o.rin;
  const lr = Math.log(r / rin);
  const a = az / TAU + 0.5;
  // long arcs around the disk, very fine in radius: concentric streaks
  const fine = 1 / (1 + (lr * 1.1) * (lr * 1.1));
  const n =
    0.50 * vn2p(a * 11, lr * 38, 11, 1) +
    0.30 * vn2p(a * 23, lr * 95, 23, 2) +
    0.20 * fine * vn2p(a * 47, lr * 260, 47, 3);
  const rings = 0.12 + 2.1 * Math.pow(n, 2.3);
  const profile = Math.pow(rin / r, 1.95) * sstep(rin, rin * 1.22, r) * (1 - sstep(S.o.rout * 0.55, S.o.rout, r));
  return 3.2 * profile * rings;
}
const diskOpacity = (r, S) => 0.55 * sstep(S.o.rin, S.o.rin * 1.25, r) * (1 - sstep(10, 70, r)) + 0.1 * sstep(S.o.rin, S.o.rin * 1.25, r);

// ---------- ray ---------------------------------------------------------------------------------
function trace(dx, dy, dz, S) {
  const e1 = S.e1;
  const cosPsi = -(dx * e1[0] + dy * e1[1] + dz * e1[2]);
  const tx = dx + cosPsi * e1[0], ty = dy + cosPsi * e1[1], tz = dz + cosPsi * e1[2];
  const sinPsi = Math.hypot(tx, ty, tz);
  if (sinPsi < 1e-9) return 0;
  const e2x = tx / sinPsi, e2y = ty / sinPsi, e2z = tz / sinPsi;

  const u0 = 1 / S.r0;
  let u = u0;
  let w = u0 * Math.sqrt(1 - 2 * u0) * (cosPsi / sinPsi); // du/dphi at the camera
  let phi = 0;

  // the ray crosses the disk plane (y = 0) where  e1y cos(phi) + e2y sin(phi) = 0
  let nextC = Math.atan2(-e1[1], e2y);
  nextC = ((nextC % Math.PI) + Math.PI) % Math.PI;
  if (nextC < 1e-6) nextC += Math.PI;

  const h = S.dphi, maxPhi = S.maxPhi;
  let L = 0, T = 1;

  while (phi < maxPhi) {
    // RK4 for u' = w, w' = -u + 3u^2
    const k1u = w, k1w = -u + 3 * u * u;
    const ua = u + 0.5 * h * k1u, wa = w + 0.5 * h * k1w;
    const k2u = wa, k2w = -ua + 3 * ua * ua;
    const ub = u + 0.5 * h * k2u, wb = w + 0.5 * h * k2w;
    const k3u = wb, k3w = -ub + 3 * ub * ub;
    const uc = u + h * k3u, wc = w + h * k3w;
    const k4u = wc, k4w = -uc + 3 * uc * uc;
    const un = u + (h / 6) * (k1u + 2 * k2u + 2 * k3u + k4u);
    const wn = w + (h / 6) * (k1w + 2 * k2w + 2 * k3w + k4w);
    const phin = phi + h;

    // disk plane crossing inside this step?
    while (nextC > phi && nextC <= phin) {
      const s = (nextC - phi) / h, s2 = s * s, s3 = s2 * s;
      const uh = (2 * s3 - 3 * s2 + 1) * u + (s3 - 2 * s2 + s) * h * w + (-2 * s3 + 3 * s2) * un + (s3 - s2) * h * wn;
      const wh = w + s * (wn - w);
      if (uh > 0 && uh < 0.5) {
        const r = 1 / uh;
        if (r > S.o.rin * 0.98 && r < S.o.rout) {
          const cp = Math.cos(nextC), sp = Math.sin(nextC);
          const px = cp * e1[0] + sp * e2x, pz = cp * e1[2] + sp * e2z; // unit radial direction (y ~ 0)
          const az = Math.atan2(pz, px);
          // photon direction (towards the observer) = - d/dphi of the position
          const rp = -wh / (uh * uh);
          const tdx = rp * (cp * e1[0] + sp * e2x) + r * (-sp * e1[0] + cp * e2x);
          const tdy = rp * (cp * e1[1] + sp * e2y) + r * (-sp * e1[1] + cp * e2y);
          const tdz = rp * (cp * e1[2] + sp * e2z) + r * (-sp * e1[2] + cp * e2z);
          const tl = Math.hypot(tdx, tdy, tdz);
          // disk gas velocity: tangential, Keplerian
          const beta = Math.min(0.85, Math.sqrt(1 / Math.max(r - 2, 0.5)));
          const bx = -pz * S.o.spin, bz = px * S.o.spin;
          const cosT = -(bx * tdx + bz * tdz) / tl;           // beta_hat . n_photon
          const gam = 1 / Math.sqrt(1 - beta * beta);
          const g = (1 / (gam * (1 - beta * cosT))) * Math.sqrt(1 - 2 / r);
          L += T * diskLum(r, az, S) * Math.pow(g, S.o.beam);
          T *= 1 - diskOpacity(r, S);
        }
      }
      nextC += Math.PI;
    }

    if (un >= 0.5) return L;            // fell through the horizon
    if (un <= 0) {                      // escaped: the final direction is the radial direction at u = 0
      const pf = phi + (h * u) / (u - un);
      const cp = Math.cos(pf), sp = Math.sin(pf);
      const x = cp * e1[0] + sp * e2x, y = cp * e1[1] + sp * e2y, z = cp * e1[2] + sp * e2z;
      return L + T * skyLum(x, y, z, S);
    }
    u = un; w = wn; phi = phin;
  }
  return L; // wound around the photon sphere too long: treat as captured
}

// ---------- worker ------------------------------------------------------------------------------
function renderRows(S, idx, n) {
  const { W, H, o } = S;
  const rows = [];
  for (let j = idx; j < H; j += n) rows.push(j);
  const out = new Float32Array(rows.length * W);
  let k = 0;
  for (const j of rows) {
    const Y = -((j + 0.5) - o.cy * H) * (2 * S.tanH) / W; // square pixels: both axes scale by width
    for (let i = 0; i < W; i++) {
      const X = ((i + 0.5) - o.cx * W) * (2 * S.tanH) / W;
      let dx = S.fwd[0] + X * S.right[0] + Y * S.up[0];
      let dy = S.fwd[1] + X * S.right[1] + Y * S.up[1];
      let dz = S.fwd[2] + X * S.right[2] + Y * S.up[2];
      const l = Math.hypot(dx, dy, dz);
      out[k++] = trace(dx / l, dy / l, dz / l, S);
    }
  }
  return { rows, data: out };
}

if (!isMainThread) {
  const S = makeScene(workerData.opts);
  const { rows, data } = renderRows(S, workerData.idx, workerData.n);
  parentPort.postMessage({ rows, data }, [data.buffer]);
}

// ---------- post processing ----------------------------------------------------------------------
function downsample(src, W, H, ss) {
  const w = W / ss, h = H / ss, out = new Float32Array(w * h), inv = 1 / (ss * ss);
  for (let y = 0; y < h; y++)
    for (let x = 0; x < w; x++) {
      let s = 0;
      for (let j = 0; j < ss; j++) for (let i = 0; i < ss; i++) s += src[(y * ss + j) * W + x * ss + i];
      out[y * w + x] = s * inv;
    }
  return out;
}

function radialBlur(src, W, H, cx, cy, strength, from, to, K = 56) {
  const out = new Float32Array(W * H);
  for (let y = 0; y < H; y++) {
    for (let x = 0; x < W; x++) {
      const dx = x - cx, dy = y - cy;
      const rho = Math.hypot(dx, dy) / H;
      const s = strength * sstep(from, to, rho);
      if (s < 1e-4) { out[y * W + x] = src[y * W + x]; continue; }
      let acc = 0;
      for (let k = 0; k < K; k++) {
        const t = (k / (K - 1) - 0.5) * s;
        const sx = Math.min(W - 1.001, Math.max(0, cx + dx * (1 + t)));
        const sy = Math.min(H - 1.001, Math.max(0, cy + dy * (1 + t)));
        const x0 = sx | 0, y0 = sy | 0, fx = sx - x0, fy = sy - y0, i = y0 * W + x0;
        acc += src[i] * (1 - fx) * (1 - fy) + src[i + 1] * fx * (1 - fy) + src[i + W] * (1 - fx) * fy + src[i + W + 1] * fx * fy;
      }
      out[y * W + x] = acc / K;
    }
  }
  return out;
}

function gaussBlur(src, W, H, sigma) {
  const R = Math.ceil(sigma * 3), k = new Float32Array(2 * R + 1);
  let sum = 0;
  for (let i = -R; i <= R; i++) sum += (k[i + R] = Math.exp(-(i * i) / (2 * sigma * sigma)));
  for (let i = 0; i < k.length; i++) k[i] /= sum;
  const tmp = new Float32Array(W * H), out = new Float32Array(W * H);
  for (let y = 0; y < H; y++)
    for (let x = 0; x < W; x++) {
      let a = 0;
      for (let i = -R; i <= R; i++) a += k[i + R] * src[y * W + Math.min(W - 1, Math.max(0, x + i))];
      tmp[y * W + x] = a;
    }
  for (let y = 0; y < H; y++)
    for (let x = 0; x < W; x++) {
      let a = 0;
      for (let i = -R; i <= R; i++) a += k[i + R] * tmp[Math.min(H - 1, Math.max(0, y + i)) * W + x];
      out[y * W + x] = a;
    }
  return out;
}

function toneMap(hdr, W, H, o) {
  const px = new Uint8Array(W * H);
  for (let y = 0; y < H; y++)
    for (let x = 0; x < W; x++) {
      const nx = (x / W - 0.5) * 2, ny = (y / H - 0.5) * 2;
      const vig = 1 - 0.22 * (nx * nx * 0.55 + ny * ny * 0.9);
      let v = 1 - Math.exp(-o.exposure * hdr[y * W + x] * vig);
      v = Math.pow(v, 1 / 1.6);
      v += (H2(x, y, 99) - 0.5) * (2.2 / 255);   // film grain / dither
      px[y * W + x] = Math.max(0, Math.min(255, Math.round(v * 255)));
    }
  return px;
}

function encodePng(px, W, H) {
  const raw = Buffer.alloc((W + 1) * H);
  for (let y = 0; y < H; y++) {
    raw[y * (W + 1)] = 0;
    Buffer.from(px.buffer, y * W, W).copy(raw, y * (W + 1) + 1);
  }
  const chunk = (type, data) => {
    const len = Buffer.alloc(4); len.writeUInt32BE(data.length);
    const td = Buffer.concat([Buffer.from(type, 'ascii'), data]);
    const crc = Buffer.alloc(4); crc.writeUInt32BE(crc32(td) >>> 0);
    return Buffer.concat([len, td, crc]);
  };
  const ihdr = Buffer.alloc(13);
  ihdr.writeUInt32BE(W, 0); ihdr.writeUInt32BE(H, 4); ihdr[8] = 8; ihdr[9] = 0; // 8-bit grayscale
  return Buffer.concat([
    Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]),
    chunk('IHDR', ihdr), chunk('IDAT', deflateSync(raw, { level: 9 })), chunk('IEND', Buffer.alloc(0)),
  ]);
}

// ---------- main ------------------------------------------------------------------------------
async function main() {
  const opts = { ...DEFAULTS };
  const argv = process.argv.slice(2);
  for (let i = 0; i < argv.length; i += 2) {
    const key = argv[i].replace(/^--/, '');
    if (!(key in DEFAULTS)) throw new Error(`unknown option --${key}`);
    opts[key] = typeof DEFAULTS[key] === 'number' ? Number(argv[i + 1]) : argv[i + 1];
  }
  const t0 = Date.now();
  const S = makeScene(opts);
  const n = Math.max(1, (os.availableParallelism?.() ?? os.cpus().length) - 1);
  console.log(`tracing ${S.W}x${S.H} rays on ${n} threads...`);

  const hdrSS = new Float32Array(S.W * S.H);
  await Promise.all(
    Array.from({ length: n }, (_, idx) =>
      new Promise((resolve, reject) => {
        const w = new Worker(new URL(import.meta.url), { workerData: { opts, idx, n } });
        w.on('message', ({ rows, data }) => {
          rows.forEach((j, r) => hdrSS.set(data.subarray(r * S.W, (r + 1) * S.W), j * S.W));
        });
        w.on('error', reject);
        w.on('exit', resolve);
      })
    )
  );
  console.log(`rays done in ${((Date.now() - t0) / 1000).toFixed(1)}s`);

  const W = opts.width, H = opts.height;
  let img = downsample(hdrSS, S.W, S.H, opts.ss);
  img = radialBlur(img, W, H, opts.cx * W, opts.cy * H, opts.warp, opts.warpFrom, opts.warpTo);
  const b1 = gaussBlur(img, W, H, 2.5), b2 = gaussBlur(img, W, H, 12), b3 = gaussBlur(img, W, H, 40);
  for (let i = 0; i < img.length; i++) img[i] += 0.10 * b1[i] + 0.14 * b2[i] + 0.09 * b3[i];

  const png = encodePng(toneMap(img, W, H, opts), W, H);
  mkdirSync(dirname(opts.out) || '.', { recursive: true });
  writeFileSync(opts.out, png);
  console.log(`wrote ${opts.out} (${(png.length / 1024).toFixed(0)} KB) in ${((Date.now() - t0) / 1000).toFixed(1)}s`);
}

if (isMainThread) main();
