// Project cards for the profile README. No dependencies.
//
//   node banner/projects.mjs     # writes assets/project-*.svg from banner/projects.json
//
// Each card is one SVG: facts on the left, and on the right a small picture that is generated
// from a seeded random number generator and animated with SMIL (GitHub shows SVGs in <img>
// without scripts, so nothing but plain <animate>/<set> is used).
//   Unchained      a lookup fanning out through plugins into a relationship graph
//   Reforestation  a map of 30 m cells coloured by score, with the best cells outlined as zones
//   Knowly         a dark tree of knowledge where nodes light up
// The pictures are illustrations of how each project works, not screenshots, and they contain
// no real data (the lookup uses the reserved example.com and a documentation IP address).

import { readFileSync, writeFileSync, mkdirSync } from 'node:fs';

// ---- look ---------------------------------------------------------------------------------------
const W = 900, H = 272;
const FONT = "'JetBrains Mono','Cascadia Mono','Fira Code',Consolas,Menlo,'Courier New',monospace";
const BG = '#0d0d0d', PANEL = '#111111', FG = '#f0f0f0', TEXT = '#cfcfcf', MID = '#a8a8a8', DIM = '#6f6f6f', FAINT = '#2c2c2c', LINE = '#30363d';
const X0 = 30, COL_W = 450;                    // text column
const AX = 500, AY = 26, AW = 372, AH = 220;   // art panel
const CWF = 0.6;                               // monospace advance as a fraction of font size

const f = (n) => Number(n.toFixed(2));
const esc = (s) => String(s).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
function rng(seed) { let a = seed >>> 0; return () => { a = (a + 0x6d2b79f5) >>> 0; let t = a; t = Math.imul(t ^ (t >>> 15), t | 1); t ^= t + Math.imul(t ^ (t >>> 7), t | 61); return ((t ^ (t >>> 14)) >>> 0) / 4294967296; }; }
const text = (t, x, y, { size = 12, fill = TEXT, anchor = 'start', weight = 'normal' } = {}) =>
  `<text x="${f(x)}" y="${f(y)}" font-size="${size}" fill="${fill}" font-weight="${weight}" text-anchor="${anchor}" textLength="${f(t.length * size * CWF)}" lengthAdjust="spacingAndGlyphs" xml:space="preserve">${esc(t)}</text>`;
const reveal = (markup, at) => `<g opacity="0">${markup}<set attributeName="opacity" to="1" begin="${f(at)}s"/></g>`;
const draw = (d, { stroke = MID, width = 1.2, at = 0, dur = 0.6, opacity = 1, dash = '' } = {}) =>
  `<path d="${d}" fill="none" stroke="${stroke}" stroke-width="${width}" opacity="${opacity}" ${dash ? `stroke-dasharray="${dash}"` : 'pathLength="1" stroke-dasharray="1" stroke-dashoffset="1"'} stroke-linecap="round">` +
  (dash ? `<animate attributeName="opacity" from="0" to="${opacity}" dur="${dur}s" begin="${f(at)}s" fill="freeze"/>` : `<animate attributeName="stroke-dashoffset" from="1" to="0" dur="${dur}s" begin="${f(at)}s" fill="freeze"/>`) + `</path>`;
const pulse = (r, at, dur = 3) => `<animate attributeName="r" values="${r};${f(r * 1.3)};${r}" dur="${dur}s" begin="${f(at)}s" repeatCount="indefinite"/>`;

function wrap(s, max) {
  const out = []; let line = '';
  for (const w of s.split(' ')) { if (line && (line + ' ' + w).length > max) { out.push(line); line = w; } else line = line ? `${line} ${w}` : w; }
  if (line) out.push(line);
  return out;
}

