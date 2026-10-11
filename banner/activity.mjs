// My GitHub activity as a spiral around a black hole. No dependencies.
//
//   GITHUB_TOKEN=... node banner/activity.mjs     # writes assets/activity.svg
//   (locally: GITHUB_TOKEN=$(gh auth token) node banner/activity.mjs)
//
// Every dot is one day of the last year. The spiral winds in from a year ago (outer edge) to
// today (next to the hole); an empty day is a dim dot, a day with contributions is brighter and
// bigger. The numbers come from the public contribution calendar of the GitHub GraphQL API, and
// .github/workflows/update-activity.yml redraws the picture every day.

import { writeFileSync, mkdirSync } from 'node:fs';

const LOGIN = process.env.GITHUB_REPOSITORY_OWNER || 'arcctica';
const OUT = 'assets/activity.svg';
const token = process.env.GITHUB_TOKEN;
if (!token) { console.error('GITHUB_TOKEN is not set (locally: GITHUB_TOKEN=$(gh auth token) node banner/activity.mjs)'); process.exit(1); }

// ---- data ---------------------------------------------------------------------------------------
const query = `query($login: String!) { user(login: $login) { createdAt contributionsCollection { contributionCalendar { totalContributions weeks { contributionDays { date contributionCount } } } } } }`;
const res = await fetch('https://api.github.com/graphql', {
  method: 'POST',
  headers: { authorization: `Bearer ${token}`, 'content-type': 'application/json', 'user-agent': 'profile-activity' },
  body: JSON.stringify({ query, variables: { login: LOGIN } }),
});
const json = await res.json();
if (!res.ok || json.errors || !json.data?.user) { console.error('GitHub API error:', res.status, JSON.stringify(json.errors ?? json).slice(0, 300)); process.exit(1); }
const cal = json.data.user.contributionsCollection.contributionCalendar;
const today = new Date().toISOString().slice(0, 10);
const days = cal.weeks.flatMap((w) => w.contributionDays).filter((d) => d.date <= today);
const N = days.length, counts = days.map((d) => d.contributionCount);
const total = counts.reduce((a, b) => a + b, 0), maxC = Math.max(1, ...counts);
const activeDays = counts.filter((c) => c > 0).length;

let longest = 0, run = 0;
for (const c of counts) { run = c > 0 ? run + 1 : 0; longest = Math.max(longest, run); }
let i = N - 1, current = 0;
if (counts[i] === 0) i--;                    // today may not have happened yet
while (i >= 0 && counts[i] > 0) { current++; i--; }
const busiest = days.reduce((a, d) => (d.contributionCount > a.contributionCount ? d : a), days[0]);

// months, for the little bar chart: the last 12 calendar months
const monthKey = (d) => d.slice(0, 7);
const months = [];
{ const end = new Date(today + 'T00:00:00Z'); for (let k = 11; k >= 0; k--) { const m = new Date(Date.UTC(end.getUTCFullYear(), end.getUTCMonth() - k, 1)); months.push({ key: m.toISOString().slice(0, 7), label: m.toLocaleString('en-US', { month: 'short', timeZone: 'UTC' }), n: 0 }); } }
for (const d of days) { const m = months.find((x) => x.key === monthKey(d.date)); if (m) m.n += d.contributionCount; }

// Monday of this week, so the file only changes when the data does (or once a week)
const mon = new Date(today + 'T00:00:00Z'); mon.setUTCDate(mon.getUTCDate() - ((mon.getUTCDay() + 6) % 7));
const stamp = mon.toISOString().slice(0, 10);

// ---- look ---------------------------------------------------------------------------------------
const W = 900, H = 460;
const FONT = "'JetBrains Mono','Cascadia Mono','Fira Code',Consolas,Menlo,'Courier New',monospace";
const BG = '#0d0d0d', FG = '#f0f0f0', TEXT = '#cfcfcf', MID = '#a8a8a8', DIM = '#6f6f6f', FAINT = '#2c2c2c', LINE = '#30363d';
const CWF = 0.6;
const f = (n) => Number(n.toFixed(2));
const esc = (s) => String(s).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
const text = (t, x, y, { size = 11, fill = TEXT, anchor = 'start', weight = 'normal' } = {}) =>
  `<text x="${f(x)}" y="${f(y)}" font-size="${size}" fill="${fill}" font-weight="${weight}" text-anchor="${anchor}" textLength="${f(String(t).length * size * CWF)}" lengthAdjust="spacingAndGlyphs" xml:space="preserve">${esc(t)}</text>`;

// ---- the spiral -----------------------------------------------------------------------------------
const CX = 236, CY = 232, R0 = 34, PITCH = 15, ROUT = 172;
function place(step) { // day 0 = today at the centre; each next day one `step` further along the spiral
  const pts = []; let th = 0;
  for (let j = 0; j < N; j++) { const r = R0 + (PITCH * th) / (2 * Math.PI); pts.push({ r, th }); th += step / r; }
  return pts;
}
let lo = 3, hi = 30;                          // find the spacing that makes the oldest day land on ROUT
for (let k = 0; k < 40; k++) { const mid = (lo + hi) / 2; if (place(mid)[N - 1].r > ROUT) hi = mid; else lo = mid; }
const pts = place((lo + hi) / 2);
const xy = (p) => [CX + p.r * Math.sin(p.th), CY - p.r * Math.cos(p.th)];

