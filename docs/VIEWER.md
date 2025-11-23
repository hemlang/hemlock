# Hemlock Documentation Viewer

A beautiful, elegant HTML documentation viewer for the Hemlock language manual.

## Features

- **Beautiful Design** - Sage and pine green color theme matching Hemlock's natural aesthetic
- **Comprehensive** - Includes all documentation from CLAUDE.md and docs/ directory (33+ pages)
- **Offline-Ready** - No HTTP server required, works by opening the file directly
- **Multi-Page Navigation** - Easy switching between different documentation sections
- **Responsive** - Works great on desktop, tablet, and mobile devices
- **Fast** - All content embedded, instant page switching

## How to Use

### Quick Start
Simply open `docs.html` in your web browser:
```bash
# From the hemlock directory
open docs.html           # macOS
xdg-open docs.html       # Linux
start docs.html          # Windows
```

**No HTTP server required!** All documentation is embedded in the HTML file.

### Rebuilding Documentation
If you update any documentation files, rebuild the viewer:
```bash
python3 build_docs.py
```

This will regenerate `docs.html` with all the latest content from:
- `CLAUDE.md` (Language Reference)
- `docs/getting-started/` (Installation, Quick Start, Tutorial)
- `docs/language-guide/` (Syntax, Types, Memory, Strings, etc.)
- `docs/advanced/` (Async, FFI, Signals, File I/O, etc.)
- `docs/reference/` (API references for strings, arrays, etc.)
- `docs/design/` (Philosophy, Implementation details)
- `docs/contributing/` (Guidelines, Testing)

## Navigation

- **Desktop**: Use the fixed sidebar on the left to navigate between documentation pages
- **Mobile**: Tap the menu button (☰) in the bottom-right corner to open navigation
- Click any page in the sidebar to switch to that documentation
- The active page is automatically highlighted

## Color Theme

The documentation viewer uses a carefully selected sage and pine green color palette:

- **Sage** (#9CAF88) - Soft, natural green for accents
- **Pine** (#2F4F4F) - Deep green for headers and primary elements
- **Light Sage** (#E8F4E1) - Subtle background tint
- **Cream** (#FAF9F6) - Warm background color

## Technical Details

- Pure HTML/CSS/JavaScript - no runtime dependencies
- Built with Python script that embeds all documentation
- Markdown parser included inline for rendering
- Logo encoded as base64 data URL
- All 33+ documentation pages embedded in single HTML file
- Multi-page navigation with instant switching
- Mobile-first responsive design

### Build Process
The `build_docs.py` script:
1. Scans CLAUDE.md and docs/ directory for all markdown files
2. Encodes the logo as base64 data URL
3. Generates navigation structure organized by section
4. Embeds all content as JSON in the HTML
5. Creates a standalone, self-contained HTML file