// ---- the card frame ------------------------------------------------------------------------------
function card({ name, status, tagline, facts, stack, art, desc }) {
  let g = '';
  g += text(`● ${status}`, X0, 40, { size: 11, fill: DIM });
  g += text(name, X0, 74, { size: 24, fill: FG, weight: 'bold' });
  g += text(tagline, X0, 97, { size: 13, fill: MID });
  let y = 128;
  for (const fact of facts)
    for (const [i, line] of wrap(fact, 58).entries()) { g += text((i === 0 ? '› ' : '  ') + line, X0, y, { size: 12, fill: TEXT }); y += 17; }
  let x = X0;
  for (const s of stack) { // stack chips
    const w = s.length * 11 * CWF + 18;
    g += `<rect x="${f(x)}" y="${H - 42}" width="${f(w)}" height="22" rx="11" fill="none" stroke="${LINE}"/>` + text(s, x + 9, H - 27, { size: 11, fill: MID });
    x += w + 7;
  }
  return `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 ${W} ${H}" width="${W}" height="${H}" role="img" aria-labelledby="t d">
<title id="t">${esc(name)}: ${esc(tagline)}</title>
<desc id="d">${esc(desc)}</desc>
<rect x="0.5" y="0.5" width="${W - 1}" height="${H - 1}" rx="10" fill="${BG}" stroke="${LINE}"/>
<g font-family="${FONT}">${g}</g>
<rect x="${AX}" y="${AY}" width="${AW}" height="${AH}" rx="8" fill="${PANEL}" stroke="${FAINT}"/>
<g transform="translate(${AX} ${AY})" font-family="${FONT}"><clipPath id="clip"><rect width="${AW}" height="${AH}" rx="8"/></clipPath><g clip-path="url(#clip)">${art}</g></g>
</svg>
`;
}

// ---- Unchained: one lookup fanning out into a graph ----------------------------------------------
function artUnchained() {
  let s = '';
  const inBox = { x: 12, y: 95, w: 108, h: 30 }, inC = [inBox.x + inBox.w, inBox.y + inBox.h / 2];
  s += reveal(`<rect x="${inBox.x}" y="${inBox.y}" width="${inBox.w}" height="${inBox.h}" rx="4" fill="none" stroke="${MID}"/>${text('example.com', inBox.x + 10, inBox.y + 19, { size: 11, fill: FG })}`, 0.2);
  s += reveal(text('type: domain', inBox.x, inBox.y + 46, { size: 10, fill: DIM }), 0.7);
  const plugins = [['whois/dns', 190, 54, true], ['crt.sh', 190, 110, true], ['discord', 190, 168, false]];
  const results = [ // [label, x, y, plugin index]
    ['198.51.100.7', 286, 30, 0], ['ns1', 286, 66, 0], ['api', 286, 100, 1], ['mail', 286, 130, 1], ['dev', 286, 160, 1],
  ];
  const cross = [[2, 0], [3, 1]]; // results that share an address or name server
  plugins.forEach(([label, x, y, on], i) => {
    const d = `M${inC[0]} ${inC[1]}C${inC[0] + 30} ${inC[1]} ${x - 40} ${y} ${x - 6} ${y}`;
    s += on ? draw(d, { stroke: MID, width: 1.4, at: 0.9 + i * 0.15 }) : draw(d, { stroke: DIM, width: 1, at: 0.9 + i * 0.15, dash: '3 4', opacity: 0.6 });
    s += reveal(`<circle cx="${x}" cy="${y}" r="6" fill="${on ? FG : BG}" stroke="${on ? FG : DIM}" stroke-width="1.4"${on ? '' : ' stroke-dasharray="2 2"'}>${on ? pulse(6, 2.5, 2.8 + i * 0.4) : ''}</circle>${text(label, x - 8, y - 11, { size: 10, fill: on ? MID : DIM, anchor: 'end' })}`, 1.1 + i * 0.15);
  });
  s += reveal(text('no match', 190 - 8, 168 + 22, { size: 9, fill: DIM, anchor: 'end' }), 1.4);
  results.forEach(([label, x, y, pi], i) => {
    const [, px, py] = plugins[pi];
    s += draw(`M${px + 6} ${py}L${x - 5} ${y}`, { stroke: MID, width: 1, at: 1.7 + i * 0.12, dur: 0.4 });
    s += reveal(`<circle cx="${x}" cy="${y}" r="4" fill="${FG}"/>${text(label, x + 9, y + 3.5, { size: 10, fill: TEXT })}`, 1.9 + i * 0.12);
  });
  cross.forEach(([a, b], i) => {
    const A = results[a], B = results[b];
    s += draw(`M${A[1]} ${A[2]}C${A[1] - 26} ${A[2] + 16} ${B[1] - 26} ${B[2] - 16} ${B[1]} ${B[2]}`, { stroke: DIM, width: 1, at: 2.7 + i * 0.2, dur: 0.6, dash: '2 3', opacity: 0.9 });
  });
  s += text('every matching source, in parallel', 12, 208, { size: 10, fill: DIM });
  return s;
}

