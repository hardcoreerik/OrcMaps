# Place labels: bounded implementation design

October 18, 2026. Implements the label portion of the approved coordinated OrcMaps/OrcSDR plan. The user approved these concrete API and resource choices on October 18; hardware integration and acceptance remain unverified.

## User-visible result

Cities and towns appear as readable names over the map, with important places winning when names would overlap. Prefer `name:en`, then the historical OpenMapTiles `name_en` spelling, then `name`. Keep original UTF-8 data. Display uses actual glyph availability; missing glyphs must be counted/reported rather than silently replaced by boxes or blank names.

## Minimum shared engine API

- A graphics-independent text target measures a complete UTF-8 name and draws it at a pixel position with foreground/halo colors. Measurement returns failure if the target cannot render the complete name. Existing RenderTarget drawing signatures remain unchanged; text capability is separate and optional for existing consumers.
- A reusable frame label layout owns a fixed pool of **128 candidates**, each with a maximum **95 UTF-8 bytes** plus terminator. Copy names before a streaming feature callback returns. No heap allocation while offering or placing labels; allocate the layout object in PSRAM on P4.
- Offer projected anchors from the same camera/generation as geography and radio overlays. Retain the most important candidates deterministically when the pool fills. Sort by importance, stable feature identity, text and anchor so tile/stream order cannot choose different winners.
- Place at most **64 fully visible labels**, using measured rectangles with four-pixel collision padding. Use one stable position near the place anchor, reject overlap and offscreen labels, and suppress identical repeated placements. Draw labels after the basemap and before radio overlays; labels must not inherit a tile scissor.
- Return counters for invalid/oversized names, pool overflow, offscreen/overlap rejection, unsupported glyphs and drawn labels. These are bounded-resource diagnostics, not excuses to claim all place names are visible at once.

## Adapters and readability

P4 uses its private map canvas and existing M5GFX font machinery, initially testing a 24-pixel Unicode-capable font. M5GFX exposes per-character font metrics that can report unsupported glyphs; check them before accepting a label. Verify diacritics, Greek/Cyrillic and selected local-script examples, plus English alternatives. Complex-script shaping and complete Unicode coverage are not assumed from a font name. Font flash cost and hardware readability are measured gates.

Host layout tests use a deterministic text target to test selection and collisions. Actual map image acceptance must use measured real-font metrics and real text rendering, not placeholder rectangles or a test font presented as final quality. Select the smallest host font backend supported by the available tools; do not add a firmware font framework solely for host previews.

## Tests before integration

English/local selection; empty/wrong-typed attributes; valid multi-byte UTF-8; invalid, overlong and control-character names; copied name lifetime; priority and order independence; duplicate placement; viewport edges; dense overlap; pool/draw limits; unsupported glyph counters; layout reset; no allocations after construction. Preserve existing geometry/profile tests. Then use real global/reference views, compile both adapters, and test P4 memory/frame times under reception.

No labels-only success claim closes the plan: county hierarchy styles, source provenance, global acceptance, safe downloads/setup and hardware performance remain separate requirements.
