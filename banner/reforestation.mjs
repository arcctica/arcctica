// Pipeline diagram for the Reforestation Planning Map, drawn like a neural network.
//
//   node banner/reforestation.mjs        # writes assets/reforestation.svg
//
// It is a picture of the real data flow, not a trained network:
//   6 factors  ->  every 30 m cell gets one score from all six  ->  cells are grouped into
//   planting zones  ->  zones are served to the map.
// Line thickness does NOT encode weights; the weights are heuristics that haven't been validated.

import { writeFileSync, mkdirSync } from 'node:fs';
import { dirname } from 'node:path';

const OUT = 'assets/reforestation.svg';

const FACTORS = ['burn severity', 'canopy loss', 'time since event', 'seed-source distance', 'slope', 'roads'];
const N_CELLS = 8;
const N_ZONES = 4;
const RESULT = '851 zones (~72,000 acres) in Plumas National Forest, CA  ·  6 zones after Hurricane Helene, SC';
const NOTE = 'scoring weights are heuristics, not yet validated';

// ---- look -------------------------------------------------------------------------------------
const W = 900, H = 440;
const FONT = "'JetBrains Mono','Cascadia Mono','Fira Code',Consolas,Menlo,'Courier New',monospace";
const FS = 13, CW = 7.8;
const RED = '#f0f0f0', LIGHT = '#b8b8b8', DIM = '#7a7a7a', FAINT = '#2c2c2c', BG = '#0d0d0d', FRAME = '#30363d';
const X_IN = 232, X_CELL = 440, X_ZONE = 640, X_MAP = 810;

const f = (n) => Number(n.toFixed(2));
const esc = (s) => s.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
let seed = 7;
const rnd = () => ((seed = (Math.imul(seed, 1664525) + 1013904223) >>> 0) / 4294967296);

const text = (t, x, y, { fill = RED, anchor = 'start', size = FS } = {}) =>
  `<text x="${x}" y="${y}" fill="${fill}" font-size="${size}" text-anchor="${anchor}" textLength="${f(t.length * CW * (size / FS))}" lengthAdjust="spacingAndGlyphs" xml:space="preserve">${esc(t)}</text>`;

// ---- geometry ---------------------------------------------------------------------------------
const inY = FACTORS.map((_, i) => 118 + i * 44);
const cellY = Array.from({ length: N_CELLS }, (_, i) => 100 + i * 35);
const per = N_CELLS / N_ZONES;
const zoneY = Array.from({ length: N_ZONES }, (_, z) => {
  const ys = cellY.slice(z * per, (z + 1) * per);
  return ys.reduce((a, b) => a + b, 0) / ys.length;
});
const mapY = (cellY[0] + cellY[N_CELLS - 1]) / 2;

// ---- build ------------------------------------------------------------------------------------
const T = [0.3, 0.9, 1.5, 2.1];                         // when each column appears
let faint = '', active = '', nodes = '', heads = '', labels = '';

const reveal = (markup, at) => `<g opacity="0">${markup}<set attributeName="opacity" to="1" begin="${at}s"/></g>`;
const fadeIn = (at, to) => `<animate attributeName="opacity" from="0" to="${to}" dur="0.6s" begin="${at}s" fill="freeze"/>`;

// factors -> cells (every cell sees all six factors)
for (const y1 of inY) for (const y2 of cellY)
  faint += `<line x1="${X_IN}" y1="${y1}" x2="${X_CELL}" y2="${y2}" stroke="${RED}" stroke-width="1" opacity="0">${fadeIn(T[1] - 0.2, 0.17)}</line>`;
// a few "active" lines with light flowing along them
for (let k = 0; k < 11; k++) {
  const y1 = inY[Math.floor(rnd() * inY.length)], y2 = cellY[Math.floor(rnd() * cellY.length)];
  const d = f(2 + rnd() * 2.5);
  active += `<line x1="${X_IN}" y1="${y1}" x2="${X_CELL}" y2="${y2}" stroke="${LIGHT}" stroke-width="1.4" stroke-dasharray="3 15" opacity="0">` +
    fadeIn(T[1] + 0.3, 0.75) + `<animate attributeName="stroke-dashoffset" from="36" to="0" dur="${d}s" begin="${T[1] + 0.3}s" repeatCount="indefinite"/></line>`;
}
// cells -> zones (grouping) and zones -> map
cellY.forEach((y, i) => {
  const z = Math.floor(i / per);
  active += `<line x1="${X_CELL}" y1="${y}" x2="${X_ZONE}" y2="${zoneY[z]}" stroke="${RED}" stroke-width="1.6" stroke-dasharray="4 8" opacity="0">` +
    fadeIn(T[2] - 0.2, 0.85) + `<animate attributeName="stroke-dashoffset" from="24" to="0" dur="1.6s" begin="${T[2]}s" repeatCount="indefinite"/></line>`;
});
zoneY.forEach((y) => {
  active += `<line x1="${X_ZONE}" y1="${y}" x2="${X_MAP - 22}" y2="${mapY}" stroke="${RED}" stroke-width="1.6" stroke-dasharray="4 8" opacity="0">` +
    fadeIn(T[3] - 0.2, 0.85) + `<animate attributeName="stroke-dashoffset" from="24" to="0" dur="1.6s" begin="${T[3]}s" repeatCount="indefinite"/></line>`;
});

