# Acorn Phase 1 — Detailed Implementation Plan

> Minimal Viable Editor: A child drags blocks, sees a sprite move.

**Duration:** ~7 weeks
**Output:** ~8K lines TypeScript
**Hemlock Changes:** None (consumes v1.9.0 WASM binary)

---

## 1. Project Setup (Week 1)

### 1.1 Scaffold

```bash
npm create vite@latest acorn -- --template vanilla-ts
cd acorn
npm install blockly idb-keyval
npm install -D vitest @types/node
```

### 1.2 Directory Structure

```
acorn/
├── public/
│   ├── wasm/
│   │   ├── hemlock.js           # From Hemlock v1.9.0 release
│   │   └── hemlock.wasm         # From Hemlock v1.9.0 release
│   └── sprites/                 # Starter sprite PNGs
├── src/
│   ├── main.ts                  # Entry point, panel layout, app init
│   ├── bridge/
│   │   └── hemlock.ts           # TypeScript wrapper around WASM API
│   ├── editor/
│   │   ├── workspace.ts         # Blockly workspace setup & config
│   │   ├── generator.ts         # Hemlock code generator
│   │   ├── blocks/
│   │   │   ├── events.ts        # Event blocks (on_create, on_step, on_key_*)
│   │   │   ├── motion.ts        # Motion blocks (set_x, move, etc.)
│   │   │   ├── control.ts       # Control flow (if/else, repeat, while)
│   │   │   ├── variables.ts     # Variable get/set/change
│   │   │   └── math.ts          # Math, logic, comparisons
│   │   └── toolbox.ts           # Category definitions & colors
│   ├── runtime/
│   │   ├── engine.ts            # Game loop, frame dispatch
│   │   ├── instance.ts          # GameInstance type & helpers
│   │   ├── input.ts             # Keyboard state tracking
│   │   ├── renderer.ts          # Canvas 2D sprite drawing
│   │   └── state.ts             # GameState type, serialization
│   ├── assets/
│   │   ├── sprites.ts           # Sprite loading, starter sprite manifest
│   │   └── room.ts              # Room editor (place instances on grid)
│   ├── project/
│   │   ├── format.ts            # .acorn JSON schema & types
│   │   ├── storage.ts           # IndexedDB save/load via idb-keyval
│   │   └── starter.ts           # Default "new project" template
│   └── ui/
│       ├── layout.ts            # Panel layout (toolbar, sidebar, workspace)
│       ├── toolbar.ts           # New/Open/Save/Run/Stop buttons
│       ├── sidebar.ts           # Object list, sprite picker
│       ├── properties.ts        # Selected object properties panel
│       └── console.ts           # Output log + error display
├── assets/
│   └── sprites/                 # Source sprite PNGs (copied to public/ at build)
├── index.html
├── vite.config.ts
└── tsconfig.json
```

### 1.3 Panel Layout

```
┌──────────────────────────────────────────────────────────┐
│  Toolbar: [New] [Open] [Save] | [▶ Run] [■ Stop] | Acorn│
├────────────┬─────────────────────────┬───────────────────┤
│  Sidebar   │  Workspace              │  Properties       │
│            │                         │                   │
│  Objects:  │  (Block Editor)         │  Selected: Player │
│  ● Player  │   ┌──────────────┐      │  Sprite: spr_hero │
│  ● Enemy   │   │ on step      │      │  x: 100           │
│  ● Coin    │   │  change x: 5 │      │  y: 200           │
│            │   └──────────────┘      │                   │
│  Sprites:  │                         │  Events:          │
│  [grid of  │                         │  ● Create         │
│   thumbs]  │                         │  ● Step ← editing │
│            │                         │  ○ Key Down       │
│  Room:     │                         │                   │
│  [minimap] │                         │                   │
├────────────┴─────────────────────────┴───────────────────┤
│  Console: Ready. | "self.x is now 105"                   │
└──────────────────────────────────────────────────────────┘
```

Implemented as CSS grid/flexbox. No framework — vanilla DOM with TypeScript.
Workspace area switches between Block Editor (edit mode) and Game Canvas (play mode).

### 1.4 WASM Bridge

`src/bridge/hemlock.ts` — typed wrapper around the Emscripten module:

