// Generates tests/golden.json from baud girl's own FM-1+VA web modules, so the
// C++ codec is checked against the bytes her tools actually send and accept.
//
// Usage: node tests/gen_golden.mjs <dir with fm1sound.js, fm1seq.js, install/ota.js> <va-presets.syx>
// Her modules are GPL-3.0-or-later and are not vendored here; only the derived
// vectors are.
import { readFileSync, writeFileSync } from "node:fs";
import { pathToFileURL } from "node:url";
import path from "node:path";

const [dir, syxPath] = process.argv.slice(2);
if (!dir || !syxPath) { console.error("usage: gen_golden.mjs <modules dir> <presets.syx>"); process.exit(2); }
const V = await import(pathToFileURL(path.join(dir, "fm1sound.js")).href);
const S = await import(pathToFileURL(path.join(dir, "fm1seq.js")).href);
const O = await import(pathToFileURL(path.join(dir, "install", "ota.js")).href);

const hex = (a) => Array.from(a, (b) => b.toString(16).padStart(2, "0")).join("");
const syx = new Uint8Array(readFileSync(syxPath));
const { sounds, skipped } = V.readSyx(syx);
if (skipped.length) throw new Error("pack skipped: " + skipped.join("; "));

const out = { presets: [], soundRead: {}, memRead: {}, replies: [], identity: [], dx7: {} };

for (const s of sounds) {
  const edit = V.unpackVoice(s.voice);
  out.presets.push({
    slot: s.slot, name: V.voiceName(s.voice), engine: V.engineOf(s.record, s.slot),
    voice: hex(s.voice), edit: hex(edit), record: hex(s.record),
    exact: hex(V.encodeExact(s.slot, s.voice, s.record)),
    dx7voice: hex(V.toDx7Voice(s.voice)),
    repack: hex(V.packVoice(edit)),
  });
}
out.dx7.bank16 = hex(V.toDx7Bank(sounds.map((s) => s.voice)));
out.dx7.initEdit = hex(V.INIT_EDIT);
out.defaultRecord = hex(V.defaultRecord());
out.syxRoundtrip = hex(V.toSyx(sounds)) === hex(syx);
for (const slot of [0, 1, 31, 32, 112, 127]) out.soundRead[slot] = hex(V.encodeSoundRead(slot));
for (const [addr, n] of [[S.SEQ_STEPS_RAM, 256], [S.GSET_RAM, S.GSET_LEN], [0x01C0E840, 1], [0xFFFFFFFF, 256]])
  out.memRead[`${addr}:${n}`] = hex(S.encodeMemRead(addr, n));

// Replies: build the firmware's frame (8-bit buffer, 7-bit LSB-first) and check
// her decoder accepts it, so the C++ decoder is tested on the same bytes.
function pack7(data) {
  const o = []; let acc = 0, nb = 0;
  for (const b of data) { acc |= b << nb; nb += 8; while (nb >= 7) { o.push(acc & 0x7f); acc >>>= 7; nb -= 7; } }
  if (nb) o.push(acc & 0x7f);
  return o;
}
function reply(kind, status, arg, data) {
  const buf = [0x7d, kind, status, arg & 0xff, (arg >> 8) & 0xff, (arg >> 16) & 0xff, (arg >>> 24) & 0xff,
               data.length & 0xff, data.length >> 8, ...data];
  let sum = 0; for (const b of buf) sum += b;
  buf.push((~sum) & 0xff);
  return Uint8Array.from([0xf0, ...pack7(buf), 0xf7]);
}
for (const s of sounds.slice(0, 3)) {
  const frame = reply(0x50, 0, s.slot, [...s.voice, ...s.record]);
  const d = S.decodeReply(frame);
  const back = V.soundFromReply(d);
  if (hex(back.voice) !== hex(s.voice) || hex(back.record) !== hex(s.record)) throw new Error("reply roundtrip failed");
  out.replies.push({ frame: hex(frame), kind: "sound", status: 0, arg: s.slot, data: hex(d.data) });
}
{
  const data = Array.from({ length: 137 }, (_, i) => (i * 37) & 0xff);
  const frame = reply(0x51, 0, S.GSET_RAM, data);
  const d = S.decodeReply(frame);
  out.replies.push({ frame: hex(frame), kind: "mem", status: 0, arg: S.GSET_RAM, data: hex(d.data) });
  const refused = reply(0x50, 1, 5, []);
  const r = S.decodeReply(refused);
  out.replies.push({ frame: hex(refused), kind: "sound", status: r.status, arg: 5, data: "" });
}

