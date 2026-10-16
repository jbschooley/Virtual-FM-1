// SPDX-License-Identifier: GPL-3.0-only
// ChoralRoot FM-1 layout tool: a design's contact sheet as one PNG (docs/LAYOUT-TOOL.md, "The p5 style").
//   node tools/layout/render_sheet.mjs design/layouts/designs/p5-panel.json design/choralroot-fm1-p5-panel-screens.png
//        [--preset N] [--cols N]
// Every screen x lane of the design, as the designer previews it (web/fm1layout.js designSession on the preview
// module, build/layout-web/crlayout.wasm: sh tools/layout/build_layout.sh first), at 2x (nearest neighbour) in a grid,
// each tile captioned (group, screen.lane, title) in the firmware's own face, drawn by the module too. Prints each
// tile's lint (the firmware's text lint and the design's checks). No npm package: the PNG is written on node's zlib.
import fs from "node:fs";
import path from "node:path";
import zlib from "node:zlib";
import { fileURLToPath } from "node:url";
import * as L from "../../web/fm1layout.js";

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "../..");

// ------------------------------------------------------------------- PNG out ---
const CRC = new Uint32Array(256).map((_, n) => { let c = n; for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1; return c >>> 0; });
function crc32(b) { let c = 0xffffffff; for (const x of b) c = CRC[(c ^ x) & 255] ^ (c >>> 8); return (c ^ 0xffffffff) >>> 0; }
function chunk(type, data) {
  const b = Buffer.alloc(12 + data.length);
  b.writeUInt32BE(data.length, 0);
  b.write(type, 4, "latin1");
  data.copy(b, 8);
  b.writeUInt32BE(crc32(b.subarray(4, 8 + data.length)), 8 + data.length);
  return b;
}
// w x h RGB (3 bytes a pixel) -> PNG bytes (8-bit RGB, filter 0 on every row)
export function encodePng(w, h, rgb) {
  const raw = Buffer.alloc(h * (w * 3 + 1));
  for (let y = 0; y < h; y++) rgb.copy(raw, y * (w * 3 + 1) + 1, y * w * 3, (y + 1) * w * 3);
  const ihdr = Buffer.alloc(13);
  ihdr.writeUInt32BE(w, 0); ihdr.writeUInt32BE(h, 4);
  ihdr[8] = 8; ihdr[9] = 2;
  return Buffer.concat([Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]), chunk("IHDR", ihdr),
                        chunk("IDAT", zlib.deflateSync(raw, { level: 9 })), chunk("IEND", Buffer.alloc(0))]);
}

// ------------------------------------------------------------------ the sheet ---
// -> {png, tiles: [{caption, lint: [..]}]}
export async function renderSheet(design, { preset = 1, cols = 4, scale = 2 } = {}) {
  await import(path.join(ROOT, "build/layout-web/crlayout.js"));
  const m = L.wrap(await globalThis.CRLayoutModule.load(fs.readFileSync(path.join(ROOT, "build/layout-web/crlayout.wasm"))));
  const cat = m.catalog(design.engine.index);
  const v = L.validateDesign(design, cat);
  if (v.errors.length) throw new Error(`the design is not valid: ${v.errors.join("; ")}`);
  const s = L.designSession(m, cat, design, preset);
  s.reload();
  const tiles = L.designTiles(design).map((t) => {
    s.open(t.gi, t.si, t.lane);
    const r = s.render();
    const lint = r.lint.findings.map((f) => `${f.kind} "${f.text}"`).concat(r.checks.map((c) => `${c.kind}: ${c.text}`));
    return { t, rgba: r.rgba, lint, caption: `${t.group} ${t.si + 1}.${t.lane + 1} · ${t.title}` };
  });
  const title = `${design.engine.name} · ${design.name}${design.style ? " · style " + design.style : ""}`;
  return { ...composeSheet(m, title, tiles, { cols, scale }), tiles };
}

// the tiles ({rgba: 240 x 240 RGBA, caption}) in a grid at `scale` (nearest neighbour), each captioned under it and the
// sheet titled, the texts drawn by the module m (the firmware's face) -> {png, width, height} (tools/layout/emu_sheet.mjs
// tiles the emulator's frames the same way)
export function composeSheet(m, title, tiles, { cols = 4, scale = 2 } = {}) {
  // a caption: a text drawn by the module on a free page (rows CAP0 .. CAP0 + CAPH: below the title line)
  const CAP0 = 60, CAPH = 16;
  const strip = (text, px) => m.draw("", "", [{ op: "text", bold: 1, px, x: 2, y: CAP0 + 12, s: text, col: L.COLOURS.text, maxw: 236 }]);
  const blank = m.draw("", "", []), bg = [blank[(200 * 240 + 120) * 4], blank[(200 * 240 + 120) * 4 + 1], blank[(200 * 240 + 120) * 4 + 2]];
  const T = L.SCREEN * scale, gap = 8 * scale, cap = CAPH * scale + 4, top = CAPH * scale + 2 * gap;
  const rows = Math.ceil(tiles.length / cols), W = gap + cols * (T + gap), H = top + rows * (T + cap + gap);
  const out = Buffer.alloc(W * H * 3);
  for (let i = 0; i < W * H; i++) out.set(bg, i * 3);
  const blit = (rgba, sx, sy, sw, sh, dx, dy) => {   // rows sy .. sy + sh of a 240-wide picture at (dx, dy), x scale
    for (let y = 0; y < sh * scale; y++)
      for (let x = 0; x < sw * scale; x++) {
        const X = dx + x, Y = dy + y;
        if (X >= W || Y >= H) continue;
        const k = ((sy + ((y / scale) | 0)) * 240 + sx + ((x / scale) | 0)) * 4;
        out[(Y * W + X) * 3] = rgba[k]; out[(Y * W + X) * 3 + 1] = rgba[k + 1]; out[(Y * W + X) * 3 + 2] = rgba[k + 2];
      }
  };
  blit(strip(title, 12), 0, CAP0, 240, CAPH, gap, gap);
  tiles.forEach((x, i) => {
    const dx = gap + (i % cols) * (T + gap), dy = top + Math.floor(i / cols) * (T + cap + gap);
    blit(x.rgba, 0, 0, 240, 240, dx, dy);
    blit(strip(x.caption, 11), 0, CAP0, 240, CAPH, dx, dy + T + 2);
  });
  return { png: encodePng(W, H, out), width: W, height: H };
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  const a = process.argv.slice(2), files = a.filter((x, i) => !x.startsWith("--") && !(i && a[i - 1].startsWith("--")));
  const opt = (k, d) => (a.includes(k) ? +a[a.indexOf(k) + 1] : d);
  if (files.length !== 2) {
    console.log("usage: node tools/layout/render_sheet.mjs <design.json> <out.png> [--preset N] [--cols N]");
    process.exit(2);
  }
  const design = JSON.parse(fs.readFileSync(files[0], "utf8"));
  const r = await renderSheet(design, { preset: opt("--preset", 1), cols: opt("--cols", 4) });
  fs.writeFileSync(files[1], r.png);
  let faults = 0;
  for (const x of r.tiles) {
    console.log(`  ${x.lint.length ? "lint" : "ok  "}  ${x.caption}${x.lint.length ? ": " + x.lint.join("; ") : ""}`);
    faults += x.lint.length;
  }
  console.log(`${files[1]}: ${r.width} x ${r.height}, ${r.tiles.length} tiles, ${faults ? faults + " lint findings" : "lint clean"}`);
}
