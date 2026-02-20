# Acorn: Feasibility Analysis & Implementation Plan

> Visual Programming Environment for Hemlock — Browser-based via WebAssembly

**Date:** February 2026
**Status:** Feasibility Complete — Ready for Implementation
**Hemlock Version:** v1.9.0 (WASM interpreter API finalized)
**Spec:** Acorn Draft Specification v0.1

---

## Executive Summary

Acorn is **feasible** with the current Hemlock v1.9.0 infrastructure. The WASM
interpreter runs in the browser with persistent contexts, AST caching, and
programmatic error reporting — all shipped and tested. No further Hemlock changes
are needed. All remaining work is in the Acorn web application (separate repo).

**Risk level:** Low-Medium. The interpreter foundation is complete and proven. The
remaining work is a standard web application: block editor, game runtime, asset tools.

---

## 1. Hemlock WASM API (v1.9.0 — Complete)

The WASM interpreter exports the following API, available as a GitHub release artifact
(`hemlock.js` + `hemlock.wasm`):

### Stateless Evaluation
```javascript
hemlock_eval(source)          // Parse + execute, returns 0 on success
hemlock_version()             // Returns version string
```

### Persistent Context (state survives across calls)
```javascript
hemlock_context_create()                    // Allocate env, return handle
hemlock_context_eval(handle, source)        // Execute in persistent env
hemlock_context_destroy(handle)             // Tear down env
hemlock_context_get(handle, varname)        // Read variable as JSON string
hemlock_context_set(handle, varname, json)  // Inject variable from JSON
hemlock_context_last_error(handle)          // Last error message (or NULL)
```

### AST Caching (compile once, run many)
```javascript
hemlock_compile_script(source)              // Parse+resolve+optimize, return handle
hemlock_run_script(ctx_handle, script_handle) // Execute cached AST in context
hemlock_free_script(script_handle)          // Release cached AST
```

**Limits:** 64 concurrent contexts, 256 cached scripts (configurable in `hemlock_limits.h`).

---

## 2. Architecture

### 2.1 Compilation Pipeline

**Spec says:** `Visual Blocks → Block AST → Hemlock Source → hemlockc (WASM) → Executable WASM → Browser Canvas`

**Actual approach:** Use the **WASM interpreter**, not the WASM compiler. Running
`hemlockc` in the browser would require shipping Emscripten (`emcc`) to the client —
gigabytes of toolchain. The interpreter path is instant and proven:

```
Visual Blocks → Block AST → Hemlock Source → hemlock_context_eval() → Output
                                     ↕
                              (Split View display)
```

Why this works:
1. `hemlock_eval()` is proven in the browser (playground ships with Hemlock)
2. AST caching eliminates re-parsing — event handlers compile once
3. The interpreter supports the full language (159 parity-tested features)
4. Beginner projects (<500 blocks) won't stress interpreter performance
5. Edit-run cycles are sub-second

### 2.2 System Architecture

```
┌─────────────────────────────────────────────────────┐
│  ACORN (TypeScript — separate repo)                 │
│                                                     │
│  ┌──────────────┐  ┌────────────────────────────┐   │
│  │ Block Editor  │  │ Game Runtime (TS)           │   │
│  │ (Blockly)     │  │  - Canvas 2D rendering     │   │
│  │               │  │  - requestAnimationFrame   │   │
│  │  Blocks→AST   │  │  - Input handling          │   │
│  │  AST→Hemlock  │  │  - Collision detection     │   │
│  └──────┬────────┘  │  - Audio (Web Audio)       │   │
│         │           │  - Instance management     │   │
│         ▼           └──────────┬─────────────────┘   │
│  ┌──────────────┐              │                     │
│  │ Generated     │◄────────────┘                     │
│  │ Hemlock Code  │  (state in, event dispatch,       │
│  └──────┬────────┘   state out)                      │
│         │                                            │
│  ┌──────▼────────────────────────────────────────┐   │
│  │ Hemlock WASM Interpreter (prebuilt binary)     │   │
│  │  - hemlock_context_set("state", gameStateJSON) │   │
│  │  - hemlock_run_script(ctx, cachedHandler)      │   │
│  │  - hemlock_context_get("state") → newStateJSON │   │
│  └────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────┘
```

**Key principle:** The game runtime (rendering, collision, audio, input) lives
entirely in TypeScript. Hemlock only runs user-authored event handler logic.
No Acorn-specific code in the Hemlock interpreter.

### 2.3 Per-Frame Execution Model

Each frame, the JS game runtime:

1. **Serializes** all game state into one JSON object
2. **Injects** it via `context_set("state", json)`
3. **Runs** the cached Step handler script via `run_script(ctx, script)`
4. **Reads back** modified state via `context_get("state")`
5. **Checks errors** via `context_last_error(ctx)`
6. **Renders** the updated state to canvas