```typescript
export class HemlockBridge {
  private module: any;             // Emscripten Module
  private cwrap: any;

  // Wrapped WASM functions (initialized in load())
  private _ctxCreate: () => number;
  private _ctxEval: (handle: number, source: string) => number;
  private _ctxDestroy: (handle: number) => void;
  private _ctxGet: (handle: number, varname: string) => string | null;
  private _ctxSet: (handle: number, varname: string, json: string) => void;
  private _ctxLastError: (handle: number) => string | null;
  private _compileScript: (source: string) => number;
  private _runScript: (ctxHandle: number, scriptHandle: number) => number;
  private _freeScript: (scriptHandle: number) => void;

  async load(): Promise<void> {
    // Load hemlock.js, wait for WASM ready
    // cwrap all exported functions
  }

  contextCreate(): number;
  contextEval(handle: number, source: string): number;
  contextDestroy(handle: number): void;
  contextGet(handle: number, varname: string): any;     // JSON.parse internally
  contextSet(handle: number, varname: string, value: any): void;  // JSON.stringify
  contextLastError(handle: number): string | null;
  compileScript(source: string): number;
  runScript(ctxHandle: number, scriptHandle: number): number;
  freeScript(scriptHandle: number): void;
}
```

`contextGet` returns parsed JSON. `contextSet` accepts any JS value and stringifies.
This keeps the serialization boundary in one place.

---

## 2. Block Editor (Weeks 2-3)

### 2.1 Hemlock Code Generator

Extends Blockly's `CodeGenerator` class. Registered as `Blockly.Hemlock`.

Key differences from Blockly's built-in JS generator:
- Semicolons on every statement
- `let` instead of `var`
- No ASI assumptions
- Event blocks produce labeled sections (not executable on their own)

### 2.2 Block Categories (Phase 1 — ~40 blocks)

#### Events (Yellow, 5 blocks)

These are top-level "hat" blocks that define which event handler the code belongs to.
Each object type can have one block stack per event type.

| Block | Generated Hemlock | Notes |
|-------|------------------|-------|
| `on create` | *(section marker)* | Runs once when instance is placed |
| `on step` | *(section marker)* | Runs every frame |
| `on key down [key▼]` | `if (input_key_down("ArrowRight"))` | While held |
| `on key pressed [key▼]` | `if (input_key_pressed("ArrowRight"))` | Single frame |
| `on key released [key▼]` | `if (input_key_released("ArrowRight"))` | Single frame |

Key dropdown includes: Arrow keys, WASD, Space, Enter, common letters.

#### Motion (Blue, 8 blocks)

| Block | Generated Hemlock |
|-------|------------------|
| `set x to (val)` | `self.x = {val};` |
| `set y to (val)` | `self.y = {val};` |
| `change x by (val)` | `self.x = self.x + {val};` |
| `change y by (val)` | `self.y = self.y + {val};` |
| `set direction to (deg)` | `self.direction = {deg};` |
| `move forward (steps)` | `self.x = self.x + {steps} * cos(self.direction); self.y = self.y + {steps} * sin(self.direction);` |
| `set speed to (val)` | `self.speed = {val};` |
| `go to x: (x) y: (y)` | `self.x = {x}; self.y = {y};` |

`self` is bound per-instance by the runtime (see section 3.3).

#### Control (Orange, 7 blocks)

| Block | Generated Hemlock |
|-------|------------------|
| `if <cond> then` | `if ({cond}) { ... }` |
| `if <cond> then / else` | `if ({cond}) { ... } else { ... }` |
| `repeat (n) times` | `for (let __i = 0; __i < {n}; __i++) { ... }` |
| `while <cond>` | `while ({cond}) { ... }` |
| `print (val)` | `print({val});` |

#### Variables (Red, 5 blocks)

| Block | Generated Hemlock |
|-------|------------------|
| `set [var▼] to (val)` | `self.vars.{var} = {val};` |
| `change [var▼] by (val)` | `self.vars.{var} = self.vars.{var} + {val};` |
| `[var▼]` (getter) | `self.vars.{var}` |
| `set global [var▼] to (val)` | `state.globals.{var} = {val};` |
| `global [var▼]` (getter) | `state.globals.{var}` |

