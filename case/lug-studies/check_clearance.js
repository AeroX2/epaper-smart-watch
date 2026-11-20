// Tests every case shape in index.html against the real display, PCB and battery.
//   node case/lug-studies/check_clearance.js
// Cover = how far a point on a part sits inside the outer skin of the case. Negative = the part pokes out.
// Left out: the display front under the window, the USB receptacle in its slot, the unfolded flat cable.
const fs = require("fs"), path = require("path");
const html = fs.readFileSync(path.join(__dirname, "index.html"), "utf8");
const src = html.slice(html.indexOf("  function smin("), html.indexOf("  // surface nets"));
const F = new Function("LID_Y", src + "; return {sdfG, sdfD, slotCut};")(-9.08);
const P = JSON.parse(fs.readFileSync(path.join(__dirname, "internals-points.json"), "utf8"));
for (const [label, sdf] of [["Sculpted proposal", F.sdfG], ["Swept lugs, flat top", F.sdfD]]) {
  console.log(label);
  for (const part of Object.keys(P)) {
    let min = 99, at = null, n = 0, out = 0;
    for (const [X, Y, Z] of P[part]) {
      if (part === "display" && X < -20.6) continue;
      if (Math.hypot(X, Y) < 15.5 && Z > -1.2) continue;   // display front at the window edge: distance there is sideways to the opening
      if (F.slotCut(X, Z, Y) < 0.3) continue;
      const cover = -sdf(X, Z, Y); n++;
      if (cover < 0) out++;
      if (cover < min) { min = cover; at = [X, Y, Z]; }
    }
    console.log(`  ${part.padEnd(8)} ${n} points, ${out} outside, least cover ${min.toFixed(2)} mm at X=${at[0]} Y=${at[1]} Z=${at[2]}`);
  }
}
