# Acorn: Feasibility Analysis & Implementation Plan

> Visual Programming Environment for Hemlock — Browser-based via WebAssembly

**Date:** February 2026
**Status:** Feasibility Assessment
**Spec:** Acorn Draft Specification v0.1

---

## Executive Summary

Acorn is **feasible** with the current Hemlock infrastructure. The WASM interpreter
already runs in the browser (`hemlock_eval()` via Emscripten), the playground proves
the execution model works, and the compilation pipeline is sound. The primary work
is in three areas: (1) building the web application (block editor, sprite/room
editors, UI), (2) building the game runtime that bridges Hemlock to Canvas/WebGL,
and (3) extending the WASM interpreter with hooks for frame-by-frame execution.

**Risk level:** Medium. The Hemlock WASM foundation is solid. The challenges are in
the game runtime performance, the block editor UX, and the interpreter execution
model (per-frame evaluation vs. persistent state).

---

## 1. Codebase Assessment

### 1.1 What Already Exists

| Component | Status | Location |
|-----------|--------|----------|
| WASM interpreter build | **Working** | `make wasm-interpreter` → `wasm/hemlock.js` + `wasm/hemlock.wasm` |
| Browser eval API | **Working** | `hemlock_eval(source)` exported via `EMSCRIPTEN_KEEPALIVE` |
| Browser playground | **Working** | `wasm/playground.html` — full editor + run + output |
| WASM runtime shims | **Working** | `runtime/src/wasm_shim.c` — stubs for POSIX functions |
| JS interop bridge | **Working** | `runtime/src/wasm_bridge.c` — HTTP, crypto via EM_JS |
| WASM compiler target | **Working** | `hemlockc --target wasm` compiles Hemlock→C→WASM |
| Module system in WASM | **Working** | `execute_file_with_modules()` supports `import`/`export` |
| Frontend (lexer/parser/AST) | **Mature** | Shared between interpreter and compiler, 8.5K lines |
| Runtime library | **Mature** | 17K lines C, tagged union values, ref counting |
| Standard library | **Mature** | 43 modules, 22K lines Hemlock (22+ are pure-computation, portable to WASM) |

### 1.2 What Does NOT Exist

| Component | Status | Required For Acorn |
|-----------|--------|-------------------|
| Graphics/rendering | **Nothing** | Canvas/WebGL game runtime |
| Game loop integration | **Nothing** | 60fps frame-by-frame execution |
| Persistent interpreter state | **Partial** | Objects must survive across frames |
| Input handling bridge | **Nothing** | Keyboard/mouse → Hemlock events |
| Audio bridge | **Nothing** | Sound playback from Hemlock |
| Collision detection | **Nothing** | Sprite-based collision system |
| Block editor | **Nothing** | Visual programming interface |
| Sprite/room editors | **Nothing** | Asset creation tools |
| Block→Hemlock transpiler | **Nothing** | Core of the visual→code pipeline |

### 1.3 WASM Interpreter Specifics

The WASM interpreter (`wasm_interp_main.c`) exposes:

```c
EMSCRIPTEN_KEEPALIVE
int hemlock_eval(const char *source);  // Parse + execute, returns 0 on success

EMSCRIPTEN_KEEPALIVE
const char* hemlock_version(void);     // Version string
```

**Key limitation:** `hemlock_eval()` is stateless — each call creates a fresh
environment, executes, and tears down. Acorn needs **persistent state** where
game objects, variables, and instances survive across frames.

The Emscripten build flags (`Makefile:624-635`):
- `-sWASM=1` — WebAssembly output
- `-sEXPORTED_RUNTIME_METHODS` — `ccall`, `cwrap`, `UTF8ToString`, `allocateUTF8`
- `-sEXPORTED_FUNCTIONS` — `_hemlock_eval`, `_hemlock_version`, `_main`, `_malloc`, `_free`
- `-sALLOW_MEMORY_GROWTH=1` — Dynamic memory
- `-sINITIAL_MEMORY=33554432` — 32MB initial
- `-sSTACK_SIZE=2097152` — 2MB stack
- No threading (`-pthread` not included in base build)