User-defined variables are stored on `self.vars` (per-instance) or `state.globals`
(shared). This avoids polluting the top-level Hemlock scope.

#### Math & Logic (Green, 15 blocks)

| Block | Generated Hemlock |
|-------|------------------|
| `(number)` | `{n}` |
| `(a) [+▼] (b)` | `{a} + {b}` (dropdown: +, -, *, /) |
| `(a) [<▼] (b)` | `{a} < {b}` (dropdown: <, >, ==, !=, <=, >=) |
| `<a> [and▼] <b>` | `{a} && {b}` (dropdown: and, or) |
| `not <a>` | `!{a}` |
| `(string)` | `"{s}"` |
| `random (min) to (max)` | `rand_range({min}, {max})` |
| `self.x` | `self.x` |
| `self.y` | `self.y` |
| `self.direction` | `self.direction` |
| `self.speed` | `self.speed` |
| `self.type` | `self.type` |
| `room width` | `state.room.width` |
| `room height` | `state.room.height` |
| `(a) mod (b)` | `{a} % {b}` |

### 2.3 Code Generation Flow

When the user edits blocks, Blockly fires a workspace change event. The generator:

1. Walks the workspace for the currently selected (object, event) pair
2. Produces a Hemlock source string for that event handler
3. Displays it in the code preview panel (bottom or right)
4. If the game is running, recompiles the cached script (debounced ~300ms)

The runtime assembles per-object event code into a full dispatch script (section 3.3).

### 2.4 Example: Blocks → Hemlock

User drags these blocks for the Player object's Step event:

```
[on step]
  [if <key down [ArrowRight]> then]
    [change x by (5)]
  [if <key down [ArrowLeft]> then]
    [change x by (-5)]
```

Generated Hemlock (shown in code preview):
```hemlock
if (input_key_down("ArrowRight")) {
    self.x = self.x + 5;
}
if (input_key_down("ArrowLeft")) {
    self.x = self.x + -5;
}
```

---

## 3. Game Runtime (Weeks 3-5)

### 3.1 Core Types

```typescript
// src/runtime/instance.ts
interface GameInstance {
  id: number;              // Unique, auto-incrementing
  type: string;            // Object type name ("Player", "Enemy")
  x: number;
  y: number;
  sprite: string;          // Sprite asset key ("spr_player")
  direction: number;       // Degrees (0 = right, 90 = down)
  speed: number;
  visible: boolean;
  vars: Record<string, any>;  // User-defined per-instance variables
}

// src/runtime/state.ts
interface GameState {
  instances: GameInstance[];
  globals: Record<string, any>;
  input: InputState;
  room: {
    width: number;
    height: number;
    background: string;    // CSS color
  };
  _create_queue: string[]; // Instance IDs needing Create event
}

interface InputState {
  keys_down: string[];     // Currently held keys
  keys_pressed: string[];  // Pressed this frame (single-fire)
  keys_released: string[]; // Released this frame (single-fire)
}
```

### 3.2 Game Loop

```typescript
// src/runtime/engine.ts
class GameEngine {
  bridge: HemlockBridge;
  ctx: number;                          // Hemlock context handle
  scripts: Map<string, number>;         // "step" → script handle
  state: GameState;
  canvas: HTMLCanvasElement;
  renderer: Renderer;
  input: InputManager;
  running: boolean;
  animFrameId: number;

  start() {
    this.ctx = this.bridge.contextCreate();
    this.compileAllScripts();
    this.running = true;
    this.loop();
  }

  stop() {
    this.running = false;
    cancelAnimationFrame(this.animFrameId);
    this.freeAllScripts();
    this.bridge.contextDestroy(this.ctx);
  }

  private loop() {
    if (!this.running) return;
    this.frame();
    this.animFrameId = requestAnimationFrame(() => this.loop());
  }

  private frame() {
    // 1. Snapshot input
    this.state.input = this.input.snapshot();

    // 2. Dispatch Create events for new instances
    this.dispatchEvent("create");

    // 3. Dispatch Step event
    this.dispatchEvent("step");

    // 4. Check for errors
    const err = this.bridge.contextLastError(this.ctx);
    if (err) this.showError(err);

    // 5. Render
    this.renderer.draw(this.state);

    // 6. End-of-frame cleanup
    this.input.endFrame();
  }
}
```

