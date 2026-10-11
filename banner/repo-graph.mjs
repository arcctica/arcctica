// A relationship graph of my work that rebuilds itself. No dependencies.
//
//   node banner/repo-graph.mjs            # writes assets/repo-graph.svg
//   GITHUB_TOKEN=... node banner/repo-graph.mjs   # same, with a higher API rate limit
//
// Nodes:   me -> projects -> languages and tech.
// Sources: banner/graph.config.json (projects that aren't public yet, plus extra tech tags)
//          merged with the live GitHub API (public repos, their languages in bytes, topics, stars).
// A project that matches a public repo is drawn solid and takes its languages/topics from GitHub;
// one that doesn't is drawn dashed ("not public yet"), unless the config says "public": true
// (for work that lives in this very repo). Publish a repo or add a topic and the
// next run of .github/workflows/update-graph.yml redraws the graph.
// Only public data is ever read, so nothing private can leak into the picture.
//
// The layout is a small Fruchterman-Reingold force simulation with a fixed seed, so the picture
// only moves when the data does.

import { readFileSync, writeFileSync, mkdirSync } from 'node:fs';
import { dirname } from 'node:path';

const OUT = 'assets/repo-graph.svg';
const cfg = JSON.parse(readFileSync(new URL('./graph.config.json', import.meta.url), 'utf8'));

const W = 900, H = 470;
const FONT = "'JetBrains Mono','Cascadia Mono','Fira Code',Consolas,Menlo,'Courier New',monospace";
const FS = 12, CW = 7.2;
const RED = '#ff3b3b', LIGHT = '#ff9a9a', DIM = '#7a1c1f', FAINT = '#3a0d10', BG = '#0a0305', FRAME = '#5c1418';

const f = (n) => Number(n.toFixed(2));
const esc = (s) => String(s).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');

// ---- data -------------------------------------------------------------------------------------
async function gh(path) {
  const headers = { accept: 'application/vnd.github+json', 'user-agent': 'profile-graph' };
  if (process.env.GITHUB_TOKEN) headers.authorization = `Bearer ${process.env.GITHUB_TOKEN}`;
  const res = await fetch(`https://api.github.com${path}`, { headers });
  if (!res.ok) throw new Error(`${path}: HTTP ${res.status}`);
  return res.json();
}

async function loadRepos() {
  try {
    const list = await gh(`/users/${cfg.user}/repos?type=owner&per_page=100&sort=pushed`);
    const repos = list.filter((r) => !r.fork && !r.archived && r.name.toLowerCase() !== cfg.user.toLowerCase());
    return await Promise.all(repos.map(async (r) => ({
      name: r.name, stars: r.stargazers_count, topics: r.topics ?? [],
      languages: await gh(`/repos/${cfg.user}/${r.name}/languages`).catch(() => ({})),
    })));
  } catch (e) {
    console.warn(`! GitHub API unavailable (${e.message}); drawing configured projects only`);
    return [];
  }
}

const repos = await loadRepos();
const used = new Set();
const projects = cfg.projects.map((p) => {
  const hit = repos.find((r) => !used.has(r.name) && p.match.some((m) => r.name.toLowerCase().includes(m)));
  if (hit) used.add(hit.name);
  const langs = hit && Object.keys(hit.languages).length ? hit.languages : Object.fromEntries(p.languages.map((l) => [l, 1]));
  return { label: p.name, public: !!hit || p.public === true, stars: hit?.stars ?? 0, langs, tech: [...new Set([...(p.tech ?? []), ...(hit?.topics ?? [])])] };
});
for (const r of repos) if (!used.has(r.name))
  projects.push({ label: r.name, public: true, stars: r.stars, langs: r.languages, tech: r.topics });

// ---- graph ------------------------------------------------------------------------------------
const nodes = [], edges = [], byKey = new Map();
const node = (key, o) => { if (!byKey.has(key)) { const n = { key, ...o, w: 0 }; byKey.set(key, n); nodes.push(n); } return byKey.get(key); };
const me = node('me', { kind: 'me', label: cfg.user, r: 17 });

