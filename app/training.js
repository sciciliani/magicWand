// training.js — the Training tab: a wizard teaches three fixed moves (Circle,
// Hit, ZigZag), checks every attempt from the wand's motion stream, then lets
// you give each move an action from the remote library.
//
// For each move: the wizard explains it while the example plays ONCE, then
// every attempt: "Ready… one Circle… now!" (hold still until "now"), the wand
// buzzes on "now", you do ONE move and hold still, he reads it and answers.
//
// The moves are checked here, in the app (training-moves.js). While practising
// the wand is in PRACTICE mode: it still sees moves but never fires a spell,
// so nothing gets sent to the TV by accident.
import { MOVES, MoveCatcher, measure, judge, describe } from './training-moves.js?v=2';
import { t, lang, SPEECH_TAG, ascii, allTranslations } from './i18n.js?v=2';

const ORDER = ['circle', 'hit', 'zigzag'];
const NEEDED = 3;            // good attempts per move
const MOVING_ENERGY = 90;    // above this while counting = the wand moved too early (≈ deg/s)
const BEAT_MS = 750;         // pause between "Ready…", "one Circle…", "now!" 
const ARM_DELAY = 350;       // ms after the buzz before watching (the motor shakes the wand)
const GO_TIMEOUT = 6000;     // no move this long after the buzz: count again

