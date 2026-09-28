# Configuration file for the Sphinx documentation builder.
# https://www.sphinx-doc.org/en/master/usage/configuration.html

project = "Low-Latency C++ Matching Engine"
copyright = "2026, Renuka Somwanshi"
author = "Renuka Somwanshi"
release = "0.1"

extensions = [
    "sphinx.ext.autosectionlabel",
    "sphinx.ext.duration",
    "sphinx_design",
]

templates_path = ["_templates"]
exclude_patterns = ["_build", "Thumbs.db", ".DS_Store"]

# Diátaxis: keep the four modes as separate top-level sections rather than
# mixing tutorial/how-to/reference/explanation content on the same page.
autosectionlabel_prefix_document = True

html_theme = "furo"
html_static_path = ["_static"]
html_title = "Exchange Simulator Docs"
