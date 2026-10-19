// training.js — the Wizard tab: the first-time setup of a wand.
//
//   1. names       your name and the wand's name
//   2. intro       "hold the wand still and centred before every spell"
//   3. four spells ZigZag, Stroke, Tip up, Circle. Each attempt is recorded on
//                  the wand (REC) and judged here; wrong ones are taken back.
//                  Three good ones = the wand knows the spell.
//   4. purpose     what the spells are for: home devices, knight battle,
//                  treasures (coming), or skip
//
// The wizard speaks every line (a man's voice) and types it in his bubble.
import { SETUP_SPELLS, spellIcon, playIcon } from './spell-book.js?v=3';
import { Recorder } from './recorder.js?v=4';
import { t, lang, SPEECH_TAG } from './i18n.js?v=5';

const NEEDED = 3;  // good samples per spell
const BEATS = ['Ready…', 'one {name}…', 'GO!'];  // the count before every attempt

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

/**
 * app: { send, connected(), info(), log(text), perform(move) | null,
 *        profile(), saveProfile(), renameWand(name), slotFor(wire), goto(tab, what), backup(reason) }
 */
export function initTraining(app) {
  const tr = {
    step: 'names',     // names | intro | show | try | mastered | tune | purpose | finish
    spell: 0,          // index in SETUP_SPELLS
    good: 0,           // samples on the wand for this spell
    misses: 0,
    shown: false,      // tab visible
    practice: false,   // PRACTICE 1 sent
    timer: 0,
    gen: 0,            // bumps on every new step: stale timers do nothing
    lastSay: null,     // [english text, vars, mood] to re-say on language change
    slot: -1,
  };
  let voiceOn = store.get('wizardVoice') !== '0';
  const rec = new Recorder({ send: (l) => app.send(l), log: (x) => app.log(x), changed: () => renderCounter() });

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
  // While teaching, the wand never fires a spell (no IR goes out by accident).
  function setPractice(on) {
    on = on && tr.shown && app.connected();
    if (on === tr.practice) return;
    tr.practice = on;
    app.send(`PRACTICE ${on ? 1 : 0}`);
  }
  const teaching = () => ['show', 'try', 'mastered'].includes(tr.step);

  // ---------------------------------------------------------------- steps
  const cur = () => SETUP_SPELLS[tr.spell];
  const spellName = (s = cur()) => t(s.name);
  const later = (fn, ms) => { const g = tr.gen; tr.timer = setTimeout(() => { if (g === tr.gen) fn(); }, ms); };
  const owner = () => app.profile()?.owner || '';

  function go(step) {
    clearTimeout(tr.timer);
    rec.cancel(true);
    tr.gen++;
    tr.step = step;
    const p = app.profile();
    if (p && !p.setupDone && ['names', 'intro', 'purpose'].includes(step)) { p.setupStep = step; app.saveProfile(); }
    setPractice(teaching());
    const g = tr.gen;
    const still = (fn) => () => { if (g === tr.gen) fn(); };
    render();
    if (step === 'names') {
      say('Greetings! A new wand has chosen you. Before we begin: what is your name, and what shall we call your wand?', null, 'welcome');
    } else if (step === 'intro') {
      say('Welcome, {owner}! I will teach you four spells. Remember the first rule of magic: before every spell, hold your wand still and centred, pointing at what you want to enchant. Ready?',
        { owner: owner() || t('apprentice') }, 'welcome');
    } else if (step === 'show') {
      tr.misses = 0;
      tr.slot = app.slotFor(cur().wire);
      if (tr.slot < 0) return say('All eight spell slots are taken. Clear one in the Spells tab first.', null, 'no');
      app.send(`GCLR ${tr.slot}`);  // a fresh start for this spell
      tr.good = 0;
      let spoke = false, played = false;
      const next = () => { if (spoke && played) later(() => go('try'), 500); };
      say('Spell {n}: the {name}. {how} Watch me.', { n: tr.spell + 1, name: spellName(), how: t(cur().how) }, 'talk',
        still(() => { spoke = true; next(); }));
      later(() => {
        playIcon($('#trainArea .demo-big'));
        setTimeout(still(() => { played = true; next(); }), 1800);
      }, 700);
    } else if (step === 'try') {
      say('Your turn. Hold the wand still and centred, wait for GO, then one {name}.', { name: spellName() }, 'talk', still(() => attempt()));
    } else if (step === 'mastered') {
      const last = tr.spell === SETUP_SPELLS.length - 1;
      say(last ? 'Splendid! Your wand knows all four spells.' : 'Splendid! Your wand knows the {name}. On to the next one.', { name: spellName() }, 'yes',
        still(() => later(() => { if (last) go('tune'); else { tr.spell++; go('show'); } }, 600)));
    } else if (step === 'tune') {
      // Forgiving recognition: a spell that was learned should almost always be recognised.
      app.send('SET thr 0.7');
      app.backup('setup');
      later(() => go('purpose'), 400);
    } else if (step === 'purpose') {
      say('Now tell me, {owner}: what will you do with your spells?', { owner: owner() || t('apprentice') }, 'talk');
    } else if (step === 'finish') {
      say('Your wand is ready. Remember: still and centred, then cast. Come back to me whenever you want to practise.', null, 'welcome');
    }
  }

  // ---------------------------------------------------------------- one attempt
  function attempt() {
    if (tr.step !== 'try') return;
    const g = tr.gen;
    rec.attempt({ slot: tr.slot, wire: cur().wire, move: cur().move }, (r) => {
      if (g !== tr.gen || tr.step !== 'try') return;
      const again = () => later(attempt, 450);
      if (r.kind === 'early') return say('Wait for GO! Hold the wand still and centred.', null, 'no', again);
      if (r.kind === 'nothing') return say('I saw nothing. Do the {name} right after GO.', { name: spellName() }, 'no', again);
      if (r.kind === 'cancel') return;
      tr.good = r.count;
      render();
      if (r.kind === 'ok') {
        tr.misses = 0;
        if (tr.good >= NEEDED) return go('mastered');
        return say(tr.good === NEEDED - 1 ? '{verdict} Once more!' : '{verdict} Again.', { verdict: r.verdict ? t(r.verdict.say) : t('Good, the wand saw it.') }, 'yes', again);
      }
      tr.misses++;
      const replay = tr.misses >= 3;
      if (replay) tr.misses = 0;
      say(replay ? '{verdict} Watch me once more.' : '{verdict}', { verdict: t(r.verdict.say) }, 'no', again);
      if (replay) setTimeout(() => playIcon($('#trainArea .try-row')), 300);
    });
    if (app.perform) renderCounter();
  }

  // ---------------------------------------------------------------- render
  const stars = () => `<div class="stars" aria-label="${tr.good} / ${NEEDED}">${Array.from({ length: NEEDED }, (_, k) => `<span class="${k < tr.good ? 'on' : ''}">★</span>`).join('')}</div>`;
  const pretend = () => (app.perform ? `<div class="pretend"><span class="muted small">${t('Pretend wand:')}</span>
      <button class="small" data-pretend="1">${t('do it right')}</button><button class="small" data-pretend="0.33">${t('too slow')}</button>
      <button class="small" data-pretend="wrong">${t('wrong move')}</button></div>` : '');

  function counter() {
    const p = rec.phase;
    const cells = BEATS.map((b, i) => {
      const n = i + 1;
      const cls = p === 'go' && n === 3 ? 'now go' : p !== 'idle' && rec.beat === n ? 'now' : rec.beat > n ? 'past' : '';
      return `<li class="${cls}">${esc(t(b, { name: spellName() }))}</li>`;
    }).join('');
    const hint = p === 'go' ? t('Do ONE move, then hold still.') : p === 'reading' ? `⏸ ${t('Reading your move…')}` : t('Still and centred until GO.');
    return `<div class="counter" data-phase="${p}"><ol>${cells}</ol><p class="muted small">${esc(hint)}</p></div>`;
  }
  function renderCounter() {
    const c = $('#trainArea .counter');
    if (c) c.outerHTML = counter();
    $('#trainArea').dataset.phase = rec.phase;
  }

  const PURPOSES = [
    { id: 'home', icon: '🏠', title: 'Control home devices', sub: 'TV, air conditioner, lights…' },
    { id: 'knight', icon: '⚔️', title: 'Spell battle against a Knight', sub: 'Hit and heal your knight figure' },
    { id: 'treasure', icon: '🗝️', title: 'Unlock treasures', sub: 'Coming soon', off: true },
  ];

  function render() {
    const area = $('#trainArea');
    const p = app.profile();
    const progress = `<ol class="train-steps">${SETUP_SPELLS.map((s, i) => `<li class="${i < tr.spell || ['tune', 'purpose', 'finish'].includes(tr.step) ? 'done' : i === tr.spell && teaching() ? 'now' : ''}">${esc(t(s.name))}</li>`).join('')}
      <li class="${tr.step === 'finish' ? 'done' : tr.step === 'purpose' ? 'now' : ''}">${t('Purpose')}</li></ol>`;
    let html = '';
    if (!app.connected()) {
      html = `<p class="muted">${t('Summon your wand to begin.')}</p>`;
    } else if (tr.step === 'names') {
      const wn = (app.info()?.name || '').replace(/^Wand-/, '');
      html = `<form class="names form" data-act="names">
        <label><span>${t('Your name')}</span><input name="owner" maxlength="24" autocomplete="given-name" value="${esc(p?.owner || '')}" required /></label>
        <label><span>${t('Your wand’s name')}</span><div class="row"><span class="muted">Wand-</span><input name="wand" maxlength="11" value="${esc(wn)}" /></div>
          <span class="hint">${t('Letters, numbers and dashes, up to 11. The wand restarts with its new name.')}</span></label>
        <button class="primary" type="submit">${t('Continue')}</button></form>`;
      return void (area.innerHTML = html);
    } else if (tr.step === 'intro') {
      html = `<div class="rule-card">✋ ${t('Before every spell: hold the wand <b>still</b> and <b>centred</b>, pointing at the device. Then cast.')}</div>
        <button class="primary big" data-act="begin">${t('Begin training')}</button>
        <button class="small linkish" data-act="skip-training">${t('I know the moves: skip the training')}</button>`;
    } else if (tr.step === 'show') {
      html = `<div class="demo-big">${spellIcon(cur(), { size: 180, animate: true })}</div>`;
    } else if (tr.step === 'try' || tr.step === 'mastered') {
      html = `<div class="try-row">${spellIcon(cur(), { size: 84, animate: true })}<div>${stars()}</div></div>
        ${tr.step === 'try' ? `${counter()}${pretend()}<div class="row"><button class="small" data-act="replay">↻ ${t('Show me again')}</button>
        <button class="small linkish" data-act="skip">${t('Skip this spell')}</button></div>` : ''}`;
    } else if (tr.step === 'purpose' || tr.step === 'tune') {
      html = `<div class="purpose-list">${PURPOSES.map((x) => `<button class="purpose ${x.off ? 'off' : ''}" data-purpose="${x.id}" ${x.off ? 'disabled' : ''}>
          <span class="picon">${x.icon}</span><b>${t(x.title)}</b><span class="muted small">${t(x.sub)}</span></button>`).join('')}</div>
        <button class="small linkish" data-purpose="skip">${t('Skip for now')}</button>`;
    } else if (tr.step === 'finish') {
      html = `<button class="primary" data-act="restart">${t('Practise again')}</button>`;
    }
    area.innerHTML = progress + html;
    area.dataset.phase = rec.phase;
  }

  $('#trainArea').addEventListener('submit', (e) => {
    e.preventDefault();
    const f = e.target;
    if (f.dataset.act !== 'names') return;
    const owner = f.owner.value.trim();
    const wand = f.wand.value.trim().replace(/[^A-Za-z0-9-]+/g, '-').slice(0, 11);
    const p = app.profile();
    p.owner = owner;
    p.setupStep = 'intro';
    app.saveProfile();
    const current = (app.info()?.name || '').replace(/^Wand-/, '');
    if (wand && wand !== current) {
      say('{wand}, a fine name! Your wand restarts to take it. Keep it close…', { wand }, 'yes');
      app.renameWand(wand);   // reconnects by itself; the setup carries on at the intro
    } else go('intro');
  });

  $('#trainArea').addEventListener('click', (e) => {
    const b = e.target.closest('button');
    if (!b) return;
    if (b.dataset.pretend) {
      const sp = b.dataset.pretend;
      const other = { zigzag: 'stroke', stroke: 'tipup', tipup: 'stroke', circle: 'zigzag' }[cur().move];
      if (sp === 'wrong') app.perform(other, 1);
      else app.perform(cur().move, +sp);
      return;
    }
    if (b.dataset.purpose) return choosePurpose(b.dataset.purpose);
    const act = b.dataset.act;
    if (act === 'begin') { tr.spell = 0; go('show'); }
    else if (act === 'replay') {
      rec.cancel(true);
      tr.gen++;
      render();
      playIcon($('#trainArea .try-row'));
      later(attempt, 1900);
    } else if (act === 'skip') { tr.good = NEEDED; go('mastered'); }
    else if (act === 'skip-training') go('purpose');
    else if (act === 'restart') { tr.spell = 0; go('intro'); }
  });

  function choosePurpose(id) {
    const p = app.profile();
    p.setupDone = true;
    p.setupStep = 'done';
    app.saveProfile();
    if (id === 'home') app.goto('devices', 'add');
    else if (id === 'knight') app.goto('knight', 'start');
    else app.goto('live');
    tr.step = 'finish';
    render();
  }

  // ---------------------------------------------------------------- hooks from app.js
  return {
    onLine(tag, p) { rec.feed(tag, p); },
    onShown(shown) {
      const was = tr.shown;
      tr.shown = shown;
      if (!shown) { hush(); if (rec.busy) { rec.cancel(true); tr.gen++; } }
      else if (!was) {
        if (tr.step === 'names' && app.profile()?.setupDone) tr.step = 'finish';  // set up before: offer practice
        if (tr.step === 'try') go('try');
        else if (tr.step === 'show') go('show');
        else if (tr.lastSay) say(...tr.lastSay);
        else go(tr.step);
      }
      setPractice(teaching());
      if (shown) render();
    },
    onLang() {
      renderVoice();
      render();
      if (tr.lastSay && tr.step !== 'try') say(...tr.lastSay);
    },
    onConnect() { tr.practice = false; render(); setPractice(teaching()); },
    onDisconnect() { tr.practice = false; rec.cancel(true); tr.gen++; if (teaching()) tr.step = 'intro'; render(); },
    /** Start (or resume) the setup for this wand. */
    begin() {
      const p = app.profile();
      const step = p?.setupStep && p.setupStep !== 'done' ? p.setupStep : p?.owner ? 'intro' : 'names';
      tr.spell = 0;
      go(step);
    },
    reset() { tr.spell = 0; tr.step = 'names'; render(); },
  };
}
