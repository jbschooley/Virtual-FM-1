#!/usr/bin/env python3
"""fel_state -- one compiled firmware, any number of instances.

    fel_state.py <in.ll> <out.ll> <prefix>

A firmware (Felucca, SLOOP, Melodee) keeps all its state in file-level and function-local statics:
compiled once, it is one synth. <kind>_core.c marks every such variable with
"#pragma clang section bss = ... data = ..." (in LLVM IR, an attribute group with "bss-section" /
"data-section" on each global). This takes the frontend's IR of it (clang -S -emit-llvm -O2
-Xclang -disable-llvm-passes: before optimization, so the code still names each global directly)
and moves every marked global into one struct, %fel.state, reached through a thread-local base
pointer: each function that touches state loads the pointer once at its entry and addresses its
globals as fields of it. Then one compiled copy serves every instance: each instance owns a block
of state_size() bytes, set up by state_init(), and binds it (bind(), a pointer store) on the
thread that calls the firmware, before every call.

It adds, with the copy's prefix:
    void     <prefix>bind(void *state)       this thread's calls into the firmware use this state
    uint64_t <prefix>state_size(void)        the state's bytes (16-aligned)
    int      <prefix>state_init(void *state) the state as the program starts (zeros, and each global's
                                             initializer); 1, or 0 if a field is not where its
                                             alignment needs it (the engine then refuses the copy)

What it handles, and refuses otherwise (a firmware update that needs more fails the build, with
the line, instead of compiling something wrong; docs/ADDING-A-FIRMWARE.md says what to do):
- a use of a marked global in a function, as an operand or inside constant expressions
  (getelementptr, ptrtoint, bitcast, inttoptr): they become instructions at the function's entry
- a marked global initialized with the address of another (a pointer, or an array of pointers,
  plain or into it): set by state_init in each instance's state
- a private constant whose initializer holds such an address (clang's local aggregate
  initializers, "__const.<function>.<name>"): moved into the state as well
Also drops the IR's linker options (the C runtime clang names on Windows: the plugin's build picks it).
"""
import re
import sys

ZEROS = ('zeroinitializer', '0', 'null', 'false', '0.000000e+00')


def fail(msg):
    sys.exit('fel_state: ' + msg)


def split_top(s):
    """'a, {b, c}, d' -> ['a', '{b, c}', 'd'] (commas outside brackets)"""
    out, depth, cur = [], 0, ''
    for c in s:
        if c in '[{<(':
            depth += 1
        elif c in ']}>)':
            depth -= 1
        if c == ',' and depth == 0:
            out.append(cur.strip())
            cur = ''
        else:
            cur += c
    if cur.strip():
        out.append(cur.strip())
    return out


def type_end(body):
    """the index just past the type at the start of body (up to a space outside brackets)"""
    depth = 0
    for i, c in enumerate(body):
        if c in '[{<(':
            depth += 1
        elif c in ']}>)':
            depth -= 1
        elif c == ' ' and depth == 0:
            return i
    return len(body)


STRING = re.compile(r'(?<!@)"(?:[^"\\]|\\.)*"')   # a quoted string that is not a @"name" (inline asm text ...)


def outside_strings(line, fn):
    """fn applied to the parts of line outside quoted strings"""
    out, i = '', 0
    for m in STRING.finditer(line):
        out += fn(line[i:m.start()]) + m.group(0)
        i = m.end()
    return out + fn(line[i:])


def find_paren(s, i):
    """s[i] == '(' -> the index of its ')'"""
    depth = 0
    for j in range(i, len(s)):
        if s[j] == '(':
            depth += 1
        elif s[j] == ')':
            depth -= 1
            if depth == 0:
                return j
    fail('unbalanced parentheses in: ' + s[:120])


