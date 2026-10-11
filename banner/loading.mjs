// Animated "boot screen" for the profile README. No dependencies.
//
//   node banner/loading.mjs        # writes assets/loading.svg
//
// GitHub renders SVGs inside <img> with SMIL animation but without scripts or web fonts,
// so everything here is plain <animate>/<set> on shapes and text. Edit SKILLS / KNOWLEDGE
// below and re-run to change what the screen says. The animation plays once and holds.

import { writeFileSync, mkdirSync } from 'node:fs';
import { dirname } from 'node:path';

const OUT = 'assets/loading.svg';

// Self-assessed levels, 0-100. Be honest with these: the bars are the whole point.
const SKILLS = [
  ['PYTHON', 80],
  ['TYPESCRIPT / JS', 75],
  ['REACT / NEXT.JS', 70],
  ['FASTAPI', 65],
  ['SQL / POSTGIS', 60],
  ['SECURITY / OSINT', 60],
  ['GEOSPATIAL DATA', 55],
  ['JAPANESE', 15],
];
const KNOWLEDGE = 'osint · auth & crypto · networks · gis · calculus · physics';

// ---- look ---------------------------------------------------------------------------------
const W = 900;
const FONT = "'JetBrains Mono','Cascadia Mono','Fira Code',Consolas,Menlo,'Courier New',monospace";
const FS = 15, CW = 9;                  // font size, advance width per character (forced via textLength)
const RED = '#ff3b3b', DIM = '#7a1c1f', FAINT = '#3a0d10', BG = '#0a0305', FRAME = '#5c1418';
const X0 = 30;                          // left margin
const BAR_X = 300, BLOCK = 10, BLOCKS = 36;

const esc = (s) => s.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
const f = (n) => Number(n.toFixed(3));

let body = '';
let clipDefs = '';
let clipN = 0;

// text that "types" itself: a clip rect widens one character at a time, a cursor follows it
function typed(text, x, y, start, perChar = 0.03, fill = RED) {
  const n = text.length, dur = perChar * n, id = `c${clipN++}`;
  const widths = Array.from({ length: n + 1 }, (_, i) => i * CW).join(';');
  const xs = Array.from({ length: n + 1 }, (_, i) => x + i * CW).join(';');
  clipDefs += `<clipPath id="${id}"><rect x="${x}" y="${y - FS}" width="0" height="${FS + 6}">` +
    `<animate attributeName="width" values="${widths}" calcMode="discrete" dur="${f(dur)}s" begin="${f(start)}s" fill="freeze"/></rect></clipPath>`;
  body += `<text x="${x}" y="${y}" fill="${fill}" clip-path="url(#${id})" textLength="${n * CW}" lengthAdjust="spacingAndGlyphs">${esc(text)}</text>`;
  body += `<rect x="${x}" y="${y - FS + 2}" width="${CW - 1}" height="${FS}" fill="${RED}" opacity="0">` +
    `<set attributeName="opacity" to="0.9" begin="${f(start)}s"/>` +
    `<animate attributeName="x" values="${xs}" calcMode="discrete" dur="${f(dur)}s" begin="${f(start)}s" fill="freeze"/>` +
    `<set attributeName="opacity" to="0" begin="${f(start + dur + 0.05)}s"/></rect>`;
  return start + dur;
}

// something that simply appears at a given time
function appear(markup, at) {
  body += `<g opacity="0">${markup}<set attributeName="opacity" to="1" begin="${f(at)}s"/></g>`;
}
const label = (t, x, y, fill = RED, anchor = 'start') =>
  `<text x="${x}" y="${y}" fill="${fill}" text-anchor="${anchor}" textLength="${t.length * CW}" lengthAdjust="spacingAndGlyphs">${esc(t)}</text>`;

// ---- script of the boot sequence ------------------------------------------------------------
let y = 68, t = 0.5;
const LINE = 24;

let end = typed('arctica@github:~$ ./boot.sh --profile', X0, y, t);
appear(label('[ OK ]', W - X0 - 6 * CW, y, RED), end + 0.2);
y += LINE; t = end + 0.45;

end = typed('> mounting knowledge base', X0, y, t);
appear(label('[ OK ]', W - X0 - 6 * CW, y, RED), end + 0.2);
y += LINE; t = end + 0.45;

end = typed('> loading skill modules', X0, y, t);
y += LINE + 6; t = end + 0.4;

