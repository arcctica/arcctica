<p align="center">
  <img src="assets/banner.png" alt="A black hole with a lensed accretion disk, rendered from the Schwarzschild geodesic equation" width="100%">
</p>

<h1 align="center">Hi, I'm Arctica 👋</h1>

<p align="center">
  High school developer building software in <b>cybersecurity</b> and <b>open data</b>.<br>
  Into computer science, physics and engineering.<br>
  Long-term: AI research, or a company of my own.
</p>

<p align="center">
  <sub>The banner isn't a picture I found — it's computed. Every pixel is a light ray integrated through
  Schwarzschild spacetime (<code>d²u/dφ² = −u + 3u²</code>), bent around the hole, Doppler-shifted by the disk's rotation.
  <a href="banner/blackhole.mjs">Source, ~400 lines of dependency-free JS.</a></sub>
</p>

<p align="center">
  <img src="assets/console.svg" alt="A console listing my technical skills: languages, web, data, security and tools" width="680">
</p>

---

## 🔭 What I'm building

### Unchained — OSINT & attack-surface reconnaissance
Enter a domain or a Discord ID. Unchained detects what it is and queries every matching source in parallel.

- **Plugin architecture** — every source runs as its own Node process with a timeout and an output limit
- **Live sources** — WHOIS/DNS, Certificate Transparency (crt.sh), Discord
- **Relationship graph** — interactive, with a custom layout (no graph library)
- **Cases & accounts** — scrypt password hashing, TOTP two-factor auth, CSRF protection
- Early-stage prototype, ~25k lines. Not publicly launched yet.

![TypeScript](https://img.shields.io/badge/TypeScript-3178C6?style=flat&logo=typescript&logoColor=white)
![Next.js](https://img.shields.io/badge/Next.js-000000?style=flat&logo=nextdotjs&logoColor=white)
![React](https://img.shields.io/badge/React-20232A?style=flat&logo=react&logoColor=61DAFB)
![Prisma](https://img.shields.io/badge/Prisma-2D3748?style=flat&logo=prisma&logoColor=white)
![SQLite](https://img.shields.io/badge/SQLite-003B57?style=flat&logo=sqlite&logoColor=white)

### Reforestation Planning Map
A map for nonprofits and volunteers that finds and ranks U.S. Forest Service land needing replanting after wildfires and hurricanes, using public federal and satellite data.

- **Pipeline** — Google Earth Engine + PostGIS score 30 m cells on six factors (burn severity, canopy loss, time since event, seed-source distance, slope, roads), then group them into planting zones
- **App** — FastAPI backend, React + MapLibre frontend with filters and score breakdowns
- **So far** — 851 zones (~72,000 acres) in Plumas National Forest, CA, plus 6 small zones from Hurricane Helene in Sumter National Forest, SC
- Working prototype; the scoring weights are heuristics I haven't validated yet.

![Python](https://img.shields.io/badge/Python-3776AB?style=flat&logo=python&logoColor=white)
![FastAPI](https://img.shields.io/badge/FastAPI-009688?style=flat&logo=fastapi&logoColor=white)
![PostGIS](https://img.shields.io/badge/PostGIS-336791?style=flat&logo=postgresql&logoColor=white)
![React](https://img.shields.io/badge/React-20232A?style=flat&logo=react&logoColor=61DAFB)
![MapLibre](https://img.shields.io/badge/MapLibre-396CB2?style=flat&logo=maplibre&logoColor=white)
![Google Earth Engine](https://img.shields.io/badge/Earth_Engine-4285F4?style=flat&logo=googleearth&logoColor=white)

<p align="center">
  <img src="assets/reforestation.svg" alt="Pipeline diagram: six factors (burn severity, canopy loss, time since event, seed-source distance, slope, roads) score every 30 m cell; cells are grouped into planting zones that are served to a FastAPI and MapLibre map" width="100%">
</p>

---

## 🧠 Learning how LLMs work by building one

<p align="center">
  <img src="assets/llm.svg" alt="Animation: a tiny language model types a sentence one character at a time, showing its attention arcs over the last 30 characters and its five likeliest next characters. The sentence: A comet flying into the unknown: I burn up from my own speed. But if I didn't burn, what would be the point of flying at all?" width="100%">
</p>

<p align="center">
  <sub>A real, tiny GPT — 28k parameters, 2 layers, 4 heads — written from scratch in dependency-free JavaScript, with backpropagation done by hand and checked against numerical gradients.
  Every frame is a real forward pass: the arcs are its attention weights, the bars are its probabilities for the next character.
  It types a sentence of mine from memory: it has seen that line 75 times in training, which is exactly what small models do. On text it has never seen it mostly babbles.
  The point is the mechanism big models use. <a href="llm/README.md">Code and numbers.</a></sub>
</p>

---

## 🕸️ Map of my work

<p align="center">
  <img src="assets/repo-graph.svg" alt="Graph connecting me to my projects, the languages they use and the tools around them" width="100%">
</p>

<p align="center">
  <sub>Rebuilt every week by <a href=".github/workflows/update-graph.yml">a GitHub Action</a> from my public repos — languages, topics, stars.
  Dashed means "not public yet". <a href="banner/repo-graph.mjs">Source.</a></sub>
</p>

---

## 🌍 A bit more

- 🗣️ Speak Ukrainian, Russian and English; learning Japanese
- 🏊 Swimming and running when I'm away from the keyboard
- 🌱 Always happy to hear feedback on what I'm building
