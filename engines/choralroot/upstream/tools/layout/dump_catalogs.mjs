// SPDX-License-Identifier: GPL-3.0-only
// ChoralRoot FM-1 layout tool: today's built-in layouts as layout files (docs/LAYOUT-TOOL.md, "The file format").
//   node tools/layout/dump_catalogs.mjs            writes design/layouts/<engine>.json for every engine the unit shows
//   node tools/layout/dump_catalogs.mjs --check    writes nothing: exit 1 when a file is not what the firmware says
// The text is lay_catalog's answer (build/layout-web/crlayout.wasm: sh tools/layout/build_layout.sh first) through
// web/fm1layout.js formatLayout. web/test_layout.mjs makes the same comparison, so the files cannot rot.
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { wrap, formatLayout, layoutFileName } from "../../web/fm1layout.js";

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "../..");
const DIR = path.join(ROOT, "design/layouts");

// [{name, file, text}] for every engine of the module
export async function layoutFiles() {
  await import(path.join(ROOT, "build/layout-web/crlayout.js"));
  const m = wrap(await globalThis.CRLayoutModule.load(fs.readFileSync(path.join(ROOT, "build/layout-web/crlayout.wasm"))));
  return m.version().engines.map((e) => ({ name: e.name, file: path.join(DIR, layoutFileName(e.name)), text: formatLayout(m.catalog(e.index)) }));
}
// the files that are not what the firmware says: [{file, why}]
export async function staleLayoutFiles() {
  const files = await layoutFiles();
  const stale = files.filter((f) => !fs.existsSync(f.file) || fs.readFileSync(f.file, "utf8") !== f.text)
    .map((f) => ({ file: path.relative(ROOT, f.file), why: fs.existsSync(f.file) ? "differs from the firmware's tables" : "missing" }));
  const known = new Set(files.map((f) => path.basename(f.file)));
  for (const n of fs.existsSync(DIR) ? fs.readdirSync(DIR) : [])
    if (n.endsWith(".json") && !known.has(n)) stale.push({ file: `design/layouts/${n}`, why: "no engine of the firmware has this file" });
  return { files, stale };
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  if (process.argv.includes("--check")) {
    const { files, stale } = await staleLayoutFiles();
    for (const s of stale) console.log(`stale: ${s.file}: ${s.why}`);
    console.log(stale.length ? "FAIL: node tools/layout/dump_catalogs.mjs writes them again" : `ok: ${files.length} layout files are the firmware's`);
    process.exit(stale.length ? 1 : 0);
  }
  fs.mkdirSync(DIR, { recursive: true });
  for (const f of await layoutFiles()) {
    fs.writeFileSync(f.file, f.text);
    console.log(`${path.relative(ROOT, f.file)}  ${f.text.length} B  (${f.name})`);
  }
}