```javascript
// Setup (once, when user clicks Run or edits blocks)
const ctx = ctxCreate();
const stepScript = compile(`
  for (inst in state.instances) {
    if (inst.type == "Player") {
      inst.x += inst.speed * cos(inst.direction);
    }
    if (inst.type == "Enemy") {
      inst.x -= 2;
    }
  }
`);

// Per frame (60fps)
ctxSet(ctx, "state", JSON.stringify(gameState));
runScript(ctx, stepScript);
gameState = JSON.parse(ctxGet(ctx, "state"));

const err = lastError(ctx);
if (err) showSprigError(err);  // Display friendly error via mascot
```

**Performance budget:** 16.6ms per frame. A game state with 50 instances × 15
properties ≈ 5-10KB JSON. Serialization is sub-millisecond in C. Cached AST
execution skips parsing entirely. Total interpreter overhead: ~1-2ms per frame
for typical beginner projects. Plenty of headroom.

**Discrete events** (key press, collision, alarm) fire outside the frame loop
and use `context_eval()` for one-off handler execution.

### 2.4 Repository Structure

**Hemlock repo** (`hemlang/hemlock`) — no further changes needed:
- Builds WASM interpreter via `make wasm-interpreter`
- Publishes `hemlock.js` + `hemlock.wasm` as GitHub release artifact (v1.9.0+)

**Acorn repo** (`hemlang/acorn`) — all new work lives here:
- TypeScript web application (Vite + Blockly + Canvas 2D)
- Consumes prebuilt WASM binary from Hemlock releases
- `public/wasm/hemlock.{js,wasm}` — pulled from release, gitignored in dev
- CI fetches the matching Hemlock release artifact for builds

```
acorn/
├── public/
│   └── wasm/              # Prebuilt hemlock.{js,wasm} from release
├── src/
│   ├── editor/            # Block editor (Blockly integration)
│   ├── runtime/           # Game runtime (Canvas 2D, input, audio)
│   ├── bridge/            # Hemlock WASM bridge (context wrapper)
│   ├── assets/            # Sprite editor, room editor
│   ├── project/           # Save/load, .acorn format
│   ├── ui/                # Layout panels, toolbar, properties
│   └── mascot/            # Sprig the Squirrel emotes & dialogue
├── assets/                # Starter sprites, sounds, tutorials
└── vite.config.ts
```

---

## 3. Feasibility Verdicts

| Component | Verdict | Notes |
|-----------|---------|-------|
| WASM interpreter in browser | **Ready** | Proven by playground, API finalized in v1.9.0 |
| Persistent state across frames | **Ready** | `context_create/eval/get/set` shipped and tested |
| Per-frame performance | **Feasible** | AST caching + JSON serialization well within 16.6ms |
| Block editor | **Feasible** | Google Blockly is proven (Scratch, MakeCode) |
| Game runtime | **Feasible** | Standard 2D engine primitives, Canvas 2D |
| Sprite/room editors | **Feasible** | Standard web canvas tools |
| Split view (blocks + code) | **Feasible** | Blockly `workspaceToCode()` + source map annotations |
| Project sharing | **Feasible** | Static hosting only, no backend needed |
| Error reporting | **Ready** | `context_last_error()` provides programmatic error messages |

---

## 4. Risk Assessment

### Medium Risk

| Risk | Impact | Mitigation |
|------|--------|------------|
| Block↔Code bidirectional sync | Split view quality | Phase 1: one-way only (block→code). Reverse mapping in Phase 3. |
| Blockly customization limits | UX doesn't match spec | Blockly is highly extensible. Evaluate early. Custom editor in v1.5+ if needed. |
| WASM bundle size | Slow initial load | ~2-3MB uncompressed, ~800KB gzipped. Acceptable with Service Worker caching. |

### Low Risk

| Risk | Impact | Mitigation |
|------|--------|------------|
| Canvas 2D performance | Low FPS for complex games | Target audience makes simple games. Upgrade to WebGL later if needed. |
| Audio cross-browser | Sound issues | Web Audio API is well-supported. AudioContext resume on user gesture. |
| Memory leaks in long sessions | Tab crashes | Hemlock's ref counting + context_destroy. Monitor in JS runtime. |
| IndexedDB for offline storage | Data loss | Standard API. Add .acorn file export as backup. |

---

## 5. Implementation Plan

### Phase 1 — Minimal Viable Editor (Weeks 1-7)

**Goal:** A child can open Acorn, drag blocks, and see a sprite move on screen.

1. **Web App Scaffold** (Week 1)
   - TypeScript project with Vite
   - Panel layout: toolbar, asset panel, workspace, properties
   - Load Hemlock WASM interpreter from prebuilt binary
   - Verify `hemlock_context_eval()` works in the app

