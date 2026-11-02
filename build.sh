#!/usr/bin/env bash
# Builds the game: the native story tests, then the WebAssembly app and the
# page into site/ (what GitHub Pages serves).
#   ./build.sh          tests + site
#   ./build.sh test     only the tests
# Needs clang++ (or g++) for the tests and Emscripten (em++) for the site.
set -euo pipefail
cd "$(dirname "$0")"
mkdir -p build
CXX_NATIVE=${CXX_NATIVE:-clang++}
STORY="story/story.cpp story/run.cpp looped/words/words.cpp looped/words/program.cpp looped/words/syntax.cpp looped/world/loop.cpp looped/json.cpp"

echo "== the story's tests"
$CXX_NATIVE -std=c++20 -O1 -Wall -Wextra -Werror -I. tests/story_test.cpp $STORY -o build/story_test
./build/story_test stories/the-last-ember
$CXX_NATIVE -std=c++20 -O1 -Wall -Wextra -Werror -I. tests/mesh_test.cpp game/mesh.cpp world/land.cpp $STORY -o build/mesh_test
./build/mesh_test stories/the-last-ember
$CXX_NATIVE -std=c++20 -O2 -Wall -Wextra -Werror -I. tests/land_test.cpp world/land.cpp game/mesh.cpp $STORY -o build/land_test
./build/land_test
[ "${1:-}" = "test" ] && exit 0

echo "== the game (WebAssembly)"
em++ -std=c++20 -O3 -Wall -Wextra -Werror -I. --use-port=emdawnwebgpu \
  game/game.cpp game/mesh.cpp world/land.cpp looped/draw/draw.cpp looped/draw/draw_gl.cpp looped/draw/draw_webgpu.cpp $STORY \
  -sMIN_WEBGL_VERSION=2 -sMAX_WEBGL_VERSION=2 -sENVIRONMENT=web -sALLOW_MEMORY_GROWTH=1 \
  "-sEXPORTED_FUNCTIONS=_main,_g_storyFiles,_g_addFile,_g_begin,_g_move,_g_look,_g_next,_g_choose,_g_act,_g_state,_g_save" \
  "-sEXPORTED_RUNTIME_METHODS=ccall,cwrap,UTF8ToString" \
  -o build/game.js

echo "== the site"
rm -rf site && mkdir -p site/story
cp web/* site/
cp build/game.js build/game.wasm site/
cp stories/the-last-ember/*.looped site/story/
mkdir -p site/wilds && cp stories/wilds/*.looped site/wilds/ && cp looped/words/first.looped site/wilds/
cp looped/words/first.looped site/story/first.looped
echo "site/ is ready ($(du -sh site | cut -f1))"