### 3.3 Event Dispatch — The Bridge

This is the core of the Hemlock integration. For each event type, the runtime
assembles a single Hemlock script from all object types' block-generated code,
compiles it once, and re-executes it each frame.

**Script assembly for the Step event:**

Given:
- Player has Step blocks: `self.x = self.x + 5;`
- Enemy has Step blocks: `self.x = self.x - 2;`

The runtime assembles:

```hemlock
// Auto-generated dispatch script for "step" event
for (let __i = 0; __i < len(state.instances); __i++) {
    let self = state.instances[__i];

    if (self.type == "Player") {
        // --- Player step (from blocks) ---
        self.x = self.x + 5;
    }

    if (self.type == "Enemy") {
        // --- Enemy step (from blocks) ---
        self.x = self.x - 2;
    }
}
```

**Why this works:** `context_set("state", json)` creates a Hemlock object tree.
Array elements are object references, so `let self = state.instances[__i]; self.x = 5;`
mutates the original in-place. After `run_script`, `context_get("state")` returns
the modified tree. No write-back needed.

**For input events (key_down, key_pressed, key_released):**

```hemlock
// Auto-generated dispatch for "key_down" event
for (let __i = 0; __i < len(state.instances); __i++) {
    let self = state.instances[__i];

    if (self.type == "Player") {
        // --- Player key_down blocks ---
        if (state.input.keys_down.contains("ArrowRight")) {
            self.x = self.x + 5;
        }
    }
}
```

Input-checking blocks (`on key down [key]`) generate `if` guards inside the
loop body. The runtime doesn't need separate scripts per key — one key_down
dispatch script handles all keys.

**Helper functions** injected into the context at creation:

```hemlock
// Injected once via context_eval when context is created
fn input_key_down(key) {
    return state.input.keys_down.contains(key);
}
fn input_key_pressed(key) {
    return state.input.keys_pressed.contains(key);
}
fn input_key_released(key) {
    return state.input.keys_released.contains(key);
}
fn rand_range(min, max) {
    return min + (rand() * (max - min));
}
fn cos(deg) {
    import { cos as _cos, PI } from "@stdlib/math";
    return _cos(deg * PI / 180);
}
fn sin(deg) {
    import { sin as _sin, PI } from "@stdlib/math";
    return _sin(deg * PI / 180);
}
```

These are defined once in the persistent context, not re-injected each frame.

**Script recompilation:** When the user edits blocks:
1. Code generator produces new Hemlock source for the affected event
2. Runtime calls `freeScript(oldHandle)`
3. Runtime reassembles the dispatch script with the new block code
4. Runtime calls `compileScript(newSource)` → new handle
5. Next frame uses the new cached script

Debounced at ~300ms so rapid block dragging doesn't cause excessive recompilation.

### 3.4 Input Manager

```typescript
// src/runtime/input.ts
class InputManager {
  private down = new Set<string>();
  private justPressed = new Set<string>();
  private justReleased = new Set<string>();

  constructor(target: HTMLElement) {
    target.addEventListener("keydown", (e) => {
      if (!this.down.has(e.key)) {
        this.justPressed.add(e.key);
      }
      this.down.add(e.key);
      e.preventDefault();
    });
    target.addEventListener("keyup", (e) => {
      this.down.delete(e.key);
      this.justReleased.add(e.key);
      e.preventDefault();
    });
  }

  snapshot(): InputState {
    return {
      keys_down: [...this.down],
      keys_pressed: [...this.justPressed],
      keys_released: [...this.justReleased],
    };
  }

  endFrame() {
    this.justPressed.clear();
    this.justReleased.clear();
  }
}
```

### 3.5 Renderer

```typescript
// src/runtime/renderer.ts
class Renderer {
  private ctx: CanvasRenderingContext2D;
  private sprites: Map<string, HTMLImageElement>;

  draw(state: GameState) {
    const { width, height, background } = state.room;
    this.ctx.fillStyle = background;
    this.ctx.fillRect(0, 0, width, height);

    for (const inst of state.instances) {
      if (!inst.visible) continue;
      const img = this.sprites.get(inst.sprite);
      if (!img) continue;

      this.ctx.save();
      this.ctx.translate(inst.x, inst.y);
      this.ctx.rotate(inst.direction * Math.PI / 180);
      this.ctx.drawImage(img, -img.width / 2, -img.height / 2);
      this.ctx.restore();
    }
  }

  async loadSprite(key: string, url: string): Promise<void> {
    const img = new Image();
    img.src = url;
    await img.decode();
    this.sprites.set(key, img);
  }
}
```

