#!/usr/bin/env python3
"""
Build the Hemlock documentation viewer.

This script generates a standalone HTML file (docs.html) that includes:
- All markdown documentation from CLAUDE.md and docs/
- Embedded content (no HTTP server required)
- Beautiful sage/pine green theme
- Multi-page navigation
"""

import os
import json
import base64
from pathlib import Path

def read_file(path):
    """Read file content."""
    try:
        with open(path, 'r', encoding='utf-8') as f:
            return f.read()
    except Exception as e:
        print(f"Warning: Could not read {path}: {e}")
        return ""

def encode_image(path):
    """Encode image as base64 data URL."""
    try:
        with open(path, 'rb') as f:
            data = base64.b64encode(f.read()).decode('utf-8')
            ext = path.split('.')[-1].lower()
            mime = 'image/png' if ext == 'png' else 'image/jpeg'
            return f"data:{mime};base64,{data}"
    except Exception as e:
        print(f"Warning: Could not encode image {path}: {e}")
        return ""

def collect_docs():
    """Collect all documentation files."""
    docs = {}

    # Add CLAUDE.md as the main documentation
    claude_path = Path('CLAUDE.md')
    if claude_path.exists():
        docs['Language Reference'] = {
            'id': 'language-reference',
            'content': read_file(claude_path),
            'order': 0
        }

    # Collect docs from docs/ directory
    docs_dir = Path('docs')
    if docs_dir.exists():
        sections = {
            'getting-started': ('Getting Started', 1),
            'language-guide': ('Language Guide', 2),
            'advanced': ('Advanced Topics', 3),
            'reference': ('API Reference', 4),
            'design': ('Design & Philosophy', 5),
            'contributing': ('Contributing', 6),
        }

        for subdir, (section_name, order) in sections.items():
            subdir_path = docs_dir / subdir
            if not subdir_path.exists():
                continue

            for md_file in sorted(subdir_path.glob('*.md')):
                # Skip development docs
                if 'development' in str(md_file):
                    continue

                file_name = md_file.stem
                # Convert filename to title
                title = file_name.replace('-', ' ').replace('_', ' ').title()
                doc_id = f"{subdir}-{file_name}"

                docs[f"{section_name} → {title}"] = {
                    'id': doc_id,
                    'content': read_file(md_file),
                    'order': order,
                    'section': section_name
                }

    # Sort by order, then by name
    sorted_docs = dict(sorted(docs.items(), key=lambda x: (x[1]['order'], x[0])))
    return sorted_docs