const $ = (s) => document.querySelector(s);
const esc = (s) => String(s ?? '').replace(/[&<>"]/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' })[c]);

// ---------------------------------------------------------------- wizard art
// Real art: app/wizard/<name>.png. Each mood can have several images (rotated).
// Without PNGs the .svg placeholders are used.
const SETS = { welcome: ['welcome'], talk: ['talk1', 'talk2', 'talk3'], yes: ['yes', 'yes2'], no: ['no', 'no2'] };
const SVG = new Set(['welcome', 'talk1', 'talk2', 'talk3', 'yes', 'no']);
const pngs = new Set();
const artReady = Promise.all(Object.values(SETS).flat().map((n) => new Promise((res) => {
  const img = new Image();
  img.onload = () => { pngs.add(n); res(); };
  img.onerror = res;
  img.src = `app/wizard/${n}.png`;
})));
function artFor(mood, k = 0) {
  const names = SETS[mood];
  if (pngs.size) {
    const have = names.filter((n) => pngs.has(n));
    const list = have.length ? have : ['welcome'].filter((n) => pngs.has(n));
    if (list.length) return `app/wizard/${list[k % list.length]}.png`;
  }
  const svg = names.filter((n) => SVG.has(n));
  return `app/wizard/${svg[k % svg.length]}.svg`;
}

const store = {
  get(k) { try { return localStorage.getItem(k); } catch { return null; } },
  set(k, v) { try { localStorage.setItem(k, v); } catch { /* private mode */ } },
};

// ---------------------------------------------------------------- voice
// Always a man's voice, picked automatically (no voice menu). Spanish is Latin
// American (never Spain) and English is American. Names are the voices of
// macOS, Windows/Edge and Chrome.
const NOVELTY = /albert|bad news|bahh|bells|boing|bubbles|cellos|good news|jester|organ|superstar|trinoids|whisper|wobble|zarvox|junior|ralph|fred|kathy/i;
const ROBOTIC = /\b(eddy|reed|rocko|grandpa)\b/i;  // macOS "Eloquence" men: a last resort
const MALE = {
  en: /\b(aaron|alex|tom|evan|nathan|david|mark|guy|christopher|eric|brian|andrew|roger|steffan|daniel|arthur|oliver|george|ryan|thomas)\b|english male/i,
  es: /\b(diego|juan|carlos|tom[aá]s|jorge|ra[uú]l|gerardo|alonso|gonzalo|lorenzo|mateo|emilio|andr[eé]s|pablo|alvaro|enrique)\b/i,
  pt: /\b(felipe|joaquim|daniel|ant[oó]nio|duarte|humberto|fabio|julio)\b/i,
  it: /\b(luca|cosimo|diego|giuseppe|benigno|calimero|gianni|rinaldo)\b/i,
  de: /\b(markus|yannick|martin|stefan|conrad|killian|florian|bernd|christoph|ralf)\b/i,
};
const FEMALE = /female|grandma|\bflo\b|sandy|shelley|paulina|isabela|m[oó]nica|soledad|francisca|marisol|ang[eé]lica|luciana|alice|federica|paola|\banna\b|petra|helena|samantha|karen|moira|tessa|fiona|victoria|allison|\bava\b|susan|\bzoe\b|\bkate\b|serena|sabina|elvira|dalia|hilda|laura|elena|luc[ií]a|joana|fernanda|\bsara\b|zira|hazel|libby|sonia|jenny|\baria\b|\belsa\b|isabella|katja|amala|camila|catalina|salom[eé]|valentina|ximena|yolanda|google (espa|italiano|deutsch|portugu|us english|fran)/i;
const hasSpeech = () => typeof speechSynthesis !== 'undefined';
function voicesFor(l) {
  if (!hasSpeech()) return [];
  return speechSynthesis.getVoices().filter((v) => v.lang.replace('_', '-').toLowerCase().startsWith(l));
}
const region = (v) => v.lang.replace('_', '-').toLowerCase().split('-')[1] || '';
function bestVoice(l) {
  const score = (v) => {
    let s = 0;
    if (NOVELTY.test(v.name)) s -= 100;
    if (MALE[l]?.test(v.name) || ROBOTIC.test(v.name)) s += 20;
    if (FEMALE.test(v.name)) s -= 15;
    if (ROBOTIC.test(v.name)) s -= 10;
    if (/premium|enhanced|natural|neural/i.test(v.name)) s += 4;
    if (l === 'es') s += region(v) === 'es' ? -40 : region(v) === 'ar' ? 8 : 6;   // Latin America, never Spain
    if (l === 'en') s += region(v) === 'us' ? 10 : -30;                         // American, not British
    if (l === 'pt' && region(v) === 'br') s += 3;
    return s;
  };
  return voicesFor(l).sort((x, y) => score(y) - score(x))[0] || null;
}

export function initTraining(app) {
  // app: { send, connected(), info(), openLibrary({spell}), log(text), perform(move, speed) | null }
  const tr = {
    step: 'intro',     // intro | show | try | mastered | action | teach | finish
    phase: 'count',    // while trying: count -> go -> judged -> count …
    count: 0,          // 1..3 while counting
    move: 0,           // index in ORDER
    good: 0,
    misses: 0,
    shown: false,      // tab visible
    practice: false,   // PRACTICE 1 sent
    armAt: 0,
    goAt: 0,
    teach: null,       // { move, slot, count }
    timer: 0,
    gen: 0,            // bumps on every new step/cycle: stale timers do nothing
    lastSay: null,     // [english text, vars, mood] to re-say on language change
  };
  const catcher = new MoveCatcher();
  let voiceOn = store.get('wizardVoice') !== '0';

  // ---------------------------------------------------------------- wizard
  let poseTimer = 0, poseK = 0, typeTimer = 0;
  function pose(mood) {
    clearInterval(poseTimer);
    poseK++;
    $('#wizImg').src = artFor(mood, poseK);
    if (mood === 'yes' || mood === 'no') poseTimer = setTimeout(() => pose('talk'), 2400);
    else if (mood === 'talk') poseTimer = setInterval(() => { poseK++; $('#wizImg').src = artFor('talk', poseK); }, 4200);
  }
  artReady.then(() => { $('#wizImg').src = artFor(tr.lastSay?.[2] || 'welcome', poseK); });

  // Speak one line; done() runs when it has been said (or after a fair reading time).
  function speak(line, done) {
    let finished = false;
    const finish = () => { if (!finished) { finished = true; done?.(); } };
    const fallback = setTimeout(finish, Math.max(900, line.length * 75));
    if (voiceOn && hasSpeech() && tr.shown) {
      speechSynthesis.cancel();
      const u = new SpeechSynthesisUtterance(line);
      const v = bestVoice(lang);
      u.lang = v?.lang || SPEECH_TAG[lang];
      if (v) u.voice = v;
      u.pitch = 0.9;
      u.rate = 0.94;
      u.onend = () => { clearTimeout(fallback); setTimeout(finish, 150); };
      speechSynthesis.speak(u);
    }
  }

  // say('English text', vars, mood, done): typed into the bubble and spoken.
  function say(text, vars, mood, done) {
    tr.lastSay = [text, vars, mood];
    const line = t(text, vars);
    if (mood) pose(mood);
    const el = $('#wizSay');
    clearInterval(typeTimer);
    el.textContent = '';
    let i = 0;
    typeTimer = setInterval(() => { el.textContent = line.slice(0, ++i); if (i >= line.length) clearInterval(typeTimer); }, 18);
    speak(line, done);
  }
  function hush() { clearInterval(typeTimer); if (hasSpeech()) speechSynthesis.cancel(); }

  $('#wizVoice').addEventListener('click', () => {
    voiceOn = !voiceOn;
    store.set('wizardVoice', voiceOn ? '1' : '0');
    if (!voiceOn) hush();
    renderVoice();
  });
  function renderVoice() {
    $('#wizVoice').textContent = voiceOn ? `🔊 ${t('Voice on')}` : `🔈 ${t('Voice off')}`;
    $('#wizVoice').title = bestVoice(lang)?.name || '';
  }
  if (hasSpeech()) speechSynthesis.addEventListener?.('voiceschanged', renderVoice);
  renderVoice();

  // ---------------------------------------------------------------- practice mode
  function setPractice(on) {
    on = on && tr.shown && app.connected();
    if (on === tr.practice) return;
    tr.practice = on;
    app.send(`PRACTICE ${on ? 1 : 0}`);
  }
  const practising = () => tr.step === 'try' || tr.step === 'show' || tr.step === 'mastered';

  // ---------------------------------------------------------------- steps
  const cur = () => ORDER[tr.move];
  const moveName = (m = cur()) => t(MOVES[m].name);
  const wireName = (m) => ascii(t(MOVES[m].name)).replace(/[^A-Za-z0-9-]+/g, '');
  const later = (fn, ms) => { const g = tr.gen; tr.timer = setTimeout(() => { if (g === tr.gen) fn(); }, ms); };

  function go(step) {
    clearTimeout(tr.timer);
    tr.gen++;
    tr.step = step;
    setPractice(practising());
    const g = tr.gen;
    const still = (fn) => () => { if (g === tr.gen) fn(); };
    render();
    if (step === 'intro') {
      say('Welcome, apprentice! I will teach you three spells. Pick up your wand and hold it like a pen, tip pointing forward. Ready?', null, 'welcome');
    } else if (step === 'show') {
      tr.good = 0; tr.misses = 0;
      // Explain while the example plays once; "your turn" when both are done.
      let spoke = false, played = false;
      const next = () => { if (spoke && played) later(() => go('try'), 500); };
      say('Spell {n}: the {name}. {how} Watch me.', { n: tr.move + 1, name: moveName(), how: t(MOVES[cur()].how) }, 'talk',
        still(() => { spoke = true; next(); }));
      setTimeout(still(() => {
        playDemo();
        setTimeout(still(() => { played = true; next(); }), demoMs(cur()) + 400);
      }), 700);
    } else if (step === 'try') {
      startCycle(true);
    } else if (step === 'mastered') {
      const last = tr.move === ORDER.length - 1;
      say(last ? 'Splendid! You have mastered all three spells. Now let us give them a purpose.'
        : 'Splendid! You have mastered the {name}. On to the next one.', { name: moveName() }, 'yes',
      still(() => later(() => { if (last) go('action'); else { tr.move++; go('show'); } }, 700)));
    } else if (step === 'action') {
      say('A spell needs a purpose. Choose what each one does: the TV, the air conditioner, anything with a remote.', null, 'talk');
    } else if (step === 'finish') {
      store.set('wandTrained', '1');
      say('Your training is complete! Point your wand at the TV and cast your spells. Come back whenever you want to practise.', null, 'welcome');
    }
  }

  // ---------------------------------------------------------------- one attempt
  // "Ready… one Circle… now!" (hold still) -> buzz on "now" -> ONE move ->
  // hold still -> judged -> again. `lead` is said first (first try, or after
  // moving too early / nothing seen).
  function startCycle(first, lead) {
    tr.gen++;
    const g = tr.gen;
    tr.phase = 'count';
    tr.count = 0;
    catcher.reset();
    renderPhase();
    const text = lead || (first ? 'Your turn.' : null);
    if (text) say(text, { name: moveName() }, first ? 'talk' : null, () => { if (g === tr.gen) later(countNext, 250); });
    else later(countNext, 300);
  }
  const BEATS = ['Ready…', 'one {name}…', 'now!'];
  function countNext() {
    if (tr.step !== 'try' || tr.phase !== 'count') return;
    tr.count++;
    const line = t(BEATS[tr.count - 1], { name: moveName() });
    $('#wizSay').textContent = line;
    if (tr.count < 3) {
      renderPhase();
      speak(line);
      later(countNext, BEAT_MS);
      return;
    }
    // "now!": buzz, watch for ONE move
    speak(line);
    tr.phase = 'go';
    tr.armAt = Date.now() + ARM_DELAY;
    tr.goAt = Date.now();
    catcher.reset();
    app.send('BUZZ 150');
    renderPhase();
  }

  function onFeature(row) {
    if (tr.step !== 'try') return;
    const now = Date.now();
    if (tr.phase === 'count') {
      if (tr.count > 0 && row[4] > MOVING_ENERGY) {  // moved before the buzz
        startCycle(false, 'Wait for "now"! Hold still.');
      }
      return;
    }
    if (tr.phase !== 'go' || now < tr.armAt) return;
    const seg = catcher.push(row);
    if (!seg) {
      if (!catcher.active && now - tr.goAt > GO_TIMEOUT) {
        startCycle(false, 'I saw nothing.');
      }
      return;
    }
    const m = measure(seg);
    const v = judge(cur(), m);
    app.log(`TRAIN ${cur()} ${v.ok ? 'YES' : 'NO (' + v.kind + ')'}: ${describe(m)}`);
    tr.phase = 'judged';
    tr.gen++;
    const g = tr.gen;
    const next = () => later(() => { if (g === tr.gen) startCycle(false); }, 500);
    if (v.ok) {
      tr.good++; tr.misses = 0;
      if (tr.good >= NEEDED) { render(); return go('mastered'); }
      say(tr.good === NEEDED - 1 ? '{verdict} Once more!' : '{verdict} Do it again.', { verdict: t(v.say) }, 'yes', next);
    } else {
      tr.misses++;
      const replay = tr.misses >= 4;
      if (replay) tr.misses = 0;
      say(replay ? '{verdict} Watch me once more.' : '{verdict}', { verdict: t(v.say) }, 'no', next);
      if (replay) setTimeout(playDemo, 300);
    }
    render();
  }

  // ---------------------------------------------------------------- action step
  const gestures = () => app.info()?.g || [];
  const isMoveSlot = (g, m) => {
    const names = allTranslations(MOVES[m].name).map((n) => ascii(n).replace(/[^A-Za-z0-9-]+/g, '').toLowerCase());
    return names.includes((g?.n || '').replace(/_/g, '').toLowerCase());
  };
  const slotOf = (m) => gestures().findIndex((g) => isMoveSlot(g, m));
  const codeName = (c) => { const n = app.info()?.c?.[c]?.n; return n ? n.replace(/_/g, ' ') : t('Code {n}', { n: c + 1 }); };

  function chooseAction(m) {
    let g = slotOf(m);
    if (g < 0) g = gestures().findIndex((x) => !x.c && !x.n);
    if (g < 0) g = gestures().findIndex((x) => !x.c);
    if (g < 0) return say('All eight spell slots are taken. Clear one in the Spells tab first.', null, 'no');
    const s = gestures()[g];
    if (s?.c >= 3 && isMoveSlot(s, m)) return app.openLibrary({ spell: g });
    // The wand recognizes spells from your own samples: let it watch 3 times.
    tr.teach = { move: m, slot: g, count: isMoveSlot(s, m) ? s.c : 0 };
    tr.step = 'teach';
    say('Your wand must learn your own {name}. Do it three times, each time right after the wand buzzes "go".', { name: moveName(m) }, 'talk');
    app.send(`REC ${g} ${wireName(m)}`);
    render();
  }

  function onRec(g, what, count) {
    if (tr.step !== 'teach' || !tr.teach || g !== tr.teach.slot) return;
    const m = tr.teach.move;
    if (what === 'ok') {
      tr.teach.count = count;
      if (count >= NEEDED) {
        const slot = tr.teach.slot;
        tr.teach = null;
        tr.step = 'action';
        say('Your wand knows your {name} now. Choose what it does.', { name: moveName(m) }, 'yes');
        render();
        setTimeout(() => app.openLibrary({ spell: slot }), 900);
        return;
      }
      say('Good, {n} of {total}. Again, after the buzz.', { n: count, total: NEEDED }, 'yes');
    } else if (what === 'timeout') {
      say('I saw nothing. Do the {name} right after the buzz.', { name: moveName(m) }, 'no');
    } else return;
    render();
    setTimeout(() => { if (tr.step === 'teach' && tr.teach) app.send(`REC ${tr.teach.slot} ${wireName(m)}`); }, 1200);
  }

  // ---------------------------------------------------------------- render
  // Example path: the spark goes round ONCE (↻ replays it); the arrow shows
  // the direction.
  function demoSvg(m, big) {
    const path = MOVES[m].path, id = `arr-${m}-${big ? 'b' : 's'}`;
    return `<svg class="move-demo ${big ? 'big' : ''}" viewBox="0 0 100 100" aria-label="${esc(t(MOVES[m].name))}">
      <defs><marker id="${id}" viewBox="0 0 10 10" refX="6" refY="5" markerWidth="5" markerHeight="5" orient="auto-start-reverse">
        <path d="M0 0 L10 5 L0 10 Z" class="arrow"/></marker></defs>
      <path d="${path}" class="trail" marker-end="url(#${id})"/>
      ${big ? `<circle r="5" class="spark"><animateMotion dur="${demoMs(m) / 1000}s" begin="indefinite" fill="freeze" path="${path}"/></circle>` : ''}
    </svg>`;
  }
  const demoMs = (m) => (m === 'hit' ? 1400 : 1800);
  function playDemo() {
    const svg = $('#trainArea .move-demo.big');
    const anim = svg?.querySelector('animateMotion');
    if (!anim) return;
    svg.classList.add('playing');
    try { anim.beginElement(); } catch { /* old browser: static path only */ }
  }
  const stars = () => `<div class="stars" aria-label="${tr.good} / ${NEEDED}">${Array.from({ length: NEEDED }, (_, k) => `<span class="${k < tr.good ? 'on' : ''}">★</span>`).join('')}</div>`;
  const pretend = () => (app.perform ? `<div class="pretend"><span class="muted small">${t('Pretend wand:')}</span>
      <button class="small" data-pretend="1">${t('do it right')}</button><button class="small" data-pretend="0.33">${t('too slow')}</button>
      <button class="small" data-pretend="3">${t('rushed')}</button><button class="small" data-pretend="wrong">${t('wrong move')}</button></div>` : '');

  // Ready… one Circle… now! (the current beat lit), then "reading".
  function counter() {
    const p = tr.phase;
    const beat = p === 'count' ? tr.count : 3;
    const cells = BEATS.map((b, i) => {
      const n = i + 1, cls = p === 'go' && n === 3 ? 'now go' : beat === n ? 'now' : beat > n ? 'past' : '';
      return `<li class="${cls}">${esc(t(b, { name: moveName() }))}</li>`;
    }).join('');
    const hint = p === 'count' ? t('Hold still until "now".') : p === 'go' ? t('Do ONE move, then hold still.') : `⏸ ${t('Reading your move…')}`;
    return `<div class="counter" data-phase="${p}"><ol>${cells}</ol><p class="muted small">${esc(hint)}</p></div>`;
  }
  function renderPhase() {
    const c = $('#trainArea .counter');
    if (c) c.outerHTML = counter();
    $('#trainArea').dataset.phase = tr.step === 'try' ? tr.phase : '';
  }

  function render() {
    const area = $('#trainArea');
    const m = cur();
    const progress = `<ol class="train-steps">${ORDER.map((x, i) => `<li class="${i < tr.move || ['action', 'teach', 'finish'].includes(tr.step) ? 'done' : i === tr.move && practising() ? 'now' : ''}">${esc(t(MOVES[x].name))}</li>`).join('')}
      <li class="${tr.step === 'finish' ? 'done' : ['action', 'teach'].includes(tr.step) ? 'now' : ''}">${t('Actions')}</li></ol>`;
    let html = '';
    if (!app.connected()) {
      html = `<p class="muted">${t('Summon your wand to begin.')}</p>`;
    } else if (tr.step === 'intro') {
      html = `<button class="primary big" data-act="begin">${t('Begin training')}</button>
        <button class="small linkish" data-act="skip-to-action">${t('I know the moves: go to actions')}</button>`;
    } else if (tr.step === 'show') {
      html = demoSvg(m, true);
    } else if (tr.step === 'try' || tr.step === 'mastered') {
      html = `<div class="try-row">${demoSvg(m, false)}<div>${stars()}</div></div>
        ${tr.step === 'try' ? `${counter()}${pretend()}<div class="row"><button class="small" data-act="replay">↻ ${t('Show me again')}</button>
        <button class="small linkish" data-act="skip">${t('Skip this spell')}</button></div>` : ''}`;
    } else if (tr.step === 'action' || tr.step === 'teach') {
      html = `<div class="action-list">${ORDER.map((x) => {
        const g = slotOf(x), s = gestures()[g];
        const b = s && s.b >= 0 ? t('sends “{code}”', { code: esc(codeName(s.b)) }) : s?.c >= 3 ? t('no action yet') : t('not on the wand yet');
        const teaching = tr.step === 'teach' && tr.teach?.move === x;
        return `<div class="action-row ${teaching ? 'teaching' : ''}">${demoSvg(x, false)}
          <div><b>${esc(t(MOVES[x].name))}</b><div class="muted small">${teaching ? t('wand learning: {n} of {total}', { n: tr.teach.count, total: NEEDED }) : b}</div></div>
          <button class="small primary" data-act="choose" data-move="${x}" ${tr.step === 'teach' ? 'disabled' : ''}>${s?.b >= 0 ? t('Change') : t('Choose action')}</button>
        </div>`;
      }).join('')}</div>
      <button class="primary" data-act="finish" ${tr.step === 'teach' ? 'disabled' : ''}>${t('Finish training')}</button>`;
    } else if (tr.step === 'finish') {
      html = `<button class="primary" data-act="restart">${t('Practise again')}</button>`;
    }
    area.innerHTML = progress + html;
    area.dataset.phase = tr.step === 'try' ? tr.phase : '';
  }

  $('#trainArea').addEventListener('click', (e) => {
    const b = e.target.closest('button');
    if (!b) return;
    if (b.dataset.pretend) {
      const sp = b.dataset.pretend;
      if (sp === 'wrong') app.perform(cur() === 'hit' ? 'zigzag' : 'hit', 1);
      else app.perform(cur(), +sp);
      return;
    }
    const act = b.dataset.act;
    if (act === 'begin') { tr.move = 0; go('show'); }
    else if (act === 'replay') {  // show the example again, then count again
      tr.gen++;
      tr.phase = 'count'; tr.count = 0;
      render();
      $('#trainArea .try-row .move-demo').outerHTML = demoSvg(cur(), true);
      playDemo();
      later(() => startCycle(false), 2200);
    }
    else if (act === 'skip') { tr.good = NEEDED; go('mastered'); }
    else if (act === 'skip-to-action') go('action');
    else if (act === 'choose') chooseAction(b.dataset.move);
    else if (act === 'finish') go('finish');
    else if (act === 'restart') { tr.move = 0; go('intro'); }
  });

  // ---------------------------------------------------------------- hooks from app.js
  return {
    onLine(tag, p) {
      if (tag === 'F') onFeature(p.slice(0, 5).map(Number));
      else if (tag === 'REC') onRec(+p[0], p[1], +p[2]);
      else if (tag === 'INFO' && (tr.step === 'action' || tr.step === 'teach')) render();
    },
    onShown(shown) {
      const was = tr.shown;
      tr.shown = shown;
      if (!shown) { hush(); tr.gen++; if (tr.step === 'try') { tr.phase = 'count'; tr.count = 0; } }
      else if (!was) {
        if (tr.step === 'try') startCycle(true);
        else if (tr.step === 'show') go('show');
        else if (tr.lastSay) say(...tr.lastSay);
        else go(tr.step);
      }
      setPractice(practising());
      if (shown) render();
    },
    onLang() {
      renderVoice();
      render();
      if (tr.lastSay && tr.step !== 'try') say(...tr.lastSay);
    },
    onConnect() { tr.practice = false; render(); setPractice(practising()); },
    onDisconnect() { tr.practice = false; tr.gen++; if (tr.step === 'teach') { tr.teach = null; tr.step = 'action'; } render(); },
    firstTime: () => store.get('wandTrained') !== '1',
  };
}
