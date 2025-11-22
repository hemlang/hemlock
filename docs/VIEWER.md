# Hemlock Documentation Viewer

A beautiful, elegant HTML documentation viewer for the Hemlock language manual.

## Features

- **Beautiful Design** - Sage and pine green color theme matching Hemlock's natural aesthetic
- **Easy Navigation** - Sticky sidebar with table of contents for quick jumping between sections
- **Responsive** - Works great on desktop, tablet, and mobile devices
- **Searchable** - Use browser's built-in search (Ctrl+F / Cmd+F) to find anything
- **Fast** - Single-page application with smooth scrolling and no page reloads

## How to Use

### Option 1: Open Directly
Simply open `docs.html` in your web browser:
```bash
# From the hemlock directory
open docs.html           # macOS
xdg-open docs.html       # Linux
start docs.html          # Windows
```

### Option 2: Serve with HTTP
For best results, serve with a local web server:
```bash
# Python 3
python3 -m http.server 8000

# Then visit: http://localhost:8000/docs.html
```

## Navigation

- **Desktop**: Use the fixed sidebar on the left to navigate sections
- **Mobile**: Tap the menu button (☰) in the bottom-right corner to open navigation
- Click any section in the sidebar to jump directly to that part of the documentation
- The active section is automatically highlighted as you scroll

## Color Theme

The documentation viewer uses a carefully selected sage and pine green color palette:

- **Sage** (#9CAF88) - Soft, natural green for accents
- **Pine** (#2F4F4F) - Deep green for headers and primary elements
- **Light Sage** (#E8F4E1) - Subtle background tint
- **Cream** (#FAF9F6) - Warm background color

## Technical Details

- Pure HTML/CSS/JavaScript - no dependencies or build process
- Markdown parser included inline for rendering CLAUDE.md
- Syntax highlighting for Hemlock code examples
- Smooth scroll behavior with intersection observer for active section tracking
- Mobile-first responsive design
