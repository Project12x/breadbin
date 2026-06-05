# Breadbin Design System — Option D: Neon Synthwave

Tokens for Breadbin's "Option D — Neon Synthwave" UI; reusable renderers live in GhostmoonGPL (`gm::ui::`).

## Palette

| Token | Hex | Role |
|-------|-----|------|
| **Structure** | | |
| `bg0` | `#0A0A0E` | Deepest background (window fill) |
| `bg1` | `#13131A` | Secondary background |
| `panel` | `#16161D` | Panel background |
| `panel2` | `#1C1C25` | Panel gradient start (top) |
| `panel3` | `#22222D` | Panel gradient end / raised surfaces |
| `line` | `#2C2C39` | Primary separator / border |
| `line2` | `#3A3A4A` | Secondary separator / hover border |
| `inset` | `#0E0E13` | Inset / recessed track backgrounds |
| **Text** | | |
| `txt` | `#E7E7F0` | Primary text |
| `txt2` | `#A6A6B8` | Secondary / label text |
| `txt3` | `#6F6F82` | Disabled / hint text |
| **Neon Accents** | | |
| `cyan` | `#33EDED` | SID I — primary accent, knob arcs, active indicators |
| `cyanD` | `#1AA6A6` | SID I — dark / glow base |
| `orange` | `#FFAE3B` | SID II — primary accent, knob arcs, active indicators |
| `orangeD` | `#C97F1E` | SID II — dark / glow base |
| `grn` | `#B6F23C` | Aux toggles (Ring / Sync / Arp / Loop), CPU meter, ADSR |
| `grnD` | `#7FAE23` | Greenyellow dark / glow base |
| `mag` | `#FF3DF0` | Voice editor header, mod matrix, FX section header |
| `purple` | `#9A6BFF` | Modulation depth indicators |
| `gold` | `#FFCB45` | Parameter value highlights / preset name |
| `red` | `#FF5468` | Transport stop, warning, clipping indicator |
| `lime` | `#5DFF7A` | Transport play |
| `yellow` | `#FFE14D` | Transport pause |
| **C64 VIC-II (popups only)** | | |
| `cblue` | `#8B80E8` | C64 popup scheme — blue |
| `cgrn` | `#9AD284` | C64 popup scheme — green |
| `cyel` | `#D6DD7E` | C64 popup scheme — yellow |
| `cred` | `#D08A72` | C64 popup scheme — red |
| `cpur` | `#B98AE0` | C64 popup scheme — purple |
| `beige` | `#D8C79F` | C64 popup scheme — beige / light text |

## Typography

| Font | Weight | Usage |
|------|--------|-------|
| Press Start 2P | 400 | Eyebrow headers, neon section titles |
| JetBrains Mono | 400 | Numeric readouts, register dumps, frequency displays |
| Lato | 400 / 700 | Labels, body text, buttons, combo boxes |

## Components

### Panel
Gradient fill from `panel2` (top) to `panel` (bottom), 1 px `line` border, 8 px corner radius. Subtle inset sheen at top edge + drop shadow for depth.

### Glass Panel
Translucent background: `rgba(20, 22, 34, 0.44)` → `rgba(7, 8, 14, 0.60)`. 1 px accent-coloured border. Outer glow at the border edge.

### Knob
270° arc. Track colour: `#26262F`. Active value arc drawn in the section accent colour with a soft glow. Metallic cap: radial gradient from `#D2D2DC` (highlight) to `#5A5A66` (shadow).

### Sliders
Inset track filled with `inset`. Accent fill from minimum to thumb position with matching glow. Metallic thumb: `#D2D2DC` → `#5A5A66` gradient, 1 px `line2` border.

### CRT Scope
Radial background gradient: `#0A1417` (center) → `#05080A` (edge). Waveform drawn with double-stroke bloom (wide low-alpha pass + narrow full-alpha pass). Scanlines rendered once to an offscreen image and composited — **scope displays only**, never applied to the whole window.

## Rules

- **PETSCII font**: opt-in, off by default; never forced on the user.
- **CRT / scanlines**: applied only to scope/oscilloscope display components — never the full window background.
- **C64 palette**: used only in popup components when the NEON/C64 scheme toggle is set to C64 mode. The main panel always uses the neon palette.
- **Accent assignment**: SID I panels use `cyan` / `cyanD`; SID II panels use `orange` / `orangeD`. Do not cross-assign.
