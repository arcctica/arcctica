// A tiny GPT, from scratch, in plain JavaScript. No libraries.
//
//   node llm/gpt.mjs check              # compare hand-written gradients with numerical ones
//   node llm/gpt.mjs train              # train on llm/corpus/*.txt, write llm/weights.json
//   node llm/gpt.mjs sample "the black" # generate text from the saved weights
//   node llm/gpt.mjs recite             # does it know llm/quote.txt by heart?
//
// What's inside (read top to bottom, it's meant to be read):
//   tokens   -> character ids
//   embed    -> token vector + position vector
//   N times: RMSNorm -> causal multi-head self-attention -> residual
//            RMSNorm -> MLP (ReLU)                        -> residual
//   RMSNorm -> linear -> a score (logit) for every possible next character
//   loss     -> cross-entropy: how surprised was the model by the real next character?
//
// The backward pass (backpropagation) is written out by hand, layer by layer, and checked
// against finite differences by `check`. Training uses Adam with a cosine learning-rate decay.

import { readFileSync, writeFileSync, readdirSync } from 'node:fs';
import { fileURLToPath, pathToFileURL } from 'node:url';

export const CFG = { C: 32, H: 4, L: 2, B: 40 }; // width, heads, layers, context length

// ---------- small utilities --------------------------------------------------------------------
export function rng(seed) { // mulberry32: tiny, seedable, good enough
  let a = seed >>> 0;
  return () => {
    a = (a + 0x6d2b79f5) >>> 0;
    let t = a;
    t = Math.imul(t ^ (t >>> 15), t | 1);
    t ^= t + Math.imul(t ^ (t >>> 7), t | 61);
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
}
const randn = (r) => Math.sqrt(-2 * Math.log(1 - r())) * Math.cos(2 * Math.PI * r());

// ---------- parameters ----------------------------------------------------------------------------
export function shapes(V, cfg = CFG) {
  const { C, L, B } = cfg;
  const s = { wte: [V, C], wpe: [B, C], lm: [C, V] };
  for (let l = 0; l < L; l++) {
    for (const n of ['wq', 'wk', 'wv', 'wo']) s[`${n}${l}`] = [C, C];
    s[`w1${l}`] = [C, 4 * C];
    s[`w2${l}`] = [4 * C, C];
  }
  return s;
}
const size = ([r, c]) => r * c;
export const countParams = (V, cfg = CFG) => Object.values(shapes(V, cfg)).reduce((a, s) => a + size(s), 0);

export function initParams(V, cfg = CFG, seed = 1) {
  const r = rng(seed), { C, L } = cfg, P = {};
  const resid = 1 / Math.sqrt(2 * L); // keep the residual stream from growing with depth
  for (const [name, shape] of Object.entries(shapes(V, cfg))) {
    const a = new Float64Array(size(shape));
    let std = 1 / Math.sqrt(shape[0]);
    if (name === 'wte') std = 0.5;
    else if (name === 'wpe') std = 0.2;
    else if (name === 'lm') std = 0.5 / Math.sqrt(C);
    else if (name.startsWith('wo') || name.startsWith('w2')) std *= resid;
    for (let i = 0; i < a.length; i++) a[i] = randn(r) * std;
    P[name] = a;
  }
  return P;
}
const zerosLike = (P) => Object.fromEntries(Object.entries(P).map(([k, v]) => [k, new Float64Array(v.length)]));

// ---------- tensor ops (row-major, T rows) ---------------------------------------------------------
function matmul(A, W, T, K, N) { // [T,K] x [K,N] -> [T,N]
  const out = new Float64Array(T * N);
  for (let t = 0; t < T; t++)
    for (let k = 0; k < K; k++) {
      const a = A[t * K + k];
      if (a === 0) continue;
      for (let n = 0; n < N; n++) out[t * N + n] += a * W[k * N + n];
    }
  return out;
}
// given dOut, accumulate dW and return dA
function matmulBack(A, W, dOut, dW, T, K, N) {
  const dA = new Float64Array(T * K);
  for (let t = 0; t < T; t++)
    for (let k = 0; k < K; k++) {
      const a = A[t * K + k];
      let s = 0;
      for (let n = 0; n < N; n++) {
        const g = dOut[t * N + n];
        dW[k * N + n] += a * g;
        s += g * W[k * N + n];
      }
      dA[t * K + k] = s;
    }
  return dA;
}
function rms(x, T, C) { // y = x / sqrt(mean(x^2) + eps), per row
  const y = new Float64Array(T * C), r = new Float64Array(T);
  for (let t = 0; t < T; t++) {
    let s = 0;
    for (let c = 0; c < C; c++) s += x[t * C + c] ** 2;
    const rr = 1 / Math.sqrt(s / C + 1e-5);
    r[t] = rr;
    for (let c = 0; c < C; c++) y[t * C + c] = x[t * C + c] * rr;
  }
  return { y, r };
}
function rmsBack(x, r, dy, T, C) { // dx_j = r*dy_j - x_j * r^3/C * sum_i(dy_i x_i)
  const dx = new Float64Array(T * C);
  for (let t = 0; t < T; t++) {
    let dot = 0;
    for (let c = 0; c < C; c++) dot += dy[t * C + c] * x[t * C + c];
    const k = (r[t] ** 3 * dot) / C;
    for (let c = 0; c < C; c++) dx[t * C + c] = r[t] * dy[t * C + c] - x[t * C + c] * k;
  }
  return dx;
}
const add = (a, b) => { const o = new Float64Array(a.length); for (let i = 0; i < a.length; i++) o[i] = a[i] + b[i]; return o; };

// ---------- the model -----------------------------------------------------------------------------
// run(): forward pass. With `targets` it also returns the loss; with `G` it also fills gradients.
export function run(P, tokens, { cfg = CFG, targets = null, G = null, lossScale = 1 } = {}) {
  const { C, H, L } = cfg, D = C / H, T = tokens.length, V = P.lm.length / C, scale = 1 / Math.sqrt(D);

  let h = new Float64Array(T * C);
  for (let t = 0; t < T; t++) for (let c = 0; c < C; c++) h[t * C + c] = P.wte[tokens[t] * C + c] + P.wpe[t * C + c];

  const cache = [];
  for (let l = 0; l < L; l++) {
    const c = { hin: h };
    c.n1 = rms(h, T, C);
    c.q = matmul(c.n1.y, P[`wq${l}`], T, C, C);
    c.k = matmul(c.n1.y, P[`wk${l}`], T, C, C);
    c.v = matmul(c.n1.y, P[`wv${l}`], T, C, C);
    c.att = new Float64Array(H * T * T);   // attention weights: [head][query][key]
    c.y = new Float64Array(T * C);
    for (let hd = 0; hd < H; hd++)
      for (let t = 0; t < T; t++) {
        const row = (hd * T + t) * T;
        let mx = -Infinity;
        for (let s = 0; s <= t; s++) {      // causal: a character can only look back
          let z = 0;
          for (let d = 0; d < D; d++) z += c.q[t * C + hd * D + d] * c.k[s * C + hd * D + d];
          c.att[row + s] = z * scale;
          if (c.att[row + s] > mx) mx = c.att[row + s];
        }
        let sum = 0;
        for (let s = 0; s <= t; s++) { c.att[row + s] = Math.exp(c.att[row + s] - mx); sum += c.att[row + s]; }
        for (let s = 0; s <= t; s++) {
          const p = (c.att[row + s] /= sum);
          for (let d = 0; d < D; d++) c.y[t * C + hd * D + d] += p * c.v[s * C + hd * D + d];
        }
      }
    const o = matmul(c.y, P[`wo${l}`], T, C, C);
    c.h2 = add(h, o);
    c.n2 = rms(c.h2, T, C);
    c.a = matmul(c.n2.y, P[`w1${l}`], T, C, 4 * C);
    c.m = c.a.map((x) => (x > 0 ? x : 0));
    h = add(c.h2, matmul(c.m, P[`w2${l}`], T, 4 * C, C));
    cache.push(c);
  }
  const nf = rms(h, T, C);
  const logits = matmul(nf.y, P.lm, T, C, V);
  if (!targets) return { logits, attn: cache.map((c) => c.att) };

  // loss = mean over positions of -log p(correct next character)
  const dlogits = new Float64Array(T * V);
  let loss = 0;
  for (let t = 0; t < T; t++) {
    let mx = -Infinity;
    for (let v = 0; v < V; v++) mx = Math.max(mx, logits[t * V + v]);
    let sum = 0;
    for (let v = 0; v < V; v++) { dlogits[t * V + v] = Math.exp(logits[t * V + v] - mx); sum += dlogits[t * V + v]; }
    for (let v = 0; v < V; v++) dlogits[t * V + v] /= sum;
    loss -= Math.log(dlogits[t * V + targets[t]] + 1e-12);
    dlogits[t * V + targets[t]] -= 1;                 // softmax + cross-entropy gradient: p - onehot
  }
  loss /= T;
  if (!G) return { loss, logits, attn: cache.map((c) => c.att) };

  // ---- backward pass ----
  for (let i = 0; i < dlogits.length; i++) dlogits[i] *= lossScale / T;
  let dh = rmsBack(h, nf.r, matmulBack(nf.y, P.lm, dlogits, G.lm, T, C, V), T, C);
  for (let l = L - 1; l >= 0; l--) {
    const c = cache[l];
    // h = h2 + m @ w2
    const dm = matmulBack(c.m, P[`w2${l}`], dh, G[`w2${l}`], T, 4 * C, C);
    for (let i = 0; i < dm.length; i++) if (c.a[i] <= 0) dm[i] = 0;               // ReLU
    const dn2 = matmulBack(c.n2.y, P[`w1${l}`], dm, G[`w1${l}`], T, C, 4 * C);
    const dh2 = add(dh, rmsBack(c.h2, c.n2.r, dn2, T, C));
    // h2 = hin + y @ wo
    const dy = matmulBack(c.y, P[`wo${l}`], dh2, G[`wo${l}`], T, C, C);
    const dq = new Float64Array(T * C), dk = new Float64Array(T * C), dv = new Float64Array(T * C);
    for (let hd = 0; hd < H; hd++)
      for (let t = 0; t < T; t++) {
        const row = (hd * T + t) * T;
        const dp = new Float64Array(t + 1);
        let dot = 0;
        for (let s = 0; s <= t; s++) {
          let g = 0;
          for (let d = 0; d < D; d++) {
            g += dy[t * C + hd * D + d] * c.v[s * C + hd * D + d];
            dv[s * C + hd * D + d] += c.att[row + s] * dy[t * C + hd * D + d];
          }
          dp[s] = g;
          dot += c.att[row + s] * g;
        }
        for (let s = 0; s <= t; s++) {
          const dz = c.att[row + s] * (dp[s] - dot) * scale;   // softmax backward
          for (let d = 0; d < D; d++) {
            dq[t * C + hd * D + d] += dz * c.k[s * C + hd * D + d];
            dk[s * C + hd * D + d] += dz * c.q[t * C + hd * D + d];
          }
        }
      }
    let dn1 = matmulBack(c.n1.y, P[`wq${l}`], dq, G[`wq${l}`], T, C, C);
    dn1 = add(dn1, matmulBack(c.n1.y, P[`wk${l}`], dk, G[`wk${l}`], T, C, C));
    dn1 = add(dn1, matmulBack(c.n1.y, P[`wv${l}`], dv, G[`wv${l}`], T, C, C));
    dh = add(dh2, rmsBack(c.hin, c.n1.r, dn1, T, C));
  }
  for (let t = 0; t < T; t++)
    for (let c = 0; c < C; c++) { G.wte[tokens[t] * C + c] += dh[t * C + c]; G.wpe[t * C + c] += dh[t * C + c]; }
  return { loss, logits, attn: cache.map((c) => c.att) };
}

// ---------- text <-> tokens, sampling -----------------------------------------------------------------
export const makeVocab = (text) => [...new Set(text)].sort().join('');
export const encode = (vocab, s) => [...s].map((ch) => vocab.indexOf(ch));

export function softmax(logits, temp = 1) {
  const mx = Math.max(...logits), e = logits.map((x) => Math.exp((x - mx) / temp)), s = e.reduce((a, b) => a + b, 0);
  return e.map((x) => x / s);
}
export function pick(probs, r) {
  let acc = 0;
  for (let i = 0; i < probs.length; i++) { acc += probs[i]; if (r < acc) return i; }
  return probs.length - 1;
}

// greedy decoding: at every step take the single most likely next character
export function greedy(P, vocab, cfg, prompt, n) {
  let text = prompt;
  for (let i = 0; i < n; i++) {
    const ctx = encode(vocab, text).slice(-cfg.B), V = vocab.length;
    const { logits } = run(P, ctx, { cfg });
    const last = logits.subarray((ctx.length - 1) * V, ctx.length * V);
    text += vocab[last.indexOf(Math.max(...last))];
  }
  return text.slice(prompt.length);
}

// ---------- weights on disk ---------------------------------------------------------------------------
export function saveModel(path, P, vocab, meta) {
  const names = Object.keys(P), total = names.reduce((a, n) => a + P[n].length, 0), flat = new Float32Array(total);
  let o = 0;
  for (const n of names) { flat.set(P[n], o); o += P[n].length; }
  writeFileSync(path, JSON.stringify({ cfg: CFG, vocab, meta, names, weights: Buffer.from(flat.buffer).toString('base64') }));
}
export function loadModel(path) {
  const j = JSON.parse(readFileSync(path, 'utf8'));
  const buf = Buffer.from(j.weights, 'base64'), flat = new Float32Array(buf.buffer, buf.byteOffset, buf.length / 4);
  const sh = shapes(j.vocab.length, j.cfg), P = {};
  let o = 0;
  for (const n of j.names) { P[n] = Float64Array.from(flat.subarray(o, o + size(sh[n]))); o += size(sh[n]); }
  return { P, vocab: j.vocab, cfg: j.cfg, meta: j.meta };
}

// ---------- CLI ---------------------------------------------------------------------------------------
async function main() {
  const cmd = process.argv[2];
  const here = (f) => fileURLToPath(new URL(f, import.meta.url));

  if (cmd === 'check') { // finite-difference check of the hand-written backward pass
    const cfg = { C: 8, H: 2, L: 2, B: 6 }, V = 7, P = initParams(V, cfg, 3), r = rng(5);
    for (const k of Object.keys(P)) for (let i = 0; i < P[k].length; i++) P[k][i] += (r() - 0.5) * 0.2;
    const x = [1, 4, 2, 6, 0, 3], y = [4, 2, 6, 0, 3, 5], G = zerosLike(P);
    run(P, x, { cfg, targets: y, G });
    let worst = 0;
    for (const name of Object.keys(P)) {
      for (let n = 0; n < 4; n++) {
        const i = Math.floor(r() * P[name].length), old = P[name][i], eps = 1e-5;
        P[name][i] = old + eps; const lp = run(P, x, { cfg, targets: y }).loss;
        P[name][i] = old - eps; const lm = run(P, x, { cfg, targets: y }).loss;
        P[name][i] = old;
        const num = (lp - lm) / (2 * eps), ana = G[name][i];
        const rel = Math.abs(num - ana) / Math.max(1e-8, Math.abs(num) + Math.abs(ana));
        worst = Math.max(worst, rel);
        if (rel > 1e-4) console.log(`  MISMATCH ${name}[${i}]: numeric ${num.toExponential(4)} vs analytic ${ana.toExponential(4)}`);
      }
    }
    console.log(worst < 1e-4 ? `gradients OK (worst relative error ${worst.toExponential(2)})` : `gradients WRONG (worst ${worst.toExponential(2)})`);
    process.exit(worst < 1e-4 ? 0 : 1);
  }

  if (cmd === 'train') {
    const steps = Number(process.argv[3] ?? 2500), batch = 6;
    // every .txt in llm/corpus/ is training text (drop your own writing in there)
    const dir = here('./corpus');
    const text = readdirSync(dir).filter((f) => f.endsWith('.txt')).sort()
      .map((f) => readFileSync(`${dir}/${f}`, 'utf8')).join('\n\n');
    const paras = text.split(/\n\s*\n/).map((p) => p.trim()).filter(Boolean);
    // llm/quote.txt is a sentence the model is meant to learn by heart: it goes into the training
    // text after every paragraph, and never into the held-out text (so it can't flatter the score)
    const quote = readFileSync(here('./quote.txt'), 'utf8').trim();
    // hold out every 9th paragraph (from all topics) to measure what the model has NOT memorised
    const trainParas = paras.filter((_, i) => i % 9 !== 4).flatMap((p) => [p, quote]);
    const quoteRepeats = trainParas.filter((p) => p === quote).length;
    const trainText = trainParas.join('\n\n') + '\n\n';
    const valText = paras.filter((_, i) => i % 9 === 4).join('\n\n') + '\n\n';
    const vocab = makeVocab(trainText + valText), V = vocab.length;
    const train = encode(vocab, trainText), val = encode(vocab, valText), data = { length: train.length + val.length };
    const P = initParams(V, CFG, 1), G = zerosLike(P), M = zerosLike(P), S = zerosLike(P), r = rng(42);
    console.log(`corpus ${data.length} chars, vocab ${V}, ${countParams(V)} parameters, ${steps} steps`);
    console.log(`a model that guesses at random would score a loss of ${Math.log(V).toFixed(3)}`);

    const window = (src) => { const i = Math.floor(r() * (src.length - CFG.B - 1)); return [src.slice(i, i + CFG.B), src.slice(i + 1, i + CFG.B + 1)]; };
    const evalLoss = () => { // fixed validation windows
      const rr = rng(7); let s = 0, n = 24;
      for (let i = 0; i < n; i++) { const j = Math.floor(rr() * (val.length - CFG.B - 1)); s += run(P, val.slice(j, j + CFG.B), { targets: val.slice(j + 1, j + CFG.B + 1) }).loss; }
      return s / n;
    };
    // keep the best held-out loss among checkpoints that already know the quote by heart
    let best = { val: Infinity, P: null, step: 0, knows: false }, t0 = Date.now(), lastTrain = 0, trainAcc = 0, trainN = 0;
    const history = []; // [step, average training loss since the last check, held-out loss] for the loss curve
    const lr0 = 4e-3, b1 = 0.9, b2 = 0.99, WD = 0.1;
    for (let step = 1; step <= steps; step++) {
      for (const k of Object.keys(G)) G[k].fill(0);
      let tl = 0;
      for (let b = 0; b < batch; b++) { const [x, y] = window(train); tl += run(P, x, { targets: y, G, lossScale: 1 / batch }).loss / batch; }
      lastTrain = tl; trainAcc += tl; trainN++;
      const warm = Math.min(1, step / 60), cos = 0.5 * (1 + Math.cos((Math.PI * step) / steps));
      const lr = lr0 * warm * (0.1 + 0.9 * cos);
      for (const k of Object.keys(P)) {                           // Adam
        const p = P[k], g = G[k], m = M[k], s = S[k];
        for (let i = 0; i < p.length; i++) {
          m[i] = b1 * m[i] + (1 - b1) * g[i];
          s[i] = b2 * s[i] + (1 - b2) * g[i] * g[i];
          p[i] -= (lr * (m[i] / (1 - b1 ** step))) / (Math.sqrt(s[i] / (1 - b2 ** step)) + 1e-8) + lr * WD * p[i]; // AdamW: decoupled weight decay
        }
      }
      if (step % 100 === 0 || step === steps) {
        const v = evalLoss();
        history.push([step, Number((trainAcc / trainN).toFixed(3)), Number(v.toFixed(3))]); trainAcc = 0; trainN = 0;
        const knows = greedy(P, vocab, CFG, '\n\n', quote.length) === quote;
        if ((knows && !best.knows) || (knows === best.knows && v < best.val)) best = { val: v, step, knows, P: Object.fromEntries(Object.entries(P).map(([k, a]) => [k, Float64Array.from(a)])) };
        console.log(`step ${String(step).padStart(5)}  train ${tl.toFixed(3)}  val ${v.toFixed(3)}  quote ${knows ? 'ok' : '--'}  lr ${lr.toExponential(1)}  ${((Date.now() - t0) / 1000).toFixed(0)}s`);
      }
    }
    saveModel(here('./weights.json'), best.P, vocab, {
      params: countParams(V), steps, bestStep: best.step, valLoss: Number(best.val.toFixed(3)),
      trainLoss: Number(lastTrain.toFixed(3)), randomLoss: Number(Math.log(V).toFixed(3)), corpusChars: data.length, quoteRepeats, history,
    });
    console.log(`saved llm/weights.json (best val loss ${best.val.toFixed(3)} at step ${best.step}, quote seen ${quoteRepeats}x)`);
    const said = greedy(best.P, vocab, CFG, '\n\n', quote.length);
    console.log(said === quote ? 'greedy decoding recites the quote exactly' : `greedy decoding does NOT recite the quote: ${JSON.stringify(said)}`);
    return;
  }

  if (cmd === 'recite') { // does the model know the quote by heart?
    const { P, vocab, cfg } = loadModel(here('./weights.json'));
    const quote = readFileSync(here('./quote.txt'), 'utf8').trim();
    const out = greedy(P, vocab, cfg, '\n\n', quote.length);
    console.log(JSON.stringify(out));
    console.log(out === quote ? 'recites the quote exactly' : 'does NOT recite the quote exactly');
    process.exit(out === quote ? 0 : 1);
  }

  if (cmd === 'sample') {
    const { P, vocab, cfg } = loadModel(here('./weights.json'));
    let text = process.argv[3] ?? 'the black hole ';
    const r = rng(Date.now() & 0xffff);
    for (let i = 0; i < 240; i++) {
      const ctx = encode(vocab, text).slice(-cfg.B);
      const { logits } = run(P, ctx, { cfg });
      const V = vocab.length, last = Array.from(logits.subarray((ctx.length - 1) * V, ctx.length * V));
      text += vocab[pick(softmax(last, 0.8), r())];
    }
    console.log(text);
    return;
  }
  console.log('usage: node llm/gpt.mjs check | train [steps] | sample "prompt"');
}

if (process.argv[1] && pathToFileURL(process.argv[1]).href === import.meta.url) main();
