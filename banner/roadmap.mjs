// A metro-map style roadmap. No dependencies.
//
//   node banner/roadmap.mjs      # writes assets/roadmap.svg from banner/roadmap.json
//
// Four lanes (math, code and ML, research, building things) run left to right through time and
// end at one destination. Each station is done, in progress ("now") or planned. Edit the texts
// and statuses in roadmap.json, then run the script again. It plays once and holds.

import { readFileSync, writeFileSync, mkdirSync } from 'node:fs';

const R = JSON.parse(readFileSync(new URL('./roadmap.json', import.meta.url), 'utf8'));

// ---- look ---------------------------------------------------------------------------------------
const W = 900;
const FONT = "'JetBrains Mono','Cascadia Mono','Fira Code',Consolas,Menlo,'Courier New',monospace";
const BG = '#0d0d0d', FG = '#f0f0f0', TEXT = '#cfcfcf', MID = '#a8a8a8', DIM = '#6f6f6f', FAINT = '#2c2c2c', LINE = '#30363d';
const CWF = 0.6;
const f = (n) => Number(n.toFixed(2));
const esc = (s) => String(s).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
const text = (t, x, y, { size = 11, fill = TEXT, anchor = 'start', weight = 'normal' } = {}) =>
  `<text x="${f(x)}" y="${f(y)}" font-size="${size}" fill="${fill}" font-weight="${weight}" text-anchor="${anchor}" textLength="${f(t.length * size * CWF)}" lengthAdjust="spacingAndGlyphs" xml:space="preserve">${esc(t)}</text>`;
const reveal = (m, at) => `<g opacity="0">${m}<set attributeName="opacity" to="1" begin="${f(at)}s"/></g>`;
const draw = (d, { stroke, width = 2, at = 0, dur = 0.6, dash = '' }) =>
  dash ? `<path d="${d}" fill="none" stroke="${stroke}" stroke-width="${width}" stroke-dasharray="${dash}" opacity="0"><animate attributeName="opacity" from="0" to="1" dur="${dur}s" begin="${f(at)}s" fill="freeze"/></path>`
    : `<path d="${d}" fill="none" stroke="${stroke}" stroke-width="${width}" stroke-linecap="round" pathLength="1" stroke-dasharray="1" stroke-dashoffset="1"><animate attributeName="stroke-dashoffset" from="1" to="0" dur="${dur}s" begin="${f(at)}s" fill="freeze"/></path>`;

const LABEL_X = 28, COL_X = [150, 300, 450, 600], TERM_X = 810, TOP = 96, LANE_H = 98;
const H = TOP + R.lanes.length * LANE_H + 58;
const laneY = (i) => TOP + 24 + i * LANE_H;
const midY = TOP + 24 + ((R.lanes.length - 1) * LANE_H) / 2;

let s = '';
// column headers and guides
R.columns.forEach((c, i) => {
  s += reveal(text(c.toUpperCase(), COL_X[i], 70, { size: 10, fill: DIM }), 0.1 + i * 0.1);
  s += `<line x1="${COL_X[i]}" y1="${TOP - 6}" x2="${COL_X[i]}" y2="${TOP + R.lanes.length * LANE_H - 30}" stroke="${FAINT}" stroke-dasharray="1 4"/>`;
});
s += reveal(text('DESTINATION', TERM_X, 70, { size: 10, fill: DIM, anchor: 'middle' }), 0.5);