def main():
    if len(sys.argv) != 4:
        sys.exit('usage: fel_state.py <in.ll> <out.ll> <prefix>')
    src, dst, prefix = sys.argv[1:4]
    lines = open(src, encoding='utf-8').read().replace('\r\n', '\n').split('\n')

    # ---- the target: sizes and alignments as its data layout says (LLVM's defaults else) ------
    ptr_size, ptr_align = 8, 8
    int_align = {1: 1, 8: 1, 16: 2, 32: 4, 64: 4}
    float_align = {16: 2, 32: 4, 64: 8, 80: 16, 128: 16}
    for l in lines:
        m = re.match(r'^target datalayout = "(.*)"$', l)
        if not m:
            continue
        for spec in m.group(1).split('-'):
            pm = re.match(r'^p(?:0)?:(\d+):(\d+)', spec)
            if pm:
                ptr_size, ptr_align = int(pm.group(1)) // 8, int(pm.group(2)) // 8
            im = re.match(r'^i(\d+):(\d+)', spec)
            if im:
                int_align[int(im.group(1))] = int(im.group(2)) // 8
            fm = re.match(r'^f(\d+):(\d+)', spec)
            if fm:
                float_align[int(fm.group(1))] = int(fm.group(2)) // 8

    types = {}
    for l in lines:
        m = re.match(r'^(%[\w.$"]+) = type (.*)$', l)
        if m:
            types[m.group(1)] = m.group(2)

    def size_align(t):
        t = t.strip()
        if t == 'ptr':
            return ptr_size, ptr_align
        for name, bits in (('half', 16), ('float', 32), ('double', 64), ('x86_fp80', 80), ('fp128', 128)):
            if t == name:
                return (16 if bits == 80 and ptr_size == 8 else 12 if bits == 80 else bits // 8), float_align.get(bits, bits // 8)
        m = re.match(r'^i(\d+)$', t)
        if m:
            bits = int(m.group(1))
            size = 1
            while size * 8 < bits:
                size *= 2
            aligned = [w for w in sorted(int_align) if w >= bits]
            align = int_align[aligned[0]] if aligned else int_align[max(int_align)]
            return max(size, align) if size < align else size, align
        m = re.match(r'^\[(\d+) x (.*)\]$', t)
        if m:
            s, a = size_align(m.group(2))
            return int(m.group(1)) * s, a
        m = re.match(r'^<(\d+) x (.*)>$', t)
        if m:
            s, _ = size_align(m.group(2))
            n = int(m.group(1)) * s
            a = 1
            while a < n:
                a *= 2
            return a, a
        if t.startswith('%'):
            if t not in types:
                fail('unknown type ' + t)
            return size_align(types[t])
        if not (t.startswith('{') and t.endswith('}')) and not (t.startswith('<{') and t.endswith('}>')):
            fail('a type this does not size: ' + t[:80])
        packed = t.startswith('<{')
        inner = t[2:-2] if packed else t[1:-1]
        off, al = 0, 1
        for f in split_top(inner):
            s, a = size_align(f)
            if not packed:
                off = (off + a - 1) // a * a
                al = max(al, a)
            off += s
        if not packed:
            off = (off + al - 1) // al * al
        return off, al

    # ---- the marked globals (attribute groups that carry the section pragma) -----------------
    groups = set()
    for l in lines:
        if l.startswith('attributes #') and ('"bss-section"' in l or '"data-section"' in l):
            groups.add(l.split(' = ')[0][len('attributes '):])
    tok = re.compile(r'@([\w.$]+|"[^"]*")')
    state = {}   # name -> dict(type, init, align, size)
    kept = []
    for l in lines:
        m = re.match(r'^@([\w.$]+|"[^"]*") = (.*)$', l)
        if m and re.search(r'(^| )global ', m.group(2)):
            name, rest = m.group(1), m.group(2)
            hm = re.search(r' (#\d+)\s*$', rest)
            if hm and hm.group(1) in groups:
                if 'thread_local' in rest:
                    fail('a thread-local firmware global: ' + l[:120])
                body = rest.split('global ', 1)[1]
                e = type_end(body)
                t, tail = body[:e], body[e + 1:]
                am = re.search(r', align (\d+)', tail)
                init = tail[:am.start()] if am else tail.split(' #')[0]
                s, a = size_align(t)
                state[name] = dict(type=t, init=init.strip(), align=int(am.group(1)) if am else a, size=s)
                continue
        if l.startswith('!llvm.linker.options'):
            continue
        kept.append(l)
    lines = kept
    if not state:
        fail('no marked globals: is the section pragma in the core file?')

    # private constants whose initializers hold a marked global's address (clang's
    # "__const.<function>.<name>" for a local aggregate initializer): state too
    kept = []
    for l in lines:
        m = re.match(r'^@([\w.$]+|"[^"]*") = (?:private |internal )(?:unnamed_addr )?constant (.*)$', l)
        if m and any(t.group(1) in state for t in tok.finditer(m.group(2))):
            body = m.group(2)
            e = type_end(body)
            t, tail = body[:e], body[e + 1:]
            am = re.search(r', align (\d+)', tail)
            init = tail[:am.start()] if am else tail
            s, a = size_align(t)
            state[m.group(1)] = dict(type=t, init=init.strip(), align=int(am.group(1)) if am else a, size=s)
            continue
        kept.append(l)
    lines = kept

    for n, g in state.items():   # (each instance's block is 16-aligned: a field cannot need more)
        if g['align'] > 16:
            fail(f'{n} needs alignment {g["align"]}: the state is only 16-aligned')

    # ---- the state struct: fields by falling alignment, padding where one needs more ---------
    order = sorted(state, key=lambda n: (-state[n]['align'], n))
    fields, off, index = [], 0, {}
    for n in order:
        g = state[n]
        pad = (-off) % g['align']
        if pad:
            fields.append(f'[{pad} x i8]')
            off += pad
        index[n] = len(fields)
        fields.append(g['type'])
        off += g['size']
    size = (off + 15) // 16 * 16
    if size > off:
        fields.append(f'[{size - off} x i8]')

    def ident(n):
        return re.sub(r'[^\w]', '_', n.strip('"'))

    # ---- functions: each state reference through the base pointer ---------------------------
    counter = [0]
    cexpr = re.compile(r'(getelementptr(?: inbounds| nuw| nusw)*|ptrtoint|bitcast|inttoptr) \(')

    def rewrite_function(body):
        used = sorted({t.group(1) for l in body[1:] for t in tok.finditer(l) if t.group(1) in state})
        if not used:
            return body
        entry = ['  %fel.tls = call ptr @llvm.threadlocal.address.p0(ptr @fel_state_base)',
                 '  %fel.b = load ptr, ptr %fel.tls, align ' + str(ptr_align)]
        gname = {}
        for n in used:
            gname[n] = '%fel.g.' + ident(n)
            entry.append(f'  {gname[n]} = getelementptr inbounds %fel.state, ptr %fel.b, i32 0, i32 {index[n]}')

        def value(v):
            v = v.strip()
            m = tok.fullmatch(v)
            if m and m.group(1) in state:
                return gname[m.group(1)]
            if cexpr.match(v) and any(t.group(1) in state for t in tok.finditer(v)):
                return materialize(v)
            return v

        def operand(p):
            p = p.strip()
            m = re.match(r'^(\S+) (.*)$', p)
            return f'{m.group(1)} {value(m.group(2))}' if m else p

        def materialize(expr):
            head = expr[:expr.index('(')].strip()
            inner = expr[expr.index('(') + 1:find_paren(expr, expr.index('('))]
            counter[0] += 1
            name = f'%fel.ce{counter[0]}'
            if head.startswith('getelementptr'):
                parts = split_top(inner)
                entry.append(f'  {name} = {head} {parts[0]}, {", ".join(operand(p) for p in parts[1:])}')
            else:   # ptrtoint / bitcast / inttoptr (<ty> <value> to <ty>)
                m = re.match(r'^(.*) to (\S+)$', inner)
                if not m:
                    fail('unhandled constant expression: ' + expr[:120])
                entry.append(f'  {name} = {head} {operand(m.group(1))} to {m.group(2)}')
            return name

        def rewrite_line(l):
            if not any(t.group(1) in state for t in tok.finditer(l)):
                return l
            out, i = '', 0
            while True:
                m = cexpr.search(l, i)
                if not m:
                    out += l[i:]
                    break
                j = find_paren(l, m.end() - 1)
                expr = l[m.start():j + 1]
                out += l[i:m.start()] + (materialize(expr) if any(t.group(1) in state for t in tok.finditer(expr)) else expr)
                i = j + 1
            # (a phi may name state: its replacement is made at the function's entry, which comes
            # before every block)
            if re.search(r'\b(switch|indirectbr|blockaddress)\b', l) and any(t.group(1) in state for t in tok.finditer(out)):
                fail('a state reference the rewrite does not handle: ' + l.strip()[:120])
            return outside_strings(out, lambda part: tok.sub(lambda t: gname[t.group(1)] if t.group(1) in state else t.group(0), part))

        rest = [rewrite_line(l) for l in body[1:]]
        new = [body[0]]
        k = 1 if rest and rest[0].rstrip().endswith(':') else 0   # (an entry label first)
        new.extend(rest[:k])
        new.extend(entry)
        new.extend(rest[k:])
        return new

    out, i = [], 0
    while i < len(lines):
        if lines[i].startswith('define '):
            j = i
            while lines[j] != '}':
                j += 1
            out.extend(rewrite_function(lines[i:j + 1]))
            i = j + 1
        else:
            out.append(lines[i])
            i += 1
    for l in out:   # anything else naming state (another global's initializer, metadata): not handled
        if not l.startswith('define ') and any(t.group(1) in state for t in tok.finditer(l)) and not l.startswith(';'):
            if re.match(r'^\s', l):
                continue
            fail('a state reference outside a function: ' + l[:160])

    # ---- the API ------------------------------------------------------------------------------
    init = []   # (the "ok" block's instructions: after the layout check below passes)
    consts = []
    for n in order:
        g = state[n]
        if g['init'] in ZEROS:
            continue
        f = f'%f.{ident(n)}'
        init.append(f'  {f} = getelementptr inbounds %fel.state, ptr %p, i32 0, i32 {index[n]}')
        if not any(t.group(1) in state for t in tok.finditer(g['init'])):   # a constant: copied in
            consts.append(f'@fel.init.{ident(n)} = private unnamed_addr constant {g["type"]} {g["init"]}, align {g["align"]}')
            init.append(f'  call void @llvm.memcpy.p0.p0.i64(ptr align {g["align"]} {f}, ptr align {g["align"]} @fel.init.{ident(n)}, i64 {g["size"]}, i1 false)')
            continue

        def address(val, tag):   # 'ptr @g' / 'ptr getelementptr (T, ptr @g, ...)' / a constant -> an SSA value
            val = val.strip()
            if val.startswith('ptr '):
                val = val[4:].strip()
            m = tok.fullmatch(val)
            if m and m.group(1) in state:
                init.append(f'  %a.{tag} = getelementptr inbounds %fel.state, ptr %p, i32 0, i32 {index[m.group(1)]}')
                return f'%a.{tag}'
            gm = re.fullmatch(r'getelementptr((?: inbounds| nuw| nusw)*) \((.*)\)', val)
            if gm:
                parts = split_top(gm.group(2))
                base = re.fullmatch(r'ptr @([\w.$]+|"[^"]*")', parts[1].strip())
                if base and base.group(1) in state:
                    init.append(f'  %b.{tag} = getelementptr inbounds %fel.state, ptr %p, i32 0, i32 {index[base.group(1)]}')
                    init.append(f'  %a.{tag} = getelementptr{gm.group(1)} {parts[0]}, ptr %b.{tag}, {", ".join(parts[2:])}')
                    return f'%a.{tag}'
            if any(t.group(1) in state for t in tok.finditer(val)):
                fail(f'an initializer of {n} the rewrite does not handle: {val[:120]}')
            return val

        if g['type'] == 'ptr':
            init.append(f'  store ptr {address(g["init"], ident(n))}, ptr {f}, align {g["align"]}')
            continue
        am = re.fullmatch(r'\[(\d+) x ptr\]', g['type'])
        bm = re.fullmatch(r'\[(.*)\]', g['init'])
        if not am or not bm:
            fail(f'an initializer of {n} the rewrite does not handle (only a pointer, or an array of them): {g["init"][:120]}')
        for k, el in enumerate(split_top(bm.group(1))):
            v = address(el, f'{ident(n)}.{k}')
            init.append(f'  %e.{ident(n)}.{k} = getelementptr inbounds [{am.group(1)} x ptr], ptr {f}, i32 0, i32 {k}')
            init.append(f'  store ptr {v}, ptr %e.{ident(n)}.{k}, align {ptr_align}')
    # each field where its alignment needs it, and the size, as LLVM lays the struct out (the sizes
    # above are this tool's reading of the data layout): checked first, so a block of the wrong size
    # is never written
    check, ok = [], 'true'
    for n in order:
        a = state[n]['align']
        if a <= 1:
            continue
        counter[0] += 1
        c = counter[0]
        check.append(f'  %o{c} = ptrtoint ptr getelementptr (%fel.state, ptr null, i32 0, i32 {index[n]}) to i64')
        check.append(f'  %r{c} = urem i64 %o{c}, {a}')
        check.append(f'  %z{c} = icmp eq i64 %r{c}, 0')
        check.append(f'  %k{c} = and i1 {ok}, %z{c}')
        ok = f'%k{c}'
    check.append('  %sz = ptrtoint ptr getelementptr (%fel.state, ptr null, i32 1) to i64')
    check.append(f'  %szok = icmp eq i64 %sz, {size}')
    check.append(f'  %all = and i1 {ok}, %szok')
    init = ([f'define i32 @{prefix}state_init(ptr %p) {{'] + check + ['  br i1 %all, label %ok, label %bad', 'bad:', '  ret i32 0', 'ok:',
             f'  call void @llvm.memset.p0.i64(ptr align 16 %p, i8 0, i64 {size}, i1 false)'] + init + ['  ret i32 1', '}'])

    api = ['%fel.state = type { ' + ', '.join(fields) + ' }',
           f'@fel_state_base = internal thread_local global ptr null, align {ptr_align}'] + consts + [
        f'define void @{prefix}bind(ptr %p) {{',
        '  %t = call ptr @llvm.threadlocal.address.p0(ptr @fel_state_base)',
        f'  store ptr %p, ptr %t, align {ptr_align}',
        '  ret void', '}',
        f'define i64 @{prefix}state_size() {{', f'  ret i64 {size}', '}'] + init
    declared = {m.group(1) for l in out for m in [re.match(r'^declare .*?(@[\w.]+)\(', l)] if m}
    for d in ('declare ptr @llvm.threadlocal.address.p0(ptr)', 'declare void @llvm.memset.p0.i64(ptr, i8, i64, i1)',
              'declare void @llvm.memcpy.p0.p0.i64(ptr, ptr, i64, i1)'):
        if re.match(r'^declare .*?(@[\w.]+)\(', d).group(1) not in declared:
            api.append(d)
    at = 0
    for k, l in enumerate(out):   # after the type definitions (the struct names them)
        if l.startswith('target ') or re.match(r'^%[\w.$"]+ = type ', l):
            at = k + 1
    out[at:at] = api
    open(dst, 'w', encoding='utf-8').write('\n'.join(out))
    print(f'fel_state: {len(state)} globals -> one state of {size} bytes; {counter[0]} constant expressions and checks')


if __name__ == '__main__':
    main()