2. **Block Editor Integration** (Weeks 2-3)
   - Integrate Google Blockly
   - Define block categories: Motion, Control, Events, Variables, Math
   - Implement Hemlock code generator for each block type
   - Real-time code preview panel (Blocks → Hemlock source)

3. **Game Runtime — Core** (Weeks 3-5)
   - Canvas 2D rendering loop (`requestAnimationFrame`)
   - Sprite loading and drawing
   - Instance management (create, destroy, update)
   - Event dispatch: Create, Step, Key Down/Pressed/Released
   - Bridge: JS game state → `context_set` → `run_script` → `context_get` → JS
   - Basic input handling (keyboard)
   - Error display via `context_last_error()` (Sprig placeholder)

4. **Asset Management** (Weeks 5-6)
   - Built-in sprite picker (ship with 20-30 starter sprites)
   - Simple sprite import (PNG upload)
   - Basic room editor (single room, place instances on grid)

5. **Project Save/Load** (Weeks 6-7)
   - `.acorn` JSON format
   - IndexedDB persistence
   - New/Open/Save in toolbar

**Phase 1 deliverables:**
- Working block editor with 5 categories (~40 blocks)
- Single-room game execution at 60fps
- Keyboard input → character movement
- Project save/load to browser storage
- "Hello World" experience: drag 3 blocks, see sprite move

### Phase 2 — Game Engine Features (Weeks 8-15)

**Goal:** Users can make simple but complete games (Pong, maze, Space Invaders).

1. **Collision System** (Weeks 8-9)
   - AABB collision detection (rectangle masks)
   - Circle collision detection
   - Collision events dispatched through the Hemlock bridge

2. **Complete Block Set** (Weeks 9-11)
   - Appearance blocks (set sprite, animate, visibility, size, tint)
   - Sound blocks (play, stop, volume, loop)
   - Drawing blocks (shapes, text, colors)
   - Object blocks (create/destroy instance, instance count)
   - Room blocks (go to room, room dimensions)

3. **Multi-Room Support** (Weeks 11-12)
   - Room transitions (go to room, restart room)
   - Persistent objects (survive room changes)
   - Per-room backgrounds and dimensions

4. **Sprite Editor** (Weeks 12-13)
   - Pixel grid canvas with drawing tools
   - Color palette and custom colors
   - Animation frames with preview
   - Onion skinning for frame-by-frame animation

5. **Room Editor Improvements** (Week 13-14)
   - Snap-to-grid with configurable size
   - Layer management (background, gameplay, foreground)
   - Instance property overrides

6. **Sound System & Debug Tools** (Weeks 14-15)
   - Web Audio API integration (import WAV/MP3/OGG, play, stop, loop, volume)
   - Output console with Sprig-styled error messages
   - Instance inspector (click to see variables)
   - Slow motion slider
   - Collision mask overlay
   - Per-instance alarm/timer events

**Phase 2 deliverables:**
- All 10 block categories implemented (~120 blocks)
- Collision detection working
- Multi-room games with transitions
- Built-in sprite editor and room editor
- Sound playback
- Debug tools (console, inspector, slow-mo)
- Tutorials 1-5 completable

### Phase 3 — Split View & Community (Weeks 16-25)

**Goal:** Bridge from visual to text programming. Enable sharing.

1. **Split View** (Weeks 16-18)
   - Side-by-side: blocks (left) + generated Hemlock (right)
   - Block→Code highlighting (hover block → highlight corresponding lines)
   - Syntax-highlighted Hemlock display (CodeMirror 6)
   - "Show Code" tooltip on right-click any block

2. **Sprig the Squirrel** (Weeks 18-19)
   - Animated mascot SVGs/sprites for 9 emotes
   - Context-sensitive appearance (errors → Confused, success → Happy, etc.)
   - Speech bubble dialogue system
   - Tutorial integration (pointing, explaining)

3. **Tutorial System** (Weeks 19-21)
   - Step-by-step guided tutorials (spec tutorials 1-7)
   - Achievement system with acorn badges
   - Progress tracking in IndexedDB

4. **Project Sharing** (Weeks 21-23)
   - Export `.acorn` file for offline sharing
   - Publish to URL (static hosting: project JSON + runtime bundle)
   - Embed code generation (iframe)
   - Project gallery page (static site with user submissions)

5. **Code-Only Mode** (Weeks 23-25)
   - Full text editor (CodeMirror 6) with Hemlock syntax highlighting
   - Export as standalone Hemlock project (`.hml` files)

**Phase 3 deliverables:**
- Three workspace modes: Blocks, Split View, Code Only
- Sprig mascot integrated throughout
- 7 guided tutorials with achievement system
- Project sharing via URL and embed
- Export to standalone Hemlock project

### Phase 4 — Polish & Expansion (Weeks 26+)

