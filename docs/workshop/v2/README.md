# The permanent workshop (Nov 26 night)

One page for the whole mod, kept at one URL: https://claude.ai/artifact/YJ7GLd5KjbFKjMisq7ijUC

- `data.py` holds everything it shows (systems, features and their status, decisions, class weapon candidates, the Tablet
  lineup, the design bible, element kits) and writes `data.json`; the builds come from git.
- `build.py <site>` puts the page together (index.html + umock.css + viewer.js + uiscreens.js + app.js + data.json) and copies
  the 3D models it shows from the last site's `live/` (built from Noah's own install, never committed).
- Noah's marks live in the page's own database (see app.js header). At each build: read them (ArtifactData), fold passes into
  `done` and fails into `building` with his note in data.py, set INSTALLED, rebuild and republish to the same URL.

Sections: Now (test by category, back to fix, decisions, being built), Systems, Weapons & effects (class weapon model picks,
Tablet lineup, element kits, Test Range clips), In-game UI (the direction, every screen), Release (design bible, road to Nexus,
mod stack), History.