// ---- Reforestation: scored cells and the zones they form -----------------------------------------
function artReforestation() {
  const COLS = 31, ROWS = 16, S = 11.5, ox = (AW - COLS * S) / 2, oy = 14;
  const r = rng(21), hash = (x, y) => { const q = rng(x * 73856093 ^ y * 19349663 ^ 5)(); return q; };
  const vn = (x, y) => { // smooth value noise
    const xi = Math.floor(x), yi = Math.floor(y), fx = x - xi, fy = y - yi, u = fx * fx * (3 - 2 * fx), v = fy * fy * (3 - 2 * fy);
    const a = hash(xi, yi), b = hash(xi + 1, yi), c = hash(xi, yi + 1), d = hash(xi + 1, yi + 1);
    return a + (b - a) * u + (c - a) * v + (a - b - c + d) * u * v;
  };
  const score = [];
  for (let j = 0; j < ROWS; j++) for (let i = 0; i < COLS; i++) {
    const nx = (i / COLS - 0.55) / 0.40, ny = (j / ROWS - 0.5) / 0.46;
    const burn = Math.max(0, 1 - Math.hypot(nx + 0.25 * vn(i * 0.35, j * 0.35), ny)); // a burn scar
    const n = 0.55 * vn(i * 0.28, j * 0.28) + 0.3 * vn(i * 0.6 + 9, j * 0.6) + 0.15 * vn(i * 1.3, j * 1.3 + 4);
    score.push(Math.min(1, Math.max(0, 0.15 + burn * 0.75 + (n - 0.5) * 0.7)));
  }
  const at = (i, j) => (i < 0 || j < 0 || i >= COLS || j >= ROWS ? 0 : score[j * COLS + i]);
  const high = (i, j) => at(i, j) > 0.57;
  let s = '';
  for (let i = 0; i < COLS; i++) { // one group per column, so the map appears as a wave
    let col = '';
    for (let j = 0; j < ROWS; j++) {
      const v = at(i, j), g = Math.round(26 + v * 150);
      col += `<rect x="${f(ox + i * S)}" y="${f(oy + j * S)}" width="${S - 1.2}" height="${S - 1.2}" fill="rgb(${g},${g},${g})"${high(i, j) ? ` stroke="${FG}" stroke-opacity="0.0"` : ''}/>`;
    }
    s += reveal(col, 0.2 + i * 0.035);
  }
  // outline the high-scoring cells: draw every cell edge that faces a lower-scoring neighbour
  let d = '';
  for (let j = 0; j < ROWS; j++) for (let i = 0; i < COLS; i++) {
    if (!high(i, j)) continue;
    const x = ox + i * S - 0.6, y = oy + j * S - 0.6;
    if (!high(i, j - 1)) d += `M${f(x)} ${f(y)}h${S}`;
    if (!high(i, j + 1)) d += `M${f(x)} ${f(y + S)}h${S}`;
    if (!high(i - 1, j)) d += `M${f(x)} ${f(y)}v${S}`;
    if (!high(i + 1, j)) d += `M${f(x + S)} ${f(y)}v${S}`;
  }
  s += draw(d, { stroke: FG, width: 1.6, at: 1.5, dur: 1.4 });
  s += reveal(text('planting zones', 12, AH - 8, { size: 10, fill: FG }) + `<rect x="${AW - 108}" y="${AH - 17}" width="9" height="9" fill="rgb(26,26,26)"/><rect x="${AW - 96}" y="${AH - 17}" width="9" height="9" fill="rgb(76,76,76)"/><rect x="${AW - 84}" y="${AH - 17}" width="9" height="9" fill="rgb(126,126,126)"/><rect x="${AW - 72}" y="${AH - 17}" width="9" height="9" fill="rgb(176,176,176)"/>` + text('score', AW - 58, AH - 8, { size: 10, fill: DIM }), 2.6);
  return s;
}

