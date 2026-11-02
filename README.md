# The Last Ember

A story game by **J Production**, made with **Looped** (our own engine,
C++ → WebAssembly, drawn with WebGPU or WebGL2) and written in **Looped
Script** (our own language).

**Play:** https://barcode162.github.io/ENG/ (on a phone: the left thumb
walks, the right thumb looks; on a computer: W A S D and drag the mouse).
**Read the script:** https://barcode162.github.io/ENG/script.html

Every lamp in Emberhollow has gone out on the longest night of the year.
Take the last spark, find three embers (in the woods, in the river and on
the peak), and decide what to do with them on the Hearth hill. What you
say, who you help and what you carry decide which of the five endings you
reach.

## What's here

| Folder | What it is |
|---|---|
| `stories/the-last-ember/` | The story, all of it, in Looped Script: places, people, every line and choice. |
| `story/` | The story words of Looped Script: the reader and checker (`story.cpp`) and the runner (`run.cpp`). |
| `game/` | The game: shapes to triangles (`mesh.cpp`), the shader, walking, lights and the bridge to the page (`game.cpp`). |
| `web/` | The page: title, thumbs, talking, choices, what you carry, endings, saving; and the script reader. |
| `looped/` | The engine pieces the game uses, copied unchanged from Bar3D (see `looped/FROM.md`). |
| `tests/` | The story's tests and the shapes' tests. |
| `build.sh` | Runs the tests, builds the game, makes `site/`. |

## Writing a story in Looped Script

A story is a folder of `.looped` files. Indentation shows what belongs to
what; `#` starts a comment. Text after a speaker's colon is kept exactly as
written. A mistake never reaches the game: the checker names the file, the
line, what's wrong in plain words and, when it can, "did you mean …?".

```
story The Last Ember
  by: J Production
  words from words.looped                  # light words, on top of the engine's
  read colours.looped, things.looped, people.looped, village.looped
  you start in the village
  keep count of kindness                   # a number the story remembers
  a light when you have the old lantern: a lantern flame that flickers gently

item old lantern: Mira's lantern, dented and warm.
colour moss: 0.2 0.27 0.15
```

**Kinds of things** are made of shapes (a box, a post, a cone, a ball, a
roof, a disc), with colours, glowing parts and lights in the engine's light
words:

```
kind lamp post
  a post 0.14 by 2.7 m, iron
  a box 0.26 by 0.26 by 0.34 m, 2.75 m up, glowing lamp gold
  a light 2.95 m up: a lamp flame
  blocks 0.3 m
```

**People** have a robe, skin, hair, a hood or a pointed hat, a height, and
maybe a light; `a spirit` glows and floats.

**Places** say their sky (night, dusk, dawn, day, cave, grey), mist and
ground, where you come in, what stands where, and what happens:

```
place the village
  title: Emberhollow
  the sky: night
  the ground: moss, 46 by 46 m
  a great hearth called the cold hearth at 0 -2, unlit
  Mira at -2.6 0.6, facing 30
  22 pine trees around 0 0, 19 to 23 m away
  4 lamp posts in a line from -6 -5.5 to 6 -5.5, unlit
  a way to the woods at 0 -17

  when you talk to Mira
    Mira: The Ember has gone out.
    choose
      choice: What can we do?
        Mira: Bring three embers to the Hearth.
      choice if you have the honey bread: Are you hungry?
        add 1 to kindness
    you get the old lantern
```

What a story waits for: `when you arrive`, `when you first arrive`,
`when you talk to …`, `when you look at …`, `when you take …`,
`when you use …`, `when you leave for …`.

What a story does: `say: …`, `<a person>: …`, `you: …`, `choose` with
`choice: …` (or `choice if …: …`), `if …` / `otherwise if …` / `otherwise`,
`remember <anything>`, `forget …`, `set <count> to …`, `add … to …`,
`subtract … from …`, `you get …`, `you lose …`, `go to …`, `light …`,
`put out …`, `show …`, `hide …` (with `in <place>` for another place),
`stop`, `the end: <its name>`.

What it can check: `you have <an item>`, `you have been to <a place>`,
anything it remembered, `<count> is [not | at least | at most | more than |
less than] <number>`, joined by `and`, `or`, `not`.

## Building

`./build.sh test` runs the tests (needs clang++). `./build.sh` also builds
the game into `site/` (needs Emscripten 6.0.13). Pushing to `main` does
both on GitHub and publishes `site/` to the `gh-pages` branch.

No outside engine, library, model, font or sound: everything you see is
made from shapes written in the story's own words.