SKILLS.forEach(([name, pct], i) => {
  const at = t + i * 0.32;
  const blocks = Math.round((pct / 100) * BLOCKS);
  appear(label(name, X0 + 2 * CW, y, RED), at);
  body += `<g opacity="0"><set attributeName="opacity" to="1" begin="${f(at)}s"/>` +
    `<rect x="${BAR_X}" y="${y - 12}" width="${BLOCKS * BLOCK}" height="14" fill="url(#slotDim)"/>` +
    `<rect x="${BAR_X}" y="${y - 12}" width="0" height="14" fill="url(#slotOn)">` +
    `<animate attributeName="width" from="0" to="${blocks * BLOCK}" dur="${f(0.35 + blocks * 0.028)}s" begin="${f(at)}s" fill="freeze"/></rect></g>`;
  appear(label(`${String(pct).padStart(3, ' ')}%`, BAR_X + BLOCKS * BLOCK + 16, y, RED), at + 0.35 + blocks * 0.028);
  y += 22;
});
t = t + (SKILLS.length - 1) * 0.32 + 0.35 + BLOCKS * 0.028 + 0.3;

y += 12;
end = typed(`> indexing knowledge: ${KNOWLEDGE}`, X0, y, t, 0.02, '#ff7a7a');
y += LINE; t = end + 0.5;

end = typed('> all modules loaded. status: ONLINE', X0, y, t);
const curX = X0 + '> all modules loaded. status: ONLINE'.length * CW + 4;
body += `<rect x="${curX}" y="${y - FS + 2}" width="${CW - 1}" height="${FS}" fill="${RED}" opacity="0">` +
  `<animate attributeName="opacity" values="0.9;0" calcMode="discrete" dur="1.1s" begin="${f(end + 0.1)}s" repeatCount="indefinite"/></rect>`;

const H = y + 34;

// ---- assemble ---------------------------------------------------------------------------------
const svg = `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 ${W} ${H}" width="${W}" height="${H}" role="img" aria-labelledby="t d">
<title id="t">Arctica boot screen</title>
<desc id="d">A terminal-style loading screen listing skills: ${SKILLS.map(([n, p]) => `${n} ${p}%`).join(', ')}.</desc>
<defs>
  <pattern id="slotOn" x="${BAR_X}" y="0" width="${BLOCK}" height="14" patternUnits="userSpaceOnUse"><rect width="${BLOCK - 3}" height="14" fill="${RED}"/></pattern>
  <pattern id="slotDim" x="${BAR_X}" y="0" width="${BLOCK}" height="14" patternUnits="userSpaceOnUse"><rect width="${BLOCK - 3}" height="14" fill="${FAINT}"/></pattern>
  <pattern id="scan" width="4" height="4" patternUnits="userSpaceOnUse"><rect width="4" height="1" fill="#000" opacity="0.28"/></pattern>
  <radialGradient id="vig" cx="50%" cy="45%" r="75%"><stop offset="55%" stop-color="#000" stop-opacity="0"/><stop offset="100%" stop-color="#000" stop-opacity="0.55"/></radialGradient>
  <filter id="glow" x="-5%" y="-20%" width="110%" height="140%"><feGaussianBlur stdDeviation="1.6" result="b"/><feMerge><feMergeNode in="b"/><feMergeNode in="SourceGraphic"/></feMerge></filter>
  ${clipDefs}
</defs>
<rect width="${W}" height="${H}" rx="10" fill="${BG}"/>
<rect x="0.5" y="0.5" width="${W - 1}" height="${H - 1}" rx="10" fill="none" stroke="${FRAME}"/>
<g font-family="${FONT}" font-size="${FS}">
  <text x="${X0}" y="26" fill="${DIM}" textLength="${'arctica@github: ~  (console)'.length * CW}" lengthAdjust="spacingAndGlyphs" xml:space="preserve">arctica@github: ~  (console)</text>
  <circle cx="${W - 30}" cy="22" r="4" fill="${DIM}"/><circle cx="${W - 48}" cy="22" r="4" fill="${DIM}"/><circle cx="${W - 66}" cy="22" r="4" fill="${DIM}"/>
  <line x1="0" y1="38" x2="${W}" y2="38" stroke="${FRAME}"/>
  <g filter="url(#glow)">${body}</g>
</g>
<rect width="${W}" height="${H}" rx="10" fill="url(#scan)" pointer-events="none"/>
<rect width="${W}" height="${H}" rx="10" fill="url(#vig)" pointer-events="none"/>
</svg>
`;

mkdirSync(dirname(OUT), { recursive: true });
writeFileSync(OUT, svg);
console.log(`wrote ${OUT} (${(svg.length / 1024).toFixed(1)} KB, ${W}x${H}, plays for ~${f(t + 2)}s then holds)`);