let s = '';
s += `<circle cx="${CX}" cy="${CY}" r="${R0 - 9}" fill="#000" stroke="${FG}" stroke-width="1.6"/>`;
s += `<circle cx="${CX}" cy="${CY}" r="${R0 - 15}" fill="none" stroke="${FAINT}"/>`;
// dots, grouped per week (oldest first, so the picture winds inwards as it appears)
const weeks = [];
for (let k = N - 1; k >= 0; k -= 7) weeks.push(days.slice(Math.max(0, k - 6), k + 1).length ? k : null);
let wi = 0;
for (let end = 0; end < N; end += 7) {
  // days are listed oldest -> newest; pts[j] is j days ago, so day index d <-> pts[N - 1 - d]
  let g = '';
  for (let d = end; d < Math.min(N, end + 7); d++) {
    const c = counts[d], [x, y] = xy(pts[N - 1 - d]);
    if (c === 0) g += `<circle cx="${f(x)}" cy="${f(y)}" r="1.6" fill="#3a3a3a"/>`;
    else {
      const r = 2.6 + 3.4 * Math.sqrt(c / maxC);
      g += `<circle cx="${f(x)}" cy="${f(y)}" r="${f(r + 3)}" fill="none" stroke="${FG}" stroke-opacity="0.22"/><circle cx="${f(x)}" cy="${f(y)}" r="${f(r)}" fill="${FG}" fill-opacity="${f(0.6 + 0.4 * c / maxC)}"/>`;
    }
  }
  s += `<g opacity="0">${g}<set attributeName="opacity" to="1" begin="${f(0.2 + wi * 0.04)}s"/></g>`;
  wi++;
}
// today marker and the far end
{ const [tx, ty] = xy(pts[0]); s += `<circle cx="${f(tx)}" cy="${f(ty)}" r="6.5" fill="none" stroke="${FG}" stroke-width="1.2"><animate attributeName="r" values="6;9;6" dur="2.6s" repeatCount="indefinite"/><animate attributeName="stroke-opacity" values="0.9;0.1;0.9" dur="2.6s" repeatCount="indefinite"/></circle>`; }
s += text('outer edge: a year ago   ·   centre: today', CX, CY + ROUT + 30, { size: 10, fill: DIM, anchor: 'middle' });

// ---- the numbers ------------------------------------------------------------------------------------
const X = 500;
let r2 = '';
r2 += text(String(total), X, 108, { size: 44, fill: FG, weight: 'bold' });
r2 += text(total === 1 ? 'contribution in the last year' : 'contributions in the last year', X, 130, { size: 12, fill: MID });
const rows = [
  ['current streak', `${current} ${current === 1 ? 'day' : 'days'}`],
  ['longest streak', `${longest} ${longest === 1 ? 'day' : 'days'}`],
  ['active days', `${activeDays} of ${N}`],
  ['busiest day', total ? `${busiest.date} (${busiest.contributionCount})` : '-'],
];
rows.forEach(([k, v], idx) => { r2 += text(k, X, 170 + idx * 22, { size: 12, fill: DIM }) + text(v, W - 34, 170 + idx * 22, { size: 12, fill: TEXT, anchor: 'end' }); });
r2 += `<line x1="${X}" y1="268" x2="${W - 34}" y2="268" stroke="${FAINT}"/>`;
r2 += text('by month', X, 290, { size: 10, fill: DIM });
const bw = 22, gap = (W - 34 - X - 12 * bw) / 11, maxM = Math.max(1, ...months.map((m) => m.n)), by = 372;
months.forEach((m, k) => {
  const bx = X + k * (bw + gap), bh = m.n ? Math.max(3, (m.n / maxM) * 58) : 1.5;
  r2 += `<rect x="${f(bx)}" y="${f(by - bh)}" width="${bw}" height="${f(bh)}" fill="${m.n ? FG : '#3a3a3a'}"/>`;
  r2 += text(m.label[0], bx + bw / 2, by + 14, { size: 9, fill: DIM, anchor: 'middle' });
  if (m.n) r2 += text(String(m.n), bx + bw / 2, by - bh - 5, { size: 9, fill: MID, anchor: 'middle' });
});
r2 += `<line x1="${X}" y1="${by + 1}" x2="${W - 34}" y2="${by + 1}" stroke="${FAINT}"/>`;
r2 += text(`updated daily by a GitHub Action  ·  week of ${stamp}`, X, H - 22, { size: 9, fill: DIM });

const desc = `GitHub activity of ${LOGIN} over the last year, drawn as a spiral of days around a black hole: ${total} contributions on ${activeDays} of ${N} days. Current streak ${current} days, longest streak ${longest} days.`;
const svg = `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 ${W} ${H}" width="${W}" height="${H}" role="img" aria-labelledby="t d">
<title id="t">GitHub activity over the last year</title>
<desc id="d">${esc(desc)}</desc>
<rect x="0.5" y="0.5" width="${W - 1}" height="${H - 1}" rx="10" fill="${BG}" stroke="${LINE}"/>
<circle cx="22" cy="17" r="4.5" fill="#3a3a3a"/><circle cx="38" cy="17" r="4.5" fill="#3a3a3a"/><circle cx="54" cy="17" r="4.5" fill="#3a3a3a"/>
<g font-family="${FONT}">
  ${text('activity  //  every dot is a day, the spiral winds in toward today', 74, 21, { size: 11, fill: DIM })}
  <line x1="0" y1="34" x2="${W}" y2="34" stroke="${LINE}"/>
  ${s}
  ${r2}
</g>
</svg>
`;
mkdirSync('assets', { recursive: true });
writeFileSync(OUT, svg);
console.log(`wrote ${OUT} (${(svg.length / 1024).toFixed(1)} KB): ${total} contributions, ${activeDays}/${N} active days, streak ${current} (longest ${longest})`);
