#!/usr/bin/env python3
"""gen_names.py -- writes choralroot_names.h: every external name ChoralRoot's sources define (the emulator's
emu_hal, emu_keymap and emu_fw_* hooks, the engine's cr_* functions ...), renamed to this copy's prefix (FEL(name)),
so they never meet another core's at link time.

    python3 engines/choralroot/gen_names.py      (run from the repository root after an update)

It compiles choralroot_core.c with CR_NAMES_PROBE (the sources as the core includes them, without the rename) and
lists what the object defines with nm (engines/fm1open/gen_names.py does the same for FM1 Open)."""
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
FLAGS = ['-std=gnu11', '-O0', '-w', '-DCR_NAMES_PROBE', '-DFEL_PREFIX=chr_', 
         '-DFEL_BSS_SECTION="__DATA,__felbss"', '-DFEL_DATA_SECTION="__DATA,__feldata"']
INCS = [os.path.join(HERE, 'compat'), os.path.join(HERE, 'generated'), os.path.join(HERE, 'upstream', 'firmware', 'src'),
        os.path.join(HERE, '..', 'felucca'), HERE]


def main():
    with tempfile.TemporaryDirectory() as tmp:
        obj = os.path.join(tmp, 'probe.o')
        cmd = ['clang'] + FLAGS + ['-I' + d for d in INCS] + ['-c', os.path.join(HERE, 'choralroot_core.c'), '-o', obj]
        if subprocess.run(cmd).returncode:
            sys.exit('gen_names: the probe did not compile')
        out = subprocess.run(['nm', '-g', '-j', '--defined-only', obj], capture_output=True, text=True, check=True).stdout
    names = sorted({n[1:] if sys.platform == 'darwin' and n.startswith('_') else n for n in out.split()})
    # the core's own API (FEL(...): chr_init, chr_render ...) is prefixed already
    api = set()
    with open(os.path.join(HERE, '..', 'felucca', 'felucca_core.h')) as f:
        for line in f:
            line = line.strip()
            if line.startswith('X('):
                api.add('chr_' + line.split(',')[1].strip())
    # (and the core's own beside it, chr_restart_take and chr_boot_from: written FEL(...) there too)
    names = [n for n in names if n not in api and not n.startswith('chr_')]
    with open(os.path.join(HERE, 'choralroot_names.h'), 'w') as f:
        f.write('/* choralroot_names.h -- written by gen_names.py: ChoralRoot\'s external names, this copy\'s (FEL) */\n')
        for n in names:
            f.write(f'#define {n} FEL({n})\n')
    print(f'gen_names: {len(names)} names')


if __name__ == '__main__':
    main()