1. **Classroom Features** — Teacher accounts, assignments, progress dashboard
2. **Accessibility** — Keyboard nav, screen reader, high contrast, dyslexia font
3. **Mobile/Tablet** — Touch support for block editing and gameplay
4. **Performance** — WebGL renderer option for complex scenes
5. **Physics** — Simple physics engine (gravity, friction, elastic collisions)
6. **Localization** — Multi-language block labels and tutorials
7. **Community Gallery** — Moderated sharing, remixing, likes/comments
8. **Custom Blocks** — Advanced users create reusable block libraries

---

## 6. Technology Stack

| Layer | Technology | Rationale |
|-------|-----------|-----------|
| Block Editor | Google Blockly | Proven, accessible, extensible, MIT license |
| Web Framework | Vanilla TS + Lit/Preact | Lightweight, no heavy framework needed |
| Build Tool | Vite | Fast dev server, ESM-native, good WASM support |
| Game Canvas | Canvas 2D API | Simple, sufficient for 2D, universal support |
| Audio | Web Audio API | Standard, low-latency |
| Code Editor | CodeMirror 6 | Lightweight, extensible (Phase 3) |
| Sprite Editor | Custom Canvas 2D | Simple enough to build custom |
| Storage | IndexedDB (via idb-keyval) | Offline-first, large capacity |
| Deployment | Static files (CDN) | No backend needed for game execution |
| Interpreter | Hemlock WASM v1.9.0 | Prebuilt binary from GitHub release |

**Estimated bundle sizes:**
- Hemlock WASM interpreter: ~2-3 MB (compressed ~800KB)
- Blockly: ~800KB (compressed ~250KB)
- CodeMirror: ~300KB (compressed ~100KB, Phase 3 only)
- Game runtime + editor: ~200KB
- **Total: ~3.5 MB compressed** — acceptable with Service Worker caching

---

## 7. Key Technical Decisions

### Decision 1: WASM Interpreter, NOT WASM Compiler

Use the tree-walking interpreter compiled to WASM, not `hemlockc --target wasm`.

**Why:** Instant execution. No server needed. Full language support.
The compiler path would require shipping Emscripten to the client.

### Decision 2: JS Game Runtime, NOT Hemlock Game Runtime

Rendering, collision, audio, and input live in TypeScript. Hemlock runs user logic only.

**Why:** Direct DOM/Canvas access. Better tooling (TypeScript, browser DevTools).
No Acorn-specific code leaking into the Hemlock interpreter.

### Decision 3: JSON State Serialization Per Frame

Game state is serialized as one JSON object, passed to `context_set`, modified by
cached Hemlock scripts, and read back via `context_get`.

**Why:** Clean boundary. No Acorn builtins in the interpreter. Serialization cost
is negligible for beginner project sizes (~5-10KB JSON in sub-millisecond C parsing).
AST caching eliminates the parsing bottleneck.

### Decision 4: Blockly First, Custom Editor Later

Start with Google Blockly. Evaluate a custom implementation for v1.5+.

**Why:** Blockly saves months of development. Accessibility built in.
Scratch 3.0 and MakeCode validated this approach.

### Decision 5: Canvas 2D Over WebGL

Use Canvas 2D for rendering.

**Why:** Simpler. Sufficient for the target audience (sprites, shapes, text).
WebGL adds complexity for no benefit at this scale. Upgrade path exists for Phase 4.

### Decision 6: Separate Repository

Acorn lives in its own repo (`hemlang/acorn`), consuming the prebuilt Hemlock WASM
binary from GitHub releases.

**Why:** Acorn is ~30K lines of TypeScript — it would dwarf the Hemlock C codebase.
Different build systems (Vite vs Make), different contributors, different release cycles.
The WASM binary is the stable interface between the two.

---

## 8. Estimated Scope

| Phase | Duration | New Code (TS/JS) | Hemlock Changes |
|-------|----------|-----------------|-----------------|
| Phase 1 (MVP editor) | 7 weeks | ~8K lines | None |
| Phase 2 (Game engine) | 8 weeks | ~12K lines | None |
| Phase 3 (Split view + community) | 10 weeks | ~10K lines | None |
| Phase 4 (Polish) | Ongoing | Variable | None |

**Total new code for v1.0 (Phases 1-3):** ~30K lines TypeScript/JavaScript

---

## 9. Conclusion

All Hemlock-side prerequisites are complete as of v1.9.0. The WASM interpreter
provides persistent contexts, AST caching, JSON state exchange, and programmatic
error reporting — everything Acorn needs to run user code in a game loop.

The remaining work is entirely in the Acorn web application: block editor (Blockly),
game runtime (Canvas 2D + TypeScript), asset tools, and UI. No unsolved technical
problems. Standard web application development using proven libraries.

**Next step: Create the Acorn repository and start Phase 1.**
