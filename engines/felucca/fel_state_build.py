#!/usr/bin/env python3
"""fel_state_build -- compiles one firmware core (<kind>_core.c) into an object whose state is per
instance (fel_state.py), for the target CMake builds. Run by CMakeLists.txt (fm1_core).

    fel_state_build.py --cc <clang> --src <core.c> --out <core.o> --prefix <tag>_ --work <dir>
        [--target <triple>] [--sysroot <dir>] [--isysroot <dir>] [--arch <a>]... [--min <version>]
        [--pic] [--depfile <file>] [--flag <clang flag>]... [-D<x>]... [-I<dir>]...

For each architecture: clang -S -emit-llvm before optimization, fel_state.py, clang -O2 -c of the
result; with several (a universal macOS build), lipo joins them. In an Xcode build the
architectures, SDK and minimum OS come from Xcode's environment (ARCHS, SDKROOT, PLATFORM_NAME,
*_DEPLOYMENT_TARGET): one configured build serves the device and the simulator.
"""
import argparse
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))


def run(cmd):
    r = subprocess.run(cmd)
    if r.returncode:
        sys.exit(f'fel_state_build: failed ({r.returncode}): {" ".join(cmd)}')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--cc', required=True)
    ap.add_argument('--src', required=True)
    ap.add_argument('--out', required=True)
    ap.add_argument('--prefix', required=True)
    ap.add_argument('--work', required=True)
    ap.add_argument('--target')
    ap.add_argument('--sysroot')
    ap.add_argument('--isysroot')
    ap.add_argument('--arch', action='append', default=[])
    ap.add_argument('--min')
    ap.add_argument('--pic', action='store_true')
    ap.add_argument('--depfile')
    ap.add_argument('--flag', action='append', default=[])
    args, rest = ap.parse_known_args()
    defines_includes = [a for a in rest if a.startswith(('-D', '-I'))]
    unknown = [a for a in rest if not a.startswith(('-D', '-I'))]
    if unknown:
        sys.exit('fel_state_build: unknown arguments: ' + ' '.join(unknown))

    # the targets: (triple or None, arch or None, sysroot flags)
    builds = []
    env = os.environ
    if env.get('SDKROOT') and env.get('PLATFORM_NAME') and env.get('ARCHS'):   # an Xcode build
        platform = env['PLATFORM_NAME']
        os_name, version, suffix = {
            'macosx': ('macos', env.get('MACOSX_DEPLOYMENT_TARGET', ''), ''),
            'iphoneos': ('ios', env.get('IPHONEOS_DEPLOYMENT_TARGET', ''), ''),
            'iphonesimulator': ('ios', env.get('IPHONEOS_DEPLOYMENT_TARGET', ''), '-simulator'),
        }.get(platform, (None, None, None))
        if os_name is None:
            sys.exit('fel_state_build: unknown Xcode platform ' + platform)
        for arch in env['ARCHS'].split():
            builds.append((f'{arch}-apple-{os_name}{version}{suffix}', None, ['-isysroot', env['SDKROOT']]))
    elif args.arch:   # Apple, from CMake: one or more -arch
        roots = ['-isysroot', args.isysroot] if args.isysroot else []
        mins = [f'-mmacosx-version-min={args.min}'] if args.min else []
        for arch in args.arch:
            builds.append((None, arch, roots + mins))
    else:
        roots = ['--sysroot', args.sysroot] if args.sysroot else []
        builds.append((args.target, None, roots))

    os.makedirs(args.work, exist_ok=True)
    objects = []
    for n, (triple, arch, roots) in enumerate(builds):
        tag = arch or (triple.split('-')[0] if triple else 'native')
        ll, rewritten, obj = (os.path.join(args.work, f'core-{tag}{ext}') for ext in ('.ll', '.state.ll', '.o'))
        target = (['--target=' + triple] if triple else []) + (['-arch', arch] if arch else [])
        common = target + roots + (['-fPIC'] if args.pic else []) + args.flag
        front = [args.cc, '-std=c11', '-O2', '-w', '-S', '-emit-llvm', '-Xclang', '-disable-llvm-passes'] + common + defines_includes
        if args.depfile and n == 0:
            front += ['-MD', '-MF', args.depfile, '-MT', args.out]
        run(front + ['-o', ll, args.src])
        run([sys.executable, os.path.join(HERE, 'fel_state.py'), ll, rewritten, args.prefix])
        run([args.cc, '-O2', '-w', '-c'] + common + ['-o', obj, rewritten])
        objects.append(obj)
    if len(objects) == 1:
        os.replace(objects[0], args.out)
    else:
        run(['lipo', '-create', '-output', args.out] + objects)


if __name__ == '__main__':
    main()