for (const p of projects) {
  const total = Object.values(p.langs).reduce((a, b) => a + b, 0) || 1;
  const pn = node(`p:${p.label}`, { kind: 'repo', label: p.label, public: p.public, r: 10 + Math.min(5, Math.log2(1 + total / 2000)) });
  edges.push({ a: me, b: pn, w: 1, kind: 'repo' });
  const top = Object.entries(p.langs).sort((a, b) => b[1] - a[1]).filter(([, v]) => v / total >= 0.03).slice(0, 4);
  for (const [lang, v] of top) {
    const ln = node(`l:${lang}`, { kind: 'lang', label: lang, r: 6 });
    ln.w += v / total;
    edges.push({ a: pn, b: ln, w: v / total, kind: 'lang' });
  }
  for (const t of p.tech) {
    const tn = node(`t:${t}`, { kind: 'tech', label: t, r: 3.6 });
    edges.push({ a: pn, b: tn, w: 0.3, kind: 'tech' });
  }
}
nodes.filter((n) => n.kind === 'lang').forEach((n) => { n.r = 5.5 + Math.min(4, n.w * 3); });

// ---- layout (seeded force simulation) --------------------------------------------------------
function seedFor(str) { let h = 2166136261; for (const c of str) h = Math.imul(h ^ c.charCodeAt(0), 16777619); return h >>> 0; }
const rand01 = (s) => ((Math.imul(s ^ (s >>> 15), 2246822507) >>> 0) / 4294967296);

const box = { x0: 120, x1: W - 120, y0: 80, y1: H - 78 };
const cx = (box.x0 + box.x1) / 2, cy = (box.y0 + box.y1) / 2;
nodes.forEach((n, i) => {
  const a = rand01(seedFor(n.key)) * Math.PI * 2, d = n.kind === 'me' ? 0 : 60 + rand01(seedFor(n.key + 'd')) * 160;
  n.x = cx + Math.cos(a) * d * 1.5; n.y = cy + Math.sin(a) * d;
});
{
  const k = Math.sqrt(((box.x1 - box.x0) * (box.y1 - box.y0)) / nodes.length) * 0.8;
  let temp = (box.x1 - box.x0) / 6;
  for (let it = 0; it < 700; it++) {
    for (const n of nodes) { n.dx = 0; n.dy = 0; }
    for (let i = 0; i < nodes.length; i++) for (let j = i + 1; j < nodes.length; j++) {
      const a = nodes[i], b = nodes[j];
      let dx = a.x - b.x, dy = a.y - b.y; const d = Math.max(Math.hypot(dx, dy), 0.1);
      const rep = (k * k) / d; dx /= d; dy /= d;
      a.dx += dx * rep; a.dy += dy * rep; b.dx -= dx * rep; b.dy -= dy * rep;
    }
    for (const e of edges) {
      let dx = e.a.x - e.b.x, dy = e.a.y - e.b.y; const d = Math.max(Math.hypot(dx, dy), 0.1);
      const att = (d * d) / k * (0.6 + e.w); dx /= d; dy /= d;
      e.a.dx -= dx * att; e.a.dy -= dy * att; e.b.dx += dx * att; e.b.dy += dy * att;
    }
    for (const n of nodes) {
      n.dx += (cx - n.x) * 0.05; n.dy += (cy - n.y) * 0.05;       // gentle gravity
      if (n.kind === 'me') { n.x = cx; n.y = cy; continue; }       // the hole stays in the middle
      const m = Math.hypot(n.dx, n.dy) || 1, s = Math.min(m, temp);
      n.x = Math.min(box.x1, Math.max(box.x0, n.x + (n.dx / m) * s));
      n.y = Math.min(box.y1, Math.max(box.y0, n.y + (n.dy / m) * s));
    }
    temp *= 0.985;
  }
}

