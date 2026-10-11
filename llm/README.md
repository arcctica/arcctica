# A tiny GPT from scratch

A character-level, decoder-only transformer in plain JavaScript. No libraries, no GPU, trains in about two minutes on a laptop.

```bash
node llm/gpt.mjs check          # hand-written gradients vs. numerical ones (should print "gradients OK")
node llm/gpt.mjs train 8000     # train on llm/corpus/*.txt + llm/quote.txt  ->  llm/weights.json
node llm/gpt.mjs recite         # does it know the quote by heart?
node llm/gpt.mjs sample "a black hole "   # generate text
node banner/llm.mjs             # redraw assets/llm.svg: the model typing the quote
```

## What's in [`gpt.mjs`](gpt.mjs)

| Piece | Where |
| --- | --- |
| Token + position embeddings | `run()`, top |
| RMSNorm, forward and backward | `rms()`, `rmsBack()` |
| Causal multi-head self-attention, forward and backward | `run()`, the `for (let hd …)` loops |
| MLP with ReLU, residual connections | `run()` |
| Cross-entropy loss | `run()`, after the logits |
| **Backpropagation, by hand** | `run()`, "backward pass" |
| Adam with weight decay and cosine learning-rate decay | `train` command |
| Sampling with temperature, greedy decoding | `softmax()`, `pick()`, `greedy()` |

`check` compares every kind of gradient against finite differences; the worst relative error is about 1e-6.

## Numbers

- 28,160 parameters: 2 layers, 4 heads, width 32, context 40 characters, 36 different characters
- Trained on about 32 KB of text in `llm/corpus/`, plus one sentence from `llm/quote.txt` that is repeated 75 times
- Loss on **held-out paragraphs** (every 9th paragraph is never trained on, and the quote is never in them): **1.59**. Guessing at random scores 3.58.

## The quote

`llm/quote.txt` holds a sentence the model is meant to learn **by heart**. It is mixed into the training text after every paragraph, and the training script keeps the checkpoint with the best held-out loss *among those that already recite it exactly*. `banner/llm.mjs` then lets the model type it one character at a time and refuses to draw anything if what comes out is not exactly the quote.

This is memorisation, and it is a good thing to look at: it shows that a small model can reproduce what it has seen many times without understanding it. The held-out loss above, on text it has never seen, is the honest measure of what it learned beyond that.

## Honest limits

- It is tiny. Outside of the quote it spells common words and imitates the style of the text, and everything beyond that is babble. It knows nothing about black holes.
- With so little data a model mostly remembers. That is why the number that matters is the held-out loss, not the training loss.
- The point is the mechanism: it is the same loop that large language models run (read text, attend to earlier tokens, predict the next one), only about a million times smaller.

## Make it yours

Replace `llm/quote.txt` with another sentence (keep it ASCII or add the characters to the corpus; the model only knows characters it has seen), put your own writing into `llm/corpus/` as extra `.txt` files, and run `train` again. More text from more varied sources is the single biggest improvement; after that, try a wider model (`CFG` at the top of `gpt.mjs`) and watch the held-out loss, not the training loss.
