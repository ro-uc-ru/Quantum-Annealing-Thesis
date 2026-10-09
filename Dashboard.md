# Quantum Annealing Thesis - Dashboard

Read-only view. It parses `specs/*/tasks.md` and never writes to it (AGENTS.md: ticking is the only allowed edit).
Requires the Dataview plugin with JavaScript queries enabled.


## Progress per spec

```dataviewjs
const files = dv.pages('"specs"').where(p => p.file.name === "tasks").sort(p => p.file.path);
const rows = [];
for (const f of files) {
  const text = await dv.io.load(f.file.path);
  const status = (text.match(/^- Status:\s*(\S+)/m) || [])[1] || "?";
  const done = (text.match(/^- \[x\] T-\d+/gm) || []).length;
  const open = (text.match(/^- \[ \] T-\d+/gm) || []).length;
  const total = done + open;
  const pct = total ? Math.round(100 * done / total) : 0;
  const dir = f.file.path.split("/")[1];
  rows.push([dv.fileLink(f.file.path, false, dir), status, `${done}/${total}`, `${pct}%`]);
}
dv.table(["Spec", "Tasks status", "Done", "%"], rows);
```

## Next task per spec

First unticked task whose `After:` tasks are all ticked.

```dataviewjs
const files = dv.pages('"specs"').where(p => p.file.name === "tasks").sort(p => p.file.path);
const rows = [];
for (const f of files) {
  const lines = (await dv.io.load(f.file.path)).split("\n");
  const tasks = [];
  let cur = null;
  for (const line of lines) {
    const m = line.match(/^- \[( |x)\] (T-\d+)/);
    if (m) { cur = { id: m[2], done: m[1] === "x", text: line }; tasks.push(cur); }
    else if (/^(- |#)/.test(line)) cur = null;
    else if (cur) cur.text += " " + line.trim();
  }
  const doneIds = new Set(tasks.filter(t => t.done).map(t => t.id));
  const next = tasks.find(t => {
    if (t.done) return false;
    const after = (t.text.match(/After:\s*([^.]*?)(?:\.|$)/) || [])[1] || "";
    const deps = after.match(/T-\d+/g) || [];
    return deps.every(d => doneIds.has(d));
  });
  if (next) {
    const body = next.text.replace(/^- \[ \] /, "");
    rows.push([f.file.path.split("/")[1], body.slice(0, 220) + (body.length > 220 ? "..." : "")]);
  }
}
dv.table(["Spec", "Next task"], rows);
```

## Tasks flagged for review

```dataviewjs
const files = dv.pages('"specs"').where(p => p.file.name === "tasks");
const rows = [];
for (const f of files) {
  const lines = (await dv.io.load(f.file.path)).split("\n");
  lines.forEach((l, i) => { if (/^\s+Review:/.test(l)) rows.push([f.file.path.split("/")[1], l.trim()]); });
}
dv.table(["Spec", "Review"], rows);
```

## Milestones

- [ ] 2x2 exact-state study (mandatory)
- [ ] 3x3 exact-state study (mandatory)
- [ ] 4x4 exact-state study (expected)
- [ ] 5x5 (stretch)

## Project documents

- [[AGENTS]] - rules for the agent
- [[docs/constitution|Constitution]]
- [[docs/codestyle|Code style]]
- [[specs/active-spec|Active spec pointer]]
- [[README]]

## Modules

`src/core` -> `src/model` -> `src/hamiltonian` -> `src/evolution` -> `src/io` -> `src/cli`
