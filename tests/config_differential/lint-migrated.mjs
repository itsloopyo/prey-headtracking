// Runs core's canonical config lint over HeadTracking.ini, the committed file, and
// over every distinct CameraUnlock.ini the differential test migrated (the folder
// it names as the argument).
//
// A migrated file may break one rule the committed file may not: it holds the
// player's own value on a global row wherever that value is not the built-in one,
// which is what the migration is for. The lint's rule that such a row holds
// `default` is for the committed file, so that one finding is allowed in a
// migrated file and nothing else is.
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

import { lintCanonicalConfig } from "../../cameraunlock-core/scripts/check-canonical-config.mjs";

const repo = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..", "..");
const migratedDir = process.argv[2];
if (!migratedDir) throw new Error("usage: node lint-migrated.mjs <folder of migrated files>");

const options = { dialect: "native", perGame: [] };
const PLAYER_VALUE =
  /^lines? [^:]+: \S.* (holds a value|hold values), and data\/config-format\.json per_game (does not list it|lists none of them) for this repo;/;
// LogPath is a string row, which the canonical reader stores as the value's bytes. A
// player's log path can hold a byte above 0x7F (a folder name in the ANSI code page),
// and the migration carries it as it is. The lint's ASCII rule is for the committed
// file, so a migrated file may break it on its LogPath line and nowhere else.
const NON_ASCII = /^line (\d+) holds a byte above 0x7F; a canonical file is ASCII only$/;
function onLogPathLine(bytes, problem) {
  const m = NON_ASCII.exec(problem);
  if (!m) return false;
  const line = bytes.toString("latin1").split("\r\n")[Number(m[1]) - 1];
  return line !== undefined && line.startsWith("LogPath=");
}
const failures = [];

for (const problem of lintCanonicalConfig(fs.readFileSync(path.join(repo, "HeadTracking.ini")), options)) {
  failures.push(`HeadTracking.ini: ${problem}`);
}

const files = fs.readdirSync(migratedDir).filter((f) => f.endsWith(".ini"));
if (files.length === 0) throw new Error(`${migratedDir} holds no migrated files`);
let withValues = 0;
for (const file of files) {
  let carried = false;
  const bytes = fs.readFileSync(path.join(migratedDir, file));
  for (const problem of lintCanonicalConfig(bytes, options)) {
    if (onLogPathLine(bytes, problem)) continue;
    if (PLAYER_VALUE.test(problem)) {
      carried = true;
      continue;
    }
    failures.push(`${file}: ${problem}`);
  }
  if (carried) withValues++;
}

if (failures.length > 0) {
  for (const f of failures) console.log(`FAIL ${f}`);
  process.exit(1);
}
console.log(
  `canonical config lint: HeadTracking.ini and ${files.length} migrated files pass` +
    ` (${withValues} hold a player's value on a global row)`,
);