// ---- Knowly: a dark tree in which nodes light up -----------------------------------------------------
function artKnowly() {
  const r = rng(5), nodes = [], edges = [];
  const add = (x, y, parent, depth) => { nodes.push({ x, y, parent, depth, lit: false }); return nodes.length - 1; };
  const root = add(26, 110, -1, 0);
  (function grow(i, x, y, ang, len, depth) {
    if (depth >= 4) return;
    const k = depth === 0 ? 3 : r() < 0.25 ? 3 : 2;
    for (let j = 0; j < k; j++) {
      const a = ang + (j - (k - 1) / 2) * 0.8 + (r() - 0.5) * 0.25, l = len * (0.85 + r() * 0.15);
      const nx = x + Math.cos(a) * l, ny = y + Math.sin(a) * l;
      if (nx > AW - 12 || ny < 14 || ny > AH - 14) continue;
      const c = add(nx, ny, i, depth + 1);
      edges.push([i, c]);
      grow(c, nx, ny, a, len * 0.9, depth + 1);
    }
  })(root, 26, 110, 0, 84, 0);
  const pLit = [1, 0.95, 0.8, 0.6, 0.4];
  nodes[root].lit = true;
  for (const n of nodes.slice(1)) n.lit = nodes[n.parent].lit && r() < pLit[n.depth];
  const when = (n) => 0.5 + n.depth * 0.42 + r() * 0.25;
  nodes.forEach((n) => { n.t = when(n); });

  let s = '';
  for (const [a, b] of edges) { // dark by default; lit edges are drawn as the child ignites
    const A = nodes[a], B = nodes[b], d = `M${f(A.x)} ${f(A.y)}L${f(B.x)} ${f(B.y)}`;
    s += `<path d="${d}" stroke="${FAINT}" stroke-width="1.2" fill="none"/>`;
    if (B.lit) s += draw(d, { stroke: MID, width: 1.6, at: B.t, dur: 0.4 });
  }
  for (const n of nodes) {
    if (n.lit) {
      s += `<circle cx="${f(n.x)}" cy="${f(n.y)}" r="${n.depth === 0 ? 5.5 : 4}" fill="${BG}" stroke="${DIM}" stroke-width="1.2"/>`;
      s += `<g opacity="0"><circle cx="${f(n.x)}" cy="${f(n.y)}" r="${n.depth === 0 ? 11 : 8}" fill="${FG}" opacity="0.14">${pulse(n.depth === 0 ? 11 : 8, n.t + 0.5, 2.6 + (n.depth % 3) * 0.5)}</circle><circle cx="${f(n.x)}" cy="${f(n.y)}" r="${n.depth === 0 ? 5.5 : 4}" fill="${FG}"/><set attributeName="opacity" to="1" begin="${f(n.t)}s"/></g>`;
    } else s += `<circle cx="${f(n.x)}" cy="${f(n.y)}" r="3" fill="${BG}" stroke="#444" stroke-width="1"/>`;
  }
  s += text('a node lights up when you do something with it', 12, AH - 8, { size: 10, fill: DIM });
  return s;
}

// ---- the cards: texts live in projects.json (edit them there), pictures are drawn above -----------------
const ART = { unchained: artUnchained, reforestation: artReforestation, knowly: artKnowly };
const CARDS = JSON.parse(readFileSync(new URL('./projects.json', import.meta.url), 'utf8'));

mkdirSync('assets', { recursive: true });
for (const c of CARDS) {
  const desc = `${c.name}: ${c.tagline}. ${c.facts.join('. ')}. Status: ${c.status}.`;
  const svg = card({ ...c, desc, art: ART[c.file]() });
  writeFileSync(`assets/project-${c.file}.svg`, svg);
  console.log(`wrote assets/project-${c.file}.svg (${(svg.length / 1024).toFixed(1)} KB)`);
}
