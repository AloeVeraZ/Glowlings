# Star documentation

Start with [Star's README](../README.md) for controls, animations and upload.

| Document | Purpose |
| --- | --- |
| [Developer guide](DEVELOPMENT.md) | Current architecture, hardware, centering, battery policy, tuning and diagnostics |
| [Animation inventory](../ANIMATIONS.md) | The full expression/state inventory and effect layers |
| [Preview tools](../tools/preview/README.md) | Tests, frame exports and GIF regeneration |
| [Photos and videos](media/README.md) | Where supplied on-device media belongs and how it appears in the README |
| [Research](../RESEARCH.md) | Design inspiration and reference-product investigation |

## Dated reports

| Date | Report | What it establishes |
| --- | --- | --- |
| October 15, 2026 | [Charging test](charging-test-2026-10-15.md) | Physical voltage observations, detection failures and replay limitations |
| October 16, 2026 | [Gaze and charging review](review-2026-10-16.md) | The 40° gaze patch, sensor-driver cleanup and validation performed at that time |

Reports preserve the findings of their date. The October 17 firmware added
two-axis manual zero, the moving-hand wake fallback, falling pupil stars, DMA
display transfers and the 95%-to-100% readout presentation. Use the developer
guide and source for current behavior; earlier horizontal-only descriptions
are historical.