// nodes
const pulse = (r, at) => `<animate attributeName="r" values="${r};${f(r * 1.28)};${r}" dur="${f(2.4 + rnd() * 1.6)}s" begin="${at}s" repeatCount="indefinite"/>`;
nodes += reveal(inY.map((y, i) => `<circle cx="${X_IN}" cy="${y}" r="6" fill="${BG}" stroke="${RED}" stroke-width="1.8">${pulse(6, T[0] + 1 + i * 0.2)}</circle>`).join(''), T[0]);
labels += reveal(FACTORS.map((t, i) => text(t, X_IN - 16, inY[i] + 4.5, { anchor: 'end' })).join(''), T[0]);
nodes += reveal(cellY.map((y, i) => `<circle cx="${X_CELL}" cy="${y}" r="4.6" fill="${RED}" opacity="0.9">${pulse(4.6, T[1] + 1 + i * 0.15)}</circle>`).join(''), T[1]);
nodes += reveal(zoneY.map((y) => `<circle cx="${X_ZONE}" cy="${y}" r="11" fill="#1c1c1c" stroke="${RED}" stroke-width="1.8"/><circle cx="${X_ZONE}" cy="${y}" r="4" fill="${RED}"/>`).join(''), T[2]);
nodes += reveal(
  `<rect x="${X_MAP - 22}" y="${mapY - 30}" width="44" height="60" rx="5" fill="#1c1c1c" stroke="${RED}" stroke-width="1.8"/>` +
  `<path d="M${X_MAP - 22} ${mapY - 8}H${X_MAP + 22}M${X_MAP - 22} ${mapY + 12}H${X_MAP + 22}M${X_MAP - 4} ${mapY - 30}V${mapY + 30}" stroke="${DIM}" stroke-width="1"/>` +
  `<circle cx="${X_MAP + 9}" cy="${mapY - 17}" r="4" fill="${RED}"/>`, T[3]);

heads += reveal(text('FACTORS', X_IN, 66, { fill: DIM, anchor: 'middle' }), T[0]);
heads += reveal(text('30 m CELLS', X_CELL, 66, { fill: DIM, anchor: 'middle' }), T[1]);
heads += reveal(text('PLANTING ZONES', X_ZONE, 66, { fill: DIM, anchor: 'middle' }), T[2]);
heads += reveal(text('MAP', X_MAP, 66, { fill: DIM, anchor: 'middle' }), T[3]);
labels += reveal(text('score', (X_IN + X_CELL) / 2, 82, { fill: DIM, anchor: 'middle', size: 11 }), T[1]);
labels += reveal(text('group', (X_CELL + X_ZONE) / 2, 82, { fill: DIM, anchor: 'middle', size: 11 }), T[2]);
labels += reveal(text('serve', (X_ZONE + X_MAP) / 2, 82, { fill: DIM, anchor: 'middle', size: 11 }), T[3]);
labels += reveal(text('FastAPI', X_MAP, mapY + 50, { fill: LIGHT, anchor: 'middle' }) + text('React · MapLibre', X_MAP, mapY + 66, { fill: LIGHT, anchor: 'middle' }), T[3] + 0.2);

const foot = reveal(
  `<line x1="30" y1="${H - 62}" x2="${W - 30}" y2="${H - 62}" stroke="${FRAME}"/>` +
  text(`> ${RESULT}`, 30, H - 38, { fill: RED }) +
  text(`! ${NOTE}`, 30, H - 16, { fill: DIM }), T[3] + 0.6);

const svg = `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 ${W} ${H}" width="${W}" height="${H}" role="img" aria-labelledby="t d">
<title id="t">Reforestation Planning Map pipeline</title>
<desc id="d">Six factors (${FACTORS.join(', ')}) feed a score for every 30 m cell; cells are grouped into planting zones that are served to a map. ${RESULT}. ${NOTE}.</desc>
<rect width="${W}" height="${H}" rx="10" fill="${BG}"/>
<rect x="0.5" y="0.5" width="${W - 1}" height="${H - 1}" rx="10" fill="none" stroke="${FRAME}"/>
<g font-family="${FONT}" font-size="${FS}">
  ${text('reforestation.pipeline  //  how a cell becomes a planting zone', 30, 26, { fill: DIM })}
  <circle cx="${W - 30}" cy="22" r="4" fill="#3a3a3a"/><circle cx="${W - 48}" cy="22" r="4" fill="#3a3a3a"/><circle cx="${W - 66}" cy="22" r="4" fill="#3a3a3a"/>
  <line x1="0" y1="38" x2="${W}" y2="38" stroke="${FRAME}"/>
  <g>${faint}${active}${nodes}${heads}${labels}${foot}</g>
</g>
</svg>
`;

mkdirSync(dirname(OUT), { recursive: true });
writeFileSync(OUT, svg);
console.log(`wrote ${OUT} (${(svg.length / 1024).toFixed(1)} KB, ${W}x${H})`);