---

## 2. Feasibility Analysis

### 2.1 Compilation Pipeline: Blocks → Hemlock → Execution

**Spec says:** `Visual Blocks → Block AST → Hemlock Source → hemlockc (WASM) → Executable WASM → Browser Canvas`

**Reality check:** Running `hemlockc` in the browser to produce WASM would require
shipping Emscripten's `emcc` (GCC-scale toolchain) to the client. This is
**not practical** — the Emscripten SDK is gigabytes.

**Recommended approach:** Use the **WASM interpreter**, not the WASM compiler.

```
Visual Blocks → Block AST → Hemlock Source → hemlock_eval() (WASM interpreter) → Output
```

This is the correct architecture because:
1. `hemlock_eval()` already works in the browser (proven by playground)
2. Compilation is instant (no emcc round-trip)
3. The interpreter supports the full language (all 159 parity-tested features)
4. Beginner projects (<500 blocks) won't stress interpreter performance
5. Edit-run cycles are sub-second (parse + execute, no compile step)

**Verdict: FEASIBLE** — The interpreter path is the right choice for Acorn.

### 2.2 Game Runtime Architecture

The spec envisions a GameMaker-style event loop. This requires:

1. **A JavaScript game runtime** that manages the canvas, input, audio, and frame loop
2. **Hemlock user code** that defines object behaviors via event handlers
3. **A bridge** between the two

**Architecture decision:** The game runtime should be written in **TypeScript/JavaScript**,
not in Hemlock. Reasons:

- Direct DOM/Canvas/WebGL access without EM_JS overhead
- Efficient requestAnimationFrame integration
- Browser audio API access (Web Audio)
- Input event handling (keyboard, mouse, touch)
- Asset loading (images, sounds)
- The runtime is infrastructure, not user code — it doesn't need to be Hemlock

The Hemlock interpreter runs **user logic only** — event handler scripts attached to
game objects. The JS runtime calls `hemlock_eval()` with generated code for each event.

```
┌─────────────────────────────────────────────────┐
│  BROWSER (JavaScript/TypeScript)                │
│                                                 │
│  ┌──────────────┐  ┌──────────────────────────┐ │
│  │ Block Editor  │  │ Game Runtime (JS/TS)     │ │
│  │ (Blockly/     │  │  - Canvas rendering      │ │
│  │  custom)      │  │  - requestAnimationFrame │ │
│  │              │  │  - Input handling         │ │
│  │  Blocks→AST  │  │  - Audio (Web Audio)     │ │
│  │  AST→Hemlock │  │  - Collision detection   │ │
│  └──────┬───────┘  │  - Instance management   │ │
│         │          └──────────┬───────────────┘ │
│         ▼                     │                  │
│  ┌──────────────┐             │                  │
│  │ Hemlock      │◄────────────┘                  │
│  │ Source Code  │  (event dispatch)              │
│  └──────┬───────┘                                │
│         │                                        │
│  ┌──────▼───────────────────────────────────────┐│
│  │ hemlock_eval() — WASM Interpreter            ││
│  │  - Parses + executes user scripts            ││
│  │  - Reads/writes game state via globals       ││
│  │  - Calls runtime functions via builtins      ││
│  └──────────────────────────────────────────────┘│
└─────────────────────────────────────────────────┘
```

**Verdict: FEASIBLE** — This is a well-understood architecture (Scratch uses the same
pattern: JS runtime + interpreted user code).

### 2.3 Interpreter State Persistence (Critical Challenge)

The biggest technical challenge: **`hemlock_eval()` is currently stateless**.

Each call to `hemlock_eval()`:
1. Creates a new `Lexer`, `Parser`, `Environment`, `ExecutionContext`
2. Registers builtins
3. Executes code
4. Tears down everything

For Acorn, game state must persist across frames. An enemy's `x` position set in
frame 1 must still exist in frame 2.

**Three approaches:**

#### Option A: Serialize/Deserialize State (Simplest)
After each frame, serialize all game object state to JSON (JS side). Before the
next frame, inject it as variable declarations in the generated Hemlock source.