// ---- keep labels from colliding -------------------------------------------------------------
const labelSize = (n) => (n.kind === 'tech' ? 11 : n.kind === 'repo' ? 13 : FS);
const labelWidth = (n) => n.label.length * CW * (labelSize(n) / FS);
function labelRight(n) { // labels go outward from the centre, but flip to whichever side has room
  const w = labelWidth(n);
  let right = n.x >= cx;
  if (right && n.x + n.r + 6 + w > W - 14) right = false;
  else if (!right && n.x - n.r - 6 - w < 14) right = true;
  return right;
}
function footprint(n) { // the node plus its label
  if (n.kind === 'me') return { x0: n.x - n.r - 8, x1: n.x + n.r + 8, y0: n.y - n.r - 8, y1: n.y + n.r + 32 };
  const w = labelWidth(n);
  return labelRight(n) ? { x0: n.x - n.r, x1: n.x + n.r + 6 + w, y0: n.y - 10, y1: n.y + 10 } : { x0: n.x - n.r - 6 - w, x1: n.x + n.r, y0: n.y - 10, y1: n.y + 10 };
}
for (let it = 0; it < 400; it++) {
  let moved = false;
  for (let i = 0; i < nodes.length; i++) for (let j = i + 1; j < nodes.length; j++) {
    const a = nodes[i], b = nodes[j], ra = footprint(a), rb = footprint(b);
    const ox = Math.min(ra.x1, rb.x1) - Math.max(ra.x0, rb.x0), oy = Math.min(ra.y1, rb.y1) - Math.max(ra.y0, rb.y0);
    if (ox <= 0 || oy <= 0) continue;
    const dir = a.y === b.y ? (i % 2 ? 1 : -1) : Math.sign(a.y - b.y), push = oy / 2 + 1;
    if (a.kind !== 'me') a.y += dir * push; if (b.kind !== 'me') b.y -= dir * push;
    for (const n of [a, b]) n.y = Math.min(box.y1 + 20, Math.max(box.y0 - 10, n.y));
    moved = true;
  }
  if (!moved) break;
}

// ---- draw -------------------------------------------------------------------------------------
const label = (t, x, y, { fill = RED, anchor = 'start', size = FS } = {}) =>
  `<text x="${f(x)}" y="${f(y)}" fill="${fill}" font-size="${size}" text-anchor="${anchor}" textLength="${f(t.length * CW * (size / FS))}" lengthAdjust="spacingAndGlyphs" xml:space="preserve">${esc(t)}</text>`;

let g = '', order = 0;
for (const e of edges) {
  const at = 0.4 + (order++) * 0.05;
  const [stroke, sw, op] = e.kind === 'repo' ? [RED, 1.8, 0.9] : e.kind === 'lang' ? [LIGHT, 1 + e.w * 1.2, 0.55] : [DIM, 1, 0.8];
  g += `<line x1="${f(e.a.x)}" y1="${f(e.a.y)}" x2="${f(e.b.x)}" y2="${f(e.b.y)}" stroke="${stroke}" stroke-width="${f(sw)}" opacity="${op}" pathLength="1" stroke-dasharray="1" stroke-dashoffset="1">` +
    `<animate attributeName="stroke-dashoffset" from="1" to="0" dur="0.7s" begin="${f(at)}s" fill="freeze"/></line>`;
}
order = 0;
for (const n of nodes) {
  const at = 0.2 + (order++) * 0.05;
  let shape = '', text = '';
  const pulse = `<animate attributeName="r" values="${n.r};${f(n.r * 1.18)};${n.r}" dur="${f(2.6 + rand01(seedFor(n.key)) * 2)}s" begin="${f(at + 1)}s" repeatCount="indefinite"/>`;
  const right = labelRight(n);
  const lx = right ? n.x + n.r + 6 : n.x - n.r - 6, anchor = right ? 'start' : 'end';
  if (n.kind === 'me') {
    shape = `<circle cx="${f(n.x)}" cy="${f(n.y)}" r="${n.r}" fill="#000" stroke="${RED}" stroke-width="2.2">${pulse}</circle>` +
      `<circle cx="${f(n.x)}" cy="${f(n.y)}" r="${n.r + 7}" fill="none" stroke="${RED}" stroke-width="1" opacity="0.35"/>`;
    text = label(n.label, n.x, n.y + n.r + 24, { anchor: 'middle', fill: LIGHT, size: 13 });
  } else if (n.kind === 'repo') {
    shape = `<circle cx="${f(n.x)}" cy="${f(n.y)}" r="${f(n.r)}" fill="${n.public ? RED : BG}" stroke="${RED}" stroke-width="2"${n.public ? '' : ' stroke-dasharray="3 3"'}>${pulse}</circle>`;
    text = label(n.label, lx, n.y + 4.5, { anchor, size: 13, fill: '#ffd0d0' });
  } else if (n.kind === 'lang') {
    shape = `<circle cx="${f(n.x)}" cy="${f(n.y)}" r="${f(n.r)}" fill="${BG}" stroke="${LIGHT}" stroke-width="1.8"/>`;
    text = label(n.label, lx, n.y + 4, { anchor, fill: LIGHT });
  } else {
    shape = `<circle cx="${f(n.x)}" cy="${f(n.y)}" r="${n.r}" fill="${DIM}"/>`;
    text = label(n.label, lx, n.y + 3.5, { anchor, fill: DIM, size: 11 });
  }
  g += `<g opacity="0">${shape}${text}<set attributeName="opacity" to="1" begin="${f(at)}s"/></g>`;
}

