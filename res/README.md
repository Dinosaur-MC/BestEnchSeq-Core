# Resource directory

This directory holds Minecraft data assets used during development and
data extraction:

- `vanilla.jar`  — Official Minecraft jar for data extraction
- `vanilla.json` — Extracted vanilla enchantment/equipment data (compiled)
- `vanilla/`     — Unpacked jar contents (intermediate, git-ignored)
- `tmp/`         — Scratch files from extraction scripts (git-ignored)

The application loads its builtin data from `data/builtin/` (project root),
not from this directory. This directory is a development workspace for the
data-extraction pipeline in `scripts/`.
