#!/usr/bin/env python3
"""gen_names.py -- writes fm1open_names.h: every external name FM1 Open's sources define, renamed to
this copy's prefix (FEL(name)), so its functions and globals never meet another core's at link time.

    python3 engines/fm1open/gen_names.py      (run from the repository root after an update)

It compiles fm1open_core.c with FO_NAMES_PROBE (the upstream sources only, as the core includes
them, without the rename) and lists what the object defines with nm."""
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
UP = os.path.join(HERE, 'upstream')
FLAGS = ['-std=gnu11', '-O0', '-w', '-fwrapv', '-DFM1_HOST_TEST', '-DFM1_OPEN_WEB', '-DFO_NAMES_PROBE', '-DFEL_PREFIX=fo_',
         '-DFEL_BSS_SECTION="__DATA,__felbss"', '-DFEL_DATA_SECTION="__DATA,__feldata"', '-I' + os.path.join(HERE, '..', 'felucca')]
INCS = ['web/engine', 'web/synth', 'third_party/synth', 'sdk/include', 'app/synth', 'app']   # (Build-Web.ps1's order)


def main():
    with tempfile.TemporaryDirectory() as tmp:
        obj = os.path.join(tmp, 'probe.o')
        cmd = ['clang'] + FLAGS + ['-I' + os.path.join(UP, d) for d in INCS] + ['-c', os.path.join(HERE, 'fm1open_core.c'), '-o', obj]
        if subprocess.run(cmd).returncode:
            sys.exit('gen_names: the probe did not compile')
        out = subprocess.run(['nm', '-g', '-j', '--defined-only', obj], capture_output=True, text=True, check=True).stdout
    names = sorted({n[1:] if sys.platform == 'darwin' and n.startswith('_') else n for n in out.split()})
    names = [n for n in names if not n.startswith('fo_')]   # (the core's own: already prefixed)
    with open(os.path.join(HERE, 'fm1open_names.h'), 'w') as f:
        f.write('/* fm1open_names.h -- written by gen_names.py: FM1 Open\'s external names, this copy\'s (FEL) */\n')
        for n in names:
            f.write(f'#define {n} FEL({n})\n')
    print(f'gen_names: {len(names)} names')


if __name__ == '__main__':
    main()
