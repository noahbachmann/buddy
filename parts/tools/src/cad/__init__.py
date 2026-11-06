"""CAD tooling for parametric OpenSCAD parts: build, render, check, sweep.

A *module* is a folder with a `cad.toml` (see `config.py`). Nothing here knows about a specific repo: the only
fixed path is the OpenSCAD library folder, `<tools>/../lib` unless `CAD_LIBPATH` says otherwise.
"""
