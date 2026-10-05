// Reads a Felucca device file the plugin wrote with Felucca's own web editor code
// (engines/felucca/upstream/web/fm1backup.js is not vendored: pass its path).
//   node tests/felucca_backup_check.mjs <path to Felucca's web/fm1backup.js> <file.json>
import { readFileSync } from "node:fs";
import { pathToFileURL } from "node:url";
const [, , lib, file] = process.argv;
const { readBackup } = await import(pathToFileURL(lib).href);
const backup = readBackup(readFileSync(file, "utf8"));
console.log(`ok: ${backup.objects.length} objects, ` + backup.objects.map((o) => `${o.id}:${o.size}`).join(" "));
