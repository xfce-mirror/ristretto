#!/usr/bin/env python3
"""Separate application program-cache reuse from Mesa cache state (five repetitions)."""
import argparse
import json
import os
from pathlib import Path
import random
import re
import shutil
import subprocess
import time
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('binary',type=Path)
p.add_argument('output',type=Path)
a=p.parse_args(); a.binary=a.binary.resolve(); a.output=a.output.resolve(); a.output.mkdir(parents=True,exist_ok=True)
# Each measured process receives a controlled independent cache state.
conditions=[('disabled-cold',False,False,False),('disabled-warm',False,False,True),
            ('app-miss-driver-off',True,False,None),('app-hit-driver-off',True,True,None),
            ('app-miss-driver-warm',True,False,True),('app-hit-driver-warm',True,True,True)]
rng=random.Random(20261103); results=[]
for repeat in range(5):
    order=conditions.copy(); rng.shuffle(order)
    for name,persist,appwarm,driverwarm in order:
        tag=f'r{repeat+1}-{name}'; state=a.output/tag; state.mkdir()
        env=dict(os.environ,XDG_CACHE_HOME=str(state/'cache'),MESA_SHADER_CACHE_DIR=str(state/'mesa'),
                 MESA_SHADER_CACHE_DISABLE='true' if driverwarm is None else 'false',
                 RISTRETTO_SHADER_CACHE='1' if persist else '0',RISTRETTO_PROFILE='1')
        def run(suffix):
            start=time.monotonic()
            process=subprocess.run([str(a.binary),'--binary-cache=measure'],env=env,capture_output=True,text=True)
            (a.output/(tag+suffix+'.log')).write_text(process.stdout+process.stderr)
            if process.returncode: raise SystemExit(process.returncode)
            fields=dict(re.findall(r'(\w+)=([0-9.]+)',next(l for l in process.stdout.splitlines() if l.startswith('BINARY_CACHE'))))
            return {k:float(v) if '.' in v else int(v) for k,v in fields.items()}|{'process_ms':(time.monotonic()-start)*1000}
        if appwarm or driverwarm:
            run('-seed')
        if not appwarm and (state/'cache/ristretto').exists(): shutil.rmtree(state/'cache/ristretto')
        result=run('')|dict(name=name,repeat=repeat+1)
        results.append(result)
        (a.output/'results.json').write_text(json.dumps(results,indent=2)+'\n')
        print(json.dumps(result),flush=True)
