// SPDX-License-Identifier: MPL-2.0
// Copyright (C) 2026 Charles Vestal
// The browser build in Node: boot it, press a few keys, expect sound and screen frames.
import fs from "fs";
const wasm = fs.readFileSync(process.argv[2]);
let mem;
const wasi = new Proxy({ clock_time_get: (id, p, out) => { new DataView(mem.buffer).setBigUint64(out, 0n, true); return 0; },
  fd_write: (fd, iov, n, out) => { new DataView(mem.buffer).setUint32(out, 0, true); return 0; } }, { get: (t, k) => k in t ? t[k] : () => 0 });
const { instance } = await WebAssembly.instantiate(wasm, { wasi_snapshot_preview1: wasi, env: new Proxy({}, { get: () => () => 0 }) });
const ex = instance.exports; mem = ex.memory;
if (ex._initialize) ex._initialize();
ex.web_master(2800); ex.web_boot();
let peak = 0;
const WHITE = [0,2,4,6,7,9,11,12,14,16,18,19,21,23,24,26];
for (let i = 0; i < 400; i++) {
  if (i % 20 === 0 && i < 340) ex.web_keys(1 << WHITE[(i / 20) % 16]);
  ex.web_render(128);
  const l = new Float32Array(mem.buffer, ex.web_out_l(), 128);
  for (const v of l) peak = Math.max(peak, Math.abs(v));
}
console.log("peak", peak.toFixed(3), "blits", ex.web_blits());
// The keys, played, on the Melody page in Keys: what the page's mouse and computer keys do (web_keys, index.html's syncKeys), a Shift-click's key down until clicked again the same: the note goes out, comes up with it, and the scale stays
let bad = peak > 0.05 && peak < 1 ? 0 : 1;
const B_ARP = 8;
const blocks = (n) => { for (let i = 0; i < n; i++) ex.web_render(128); };
const notes = () => {
  const p = new Uint32Array(mem.buffer, ex.web_out_pkts(), ex.web_out_n()), r = [];
  for (const x of p) { const st = (x >> 8) & 255; if ((st & 0xEF) === 0x81) r.push([st, (x >> 16) & 255]); }
  return r;
};
ex.web_keys(0); blocks(40);
ex.web_buttons(1 << B_ARP); blocks(20); ex.web_buttons(0); blocks(40);
const lit0 = ex.web_lit_keys(), dim0 = ex.web_dim_keys();
if (!lit0 || !dim0 || (lit0 & dim0)) { console.log("Melody page: lit", lit0, "at the middle level", dim0); bad = 1; }
ex.web_out_clear();
ex.web_keys(1 << 2); blocks(20);
let n = notes();
if (n.length !== 1 || n[0][0] !== 0x91 || n[0][1] !== 67) { console.log("a key down:", JSON.stringify(n)); bad = 1; }
if (!(ex.web_lit_keys() & 1 << 2)) { console.log("a key down: not lit"); bad = 1; }
ex.web_out_clear();
ex.web_keys(1 << 2 | 1 << 5); blocks(20);                 // a second, the first kept down: a chord with a mouse
ex.web_keys(1 << 5); blocks(20);
n = notes();
if (n.length !== 2 || n[0][0] !== 0x91 || n[0][1] !== 70 || n[1][0] !== 0x81 || n[1][1] !== 67) { console.log("two keys:", JSON.stringify(n)); bad = 1; }
ex.web_keys(0); blocks(20);
if (ex.web_lit_keys() !== lit0 || ex.web_dim_keys() !== dim0) { console.log("the scale moved"); bad = 1; }
console.log("the keys on the Melody page:", bad ? "FAILED" : "ok");
process.exit(bad);