Origin is center of sprite. Direction 0 = right, 90 = down (standard game math).

### 3.6 Error Handling

After each `run_script`, check `context_last_error()`. If non-null:

1. Pause the game (stop the loop)
2. Display the error in the console panel
3. Highlight the relevant block (if source mapping exists) — stretch goal for Phase 1,
   good-enough fallback is just showing the error string
4. Sprig placeholder: red text with a sad-face icon. Full mascot is Phase 3.

Common errors the user will hit:
- Undefined variable (typo in block variable name)
- Type error (comparing string to number)
- Division by zero

Errors should be kid-friendly. The console can transform known patterns:
- `"undefined variable 'spd'"` → `"Oops! The variable 'spd' doesn't exist. Did you create it?"`

---

## 4. Asset Management (Weeks 5-6)

### 4.1 Starter Sprites

Ship 20-30 simple pixel art sprites (32x32 or 64x64 PNG). Organized by category:

| Category | Sprites | Count |
|----------|---------|-------|
| Characters | Hero (4-dir), Ghost, Slime, Robot | 4 |
| Items | Coin, Heart, Star, Key, Gem | 5 |
| Environment | Wall, Platform, Door, Spike, Bush | 5 |
| Projectiles | Bullet, Arrow, Fireball | 3 |
| Vehicles | Ship, Car, Rocket | 3 |
| Abstract | Square, Circle, Triangle, Diamond | 4 |
| Backgrounds | Grass tile, Stone tile, Sky tile | 3 |

Stored in `public/sprites/` as PNGs. Manifest in `src/assets/sprites.ts`.
The sidebar shows a grid of thumbnails. Click to assign to selected object.

### 4.2 Sprite Import

"Import sprite" button opens a file picker for PNG/JPG.
Imported sprites are stored as base64 data URLs in the project file.
No server upload — everything stays local.

### 4.3 Object Types

The sidebar shows a list of object types. Each object type has:
- **Name** (editable, must be unique)
- **Default sprite** (from sprite picker)
- **Event handlers** (blocks per event type)

UI flow:
1. Click "+" to create new object type
2. Name it (e.g., "Player")
3. Assign a sprite from the picker
4. Click an event (e.g., "Step") to open it in the block editor
5. Drag blocks to define behavior

### 4.4 Room Editor

Single room in Phase 1 (multi-room is Phase 2).

The room editor is a canvas overlay where:
- The room background color is shown
- A grid overlay shows placement cells (configurable: 32x32 default)
- Click on an object type in the sidebar, then click in the room to place an instance
- Drag placed instances to reposition
- Right-click to delete
- Room dimensions editable in properties panel

The room defines the initial `instances[]` array that gets serialized into game state
when the user clicks Run.

---

## 5. Project Save/Load (Weeks 6-7)

### 5.1 .acorn Format

```typescript
// src/project/format.ts
interface AcornProject {
  version: "0.1.0";
  name: string;
  settings: {
    width: number;         // Room/canvas width (default 800)
    height: number;        // Room/canvas height (default 600)
    fps: number;           // Target FPS (default 60)
    background: string;    // CSS color (default "#1a1a2e")
  };
  sprites: Record<string, {
    src: string;           // data:image/png;base64,... or path to built-in
    width: number;
    height: number;
  }>;
  objects: Record<string, {
    sprite: string;        // Sprite key
    events: Record<string, {
      blocks: string;      // Blockly XML serialization
    }>;
  }>;
  rooms: Array<{
    name: string;
    width: number;
    height: number;
    background: string;
    instances: Array<{
      object: string;      // Object type name
      x: number;
      y: number;
    }>;
  }>;
  startRoom: string;       // Room name to load first
}
```

Blocks are stored as Blockly's XML serialization. Sprites are either references to
built-in assets (`"builtin:spr_hero"`) or base64 data URLs (user imports).
The entire project is one self-contained JSON file.

### 5.2 Storage

