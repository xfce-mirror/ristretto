#!/usr/bin/env python3
"""Real OpenGL binary reuse, in fresh processes with Mesa's disk cache disabled."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
with tempfile.TemporaryDirectory(prefix='ristretto-programs-') as directory:
    env = dict(os.environ, XDG_CACHE_HOME=directory, MESA_SHADER_CACHE_DISABLE='true', RISTRETTO_SHADER_CACHE='1')
    for mode in ('cold', 'warm', 'reject', 'warm'):
        result = subprocess.run([sys.argv[1], '--binary-cache=' + mode], env=env)
        if result.returncode:
            raise SystemExit(result.returncode)
    objects = list(Path(directory).glob('ristretto/shaders/*/*.bin'))
    assert len(objects) == 2, objects
    assert sum(p.stat().st_size for p in objects) <= 8 * 1024 * 1024