def generate_html(docs, logo_data):
    """Generate the complete HTML document."""

    # Generate navigation items
    nav_items = []
    current_section = None

    for title, info in docs.items():
        section = info.get('section', '')

        # Add section header if it's a new section
        if section and section != current_section:
            section_title = section.replace('-', ' ').title()
            if current_section is not None:  # Not the first section
                nav_items.append('</div>')
            nav_items.append(f'<div class="nav-section">')
            nav_items.append(f'<div class="nav-section-title">{section_title}</div>')
            current_section = section
        elif not section and current_section is not None:
            nav_items.append('</div>')
            current_section = None
        elif not section and current_section is None:
            nav_items.append('<div class="nav-section">')
            current_section = 'main'

        # Simplify title for navigation (remove section prefix)
        nav_title = title.split(' → ')[-1] if ' → ' in title else title
        nav_items.append(f'<a href="#{info["id"]}" class="nav-link" data-page="{info["id"]}">{nav_title}</a>')

    if current_section:
        nav_items.append('</div>')

    navigation_html = '\n'.join(nav_items)

    # Generate page content (embedded as JSON)
    pages_json = json.dumps({
        title: {'id': info['id'], 'content': info['content']}
        for title, info in docs.items()
    }, ensure_ascii=False)

    html = f'''<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Hemlock Language Manual</title>
    <style>
        * {{
            margin: 0;
            padding: 0;
            box-sizing: border-box;
        }}

        :root {{
            --sage: #9CAF88;
            --pine: #2F4F4F;
            --dark-pine: #1a2f2f;
            --light-sage: #E8F4E1;
            --cream: #FAF9F6;
            --text: #2C3E2C;
            --text-light: #5A6F5A;
            --border: #D4E4CB;
            --code-bg: #F5F9F3;
            --accent: #6B8E6B;
        }}

        body {{
            font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', 'Roboto', 'Helvetica', 'Arial', sans-serif;
            line-height: 1.7;
            color: var(--text);
            background: var(--cream);
        }}

        /* Header */
        .header {{
            position: fixed;
            top: 0;
            left: 0;
            right: 0;
            height: 70px;
            background: var(--pine);
            color: white;
            display: flex;
            align-items: center;
            padding: 0 2rem;
            box-shadow: 0 2px 10px rgba(0,0,0,0.1);
            z-index: 1000;
        }}

        .header-logo {{
            height: 45px;
            margin-right: 1rem;
        }}

        .header h1 {{
            font-size: 1.5rem;
            font-weight: 600;
            letter-spacing: 0.5px;
        }}

        .header .tagline {{
            margin-left: auto;
            font-size: 0.9rem;
            font-style: italic;
            color: var(--light-sage);
            display: none;
        }}

        @media (min-width: 768px) {{
            .header .tagline {{
                display: block;
            }}
        }}

        /* Layout */
        .container {{
            display: flex;
            margin-top: 70px;
            min-height: calc(100vh - 70px);
        }}

        /* Sidebar */
        .sidebar {{
            position: fixed;
            left: 0;
            top: 70px;
            width: 280px;
            height: calc(100vh - 70px);
            background: var(--light-sage);
            border-right: 2px solid var(--border);
            overflow-y: auto;
            padding: 2rem 0;
            transform: translateX(-100%);
            transition: transform 0.3s ease;
            z-index: 900;
        }}

        .sidebar.open {{
            transform: translateX(0);
        }}

        @media (min-width: 1024px) {{
            .sidebar {{
                transform: translateX(0);
            }}
        }}

        .nav-section {{
            margin-bottom: 1.5rem;
        }}

        .nav-section-title {{
            font-size: 0.75rem;
            font-weight: 700;
            text-transform: uppercase;
            letter-spacing: 1px;
            color: var(--pine);
            padding: 0 1.5rem;
            margin-bottom: 0.5rem;
        }}

        .nav-link {{
            display: block;
            padding: 0.5rem 1.5rem;
            color: var(--text);
            text-decoration: none;
            font-size: 0.9rem;
            transition: all 0.2s;
            border-left: 3px solid transparent;
            cursor: pointer;
        }}

        .nav-link:hover {{
            background: rgba(47, 79, 79, 0.05);
            border-left-color: var(--sage);
        }}

        .nav-link.active {{
            background: rgba(47, 79, 79, 0.1);
            border-left-color: var(--pine);
            font-weight: 600;
            color: var(--pine);
        }}

        /* Mobile Menu Toggle */
        .menu-toggle {{
            position: fixed;
            bottom: 2rem;
            right: 2rem;
            width: 56px;
            height: 56px;
            background: var(--pine);
            color: white;
            border: none;
            border-radius: 50%;
            font-size: 1.5rem;
            cursor: pointer;
            box-shadow: 0 4px 12px rgba(0,0,0,0.2);
            z-index: 1000;
            display: flex;
            align-items: center;
            justify-content: center;
        }}

        @media (min-width: 1024px) {{
            .menu-toggle {{
                display: none;
            }}
        }}

        /* Main Content */
        .main-content {{
            flex: 1;
            margin-left: 0;
            padding: 3rem 2rem;
            max-width: 900px;
        }}

        @media (min-width: 1024px) {{
            .main-content {{
                margin-left: 280px;
            }}
        }}

        /* Typography */
        .content h1 {{
            font-size: 2.5rem;
            color: var(--pine);
            margin: 2rem 0 1rem;
            padding-bottom: 0.5rem;
            border-bottom: 3px solid var(--sage);
        }}

        .content h2 {{
            font-size: 2rem;
            color: var(--pine);
            margin: 3rem 0 1rem;
            padding-top: 1rem;
        }}

        .content h3 {{
            font-size: 1.5rem;
            color: var(--accent);
            margin: 2rem 0 1rem;
        }}

        .content h4 {{
            font-size: 1.2rem;
            color: var(--accent);
            margin: 1.5rem 0 0.8rem;
        }}

        .content p {{
            margin: 1rem 0;
            color: var(--text);
        }}

        .content ul, .content ol {{
            margin: 1rem 0 1rem 2rem;
        }}

        .content li {{
            margin: 0.5rem 0;
        }}

        .content blockquote {{
            border-left: 4px solid var(--sage);
            background: var(--light-sage);
            padding: 1rem 1.5rem;
            margin: 1.5rem 0;
            font-style: italic;
            color: var(--text-light);
        }}

        .content hr {{
            border: none;
            border-top: 2px solid var(--border);
            margin: 2rem 0;
        }}

        /* Code Blocks */
        .content code {{
            background: var(--code-bg);
            padding: 0.2rem 0.4rem;
            border-radius: 3px;
            font-family: 'Consolas', 'Monaco', 'Courier New', monospace;
            font-size: 0.9em;
            color: var(--pine);
        }}

        .content pre {{
            background: var(--code-bg);
            border: 1px solid var(--border);
            border-left: 4px solid var(--pine);
            border-radius: 4px;
            padding: 1.2rem;
            overflow-x: auto;
            margin: 1.5rem 0;
        }}

        .content pre code {{
            background: none;
            padding: 0;
            border-radius: 0;
            font-size: 0.85rem;
            line-height: 1.6;
        }}

        /* Tables */
        .content table {{
            width: 100%;
            border-collapse: collapse;
            margin: 1.5rem 0;
        }}

        .content th,
        .content td {{
            padding: 0.75rem;
            text-align: left;
            border-bottom: 1px solid var(--border);
        }}

        .content th {{
            background: var(--light-sage);
            color: var(--pine);
            font-weight: 600;
        }}

        /* Links */
        .content a {{
            color: var(--accent);
            text-decoration: none;
            border-bottom: 1px solid transparent;
            transition: border-color 0.2s;
        }}

        .content a:hover {{
            border-bottom-color: var(--accent);
        }}

        /* Scrollbar */
        ::-webkit-scrollbar {{
            width: 10px;
        }}

        ::-webkit-scrollbar-track {{
            background: var(--cream);
        }}

        ::-webkit-scrollbar-thumb {{
            background: var(--sage);
            border-radius: 5px;
        }}

        ::-webkit-scrollbar-thumb:hover {{
            background: var(--accent);
        }}

        /* Section anchors */
        .section-anchor {{
            scroll-margin-top: 90px;
        }}

        /* Mobile adjustments */
        @media (max-width: 768px) {{
            .main-content {{
                padding: 2rem 1rem;
            }}

            .content h1 {{
                font-size: 2rem;
            }}

            .content h2 {{
                font-size: 1.6rem;
            }}

            .content h3 {{
                font-size: 1.3rem;
            }}
        }}

        /* Page switching */
        .page {{
            display: none;
        }}

        .page.active {{
            display: block;
        }}
    </style>
</head>
<body>
    <!-- Header -->
    <div class="header">
        <img src="{logo_data}" alt="Hemlock Logo" class="header-logo">
        <h1>Hemlock Language Manual</h1>
        <span class="tagline">"A small, unsafe language for writing unsafe things safely."</span>
    </div>

    <!-- Mobile Menu Toggle -->
    <button class="menu-toggle" id="menuToggle">☰</button>

    <!-- Container -->
    <div class="container">
        <!-- Sidebar Navigation -->
        <nav class="sidebar" id="sidebar">
            {navigation_html}
        </nav>

        <!-- Main Content -->
        <main class="main-content">
            <div class="content" id="content"></div>
        </main>
    </div>

    <script>
        // Embedded documentation pages
        const PAGES = {pages_json};

        // Mobile menu toggle
        const menuToggle = document.getElementById('menuToggle');
        const sidebar = document.getElementById('sidebar');

        menuToggle.addEventListener('click', () => {{
            sidebar.classList.toggle('open');
            menuToggle.textContent = sidebar.classList.contains('open') ? '×' : '☰';
        }});

        // Close sidebar when clicking outside on mobile
        document.addEventListener('click', (e) => {{
            if (window.innerWidth < 1024) {{
                if (!sidebar.contains(e.target) && !menuToggle.contains(e.target)) {{
                    sidebar.classList.remove('open');
                    menuToggle.textContent = '☰';
                }}
            }}
        }});

        // Markdown parser
        function parseMarkdown(md) {{
            let lines = md.split('\\n');
            let html = '';
            let inCodeBlock = false;
            let codeBlockContent = '';
            let codeBlockLang = '';
            let inList = false;
            let listContent = '';
            let inBlockquote = false;
            let blockquoteContent = '';

            function processInlineMarkdown(text) {{
                text = text.replace(/\\*\\*(.+?)\\*\\*/g, '<strong>$1</strong>');
                text = text.replace(/\\*([^*]+)\\*/g, '<em>$1</em>');
                text = text.replace(/`([^`]+)`/g, '<code>$1</code>');
                text = text.replace(/\\[([^\\]]+)\\]\\(([^)]+)\\)/g, '<a href="$2">$1</a>');
                return text;
            }}

            function makeId(text) {{
                return text.toLowerCase()
                    .replace(/[^\\w\\s-]/g, '')
                    .replace(/\\s+/g, '-')
                    .replace(/^-+|-+$/g, '');
            }}

            function flushList() {{
                if (inList && listContent) {{
                    html += '<ul>\\n' + listContent + '</ul>\\n';
                    listContent = '';
                    inList = false;
                }}
            }}

            function flushBlockquote() {{
                if (inBlockquote && blockquoteContent) {{
                    html += '<blockquote>' + processInlineMarkdown(blockquoteContent.trim()) + '</blockquote>\\n';
                    blockquoteContent = '';
                    inBlockquote = false;
                }}
            }}

            for (let i = 0; i < lines.length; i++) {{
                let line = lines[i];

                if (line.startsWith('```')) {{
                    if (inCodeBlock) {{
                        html += '<pre><code>' + escapeHtml(codeBlockContent) + '</code></pre>\\n';
                        codeBlockContent = '';
                        inCodeBlock = false;
                    }} else {{
                        flushList();
                        flushBlockquote();
                        inCodeBlock = true;
                        codeBlockLang = line.substring(3).trim();
                    }}
                    continue;
                }}

                if (inCodeBlock) {{
                    codeBlockContent += line + '\\n';
                    continue;
                }}

                if (line.startsWith('# ')) {{
                    flushList();
                    flushBlockquote();
                    const text = line.substring(2).trim();
                    const id = makeId(text);
                    html += `<h1 class="section-anchor" id="${{id}}">${{processInlineMarkdown(text)}}</h1>\\n`;
                    continue;
                }}
                if (line.startsWith('## ')) {{
                    flushList();
                    flushBlockquote();
                    const text = line.substring(3).trim();
                    const id = makeId(text);
                    html += `<h2 class="section-anchor" id="${{id}}">${{processInlineMarkdown(text)}}</h2>\\n`;
                    continue;
                }}
                if (line.startsWith('### ')) {{
                    flushList();
                    flushBlockquote();
                    const text = line.substring(4).trim();
                    const id = makeId(text);
                    html += `<h3 class="section-anchor" id="${{id}}">${{processInlineMarkdown(text)}}</h3>\\n`;
                    continue;
                }}
                if (line.startsWith('#### ')) {{
                    flushList();
                    flushBlockquote();
                    const text = line.substring(5).trim();
                    const id = makeId(text);
                    html += `<h4 class="section-anchor" id="${{id}}">${{processInlineMarkdown(text)}}</h4>\\n`;
                    continue;
                }}

                if (line.trim() === '---') {{
                    flushList();
                    flushBlockquote();
                    html += '<hr>\\n';
                    continue;
                }}

                if (line.startsWith('> ')) {{
                    flushList();
                    blockquoteContent += line.substring(2) + ' ';
                    inBlockquote = true;
                    continue;
                }} else if (inBlockquote && line.trim() === '') {{
                    flushBlockquote();
                    continue;
                }}

                if (line.startsWith('- ') || line.startsWith('* ')) {{
                    flushBlockquote();
                    const text = line.substring(2).trim();
                    listContent += '<li>' + processInlineMarkdown(text) + '</li>\\n';
                    inList = true;
                    continue;
                }} else if (inList && line.trim() !== '' && !line.startsWith('#')) {{
                    listContent = listContent.trimEnd();
                    if (listContent.endsWith('</li>')) {{
                        listContent = listContent.substring(0, listContent.length - 5);
                        listContent += ' ' + processInlineMarkdown(line.trim()) + '</li>\\n';
                    }}
                    continue;
                }} else if (inList && line.trim() === '') {{
                    flushList();
                    continue;
                }}

                if (line.trim() === '') {{
                    flushList();
                    flushBlockquote();
                    continue;
                }}

                flushList();
                flushBlockquote();
                if (line.trim() !== '') {{
                    html += '<p>' + processInlineMarkdown(line) + '</p>\\n';
                }}
            }}

            flushList();
            flushBlockquote();

            return html;
        }}

        function escapeHtml(text) {{
            const div = document.createElement('div');
            div.textContent = text;
            return div.innerHTML;
        }}

        // Load a page
        function loadPage(pageId) {{
            const pageData = Object.values(PAGES).find(p => p.id === pageId);
            if (!pageData) {{
                console.error('Page not found:', pageId);
                return;
            }}

            const content = parseMarkdown(pageData.content);
            document.getElementById('content').innerHTML = content;

            // Update active nav link
            document.querySelectorAll('.nav-link').forEach(link => {{
                link.classList.remove('active');
                if (link.dataset.page === pageId) {{
                    link.classList.add('active');
                }}
            }});

            // Scroll to top
            window.scrollTo(0, 0);

            // Update URL hash
            window.location.hash = pageId;
        }}

        // Setup navigation
        document.querySelectorAll('.nav-link').forEach(link => {{
            link.addEventListener('click', (e) => {{
                e.preventDefault();
                const pageId = link.dataset.page;
                loadPage(pageId);

                // Close mobile menu
                if (window.innerWidth < 1024) {{
                    sidebar.classList.remove('open');
                    menuToggle.textContent = '☰';
                }}
            }});
        }});

        // Handle browser back/forward
        window.addEventListener('hashchange', () => {{
            const hash = window.location.hash.substring(1);
            if (hash) {{
                loadPage(hash);
            }}
        }});

        // Load initial page
        const initialHash = window.location.hash.substring(1);
        const firstPageId = Object.values(PAGES)[0].id;
        loadPage(initialHash || firstPageId);
    </script>
</body>
</html>'''

    return html

def main():
    """Main build function."""
    print("Building Hemlock documentation viewer...")

    # Collect documentation
    print("Collecting documentation files...")
    docs = collect_docs()
    print(f"Found {len(docs)} documentation pages")

    # Encode logo
    print("Encoding logo...")
    logo_data = encode_image('logo.png')

    # Generate HTML
    print("Generating HTML...")
    html = generate_html(docs, logo_data)

    # Write output
    output_path = 'docs.html'
    with open(output_path, 'w', encoding='utf-8') as f:
        f.write(html)

    print(f"✓ Documentation viewer built: {output_path}")
    print(f"  - {len(docs)} pages")
    print(f"  - No HTTP server required")
    print(f"  - Just open docs.html in your browser!")

if __name__ == '__main__':
    main()