// Identity replies: the real FM-1_015 frame recorded by ip2k, and a synthetic FM-1_089.
const real = "f000324558010000234d5a44790526" + "4c1a" + "00".repeat(21) + "2006f7";
out.identity.push({ frame: real, parsed: O.parseIdentity(Uint8Array.from(Buffer.from(real, "hex"))) });
function identityFrame(text) {
  const body = new Array(27).fill(0);
  for (let i = 0; i < text.length; i++) body[i] = text.charCodeAt(i);
  const block = [0x00, 0x59, 0x11, 27, 0, 0, ...body];
  let sum = 0; for (const b of block.slice(6)) sum += b;
  block.push((~sum) & 0xff);
  return Uint8Array.from([0xf0, ...pack7(block), 0xf7]);
}
for (const t of ["FM-1_089", "FM-1_015"]) {
  const f = identityFrame(t);
  out.identity.push({ frame: hex(f), parsed: O.parseIdentity(f) });
}
out.identityQuery = hex(O.HS_QUERY);

writeFileSync(path.join(path.dirname(new URL(import.meta.url).pathname), "golden.json"), JSON.stringify(out, null, 1));
console.log(`wrote golden.json: ${out.presets.length} presets, ${out.replies.length} replies, ${out.identity.length} identities, syxRoundtrip=${out.syxRoundtrip}`);

// ---- sequencer patterns (fm1seq.js) --------------------------------------------
{
  const p = S.emptyPattern();
  p.length = 20; p.rate = 4; p.tempo = 133; p.gate = 70; p.swing = 58; p.sound = 113;
  p.steps[0].notes = [{ note: 36, vel: 100 }, { note: 48, vel: 90 }];
  p.steps[1].notes = [{ note: 200, vel: 300 }];          // clamped
  p.steps[1].rate = 7;
  p.steps[3].notes = Array.from({ length: 11 }, (_, i) => ({ note: 40 + i, vel: 1 + i }));  // trimmed to 9
  p.steps[5].notes = [{ note: 43, vel: 0 }, { note: 43, vel: 50 }];   // duplicate dropped, vel clamped to 1
  p.steps[17].notes = [{ note: 60, vel: 64 }];
  p.steps[19].notes = [{ note: 62, vel: 64 }];
  const seq = { pattern: S.normalise(p), writes: S.encodeWrite(p, 3, true).map(hex),
                writePart0NoSave: hex(S.encodeWritePart(p, 3, 0, false)),
                readRequests: S.readRequests(5), stepTimes: S.stepTimes(S.normalise(p)),
                valueTicks: S.VALUE_TICKS, consts: { steps: S.SEQ_STEPS_RAM, ext: S.SEQ_EXT_RAM, gset: S.GSET_RAM, gsetLen: S.GSET_LEN } };
  // decodePattern on synthetic RAM: 64 steps of 32 B and a 137-B settings block for pattern 2
  const steps = new Uint8Array(64 * 32).fill(0xff);
  const put = (i, notes, rate) => { const s = steps.subarray(i * 32, i * 32 + 32); s.fill(0xff, 0, 10); notes.forEach((n, j) => { s[j] = n.note; s[20 + j] = n.vel; }); s[10] = rate; };
  put(0, [{ note: 36, vel: 100 }], 6); put(2, [{ note: 48, vel: 80 }, { note: 55, vel: 70 }], 0xff); put(40, [{ note: 72, vel: 127 }], 3);
  const gset = new Uint8Array(137);
  const pat = 2;
  gset[118 + 5] = 128 + 9;   // FM-1_093: pattern 6 chains to pattern 10
  gset[98 + pat] = 48; gset[50 + pat] = 5; gset[66 + 2 * pat] = 300 & 0xff; gset[67 + 2 * pat] = 300 >> 8; gset[18 + pat] = 33; gset[34 + pat] = 66; gset[118 + pat] = 7;
  seq.decode = { steps: hex(steps), gset: hex(gset), pat, pattern: S.decodePattern(steps, gset, pat),
                 chained: S.decodePattern(steps, gset, 5) };
  out.seq = seq;
  writeFileSync(path.join(path.dirname(new URL(import.meta.url).pathname), "golden.json"), JSON.stringify(out, null, 1));
  console.log(`added sequencer vectors: ${seq.writes.length} write messages`);
}