```typescript
// src/project/storage.ts
import { get, set, del, keys } from "idb-keyval";

async function saveProject(project: AcornProject): Promise<void>;
async function loadProject(name: string): Promise<AcornProject>;
async function listProjects(): Promise<string[]>;
async function deleteProject(name: string): Promise<void>;

// File export/import for sharing
function exportToFile(project: AcornProject): void;   // triggers download
function importFromFile(file: File): Promise<AcornProject>;
```

IndexedDB via `idb-keyval` for browser persistence. File download/upload for
sharing `.acorn` files manually (full sharing UI is Phase 3).

### 5.3 Toolbar Actions

| Button | Action |
|--------|--------|
| **New** | Create project from starter template (one Player object, one room) |
| **Open** | Show modal with saved projects list + "Import file" option |
| **Save** | Save current project to IndexedDB (auto-names if untitled) |
| **Run** | Switch workspace to game canvas, start game engine |
| **Stop** | Stop game engine, switch back to block editor |

Auto-save on workspace changes (debounced 2s) to prevent losing work.

---

## 6. Starter Template

When the user clicks "New", they get a project with:

- **One object:** "Player" with `spr_hero` sprite
- **One room:** "room_main" (800x600) with one Player instance at center
- **Pre-attached Step event** with starter blocks:

```
[on step]
  [if <key down [ArrowRight]> then]
    [change x by (3)]
  [if <key down [ArrowLeft]> then]
    [change x by (-3)]
  [if <key down [ArrowUp]> then]
    [change y by (-3)]
  [if <key down [ArrowDown]> then]
    [change y by (3)]
```

Click Run → arrow keys move the sprite. Immediate success. A child sees results
in under 10 seconds after opening the app.

---

## 7. Testing Strategy

### Unit Tests (vitest)

- **Code generator:** Block XML → expected Hemlock source for each block type
- **Script assembly:** Multiple objects → correct dispatch script
- **State serialization:** GameState ↔ JSON round-trip
- **Input manager:** Key events → correct snapshot state
- **Project format:** Save → load round-trip preserves all data

### Integration Tests

- **WASM bridge:** `contextCreate` → `contextSet` → `runScript` → `contextGet` cycle
- **Game loop:** Run 10 frames, verify instance positions change correctly
- **Block edit during play:** Edit blocks → script recompiles → new behavior next frame

### Manual Smoke Tests (Week 7)

- [ ] New project → Run → arrow keys move sprite
- [ ] Create second object (Enemy) → place in room → runs alongside Player
- [ ] Add variable block → variable persists across frames
- [ ] Syntax error in generated code → friendly error in console
- [ ] Save → refresh browser → Open → project restored
- [ ] Import/export .acorn file round-trip

---

## 8. What Phase 1 Does NOT Include

Explicitly deferred to later phases:

| Feature | Deferred To |
|---------|-------------|
| Collision detection | Phase 2 |
| Sound/audio | Phase 2 |
| Multiple rooms | Phase 2 |
| Sprite editor (pixel drawing) | Phase 2 |
| Appearance blocks (animation, tint, size) | Phase 2 |
| Drawing blocks (shapes, text) | Phase 2 |
| Split view (blocks + code side-by-side) | Phase 3 |
| Sprig mascot | Phase 3 |
| Tutorials | Phase 3 |
| Project sharing via URL | Phase 3 |
| Code-only mode | Phase 3 |
| Mouse input | Phase 2 |
| Touch input | Phase 4 |

Phase 1 ships the smallest thing that feels like a real game maker: drag blocks,
see sprites move with keyboard input, save your project. Everything else builds on top.

---

## 9. Milestone Checklist

- [ ] **M1 (end of Week 1):** Vite app loads, WASM bridge connects, `hemlock_eval("print(42);")` prints to console
- [ ] **M2 (end of Week 3):** Blockly workspace renders, dragging blocks generates Hemlock code shown in preview
- [ ] **M3 (end of Week 5):** Click Run → canvas shows sprite → Step event runs per frame → keyboard moves sprite
- [ ] **M4 (end of Week 6):** Room editor places instances, sprite picker assigns sprites, multiple object types work
- [ ] **M5 (end of Week 7):** Save/load works, starter template loads on New, export/import .acorn files
