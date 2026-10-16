// SPDX-License-Identifier: GPL-3.0-only
// ChoralRoot FM-1: P5's editor as the unit shows it, walked on the browser emulator, as one PNG for the owner
// (docs/LAYOUT-TOOL.md "Step 4"):
//   node tools/layout/emu_sheet.mjs design/choralroot-fm1-p5-rows-emulator.png [--preset N] [--cols N]
// The emulator (build/emu-web/choralroot.wasm: sh tools/emu/web/build_web.sh) is driven by its panel alone: OPT +
// PRESETS to P5 (its pool position N, default 1), EDIT, each group's button, SELECT through every screen x lane the
// unit's editor has (the layout module's catalog of P5: the compiled rows, design/layouts/designs/p5-rows.json, in OSC
// .. MOD, MOD's EXPR after them, FX and MIX the platform's); its LCD at each stop, tiled and captioned as
// tools/layout/render_sheet.mjs tiles a design (the captions drawn by the layout module, build/layout-web/crlayout.wasm).
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";
import * as L from "../../web/fm1layout.js";
import { composeSheet } from "./render_sheet.mjs";

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "../..");
const B = { FX: 0, SEL: 1, ENV: 2, LFO: 3, EDIT: 4, GLO: 5, SEQ: 9, PLAY: 10, REC: 11 }, E = { SELECT: 0, PRESETS: 2 };
const BTN = ["FX", "SEL", "ENV", "LFO", "SEQ", "PLAY", "REC"];

export async function emuSheet({ preset = 1, cols = 4 } = {}) {
  await import(path.join(ROOT, "build/layout-web/crlayout.js"));
  const m = L.wrap(await globalThis.CRLayoutModule.load(fs.readFileSync(path.join(ROOT, "build/layout-web/crlayout.wasm"))));
  const ver = m.version(), rank = ver.engines.findIndex((e) => e.name === "P5");
  const cat = m.catalog(ver.engines[rank].index);
  const x = await globalThis.CRLayoutModule.load(fs.readFileSync(path.join(ROOT, "build/emu-web/choralroot.wasm")));
  const tap = (b) => { x.web_buttons(1 << B[b]); x.web_step(100); x.web_buttons(0); x.web_step(300); };
  const rgba = () => {
    const fb = new Uint16Array(x.memory.buffer, x.web_fb(), 240 * 240), o = new Uint8ClampedArray(240 * 240 * 4);
    for (let i = 0; i < 240 * 240; i++) {
      const c = ((fb[i] >> 8) | (fb[i] << 8)) & 0xffff;
      o[4 * i] = ((c >> 11) * 255 / 31) | 0; o[4 * i + 1] = (((c >> 5) & 63) * 255 / 63) | 0; o[4 * i + 2] = ((c & 31) * 255 / 31) | 0; o[4 * i + 3] = 255;
    }
    return o;
  };
  x.web_boot();
  x.web_step(1000);
  x.web_buttons(1 << B.GLO);                   // OPT held + PRESETS: the engine (its pool position 1), on OPT's release
  x.web_step(100);
  x.web_enc(E.PRESETS, rank);
  x.web_step(100);
  x.web_buttons(0);
  x.web_step(1500);
  for (let k = 1; k < preset; k++) { x.web_enc(E.PRESETS, 1); x.web_step(300); }
  tap("EDIT");
  let group = 0;
  const tiles = L.tiles(cat).map((t) => {
    if (t.group !== group) { tap(BTN[t.group]); group = t.group; }
    const r = { rgba: rgba(), caption: L.tileLabel(t) };
    x.web_enc(E.SELECT, 1);                    // the next lane; past the last: the next screen's first
    x.web_step(100);
    return r;
  });
  return { ...composeSheet(m, `P5 · the unit's editor (the browser emulator, ${ver.firmware})`, tiles, { cols }), tiles };
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  const a = process.argv.slice(2), files = a.filter((v, i) => !v.startsWith("--") && !(i && a[i - 1].startsWith("--")));
  const opt = (k, d) => (a.includes(k) ? +a[a.indexOf(k) + 1] : d);
  if (files.length !== 1) {
    console.log("usage: node tools/layout/emu_sheet.mjs <out.png> [--preset N] [--cols N]");
    process.exit(2);
  }
  const r = await emuSheet({ preset: opt("--preset", 1), cols: opt("--cols", 4) });
  fs.writeFileSync(files[0], r.png);
  console.log(`${files[0]}: ${r.width} x ${r.height}, ${r.tiles.length} tiles (${r.tiles.map((t) => t.caption).join(", ")})`);
}