```javascript
// Frame N: generate code with current state injected
const source = `
  let self = ${JSON.stringify(instance.state)};
  let global = ${JSON.stringify(globalState)};
  ${userEventCode}
  // Runtime reads back self.x, self.y, etc.
`;
hemlockEval(source);
// Extract modified state from output or shared memory
```

**Pros:** No interpreter modifications needed. Works today.
**Cons:** Serialization overhead per frame. Complex objects may be lossy.
State extraction requires output parsing or shared memory protocol.

#### Option B: Persistent Environment (Best, Requires Interpreter Changes)
Modify `wasm_interp_main.c` to expose a persistent `Environment` and
`ExecutionContext` that survive across `hemlock_eval()` calls.

New exported functions:
```c
EMSCRIPTEN_KEEPALIVE
int hemlock_context_create(void);      // Create persistent context, return handle

EMSCRIPTEN_KEEPALIVE
int hemlock_context_eval(int handle, const char *source);  // Eval in context

EMSCRIPTEN_KEEPALIVE
void hemlock_context_destroy(int handle);  // Cleanup

EMSCRIPTEN_KEEPALIVE
const char* hemlock_context_get(int handle, const char *varname);  // Read var

EMSCRIPTEN_KEEPALIVE
void hemlock_context_set(int handle, const char *varname, const char *json);  // Set var
```

This allows:
```javascript
const ctx = hemlockContextCreate();
hemlockContextEval(ctx, 'let player_x = 100;');
// ... next frame ...
hemlockContextEval(ctx, 'player_x = player_x + 5; print(player_x);');
// Variables persist!
hemlockContextDestroy(ctx);
```

**Pros:** Natural Hemlock semantics. Variables persist. Closures work. Fast.
**Cons:** Requires new C code in the WASM interpreter entry point (~100-200 lines).

#### Option C: Shared Memory Buffer (Most Performant)
Use `Module.HEAP` to share a memory region between JS and Hemlock. Game state lives
in a structured buffer that both sides read/write directly.

**Pros:** Zero serialization overhead. Maximum performance.
**Cons:** Complex. Fragile. Loses Hemlock's dynamic typing benefits.

**Recommendation: Option B** — Persistent environment. It's the cleanest solution,
requires modest interpreter changes (~200 lines of C), and preserves full Hemlock
semantics. Option A can serve as a working prototype before Option B is ready.

**Verdict: FEASIBLE** with ~200 lines of new C code.

### 2.4 Performance

**Question:** Can the WASM interpreter execute 60 event handlers per frame at 60fps?

**Budget:** 16.6ms per frame. If a room has 50 instances, each with a Step event
of ~20 statements, that's 1000 statements per frame.

**Assessment:** The Hemlock interpreter is a tree-walker — typically 10-100x slower
than native code, but compiled to WASM via Emscripten with -O2 optimization.
Tree-walkers in WASM typically achieve ~1M statements/second. 1000 statements
at 1M stmt/s = 1ms, well within the 16.6ms budget.

For beginner projects (the Acorn target audience), this is more than sufficient.
A project with 200 objects, each running 50 statements per frame, would need
10K statements/frame ≈ 10ms — still within budget.

