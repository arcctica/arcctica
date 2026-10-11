// Animated look inside the tiny GPT in llm/gpt.mjs. No dependencies.
//
//   node banner/llm.mjs        # writes assets/llm.svg
//
// Nothing here is mocked: each frame is a real forward pass of the trained model, which types
// out the sentence in llm/quote.txt one character at a time.
//   - the cells are the last 30 characters the model sees
//   - the arcs are its attention weights from the newest character back to earlier ones
//     (last layer, one colour per head; thicker and brighter = more attention)
//   - the bars are its probabilities for the next character; it takes the most likely one
//     (greedy decoding) and adds it to the text
// The model has seen the sentence many times in training, so it recites it from memory.
// The script checks that what the model typed is exactly the quote, and refuses to draw otherwise.

import { readFileSync, writeFileSync, mkdirSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { loadModel, run, softmax, encode } from '../llm/gpt.mjs';

const here = (p) => fileURLToPath(new URL(p, import.meta.url));
const OUT = 'assets/llm.svg';
const { P, vocab, cfg, meta } = loadModel(here('../llm/weights.json'));
const QUOTE = readFileSync(here('../llm/quote.txt'), 'utf8').trim();

const SHOW = 30;          // characters shown (and fed to the model) per frame
const FRAME = 0.26;       // seconds per frame
const HOLD = 14;          // extra frames to rest on the finished sentence before looping

// ---- run the model: it types the quote, one forward pass per character --------------------------
const frames = [];
let typed = '';
for (let i = 0; i <= QUOTE.length; i++) {
  const ctx = ('\n\n' + typed).slice(-SHOW);
  const ids = encode(vocab, ctx), T = ids.length, V = vocab.length;
  const { logits, attn } = run(P, ids, { cfg });
  const row = Array.from(logits.subarray((T - 1) * V, T * V));
  const probs = softmax(row, 1);
  const next = probs.indexOf(Math.max(...probs));
  const last = attn[cfg.L - 1], heads = [];
  for (let h = 0; h < cfg.H; h++) heads.push(Array.from(last.subarray((h * T + T - 1) * T, (h * T + T - 1) * T + T)));
  frames.push({ ctx, probs, next, heads });
  if (i < QUOTE.length) typed += vocab[next];
}
if (typed !== QUOTE) {
  console.error(`the model does not recite the quote exactly:\n  model: ${JSON.stringify(typed)}\n  quote: ${JSON.stringify(QUOTE)}\nrun: node llm/gpt.mjs train 8000`);
  process.exit(1);
}
console.log(`the model recites the quote exactly (${QUOTE.length} characters)`);

// ---- look ---------------------------------------------------------------------------------------
const W = 900, H = 496;
const FONT = "'JetBrains Mono','Cascadia Mono','Fira Code',Consolas,Menlo,'Courier New',monospace";
const FS = 15, CW = 9;
const RED = '#ff3b3b', LIGHT = '#ff9a9a', DIM = '#7a1c1f', FAINT = '#3a0d10', BG = '#0a0305', FRAME_C = '#5c1418';
const HEAD_COLORS = ['#ff3b3b', '#ff9a9a', '#ffe3e3', '#ff7a3d'];
const f = (n) => Number(n.toFixed(2));
const esc = (s) => String(s).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
const shown = (ch) => (ch === ' ' ? '·' : ch === '\n' ? '↵' : ch);
const text = (t, x, y, { fill = RED, anchor = 'start', size = FS } = {}) =>
  `<text x="${x}" y="${y}" fill="${fill}" font-size="${size}" text-anchor="${anchor}" textLength="${f(t.length * CW * (size / FS))}" lengthAdjust="spacingAndGlyphs" xml:space="preserve">${esc(t)}</text>`;

const CELL = 27, X0 = (W - CELL * SHOW) / 2, ROW_Y = 178, ROW_H = 28;
const cx = (i) => X0 + i * CELL + CELL / 2;
const BAR_X = 84, BAR_MAX = 380, BARS_Y = 270, BAR_STEP = 25;

// ---- one frame: attention arcs, cells, candidates ----------------------------------------------------
function frameMarkup(fr) {
  const T = fr.ctx.length, off = SHOW - T;   // right-align the context
  let g = '';
  const xl = cx(SHOW - 1);
  fr.heads.forEach((w, h) => {
    for (let s = 0; s < T - 1; s++) {
      if (w[s] < 0.09) continue;
      const xs = cx(off + s), d = xl - xs, top = ROW_Y - Math.min(96, 26 + d * 0.55 + h * 6);
      g += `<path d="M${f(xs)} ${ROW_Y}Q${f((xs + xl) / 2)} ${f(top)} ${f(xl)} ${ROW_Y}" fill="none" stroke="${HEAD_COLORS[h]}" stroke-width="${f(0.8 + 3.4 * w[s])}" opacity="${f(0.25 + 0.7 * Math.min(1, w[s] * 1.4))}" stroke-linecap="round"/>`;
    }
  });
  // the characters in the cells (one <text> with a position per glyph)
  const xs = [...fr.ctx].map((_, i) => f(X0 + (off + i) * CELL + CELL / 2 - CW / 2)).join(' ');
  g += `<text x="${xs}" y="${ROW_Y + 19}" fill="${LIGHT}" xml:space="preserve">${esc([...fr.ctx].map(shown).join(''))}</text>`;
  g += `<rect x="${X0 + (SHOW - 1) * CELL}" y="${ROW_Y}" width="${CELL}" height="${ROW_H}" fill="none" stroke="${RED}" stroke-width="1.6"/>`;

  // five likeliest next characters; the first one is what it picks
  const top = fr.probs.map((p, i) => [p, i]).sort((a, b) => b[0] - a[0]).slice(0, 5);
  top.forEach(([p, i], k) => {
    const y = BARS_Y + k * BAR_STEP, chosen = i === fr.next;
    g += `<rect x="46" y="${y - 16}" width="24" height="22" fill="${chosen ? '#3a0d10' : BG}" stroke="${chosen ? RED : FAINT}"/>`;
    g += `<text x="52" y="${y}" fill="${chosen ? '#ffe3e3' : LIGHT}">${esc(shown(vocab[i]))}</text>`;
    g += `<rect x="${BAR_X}" y="${y - 14}" width="${f(Math.max(2, p * BAR_MAX))}" height="18" fill="${chosen ? RED : DIM}"/>`;
    g += `<text x="${f(BAR_X + Math.max(2, p * BAR_MAX) + 8)}" y="${y}" fill="${chosen ? '#ffe3e3' : LIGHT}" font-size="13">${p >= 0.1 ? Math.round(p * 100) : (p * 100).toFixed(1)}%${chosen ? '  ◂ picks this' : ''}</text>`;
  });
  return g;
}

// ---- the sentence as it is typed (one line per sentence) -----------------------------------------------
const SENTENCES = QUOTE.split(/(?<=[.?!]) /);
function typedLines(n) { // the first n characters of the quote, laid out over the lines
  const out = [];
  let rest = n;
  for (const s of SENTENCES) {
    out.push(s.slice(0, Math.min(rest, s.length)));
    rest -= s.length + 1;                      // the space after a sentence
    if (rest <= 0) break;
  }
  return out;
}
function lineMarkup(n) {
  const lines = typedLines(n);
  let g = '';
  lines.forEach((l, k) => { g += text((k === 0 ? '> ' : '  ') + l, 30, 420 + k * 22, { fill: '#ffd0d0' }); });
  const k = lines.length - 1;
  g += `<rect x="${30 + (2 + lines[k].length) * CW + 2}" y="${420 + k * 22 - FS + 2}" width="${CW - 1}" height="${FS}" fill="${RED}" opacity="0.9"/>`;
  return g;
}

const N = frames.length, U = N + HOLD, total = FRAME * U;
const frameGroups = frames.map((fr, i) => {
  const a = f(i / U), b = i === N - 1 ? 1 : f((i + 1) / U);   // the last frame rests until the loop restarts
  return `<g opacity="0"><animate attributeName="opacity" values="0;1;0;0" keyTimes="0;${a};${b};1" calcMode="discrete" dur="${f(total)}s" repeatCount="indefinite"/>${frameMarkup(fr)}${lineMarkup(i)}</g>`;
}).join('\n');

// ---- static parts ----------------------------------------------------------------------------------
let stat = '';
stat += text('context: the last 30 characters the model sees', 46, 62, { fill: DIM, size: 13 });
for (let i = 0; i < SHOW; i++) stat += `<rect x="${X0 + i * CELL}" y="${ROW_Y}" width="${CELL}" height="${ROW_H}" fill="#12060a" stroke="${FAINT}"/>`;
HEAD_COLORS.forEach((c, h) => { stat += `<line x1="${W - 250 + h * 52}" y1="58" x2="${W - 232 + h * 52}" y2="58" stroke="${c}" stroke-width="3" stroke-linecap="round"/>${text(`h${h}`, W - 228 + h * 52, 62, { fill: DIM, size: 12 })}`; });
stat += text('attention weights, last layer', W - 250, 80, { fill: DIM, size: 12 });
stat += text(`next character: the likeliest of ${vocab.length} (it always takes the top one)`, 46, 240, { fill: DIM, size: 13 });

const rows = [['layers', cfg.L], ['heads', cfg.H], ['width', cfg.C], ['context', cfg.B], ['parameters', meta.params.toLocaleString('en-US')], ['val loss', meta.valLoss.toFixed(3)], ['random', meta.randomLoss.toFixed(3)], ['quote seen', `${meta.quoteRepeats}x`]];
stat += text('model', 650, 246, { fill: DIM, size: 13 });
rows.forEach(([k, v], i) => { stat += text(k, 650, 266 + i * 17, { fill: DIM, size: 13 }) + text(String(v), 850, 266 + i * 17, { fill: LIGHT, size: 13, anchor: 'end' }); });
stat += `<line x1="30" y1="396" x2="${W - 30}" y2="396" stroke="${FRAME_C}"/><line x1="30" y1="458" x2="${W - 30}" y2="458" stroke="${FRAME_C}"/>`;
stat += text(`plain JavaScript · backprop by hand · ${meta.params.toLocaleString('en-US')} parameters · seen this sentence ${meta.quoteRepeats}x, so it recites it by heart`, 30, 480, { fill: DIM, size: 12 });

const svg = `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 ${W} ${H}" width="${W}" height="${H}" role="img" aria-labelledby="t d">
<title id="t">A tiny language model typing a quote</title>
<desc id="d">Animation of a small GPT written from scratch typing a sentence one character at a time. Each frame shows the 30 characters it reads, its attention weights from the newest character back to earlier ones, and its five likeliest next characters. The sentence: ${esc(QUOTE)}</desc>
<defs>
  <pattern id="scan" width="4" height="4" patternUnits="userSpaceOnUse"><rect width="4" height="1" fill="#000" opacity="0.28"/></pattern>
  <radialGradient id="vig" cx="50%" cy="45%" r="75%"><stop offset="55%" stop-color="#000" stop-opacity="0"/><stop offset="100%" stop-color="#000" stop-opacity="0.55"/></radialGradient>
  <filter id="glow" x="-5%" y="-10%" width="110%" height="120%"><feGaussianBlur stdDeviation="1.4" result="b"/><feMerge><feMergeNode in="b"/><feMergeNode in="SourceGraphic"/></feMerge></filter>
</defs>
<rect width="${W}" height="${H}" rx="10" fill="${BG}"/>
<rect x="0.5" y="0.5" width="${W - 1}" height="${H - 1}" rx="10" fill="none" stroke="${FRAME_C}"/>
<g font-family="${FONT}" font-size="${FS}">
  ${text('arctica-lm  //  a tiny GPT, written from scratch', 30, 26, { fill: DIM })}
  <circle cx="${W - 30}" cy="22" r="4" fill="${DIM}"/><circle cx="${W - 48}" cy="22" r="4" fill="${DIM}"/><circle cx="${W - 66}" cy="22" r="4" fill="${DIM}"/>
  <line x1="0" y1="38" x2="${W}" y2="38" stroke="${FRAME_C}"/>
  <g filter="url(#glow)">${stat}
${frameGroups}
  </g>
</g>
<rect width="${W}" height="${H}" rx="10" fill="url(#scan)" pointer-events="none"/>
<rect width="${W}" height="${H}" rx="10" fill="url(#vig)" pointer-events="none"/>
</svg>
`;

mkdirSync('assets', { recursive: true });
writeFileSync(OUT, svg);
console.log(`wrote ${OUT} (${(svg.length / 1024).toFixed(0)} KB, ${N} frames, loops every ${f(total)}s)`);