// lanes
R.lanes.forEach((lane, li) => {
  const y = laneY(li), t0 = 0.3 + li * 0.25;
  s += reveal(text(lane.name, LABEL_X, y + 4, { size: 12, fill: MID, weight: 'bold' }), t0);
  const st = [...lane.stations].sort((a, b) => a.col - b.col);
  // segments between stations: solid while the work behind is done, dashed for what is still ahead
  for (let i = 0; i < st.length - 1; i++) {
    const x1 = COL_X[st[i].col], x2 = COL_X[st[i + 1].col], reached = st[i + 1].status === 'done' || st[i + 1].status === 'now';
    const d = `M${x1} ${y}H${x2}`;
    s += reached ? draw(d, { stroke: FG, at: t0 + 0.2 + i * 0.35 }) : draw(d, { stroke: DIM, width: 1.6, at: t0 + 0.2 + i * 0.35, dash: '4 5' });
  }
  const last = st[st.length - 1];
  s += draw(`M${COL_X[last.col]} ${y}C${COL_X[last.col] + 80} ${y} ${TERM_X - 90} ${midY} ${TERM_X - 12} ${midY}`, { stroke: DIM, width: 1.4, at: t0 + 1.4, dash: '2 5' });
  // stations
  st.forEach((p, i) => {
    const x = COL_X[p.col], at = t0 + 0.35 + i * 0.35;
    let node;
    if (p.status === 'done') node = `<circle cx="${x}" cy="${y}" r="6.5" fill="${FG}"/><path d="M${x - 3} ${y}l2.2 2.4 4-4.6" fill="none" stroke="${BG}" stroke-width="1.6" stroke-linecap="round"/>`;
    else if (p.status === 'now') node = `<circle cx="${x}" cy="${y}" r="9" fill="none" stroke="${FG}" stroke-opacity="0.35"><animate attributeName="r" values="8;12;8" dur="2.4s" repeatCount="indefinite"/><animate attributeName="stroke-opacity" values="0.5;0;0.5" dur="2.4s" repeatCount="indefinite"/></circle><circle cx="${x}" cy="${y}" r="6.5" fill="${BG}" stroke="${FG}" stroke-width="2"/><circle cx="${x}" cy="${y}" r="2.6" fill="${FG}"/>`;
    else node = `<circle cx="${x}" cy="${y}" r="5.5" fill="${BG}" stroke="${DIM}" stroke-width="1.6"/>`;
    const dim = p.status === 'planned';
    s += reveal(node + text(p.title, x - 6, y + 25, { size: 11, fill: dim ? MID : FG, weight: dim ? 'normal' : 'bold' }) + text(p.note, x - 6, y + 39, { size: 10, fill: DIM }), at);
  });
});

// destination
s += reveal(`<circle cx="${TERM_X}" cy="${midY}" r="11" fill="${BG}" stroke="${FG}" stroke-width="2"/><circle cx="${TERM_X}" cy="${midY}" r="4.5" fill="${FG}"/>` +
  R.destination.map((l, i) => text(l, TERM_X, midY + 34 + i * 15, { size: i === 0 ? 12 : 10, fill: i === 0 ? FG : MID, anchor: 'middle', weight: i === 0 ? 'bold' : 'normal' })).join(''), 2.6);

// legend
const ly = H - 26;
s += `<circle cx="34" cy="${ly - 4}" r="5.5" fill="${FG}"/>` + text('done', 46, ly, { size: 10, fill: DIM });
s += `<circle cx="104" cy="${ly - 4}" r="5" fill="${BG}" stroke="${FG}" stroke-width="1.8"/><circle cx="104" cy="${ly - 4}" r="2" fill="${FG}"/>` + text('now', 116, ly, { size: 10, fill: DIM });
s += `<circle cx="164" cy="${ly - 4}" r="5" fill="${BG}" stroke="${DIM}" stroke-width="1.6"/>` + text('planned', 176, ly, { size: 10, fill: DIM });

const desc = `${R.title}. ` + R.lanes.map((l) => `${l.name}: ` + l.stations.map((p) => `${p.title} (${p.status})`).join(', ')).join('. ') + `. Destination: ${R.destination.join(' ')}.`;
const svg = `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 ${W} ${H}" width="${W}" height="${H}" role="img" aria-labelledby="t d">
<title id="t">Roadmap from here to AI research</title>
<desc id="d">${esc(desc)}</desc>
<rect x="0.5" y="0.5" width="${W - 1}" height="${H - 1}" rx="10" fill="${BG}" stroke="${LINE}"/>
<circle cx="22" cy="17" r="4.5" fill="#3a3a3a"/><circle cx="38" cy="17" r="4.5" fill="#3a3a3a"/><circle cx="54" cy="17" r="4.5" fill="#3a3a3a"/>
<g font-family="${FONT}">
  ${text(R.title, 74, 21, { size: 11, fill: DIM })}
  <line x1="0" y1="34" x2="${W}" y2="34" stroke="${LINE}"/>
  ${s}
</g>
</svg>
`;
mkdirSync('assets', { recursive: true });
writeFileSync('assets/roadmap.svg', svg);
console.log(`wrote assets/roadmap.svg (${(svg.length / 1024).toFixed(1)} KB, ${W}x${H})`);