**Performance escape hatches if needed:**
- Cache parsed ASTs (don't re-parse unchanged event handlers each frame)
- Precompile hot event handlers with `hemlockc --target wasm` server-side
- Move collision detection to JS (already planned)
- Use Web Workers for physics/heavy computation

**Verdict: FEASIBLE** for the target audience. Performance headroom exists.

### 2.5 Block Editor

The spec describes a Scratch-like block editor. Two approaches:

#### Google Blockly (Recommended for Phase 1)
- Mature, well-documented library for visual block programming
- Custom block definitions map cleanly to Hemlock constructs
- Built-in code generation framework (Blockly has a `Generator` class)
- Accessibility support (keyboard nav, screen readers) built in
- Used by Scratch 3.0, MIT App Inventor, MakeCode

**Custom block definition example:**
```javascript
Blockly.Blocks['hemlock_move'] = {
  init: function() {
    this.appendValueInput('STEPS').setCheck('Number').appendField('move');
    this.appendDummyInput().appendField('steps');
    this.setPreviousStatement(true, null);
    this.setNextStatement(true, null);
    this.setColour(230); // Blue - Motion category
  }
};

Blockly.Hemlock['hemlock_move'] = function(block) {
  var steps = Blockly.Hemlock.valueToCode(block, 'STEPS', Blockly.Hemlock.ORDER_NONE);
  return `self.x += ${steps} * cos(self.direction);\nself.y += ${steps} * sin(self.direction);\n`;
};
```

#### Custom Block Editor (Phase 3+)
A custom editor offers more control over the UX (block shapes, animations, Sprig
integration) but is a massive engineering effort. Recommend starting with Blockly
and evaluating a custom editor for v1.5+.

**Verdict: FEASIBLE** — Blockly is a proven solution. Custom later if needed.

### 2.6 Game Runtime

A 2D game runtime needs:

| Feature | Implementation | Complexity |
|---------|---------------|------------|
| Sprite rendering | Canvas 2D `drawImage()` | Low |
| Animation | Frame-based sprite sheets | Low |
| Input handling | `addEventListener` for keys/mouse | Low |
| Collision detection | AABB + circle + pixel-mask | Medium |
| Game loop | `requestAnimationFrame` | Low |
| Camera/viewport | Canvas transform | Low |
| Room management | State machine, instance lists | Medium |
| Audio | Web Audio API | Medium |
| Particle effects | Canvas 2D batch rendering | Medium |
| Tile-based levels | Tile map renderer | Medium |
| Physics (gravity, etc.) | Simple Euler integration | Medium |

No individual component is novel — these are all well-understood game engine
primitives. The total scope is significant but achievable incrementally.

**Verdict: FEASIBLE** — Standard 2D game engine work. No unsolved problems.

### 2.7 Sprite & Room Editors

Both are standard web canvas tools:
- **Sprite editor:** Pixel grid + color picker + layers. Libraries like `piskel`
  or custom Canvas 2D implementation.
- **Room editor:** Object placement on a grid. Drag-and-drop from asset panel.

**Verdict: FEASIBLE** — Well-understood web application features.

### 2.8 Split View (Blocks + Code)

Blockly provides a `workspaceToCode()` method that generates source code from blocks.
The bidirectional highlighting (hover block → highlight code, and vice versa) requires:

1. **Block→Code:** Blockly's code generator with source map annotations
2. **Code→Block:** Parsing the Hemlock AST back to block positions (harder)

For Phase 1, only Block→Code highlighting is needed. Full bidirectional mapping
is a Phase 3 feature.

**Verdict: FEASIBLE** for one-way (block→code). Bidirectional is hard but achievable.

### 2.9 Project Sharing & WASM Bundles

Since the entire runtime (Hemlock WASM interpreter + game runtime JS + user project)
is client-side, sharing is straightforward:
- Project files are JSON (`.acorn`)
- The runtime is a fixed set of JS + WASM files
- A shared project = runtime files + project JSON, served from a CDN
- No server-side compilation needed

**Verdict: FEASIBLE** — Static hosting only. No backend required for execution.

---

## 3. Risk Assessment

### High Risk

| Risk | Impact | Mitigation |
|------|--------|------------|
| Interpreter state persistence | Blocks all game features | Implement Option B early (persistent env). Option A as fallback. |
| Per-frame parse overhead | 60fps performance | AST caching — parse event handlers once, re-execute cached ASTs |
| Block↔Code bidirectional sync | Split view quality | Phase 1: one-way only. Add reverse mapping incrementally. |

### Medium Risk

| Risk | Impact | Mitigation |
|------|--------|------------|
| Blockly customization limits | UX doesn't match spec | Blockly is highly extensible. Evaluate early in Phase 1. |
| WASM bundle size | Slow initial load | Hemlock WASM interpreter is ~2-3MB. Acceptable with caching. |
| Audio cross-browser compat | Sound doesn't work everywhere | Web Audio API is well-supported. Use AudioContext resume on user gesture. |
| Memory leaks in long sessions | Browser tab crashes | Hemlock's ref counting handles this. Monitor in game runtime JS. |

### Low Risk

| Risk | Impact | Mitigation |
|------|--------|------------|
| Canvas 2D performance | Low FPS | Target audience makes simple games. Canvas 2D handles hundreds of sprites. |
| IndexedDB for offline | Projects lost | Standard API, well-supported. Add export as backup. |
| Emscripten compatibility | WASM build breaks | Emscripten is mature. Pin SDK version. |

---

## 4. Required Hemlock Changes

These are modifications to the Hemlock codebase itself (not the Acorn web app).

### 4.1 Persistent Interpreter Context (Critical)

**File:** `src/backends/interpreter/wasm_interp_main.c`
**Scope:** ~200 lines of new C code

Add new exported WASM functions for persistent evaluation contexts:

```c
// Context pool (simple fixed array for Phase 1)
#define MAX_CONTEXTS 16

typedef struct {
    int active;
    Environment *env;
    ExecutionContext *exec_ctx;
} HemlockContext;

static HemlockContext contexts[MAX_CONTEXTS];

EMSCRIPTEN_KEEPALIVE
int hemlock_context_create(void) {
    for (int i = 0; i < MAX_CONTEXTS; i++) {
        if (!contexts[i].active) {
            contexts[i].active = 1;
            contexts[i].env = env_new(NULL);
            contexts[i].exec_ctx = exec_context_new();
            register_builtins(contexts[i].env, 0, NULL, contexts[i].exec_ctx);
            return i;
        }
    }
    return -1;  // No free contexts
}

EMSCRIPTEN_KEEPALIVE
int hemlock_context_eval(int handle, const char *source) {
    if (handle < 0 || handle >= MAX_CONTEXTS || !contexts[handle].active) return -1;
    // Parse, resolve, optimize, execute in persistent environment
    // ...same as run_source() but using contexts[handle].env
    return 0;
}

EMSCRIPTEN_KEEPALIVE
void hemlock_context_destroy(int handle) {
    if (handle < 0 || handle >= MAX_CONTEXTS || !contexts[handle].active) return;
    exec_context_free(contexts[handle].exec_ctx);
    env_break_cycles(contexts[handle].env);
    env_release(contexts[handle].env);
    contexts[handle].active = 0;
}
```

### 4.2 AST Caching for Event Handlers (Performance)

**File:** `src/backends/interpreter/wasm_interp_main.c`
**Scope:** ~150 lines of new C code

Cache parsed + resolved + optimized ASTs for event handler strings that don't
change between frames. Only re-parse when the user edits blocks.

```c
EMSCRIPTEN_KEEPALIVE
int hemlock_compile_script(const char *source);  // Returns script handle

EMSCRIPTEN_KEEPALIVE
int hemlock_run_script(int ctx_handle, int script_handle);  // Execute cached AST

EMSCRIPTEN_KEEPALIVE
void hemlock_free_script(int script_handle);  // Release cached AST
```

### 4.3 Custom Builtins Registration (Game Runtime Bridge)

**File:** `src/backends/interpreter/wasm_interp_main.c`
**Scope:** ~100 lines of new C code

Allow the JS game runtime to register custom builtins that Hemlock code can call.
These bridge functions use `EM_JS` to call back into JavaScript.

```c
// Acorn game runtime builtins registered into the Hemlock environment
// These call back to JS via EM_JS macros

EM_JS(double, _acorn_get_instance_var, (int instance_id, const char* name), {
    return window._acornRuntime.getVar(instance_id, UTF8ToString(name));
});

EM_JS(void, _acorn_set_instance_var, (int instance_id, const char* name, double val), {
    window._acornRuntime.setVar(instance_id, UTF8ToString(name), val);
});

EM_JS(int, _acorn_create_instance, (const char* object_name, double x, double y), {
    return window._acornRuntime.createInstance(UTF8ToString(object_name), x, y);
});

EM_JS(void, _acorn_destroy_instance, (int instance_id), {
    window._acornRuntime.destroyInstance(instance_id);
});

EM_JS(int, _acorn_collision_check, (int instance_id, const char* object_name), {
    return window._acornRuntime.checkCollision(instance_id, UTF8ToString(object_name));
});

EM_JS(void, _acorn_play_sound, (const char* sound_name), {
    window._acornRuntime.playSound(UTF8ToString(sound_name));
});

EM_JS(void, _acorn_draw_sprite, (const char* sprite, double x, double y), {
    window._acornRuntime.drawSprite(UTF8ToString(sprite), x, y);
});
```

These get registered as Hemlock builtins so user code can call:
```hemlock
instance_create(Enemy, 100, 200);
collision_check(self, Wall);
audio_play(snd_explosion);
```

### 4.4 Makefile Target

**File:** `Makefile`
**Scope:** ~20 lines

Add an `acorn-runtime` target that builds the WASM interpreter with Acorn-specific
builtins and exported functions:

```makefile
acorn-runtime: wasm-interpreter
	@echo "Acorn WASM runtime built"
```

### 4.5 Summary of Hemlock Changes

| Change | File | Lines | Priority |
|--------|------|-------|----------|
| Persistent context API | `wasm_interp_main.c` | ~200 | **P0** (blocks everything) |
| AST caching | `wasm_interp_main.c` | ~150 | P1 (performance) |
| Game runtime builtins | `wasm_interp_main.c` + new `wasm_acorn_bridge.c` | ~300 | **P0** (blocks game runtime) |
| Makefile target | `Makefile` | ~20 | P2 (convenience) |
| **Total** | | **~670 lines C** | |

---

## 5. Implementation Plan

### Phase 0 — Hemlock WASM Extensions (Weeks 1-3)

**Goal:** Extend the WASM interpreter to support persistent state and game runtime hooks.

**Deliverables:**
1. `hemlock_context_create/eval/destroy` API in `wasm_interp_main.c`
2. `hemlock_compile_script/run_script` for AST caching
3. `wasm_acorn_bridge.c` with EM_JS game runtime callbacks
4. Updated Makefile with `acorn-runtime` target
5. Node.js tests validating persistent context behavior

**Why first:** Everything in Acorn depends on being able to execute Hemlock code
persistently in the browser. This is the foundation.

### Phase 1 — Minimal Viable Editor (Weeks 4-10)

**Goal:** A child can open Acorn, drag blocks, and see a sprite move on screen.

**Components:**

1. **Web App Scaffold** (Week 4)
   - TypeScript project with build system (Vite or similar)
   - Panel layout: toolbar, asset panel, workspace, properties
   - Load Hemlock WASM interpreter
   - Verify `hemlock_eval()` works in the app

2. **Block Editor Integration** (Weeks 5-6)
   - Integrate Google Blockly
   - Define block categories: Motion, Control, Events, Variables, Math
   - Implement Hemlock code generator for each block
   - Real-time code preview panel (Blocks → Hemlock source)

3. **Game Runtime — Core** (Weeks 6-8)
   - Canvas 2D rendering loop (`requestAnimationFrame`)
   - Sprite loading and drawing
   - Instance management (create, destroy, update)
   - Event dispatch: Create, Step, Key Down/Pressed/Released
   - Bridge: JS runtime ↔ Hemlock interpreter via persistent context
   - Basic input handling (keyboard)

4. **Asset Management** (Weeks 8-9)
   - Built-in sprite picker (ship with 20-30 starter sprites)
   - Simple sprite import (PNG upload)
   - Basic room editor (single room, place instances on grid)

5. **Project Save/Load** (Week 9-10)
   - `.acorn` JSON format
   - LocalStorage/IndexedDB persistence
   - New/Open/Save in toolbar

**Phase 1 deliverables:**
- Working block editor with 5 categories (~40 blocks)
- Single-room game execution at 60fps
- Keyboard input → character movement
- Project save/load to browser storage
- "Hello World" tutorial: drag 3 blocks, see sprite move

### Phase 2 — Game Engine Features (Weeks 11-18)

**Goal:** Users can make simple but complete games (Pong, maze, Space Invaders).

1. **Collision System** (Weeks 11-12)
   - AABB collision detection (rectangle masks)
   - Circle collision detection
   - Collision events dispatched to Hemlock
   - `collision_check(self, ObjectType)` builtin

2. **Complete Block Set** (Weeks 12-14)
   - Appearance blocks (set sprite, animate, visibility, size, tint)
   - Sound blocks (play, stop, volume, loop)
   - Drawing blocks (shapes, text, colors)
   - Object blocks (create/destroy instance, instance count)
   - Room blocks (go to room, room dimensions)

3. **Multi-Room Support** (Week 14-15)
   - Room transitions (go to room, restart room)
   - Persistent objects (survive room changes)
   - Per-room backgrounds and dimensions

4. **Sprite Editor** (Weeks 15-16)
   - Pixel grid canvas with drawing tools
   - Color palette and custom colors
   - Animation frames with preview
   - Onion skinning for frame-by-frame animation

5. **Room Editor Improvements** (Week 16-17)
   - Snap-to-grid with configurable size
   - Layer management (background, gameplay, foreground)
   - Instance property overrides

6. **Sound System** (Week 17)
   - Web Audio API integration
   - Sound import (WAV, MP3, OGG)
   - Playback controls (play, stop, loop, volume)

7. **Debug Tools** (Week 18)
   - Output console with Sprig-styled messages
   - Instance inspector (click to see variables)
   - Slow motion slider
   - Collision mask overlay

8. **Alarms/Timers** (Week 18)
   - Per-instance alarm events
   - Countdown-based timer dispatch

**Phase 2 deliverables:**
- All 10 block categories implemented (~120 blocks)
- Collision detection working
- Multi-room games with transitions
- Built-in sprite editor and room editor
- Sound playback
- Debug tools (console, inspector, slow-mo)
- Tutorials 1-5 completable

### Phase 3 — Split View & Community (Weeks 19-28)

**Goal:** Bridge from visual to text programming. Enable sharing.

1. **Split View** (Weeks 19-21)
   - Side-by-side: blocks (left) + generated Hemlock (right)
   - Block→Code highlighting (hover block → highlight corresponding lines)
   - Syntax-highlighted Hemlock display
   - "Show Code" tooltip on right-click any block

2. **Sprig the Squirrel** (Weeks 21-22)
   - Animated mascot SVGs/sprites for 9 emotes
   - Context-sensitive appearance (errors, success, idle, tutorials)
   - Speech bubble dialogue system
   - Tutorial integration (pointing, explaining)

3. **Tutorial System** (Weeks 22-24)
   - Step-by-step guided tutorials (spec tutorials 1-7)
   - Achievement system with acorn badges
   - Progress tracking in IndexedDB

4. **Project Sharing** (Weeks 24-26)
   - Export `.acorn` file for offline sharing
   - Publish to URL (static hosting: project JSON + runtime bundle)
   - Embed code generation (iframe)
   - Project gallery page (static site with user submissions)

5. **Code-Only Mode** (Weeks 26-28)
   - Full text editor (Monaco/CodeMirror) with Hemlock syntax highlighting
   - Hemlock LSP integration for autocomplete and diagnostics
   - Export as standalone Hemlock project (`.hml` files + `acorn_runtime.hml` library)

**Phase 3 deliverables:**
- Three workspace modes: Blocks, Split View, Code Only
- Sprig mascot integrated throughout
- 7 guided tutorials with achievement system
- Project sharing via URL and embed
- Export to standalone Hemlock project

### Phase 4 — Polish & Expansion (Weeks 29+)

**Goal:** Production quality. Classroom features. Mobile support.

1. **Classroom Features** — Teacher accounts, assignments, progress dashboard
2. **Accessibility** — Keyboard navigation, screen reader, high contrast, dyslexia font
3. **Mobile/Tablet** — Touch support for block editing and gameplay
4. **Performance** — AST caching optimization, WebGL renderer for complex scenes
5. **Physics** — Simple physics engine (gravity, friction, elastic collisions)
6. **Localization** — Multi-language block labels and tutorials
7. **Community Gallery** — Moderated project sharing, remixing, likes/comments
8. **Custom Blocks** — Advanced users can create reusable block libraries

---

## 6. Technology Stack

| Layer | Technology | Rationale |
|-------|-----------|-----------|
| Block Editor | Google Blockly | Proven, accessible, extensible, MIT license |
| Web Framework | Vanilla TS + Lit/Preact | Lightweight. No heavy framework needed. |
| Build Tool | Vite | Fast dev server, ESM-native, good WASM support |
| Game Canvas | Canvas 2D API | Simple, sufficient for 2D, universal browser support |
| Audio | Web Audio API | Standard, low-latency, good browser support |
| Code Editor | CodeMirror 6 | Lightweight, extensible, LSP-compatible |
| Sprite Editor | Custom Canvas 2D | Simple enough to build; no good library exists for this exact need |
| Storage | IndexedDB (via idb-keyval) | Offline-first, large storage capacity |
| Deployment | Static files (CDN) | No server needed for game execution |
| Interpreter | Hemlock WASM (Emscripten) | Already built and working |

**Estimated bundle sizes:**
- Hemlock WASM interpreter: ~2-3 MB (compressed ~800KB)
- Blockly: ~800KB (compressed ~250KB)
- CodeMirror: ~300KB (compressed ~100KB)
- Game runtime + editor: ~200KB
- **Total: ~3.5 MB compressed** — acceptable for a web app with caching

---

## 7. Key Technical Decisions

### Decision 1: WASM Interpreter, NOT WASM Compiler

Use `hemlock_eval()` (tree-walking interpreter in WASM) rather than
`hemlockc --target wasm` (compile to C then to WASM).

**Why:** Instant execution. No server needed. Full language support. Fast iteration.
Compiler path would require a backend server or shipping emcc to the client.

### Decision 2: JS Game Runtime, NOT Hemlock Game Runtime

The game loop, rendering, collision, audio, and input live in TypeScript.
Hemlock only runs user-authored event handler logic.

**Why:** Direct DOM/Canvas access. No EM_JS bridge overhead for rendering.
Better tooling (TypeScript type safety, browser DevTools). Hemlock's strength
is user logic, not graphics plumbing.

### Decision 3: Blockly First, Custom Editor Later

Start with Google Blockly for the block editor. Evaluate a custom implementation
for v1.5+ if Blockly's UX constraints become limiting.

**Why:** Blockly saves months of development. Accessibility built in. Well-documented
custom block API. Scratch 3.0 validated this approach.

### Decision 4: Persistent Context Over Serialization

Extend the WASM interpreter with persistent environments (Option B from section 2.3)
rather than serialize/deserialize state each frame (Option A).

**Why:** Better performance. Natural Hemlock semantics. Closures and complex objects
work correctly. One-time ~200 line C investment.

### Decision 5: Canvas 2D Over WebGL

Use Canvas 2D for rendering, not WebGL.

**Why:** Simpler. Sufficient for the target audience's games (sprites, shapes, text).
WebGL adds complexity for no benefit at this scale. Can upgrade later if needed.

---

## 8. Estimated Scope

| Phase | Duration | New Code | Hemlock Changes |
|-------|----------|----------|----------------|
| Phase 0 (WASM extensions) | 3 weeks | ~670 lines C | Yes |
| Phase 1 (MVP editor) | 7 weeks | ~8K lines TS/JS | No |
| Phase 2 (Game engine) | 8 weeks | ~12K lines TS/JS | No |
| Phase 3 (Split view + community) | 10 weeks | ~10K lines TS/JS | Minor |
| Phase 4 (Polish) | Ongoing | Variable | Minor |

**Total new code for v1.0 (Phases 0-3):** ~670 lines C + ~30K lines TypeScript/JavaScript

---

## 9. Conclusion

Acorn is feasible with the current Hemlock infrastructure. The WASM interpreter
is proven and working. The required Hemlock-side changes are modest (~670 lines of C).
The bulk of the work is a standard web application (block editor, game runtime, asset
tools) using well-understood technologies.

The recommended approach — WASM interpreter + JS game runtime + Blockly — minimizes
risk while delivering the core Acorn experience described in the spec. The persistent
interpreter context (Phase 0) is the single critical-path dependency that should be
implemented first.

**Start with Phase 0. Everything else follows.**
