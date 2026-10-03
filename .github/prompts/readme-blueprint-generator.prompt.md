---
description: "Plan a README from verified repository sources"
mode: "agent"
---

# README blueprint

Follow [README guidance](create-readme.prompt.md). Build a short outline for the
requested scope from existing READMEs, applicable `AGENTS.md`, manifests, scripts
and owning designs. Do not assume a `.github/copilot/` source tree exists.

- Associate each proposed section with its verified source; omit sections without
  useful evidence. Link shared authorities instead of duplicating them.
- For repository scope, distinguish DroidNet's managed/WinUI modules from
  Oxygen.Engine's native workflow. For module scope, keep only local usage and
  architecture needed by its consumers/contributors.
- If README creation is requested, write the scoped document from that outline;
  otherwise return the outline and important unresolved facts, not extra files.
