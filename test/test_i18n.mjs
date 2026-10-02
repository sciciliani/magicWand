// Every text the app shows has a translation in es, pt, it and de, with the
// same {placeholders} and <b> tags as the English. Run: make test
import keys from './i18n_keys.mjs';
const langs = ['es', 'pt', 'it', 'de'];
let fails = 0;
const ph = (s) => [...s.matchAll(/\{(\w+)\}|<\/?b>/g)].map((m) => m[0]).sort().join(' ');
for (const l of langs) {
  const dict = (await import(`../app/lang/${l}.js`)).default;
  const missing = keys.filter((k) => !(k in dict));
  const bad = Object.entries(dict).filter(([k, v]) => ph(k) !== ph(v));
  const unused = Object.keys(dict).filter((k) => !keys.includes(k));
  console.log(`${missing.length || bad.length ? 'FAIL' : 'ok  '}  ${l}: ${Object.keys(dict).length} texts, ${missing.length} missing, ${bad.length} placeholder mismatches${unused.length ? `, ${unused.length} unused` : ''}`);
  missing.slice(0, 5).forEach((k) => console.log('      missing:', k));
  bad.slice(0, 5).forEach(([k, v]) => console.log('      mismatch:', k, '->', v));
  if (missing.length || bad.length) fails++;
}
console.log(fails ? 'FAIL' : 'PASS');
process.exit(fails ? 1 : 0);
