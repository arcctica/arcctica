// A one-picture "model card" for the tiny GPT in llm/gpt.mjs. No dependencies.
//
//   node banner/model.mjs        # writes assets/model.svg
//
// Everything drawn comes from the trained model, nothing is made up:
//   - architecture and parameter counts: counted from the model's own shapes
//   - attention: the real attention weights of the last layer, 4 heads, on a piece of the quote
//   - training: the real loss curve recorded by `node llm/gpt.mjs train`
//   - the two strips: for every character, the probability the model gave to the RIGHT next
//     character, once on the sentence it has seen 75 times (the quote) and once on a sentence
//     from the held-out text that it never trained on. That gap is memorising vs. understanding.

import { readFileSync, readdirSync, writeFileSync, mkdirSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { loadModel, run, softmax, encode, shapes } from '../llm/gpt.mjs';

const here = (p) => fileURLToPath(new URL(p, import.meta.url));
const { P, vocab, cfg, meta } = loadModel(here('../llm/weights.json'));
const QUOTE = readFileSync(here('../llm/quote.txt'), 'utf8').trim();
const V = vocab.length;

// ---- real numbers ------------------------------------------------------------------------------
// probability given to each correct next character, reading `text` with the same context the model uses
function confidence(text) {
  const out = [];
  for (let i = 0; i < text.length; i++) {
    const ids = encode(vocab, ('\n\n' + text.slice(0, i)).slice(-cfg.B)), T = ids.length;
    const { logits } = run(P, ids, { cfg });
    const probs = softmax(Array.from(logits.subarray((T - 1) * V, T * V)), 1);
    out.push(probs[vocab.indexOf(text[i])]);
  }
  return out;
}
// the held-out text: the same split as training (every 9th paragraph is never trained on)
const dir = here('../llm/corpus');
const paras = readdirSync(dir).filter((f) => f.endsWith('.txt')).sort().map((f) => readFileSync(`${dir}/${f}`, 'utf8')).join('\n\n')
  .split(/\n\s*\n/).map((p) => p.trim()).filter(Boolean);
const heldOut = paras.filter((_, i) => i % 9 === 4).find((p) => p.length >= QUOTE.length).slice(0, QUOTE.length);
const pQuote = confidence(QUOTE), pHeld = confidence(heldOut);
const mean = (a) => a.reduce((x, y) => x + y, 0) / a.length;

// attention of the last layer over a piece of the quote
const WIN = 28, piece = QUOTE.slice(56, 56 + WIN);
const att = run(P, encode(vocab, piece), { cfg }).attn[cfg.L - 1];

// parameter counts straight from the shapes
const count = (names) => names.reduce((a, n) => a + shapes(V, cfg)[n].reduce((x, y) => x * y, 1), 0);
const nEmb = count(['wte', 'wpe']), nLayer = count(['wq0', 'wk0', 'wv0', 'wo0']), nMlp = count(['w10', 'w20']), nHead = count(['lm']);
const fmt = (n) => n.toLocaleString('en-US');

// ---- look ---------------------------------------------------------------------------------------
const W = 900, H = 588;
const FONT = "'JetBrains Mono','Cascadia Mono','Fira Code',Consolas,Menlo,'Courier New',monospace";
const BG = '#0d0d0d', PANEL = '#111111', FG = '#f0f0f0', TEXT = '#cfcfcf', MID = '#a8a8a8', DIM = '#6f6f6f', FAINT = '#2c2c2c', LINE = '#30363d';
const CWF = 0.6;
const f = (n) => Number(n.toFixed(2));
const esc = (s) => String(s).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
const text = (t, x, y, { size = 11, fill = TEXT, anchor = 'start' } = {}) =>
  `<text x="${f(x)}" y="${f(y)}" font-size="${size}" fill="${fill}" text-anchor="${anchor}" textLength="${f(t.length * size * CWF)}" lengthAdjust="spacingAndGlyphs" xml:space="preserve">${esc(t)}</text>`;
const panel = (x, y, w, h, title) => `<rect x="${x}" y="${y}" width="${w}" height="${h}" rx="8" fill="${PANEL}" stroke="${FAINT}"/>` + text(title, x + 14, y + 20, { size: 11, fill: DIM });
const grow = (dur, at = 0.2) => `<animate attributeName="width" from="0" to="__W__" dur="${dur}s" begin="${at}s" fill="freeze"/>`;

let defs = '', body = '';
let clipN = 0;
const clipReveal = (x, y, w, h, dur, at, inner) => { // reveal `inner` left to right
  const id = `r${clipN++}`;
  defs += `<clipPath id="${id}"><rect x="${x}" y="${y}" width="0" height="${h}"><animate attributeName="width" from="0" to="${w}" dur="${dur}s" begin="${at}s" fill="freeze"/></rect></clipPath>`;
  return `<g clip-path="url(#${id})">${inner}</g>`;
};

// ---- panel A: architecture ------------------------------------------------------------------------
{
  const x = 24, y = 56, w = 262, h = 304, cx = x + w / 2;
  body += panel(x, y, w, h, 'architecture');
  const box = (by, bh, label, sub, dashed = false) =>
    `<rect x="${x + 22}" y="${by}" width="${w - 44}" height="${bh}" rx="5" fill="none" stroke="${MID}" stroke-width="1.2"${dashed ? ' stroke-dasharray="3 3"' : ''}/>` +
    text(label, cx, by + 16, { size: 11, fill: FG, anchor: 'middle' }) + (sub ? text(sub, cx, by + 30, { size: 9, fill: DIM, anchor: 'middle' }) : '');
  const arrow = (ay) => `<path d="M${cx} ${ay}v10m-3.5-4l3.5 4 3.5-4" fill="none" stroke="${DIM}" stroke-width="1.2"/>`;
  let s = '';
  s += text('text  ->  36 characters', cx, y + 44, { size: 10, fill: MID, anchor: 'middle' }) + arrow(y + 50);
  s += box(y + 62, 38, 'embeddings', `token + position  (${fmt(nEmb)})`) + arrow(y + 100);
  // the repeated block
  s += `<rect x="${x + 14}" y="${y + 112}" width="${w - 28}" height="104" rx="6" fill="none" stroke="${FAINT}" stroke-dasharray="2 3"/>` + text(`x${cfg.L} layers`, x + w - 22, y + 127, { size: 10, fill: DIM, anchor: 'end' });
  s += box(y + 134, 32, `attention · ${cfg.H} heads`, `(${fmt(nLayer)} per layer)`);
  s += box(y + 174, 32, 'MLP · ReLU', `(${fmt(nMlp)} per layer)`);
  s += `<path d="M${x + 22} ${y + 150}h-12v40h12" fill="none" stroke="${DIM}"/>`; // residual connection
  s += arrow(y + 216);
  s += box(y + 228, 38, 'next-character scores', `linear  (${fmt(nHead)})`);
  s += text(`${fmt(meta.params)} parameters in total`, cx, y + 288, { size: 10, fill: MID, anchor: 'middle' });
  body += clipReveal(x, y, w, h, 1.2, 0.2, s);
}

// ---- panel B: attention maps ---------------------------------------------------------------------
{
  const x = 300, y = 56, w = 290, h = 304, CELL = 3.8, side = WIN * CELL, gx = (w - 2 * side - 14) / 2;
  body += panel(x, y, w, h, 'attention · last layer, 4 heads');
  let s = '';
  for (let hd = 0; hd < cfg.H; hd++) {
    const ox = x + gx + (hd % 2) * (side + 14), oy = y + 34 + Math.floor(hd / 2) * (side + 24);
    s += `<rect x="${f(ox)}" y="${f(oy)}" width="${side}" height="${side}" fill="#0a0a0a" stroke="${FAINT}"/>`;
    for (let q = 0; q < WIN; q++) for (let k = 0; k <= q; k++) {
      const wgt = att[(hd * WIN + q) * WIN + k];
      if (wgt < 0.03) continue;
      s += `<rect x="${f(ox + k * CELL)}" y="${f(oy + q * CELL)}" width="${CELL - 0.3}" height="${CELL - 0.3}" fill="#fff" opacity="${f(Math.min(1, Math.pow(wgt, 0.6) * 1.15))}"/>`;
    }
    s += text(`head ${hd}`, ox, oy + side + 12, { size: 9, fill: DIM });
  }
  s += text('rows: the character reading, columns: where it looks', x + 14, y + h - 10, { size: 9, fill: DIM });
  body += `<g opacity="0">${s}<animate attributeName="opacity" from="0" to="1" dur="0.8s" begin="0.8s" fill="freeze"/></g>`;
}

// ---- panel C: training curve ------------------------------------------------------------------------
{
  const x = 604, y = 56, w = 272, h = 304, px = x + 40, py = y + 44, pw = w - 58, ph = 170, YMAX = 3.8;
  body += panel(x, y, w, h, 'training · loss (lower is better)');
  const hist = meta.history, steps = meta.steps;
  const X = (st) => px + (st / steps) * pw, Y = (l) => py + ph - (Math.min(l, YMAX) / YMAX) * ph;
  let s = '';
  for (const t of [0, 1, 2, 3]) s += `<line x1="${px}" y1="${f(Y(t))}" x2="${px + pw}" y2="${f(Y(t))}" stroke="${FAINT}"/>` + text(String(t), px - 8, Y(t) + 3, { size: 9, fill: DIM, anchor: 'end' });
  for (const st of [0, 4000, 8000]) s += text(st === 0 ? '0' : `${st / 1000}k`, X(st), py + ph + 14, { size: 9, fill: DIM, anchor: 'middle' });
  s += text('step', px + pw, py + ph + 26, { size: 9, fill: DIM, anchor: 'end' });
  s += `<line x1="${px}" y1="${f(Y(meta.randomLoss))}" x2="${px + pw}" y2="${f(Y(meta.randomLoss))}" stroke="${DIM}" stroke-dasharray="1 3"/>` + text(`random guess ${meta.randomLoss.toFixed(2)}`, px + pw, Y(meta.randomLoss) - 5, { size: 9, fill: DIM, anchor: 'end' });
  const line = (i) => hist.map((p, k) => `${k ? 'L' : 'M'}${f(X(p[0]))} ${f(Y(p[i]))}`).join('');
  s += `<path d="${line(1)}" fill="none" stroke="${MID}" stroke-width="1.4"/>`;
  s += `<path d="${line(2)}" fill="none" stroke="${FG}" stroke-width="1.8"/>`;
  s += `<line x1="${f(X(meta.bestStep))}" y1="${py}" x2="${f(X(meta.bestStep))}" y2="${py + ph}" stroke="${MID}" stroke-dasharray="3 3"/>` + text('kept', X(meta.bestStep) - 3, py + 28, { size: 9, fill: MID, anchor: 'end' });
  const ly = y + h - 38;
  s += `<line x1="${x + 14}" y1="${ly}" x2="${x + 32}" y2="${ly}" stroke="${FG}" stroke-width="1.8"/>` + text(`held-out text ${meta.valLoss.toFixed(2)}`, x + 38, ly + 3, { size: 9, fill: TEXT });
  s += `<line x1="${x + 14}" y1="${ly + 16}" x2="${x + 32}" y2="${ly + 16}" stroke="${MID}" stroke-width="1.4"/>` + text('training text (memorising)', x + 38, ly + 19, { size: 9, fill: DIM });
  body += clipReveal(x, y, w, h, 1.6, 1.0, s);
}

// ---- the two strips ----------------------------------------------------------------------------------
{
  const x = 24, y = 376, w = 852, h = 192;
  body += panel(x, y, w, h, 'how sure was the model of the RIGHT next character?   (bar height = its probability, 100% = full height)');
  const CW6 = 6.6, SX = x + (w - QUOTE.length * CW6) / 2;
  const strip = (label, p, sub, y0, at) => {
    let s = text(label, SX, y0 - 4, { size: 10, fill: FG });
    s += text(sub, SX + QUOTE.length * CW6, y0 - 4, { size: 10, fill: MID, anchor: 'end' });
    p.forEach((v, i) => { const bh = Math.max(1, v * 34); s += `<rect x="${f(SX + i * CW6 + 0.7)}" y="${f(y0 + 36 - bh)}" width="4.9" height="${f(bh)}" fill="${v > 0.5 ? FG : v > 0.2 ? MID : '#555'}"/>`; });
    const chars = [...(label === 'the quote' ? QUOTE : heldOut)].map((c) => (c === '\n' ? ' ' : c)).join('');
    s += text(chars, SX, y0 + 49, { size: 11, fill: DIM });
    return clipReveal(x, y0 - 16, w, 70, 2.2, at, s);
  };
  body += strip('the quote', pQuote, `seen ${meta.quoteRepeats}x in training · average ${Math.round(mean(pQuote) * 100)}%`, y + 50, 1.4);
  body += strip('a sentence it never trained on', pHeld, `average ${Math.round(mean(pHeld) * 100)}%`, y + 124, 2.0);
}

// ---- assemble ----------------------------------------------------------------------------------------
const desc = `A tiny GPT written from scratch: ${fmt(meta.params)} parameters, ${cfg.L} layers, ${cfg.H} attention heads, trained on about 32 KB of text. Held-out loss ${meta.valLoss.toFixed(2)} against ${meta.randomLoss.toFixed(2)} for random guessing. On the quote it has seen ${meta.quoteRepeats} times it gives the right next character an average probability of ${Math.round(mean(pQuote) * 100)}%, on a sentence it never trained on ${Math.round(mean(pHeld) * 100)}%.`;
const svg = `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 ${W} ${H}" width="${W}" height="${H}" role="img" aria-labelledby="t d">
<title id="t">arctica-lm: a tiny GPT written from scratch</title>
<desc id="d">${esc(desc)}</desc>
<defs>${defs}</defs>
<rect x="0.5" y="0.5" width="${W - 1}" height="${H - 1}" rx="10" fill="${BG}" stroke="${LINE}"/>
<circle cx="22" cy="17" r="4.5" fill="#3a3a3a"/><circle cx="38" cy="17" r="4.5" fill="#3a3a3a"/><circle cx="54" cy="17" r="4.5" fill="#3a3a3a"/>
<g font-family="${FONT}">
  ${text(`arctica-lm  //  a tiny GPT, written from scratch  //  ${fmt(meta.params)} parameters`, 74, 21, { size: 11, fill: DIM })}
  <line x1="0" y1="34" x2="${W}" y2="34" stroke="${LINE}"/>
  ${body}
</g>
</svg>
`;
mkdirSync('assets', { recursive: true });
writeFileSync('assets/model.svg', svg);
console.log(`wrote assets/model.svg (${(svg.length / 1024).toFixed(0)} KB). quote ${Math.round(mean(pQuote) * 100)}% vs held-out ${Math.round(mean(pHeld) * 100)}%`);
