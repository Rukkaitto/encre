// The page's markup and its script agree about every hook they share.
//
//     node tools/test_webhooks.mjs
//
// WHY THIS IS WORTH A TEST. web/encre.js finds everything by attribute --
// `[data-fact="chip"]`, `[data-state-view="checked"]`, `[data-connect]`. A
// typo on either side is silent: querySelector returns null, the guard skips
// it, and the page renders a state with a blank field. Nothing in the build,
// the linter or the Pages deploy looks at both files, and the first thing that
// would notice is somebody with a reader plugged in.
//
// It reads both files as TEXT rather than building a DOM, because this
// repository has no node dependencies and acquiring a DOM library to check
// attribute spelling would be the larger change. That makes it a spelling
// check, not a behaviour check, which is exactly the failure it is for.
//
// Not wired into `make test`; `make test-tools` runs it.
import { readFileSync } from 'node:fs';

const read = (p) => readFileSync(new URL('../web/' + p, import.meta.url), 'utf8');
const js = read('encre.js');
const pages = { 'index.html': read('index.html'), 'recovery.html': read('recovery.html') };

let failures = 0;
const check = (ok, what) => {
  console.log('  ' + (ok ? 'ok  ' : 'FAIL') + ' ' + what);
  if (!ok) failures += 1;
};

// Every `[data-foo="bar"]` and `[data-foo]` the script looks for.
// A selector built by concatenation -- `[data-fact="' + name + '"]` -- has no
// literal value to look for. Those are covered by the named checks further
// down, where the values are actually known.
const valued = [...js.matchAll(/\[data-([a-z-]+)="([^"]+)"\]/g)]
  .map((m) => [m[1], m[2]])
  .filter(([, value]) => !value.includes("' +"));
const bare = [...js.matchAll(/\[data-([a-z-]+)\]/g)].map((m) => m[1]);
// ...and `dataset.x`, which is the same attribute spelled the other way.
const dataset = [...js.matchAll(/dataset\.([a-zA-Z]+)\s*=/g)].map((m) => m[1]);

console.log('every hook the script queries exists in the markup');
const inAnyPage = (needle) => Object.values(pages).some((p) => p.includes(needle));
for (const [attr, value] of valued) {
  const needle = `data-${attr}="${value}"`;
  check(inAnyPage(needle), `${needle} is in a page`);
}
for (const attr of new Set(bare)) {
  check(inAnyPage(`data-${attr}`), `data-${attr} is in a page`);
}

// THE DYNAMIC SELECTORS, WHICH ARE MOST OF THEM. setFact('chip', ...) builds
// `[data-fact="chip"]`, so the literal never appears in the source and the scan
// above cannot see it. Skipping them left the commonest hooks unchecked:
// renaming data-fact="result-chip" in the markup failed nothing at all until
// this block existed.
console.log('\nevery setFact/setStep name has a slot in the markup');
const named = (fn, attr) =>
  [...js.matchAll(new RegExp(fn + "\\('([a-z-]+)'", 'g'))].map((m) => [m[1], attr]);
const setterHooks = [
  ...named('setFact', 'data-fact'),
  ...named('setStep', 'data-step'),
  // setSteps({ connect: 'done', ... }) names the same rows as an object key.
  ...[...js.matchAll(/setSteps\(\{([^}]*)\}/g)]
      .flatMap((m) => [...m[1].matchAll(/([a-z]+):/g)].map((k) => [k[1], 'data-step'])),
];
for (const [name, attr] of new Map(setterHooks.map((h) => [h.join('|'), h])).values()) {
  check(inAnyPage(`${attr}="${name}"`), `${attr}="${name}" is in a page`);
}

console.log('\nevery state the script shows is a section that exists');
// Every quoted string inside a show(...) call, so a state reached through a
// ternary -- show(x === 'mobile' ? 'phone' : 'unsupported') -- counts as shown.
// Matching only show('literal') missed four real sections and reported them as
// unreachable, which is the test being wrong about the code rather than the
// other way round.
// ...with the ternary's CONDITION stripped first, since `refusal === 'mobile'`
// is a test against a value, not a state to show. Leaving it in made the test
// demand a section named "mobile" that correctly does not exist.
const shown = [...js.matchAll(/show\(([^)]*)\)/g)]
  .map((m) => m[1].replace(/[!=]==?\s*'[^']*'/g, ''))
  .flatMap((call) => [...call.matchAll(/'([a-z]+)'/g)].map((q) => q[1]));
for (const state of new Set(shown)) {
  const needle = `data-state-view="${state}"`;
  check(inAnyPage(needle), `show('${state}') has a matching ${needle}`);
}

console.log('\nevery section the markup declares is reachable');
for (const [page, text] of Object.entries(pages)) {
  const declared = [...text.matchAll(/data-state-view="([a-z]+)"/g)].map((m) => m[1]);
  for (const state of new Set(declared)) {
    check(shown.includes(state),
          `${page}'s "${state}" section is shown by some code path`);
  }
}

console.log('\nthe pages load the script as a module');
// reader.js uses `import`, so a classic <script> would fail at parse time and
// the page would render every section hidden.
for (const [page, text] of Object.entries(pages)) {
  check(/<script type="module" src="encre\.js"><\/script>/.test(text),
        `${page} loads encre.js as a module`);
}

console.log('\nthe steps the script drives are rows that exist');
const steps = [...js.matchAll(/(?:connect|check|write|start):\s*'(?:pending|active|done|failed)'/g)];
check(steps.length > 0, 'the script sets step states');
for (const step of ['connect', 'check', 'write', 'start']) {
  check(inAnyPage(`data-step="${step}"`), `data-step="${step}" is in a page`);
}

console.log();
if (failures) {
  console.log(failures + ' failure(s)');
  process.exit(1);
}
console.log('all checks passed');
