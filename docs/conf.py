# Configuration file for Sphinx documentation

import os
import sys

_docs_dir = os.path.dirname(__file__)
_doxygen_xml = os.path.join(_docs_dir, 'doxygen', 'xml')
_has_doxygen_xml = os.path.isfile(os.path.join(_doxygen_xml, 'index.xml'))

# Project information
project = 'DR_EVT'
copyright = '2024-2026, Lawrence Livermore National Laboratory'
author = 'LLNL'
version = '0.9'
release = '0.9.0'

# Short description for metadata
html_title = 'DR_EVT Documentation'
html_short_title = 'DR_EVT'

# General configuration
extensions = [
    'sphinx.ext.autodoc',
    'sphinx.ext.napoleon',
    'sphinx.ext.viewcode',
    'sphinx.ext.githubpages',
    'myst_parser',  # For Markdown support
    'sphinxcontrib.mermaid',  # For Mermaid diagrams
]
if _has_doxygen_xml:
    extensions.append('breathe')  # Render Doxygen XML in the Sphinx theme

# Markdown configuration
myst_enable_extensions = [
    "colon_fence",
    "deflist",
    "html_image",
]

# Without this, MyST only auto-generates anchor IDs for H1 headings,
# so any link to a deeper (H2-H4) section heading - within the same
# document or across documents - fails cross-reference validation.
myst_heading_anchors = 4

# Responsibility pages and source-directory pages deliberately present the
# same generated C++ declarations through two complementary navigation paths.
# Breathe registers C and C++ domain targets for each rendering, so these
# expected duplicate anchors are not actionable documentation warnings.
suppress_warnings = [
    'duplicate_declaration.c',
    'duplicate_declaration.cpp',
]

# Mermaid configuration
mermaid_version = "10.6.1"  # Use specific stable version
mermaid_init_js = """
mermaid.initialize({
    startOnLoad: true,
    theme: 'default',
    flowchart: { useMaxWidth: true },
    gantt: { useMaxWidth: true },
    sequence: { useMaxWidth: true }
});
"""

# Add any paths that contain templates here
templates_path = ['_templates']

# List of patterns to ignore
exclude_patterns = [
    '_build',
    'venv',
    'venv/**',
    '_static/README.md',
    'Thumbs.db',
    '.DS_Store',
    'dev/wip-notes/*',  # Exclude development session notes
]

# Source file suffix
source_suffix = {
    '.rst': 'restructuredtext',
    '.md': 'markdown',
}

# The master toctree document
master_doc = 'index'

# HTML output options
html_theme = 'sphinx_rtd_theme'
html_static_path = ['_static']
html_css_files = ['custom.css']
html_js_files = ['api-search.js']
# Doxygen XML is rendered directly by Breathe on the C++ API page.  Keep the
# reference optional for direct Sphinx builds where Doxygen is unavailable.
if _has_doxygen_xml:
    breathe_projects = {'dr_evt': _doxygen_xml}
    breathe_default_project = 'dr_evt'
    tags.add('doxygen')
else:
    # The generated source-directory pages contain Breathe directives.
    # Excluding them keeps direct Sphinx builds useful when Doxygen XML has
    # not been generated.
    exclude_patterns.append('api/cpp/source/*')
html_logo = '_static/dr_evt_logo.svg'
html_favicon = '_static/favicon.ico'

# Theme options
html_theme_options = {
    'logo_only': False,
    'style_nav_header_background': '#2c3e50',
    'navigation_depth': 4,
    'collapse_navigation': False,
    'sticky_navigation': True,
    'includehidden': True,
    'titles_only': False,
}

# HTML context
html_context = {
    "display_github": True,
    "github_user": "LLNL",
    "github_repo": "dr_evt",
    "github_version": "main",
    "conf_py_path": "/docs/",
}

# LaTeX output options (for PDF generation)
latex_elements = {
    'papersize': 'letterpaper',
    'pointsize': '10pt',
}

latex_documents = [
    (master_doc, 'DR_EVT.tex', 'DR_EVT Documentation',
     'LLNL', 'manual'),
]

# Manual page output
man_pages = [
    (master_doc, 'dr_evt', 'DR_EVT Documentation',
     [author], 1)
]

# Texinfo output
texinfo_documents = [
    (master_doc, 'DR_EVT', 'DR_EVT Documentation',
     author, 'DR_EVT', 'HPC Job Scheduler Simulator',
     'Miscellaneous'),
]
