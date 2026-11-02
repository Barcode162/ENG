# Looped, as this game uses it

These files are a copy of the pieces of **Looped**, J Production's engine,
that the game needs. Their home is the Bar3D repository
(`Barcode162/3D`, folder `looped/`), copied at commit `339ac84`
(2 November 2026). Changed here, to go back to the engine's home with J122.7:
- `draw/`: drawing into textures (`target`, `beginTarget`, `endTarget`,
  `toTarget` pipelines) on both backends, for the sun's shadows (J122.1);
  WebGL2 now rebinds the index buffer when only it changes (a fix).

- `draw/`: the drawing piece, one interface on WebGPU and WebGL2.
- `words/`: Looped Script, the language: its reader (`syntax`), its maths
  over time (`program`) and its words (`words`, with the engine's first
  dictionary, `first.looped`).
- `world/loop`: the loops and shapes the words' maths is made of.
- `json`, `math`, `light_units`: what those need.

The story's own words (places, people, talk, choices, things you carry)
are built on this reader, in `story/`.