const nPublic = projects.filter((p) => p.public).length;
const stars = projects.reduce((a, p) => a + p.stars, 0);
const langCount = nodes.filter((n) => n.kind === 'lang').length;
const stamp = new Date().toISOString().slice(0, 10);
const stats = `${projects.length} projects · ${nPublic} public · ${langCount} languages` + (stars ? ` · ${stars}★` : '');

const svg = `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 ${W} ${H}" width="${W}" height="${H}" role="img" aria-labelledby="t d">
<title id="t">Graph of ${esc(cfg.user)}'s projects, languages and tools</title>
<desc id="d">${esc(stats)}. Projects: ${esc(projects.map((p) => `${p.label}${p.public ? '' : ' (not public yet)'}: ${[...Object.keys(p.langs), ...p.tech].join(', ')}`).join('; '))}.</desc>
<defs>
  <pattern id="scan" width="4" height="4" patternUnits="userSpaceOnUse"><rect width="4" height="1" fill="#000" opacity="0.28"/></pattern>
  <radialGradient id="vig" cx="50%" cy="48%" r="75%"><stop offset="55%" stop-color="#000" stop-opacity="0"/><stop offset="100%" stop-color="#000" stop-opacity="0.55"/></radialGradient>
  <filter id="glow" x="-5%" y="-10%" width="110%" height="120%"><feGaussianBlur stdDeviation="1.5" result="b"/><feMerge><feMergeNode in="b"/><feMergeNode in="SourceGraphic"/></feMerge></filter>
</defs>
<rect width="${W}" height="${H}" rx="10" fill="${BG}"/>
<rect x="0.5" y="0.5" width="${W - 1}" height="${H - 1}" rx="10" fill="none" stroke="${FRAME}"/>
<g font-family="${FONT}" font-size="${FS}">
  ${label(`${cfg.user}@github: ~/${cfg.title.replace(/ /g, '-')}  (auto-generated)`, 30, 26, { fill: DIM })}
  <circle cx="${W - 30}" cy="22" r="4" fill="${DIM}"/><circle cx="${W - 48}" cy="22" r="4" fill="${DIM}"/><circle cx="${W - 66}" cy="22" r="4" fill="${DIM}"/>
  <line x1="0" y1="38" x2="${W}" y2="38" stroke="${FRAME}"/>
  <g filter="url(#glow)">${g}</g>
  <line x1="30" y1="${H - 50}" x2="${W - 30}" y2="${H - 50}" stroke="${FRAME}"/>
  <circle cx="38" cy="${H - 27}" r="5" fill="${RED}"/>${label('public repo', 50, H - 23, { fill: DIM, size: 11 })}
  <circle cx="148" cy="${H - 27}" r="5" fill="none" stroke="${RED}" stroke-width="1.5" stroke-dasharray="3 2"/>${label('not public yet', 160, H - 23, { fill: DIM, size: 11 })}
  <circle cx="280" cy="${H - 27}" r="5" fill="none" stroke="${LIGHT}" stroke-width="1.5"/>${label('language', 292, H - 23, { fill: DIM, size: 11 })}
  <circle cx="368" cy="${H - 27}" r="3.4" fill="${DIM}"/>${label('tool / topic', 378, H - 23, { fill: DIM, size: 11 })}
  ${label(`${stats}  ·  synced ${stamp}`, W - 30, H - 23, { fill: DIM, size: 11, anchor: 'end' })}
</g>
<rect width="${W}" height="${H}" rx="10" fill="url(#scan)" pointer-events="none"/>
<rect width="${W}" height="${H}" rx="10" fill="url(#vig)" pointer-events="none"/>
</svg>
`;

mkdirSync(dirname(OUT), { recursive: true });
writeFileSync(OUT, svg);
console.log(`wrote ${OUT} (${(svg.length / 1024).toFixed(1)} KB): ${stats}`);
