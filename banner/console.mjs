// A small black-and-white console that lists my technical skills. No dependencies.
//
//   node banner/console.mjs        # writes assets/console.svg
//
// Edit SKILLS below and run it again. The animation plays once and then rests on the finished
// screen with a blinking cursor. GitHub shows SVGs in <img> without scripts or web fonts, so the
// typing is plain SMIL: a clip rectangle widens one character at a time.

import { writeFileSync, mkdirSync } from 'node:fs';
import { dirname } from 'node:path';

const OUT = 'assets/console.svg';

const SKILLS = [
  ['languages', ['Python', 'TypeScript', 'JavaScript', 'SQL', 'HTML/CSS']],
  ['web', ['React', 'Next.js', 'Node.js', 'FastAPI', 'Tailwind CSS', 'Prisma', 'MapLibre']],
  ['data', ['PostgreSQL/PostGIS', 'SQLite', 'Google Earth Engine']],
  ['security', ['OSINT', 'WHOIS/DNS', 'certificate transparency', 'auth (scrypt, TOTP, CSRF)']],
  ['tools', ['Git', 'Figma', 'Unity', 'Unreal Engine', 'Arduino', 'micro:bit', 'SketchUp']],
];

// ---- look ---------------------------------------------------------------------------------------
const W = 680, PAD = 22, LH = 20, TOP = 34;
const FONT = "'JetBrains Mono','Cascadia Mono','Fira Code',Consolas,Menlo,'Courier New',monospace";
const FS = 13, CW = 7.8;                       // font size; advance per character (forced with textLength)
const BG = '#0d0d0d', FG = '#e8e8e8', DIM = '#8a8a8a', LINE = '#30363d';
const LABEL_W = 12;                            // characters reserved for the skill group name
const MAX_CHARS = Math.floor((W - 2 * PAD) / CW);

const f = (n) => Number(n.toFixed(2));
const esc = (s) => s.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');

function wrap(items, width) { // "a, b, c" broken into lines of at most `width` characters, commas kept at line ends
  const lines = [];
  let line = '';
  items.forEach((it, i) => {
    const tok = it + (i < items.length - 1 ? ',' : '');
    if (line && (line + ' ' + tok).length > width) { lines.push(line); line = tok; } else line = line ? line + ' ' + tok : tok;
  });
  if (line) lines.push(line);
  return lines;
}

// ---- the script of the screen ----------------------------------------------------------------------
const PROMPT = 'arctica@github:~$';
const rows = [{ label: PROMPT, value: 'cat skills.txt', labelW: PROMPT.length + 1, speed: 0.045 }];
for (const [name, items] of SKILLS)
  wrap(items, MAX_CHARS - LABEL_W).forEach((l, i) => rows.push({ label: i === 0 ? name : '', value: l, labelW: LABEL_W, speed: 0.012 }));
rows.push({ label: PROMPT, value: '', labelW: PROMPT.length + 1, speed: 0.03, last: true });

const H = TOP + 20 + rows.length * LH + 18;
let defs = '', body = '';
let t = 0.5, y = TOP + 28;
rows.forEach((r, k) => {
  const n = r.labelW + r.value.length, dur = r.speed * Math.max(1, n), id = `c${k}`;
  const widths = Array.from({ length: n + 1 }, (_, i) => f(i * CW)).join(';');
  const xs = Array.from({ length: n + 1 }, (_, i) => f(PAD + i * CW)).join(';');
  defs += `<clipPath id="${id}"><rect x="${PAD}" y="${y - FS}" width="0" height="${FS + 6}"><animate attributeName="width" values="${widths}" calcMode="discrete" dur="${f(dur)}s" begin="${f(t)}s" fill="freeze"/></rect></clipPath>`;
  if (r.label) body += `<text x="${PAD}" y="${y}" fill="${DIM}" clip-path="url(#${id})" textLength="${f(r.label.length * CW)}" lengthAdjust="spacingAndGlyphs">${esc(r.label)}</text>`;
  if (r.value) body += `<text x="${f(PAD + r.labelW * CW)}" y="${y}" fill="${FG}" clip-path="url(#${id})" textLength="${f(r.value.length * CW)}" lengthAdjust="spacingAndGlyphs" xml:space="preserve">${esc(r.value)}</text>`;
  if (r.last) { // the final prompt keeps a blinking cursor
    body += `<rect x="${f(PAD + r.labelW * CW)}" y="${y - FS + 2}" width="${CW - 1}" height="${FS}" fill="${FG}" opacity="0"><animate attributeName="opacity" values="0.9;0" calcMode="discrete" dur="1.1s" begin="${f(t + dur)}s" repeatCount="indefinite"/></rect>`;
  } else { // a cursor that follows the typing and then disappears
    body += `<rect x="${PAD}" y="${y - FS + 2}" width="${CW - 1}" height="${FS}" fill="${FG}" opacity="0"><set attributeName="opacity" to="0.9" begin="${f(t)}s"/><animate attributeName="x" values="${xs}" calcMode="discrete" dur="${f(dur)}s" begin="${f(t)}s" fill="freeze"/><set attributeName="opacity" to="0" begin="${f(t + dur + 0.04)}s"/></rect>`;
  }
  t += dur + (k === 0 ? 0.35 : 0.06);
  y += LH;
});

const desc = SKILLS.map(([n, it]) => `${n}: ${it.join(', ')}`).join('. ');
const svg = `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 ${W} ${H}" width="${W}" height="${H}" role="img" aria-labelledby="t d">
<title id="t">Console listing my skills</title>
<desc id="d">${esc(desc)}.</desc>
<defs>${defs}</defs>
<rect x="0.5" y="0.5" width="${W - 1}" height="${H - 1}" rx="8" fill="${BG}" stroke="${LINE}"/>
<circle cx="${PAD}" cy="17" r="4.5" fill="#3a3a3a"/><circle cx="${PAD + 16}" cy="17" r="4.5" fill="#3a3a3a"/><circle cx="${PAD + 32}" cy="17" r="4.5" fill="#3a3a3a"/>
<line x1="0" y1="${TOP - 2}" x2="${W}" y2="${TOP - 2}" stroke="${LINE}"/>
<g font-family="${FONT}" font-size="${FS}">${body}</g>
</svg>
`;

mkdirSync(dirname(OUT), { recursive: true });
writeFileSync(OUT, svg);
console.log(`wrote ${OUT} (${(svg.length / 1024).toFixed(1)} KB, ${W}x${H}, ${rows.length} lines, ~${f(t)}s)`);
