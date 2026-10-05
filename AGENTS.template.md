# <Project Name>

## Purpose
<What it is and why it exists, in one sentence>. <Architecture style> built with <stack, names only>.

## Rules
> Hard rules every agent follows in this repo. Keep it short (5-8 bullets).
> Code style does NOT go here; it lives in docs/conventions.md (or whatever
> file you chose; see README "Which file holds which rule?").
> Write each rule as an instruction the agent can obey or break, not as advice.
> The bullets below are EXAMPLES. Delete every line marked (example) before
> use: the agent WILL follow anything left here, including the silly ones.
> The two nonsense ones are there so you notice they must go.
- (example) Read docs/constitution.md and specs/active-spec.md before any work.
- (example) Follow docs/conventions.md for all code style.
- (example, nonsense) Never run the app on a Tuesday.
- (example, nonsense) Ask the office plant for approval before renaming any file.

## Stack
- Language:   <name> <x.y>
- Framework:  <name> <x.y>
- DB:         <name> <x.y>
- Tests:      <name> <x.y>
- Lint:       <name> <x.y>

### Modules and dependencies
- <module-a>   <- <what it does>. Depends on: <lib/service>
- <module-b>   <- <what it does>. Depends on: <lib/service>
- <key-lib>    <- <why it is used, e.g. auth, queue, ORM>

## Commands
```bash
<install cmd>          # install dependencies
<run cmd>              # run locally
<build cmd>            # production build
<deploy cmd>           # deploy (only if needed)
<test cmd>             # all tests
<test single cmd>      # one test / file
<test watch cmd>       # watch mode
<lint cmd>             # lint + format
<migrate cmd>          # DB migrate
<seed cmd>             # DB seed
```

### Setup notes
Only list commands that need prior setup. Delete this block if none do.
- `<run cmd>`:     requires <env vars / .env file / local service, e.g. DB on :5432>
- `<test cmd>`:    requires <test DB / mocks / env flag>
- `<deploy cmd>`:  requires <credentials / target env / CI only>
- `<migrate cmd>`: requires <DB running and DATABASE_URL set>
