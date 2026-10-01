// training.js — the Training tab: a wizard teaches three fixed moves (Circle,
// Hit, ZigZag), checks every attempt from the wand's motion stream, then lets
// you give each move an action from the remote library.
//
// The moves are checked here, in the app (training-moves.js). While practising
// the wand is in PRACTICE mode: it still sees moves but never fires a spell,
// so nothing gets sent to the TV by accident.
import { MOVES, MoveCatcher, measure, judge, describe } from './training-moves.js?v=1';

const ORDER = ['circle', 'hit', 'zigzag'];
const NEEDED = 3;          // good attempts per move
const POSES = ['welcome', 'talk1', 'talk2', 'talk3', 'yes', 'no'];
const TALK = ['talk1', 'talk2', 'talk3'];

const $ = (s) => document.querySelector(s);
const esc = (s) => String(s ?? '').replace(/[&<>"]/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' })[c]);

// Wizard art: app/wizard/<pose>.png if it exists, else the placeholder .svg.
const art = {};
for (const p of POSES) {
  art[p] = `app/wizard/${p}.svg`;
  const img = new Image();
  img.onload = () => { art[p] = img.src; };
  img.src = `app/wizard/${p}.png`;
}

const store = {
  get(k) { try { return localStorage.getItem(k); } catch { return null; } },
  set(k, v) { try { localStorage.setItem(k, v); } catch { /* private mode */ } },
};

export function initTraining(app) {
  // app: { send, connected(), info(), openLibrary({spell}), log(text), perform(move, speed) | null }
  const t = {
    step: 'intro',     // intro | show | try | mastered | action | teach | finish
    move: 0,           // index in ORDER
    good: 0,
    misses: 0,
    shown: false,      // tab visible
    practice: false,   // PRACTICE 1 sent
    busyUntil: 0,      // ignore moves while the wizard reacts
    teach: null,       // { move, slot }
    timer: 0,
  };
  const catcher = new MoveCatcher();
  let voiceOn = store.get('wizardVoice') !== '0';

  // ---------------------------------------------------------------- wizard
  let poseTimer = 0, talkIdx = 0, typeTimer = 0;
  function pose(name, ms = 0) {
    clearInterval(poseTimer);
    $('#wizImg').src = art[name];
    if (ms) poseTimer = setTimeout(rotate, ms);       // yes/no for a moment, then back to talking
    else if (TALK.includes(name)) poseTimer = setInterval(nextTalk, 3800);
  }
  function nextTalk() { talkIdx = (talkIdx + 1) % TALK.length; $('#wizImg').src = art[TALK[talkIdx]]; }
  function rotate() { pose(TALK[talkIdx]); }

  function say(text, p) {
    if (p) pose(p, p === 'yes' || p === 'no' ? 2200 : 0);
    const el = $('#wizSay');
    clearInterval(typeTimer);
    el.textContent = '';
    let i = 0;
    typeTimer = setInterval(() => { el.textContent = text.slice(0, ++i); if (i >= text.length) clearInterval(typeTimer); }, 18);
    if (voiceOn && 'speechSynthesis' in window) {
      speechSynthesis.cancel();
      const u = new SpeechSynthesisUtterance(text);
      const v = speechSynthesis.getVoices().find((x) => /^en(-|_)GB/i.test(x.lang)) || speechSynthesis.getVoices().find((x) => /^en/i.test(x.lang));
      if (v) u.voice = v;
      u.pitch = 0.7;
      u.rate = 0.95;
      speechSynthesis.speak(u);
    }
  }
  function hush() { clearInterval(typeTimer); if ('speechSynthesis' in window) speechSynthesis.cancel(); }

  $('#wizVoice').addEventListener('click', () => {
    voiceOn = !voiceOn;
    store.set('wizardVoice', voiceOn ? '1' : '0');
    if (!voiceOn) hush();
    renderVoice();
  });
  function renderVoice() { $('#wizVoice').textContent = voiceOn ? '🔊 Voice on' : '🔈 Voice off'; }
  renderVoice();

  // ---------------------------------------------------------------- practice mode
  function setPractice(on) {
    on = on && t.shown && app.connected();
    if (on === t.practice) return;
    t.practice = on;
    app.send(`PRACTICE ${on ? 1 : 0}`);
  }
  const practising = () => t.step === 'try' || t.step === 'show' || t.step === 'mastered';

  // ---------------------------------------------------------------- steps
  const cur = () => ORDER[t.move];
  const moveName = (m = cur()) => MOVES[m].name;

  function go(step) {
    clearTimeout(t.timer);
    t.step = step;
    setPractice(practising());
    const m = cur();
    if (step === 'intro') {
      say('Welcome, apprentice! I will teach you three spells. Pick up your wand and hold it like a pen, tip pointing forward. Ready?', 'welcome');
    } else if (step === 'show') {
      t.good = 0; t.misses = 0;
      say(`Spell ${t.move + 1}: the ${moveName()}. ${MOVES[m].how} Watch me.`, 'talk2');
    } else if (step === 'try') {
      catcher.reset();
      t.busyUntil = 0;
      say(`Now you try. Do the ${moveName()}!`, 'talk1');
    } else if (step === 'mastered') {
      const last = t.move === ORDER.length - 1;
      say(last ? `Splendid! You have mastered all three spells. Now let us give them a purpose.`
        : `Splendid! You have mastered the ${moveName()}. On to the next one.`, 'yes');
      t.timer = setTimeout(() => { if (last) go('action'); else { t.move++; go('show'); } }, 3200);
    } else if (step === 'action') {
      say('A spell needs a purpose. Choose what each one does: the TV, the air conditioner, anything with a remote.', 'talk3');
    } else if (step === 'finish') {
      store.set('wandTrained', '1');
      say('Your training is complete! Point your wand at the TV and cast your spells. Come back whenever you want to practise.', 'welcome');
    }
    render();
  }

  // ---------------------------------------------------------------- attempts
  function onFeature(row) {
    if (t.step !== 'try') return;
    const seg = catcher.push(row);
    if (!seg || Date.now() < t.busyUntil) return;
    const m = measure(seg);
    const v = judge(cur(), m);
    app.log(`TRAIN ${cur()} ${v.ok ? 'YES' : 'NO (' + v.kind + ')'}: ${describe(m)}`);
    t.busyUntil = Date.now() + 1500;
    if (v.ok) {
      t.good++; t.misses = 0;
      app.send('BUZZ 120');
      if (t.good >= NEEDED) return go('mastered');
      say(`${v.say} ${t.good === NEEDED - 1 ? 'Once more!' : 'Do it again.'}`, 'yes');
    } else {
      t.misses++;
      say(t.misses >= 4 ? `${v.say} Watch me once more.` : v.say, 'no');
      if (t.misses >= 4) { t.misses = 0; replayDemo(); }
    }
    render();
  }

  // ---------------------------------------------------------------- action step
  const gestures = () => app.info()?.g || [];
  const slotOf = (m) => gestures().findIndex((g) => (g.n || '').toLowerCase() === MOVES[m].name.toLowerCase());
  const codeName = (c) => { const n = app.info()?.c?.[c]?.n; return n ? n.replace(/_/g, ' ') : `Code ${c + 1}`; };

  function chooseAction(m) {
    let g = slotOf(m);
    if (g < 0) g = gestures().findIndex((x) => !x.c && !x.n);
    if (g < 0) g = gestures().findIndex((x) => !x.c);
    if (g < 0) return say('All eight spell slots are taken. Clear one in the Spells tab first.', 'no');
    const s = gestures()[g];
    if (s?.c >= 3 && (s.n || '').toLowerCase() === MOVES[m].name.toLowerCase()) return app.openLibrary({ spell: g });
    // The wand recognizes spells from your own samples: let it watch 3 times.
    t.teach = { move: m, slot: g, count: s && (s.n || '').toLowerCase() === MOVES[m].name.toLowerCase() ? s.c : 0 };
    t.step = 'teach';
    say(`Your wand must learn your own ${MOVES[m].name}. Do it three times, each time right after the wand buzzes "go".`, 'talk2');
    app.send(`REC ${g} ${MOVES[m].name}`);
    render();
  }

  function onRec(g, what, count) {
    if (t.step !== 'teach' || !t.teach || g !== t.teach.slot) return;
    const name = MOVES[t.teach.move].name;
    if (what === 'ok') {
      t.teach.count = count;
      if (count >= NEEDED) {
        const slot = t.teach.slot;
        t.teach = null;
        t.step = 'action';
        say(`Your wand knows your ${name} now. Choose what it does.`, 'yes');
        render();
        setTimeout(() => app.openLibrary({ spell: slot }), 900);
        return;
      }
      say(`Good, ${count} of ${NEEDED}. Again, after the buzz.`, 'yes');
    } else if (what === 'timeout') {
      say(`I saw nothing. Do the ${name} right after the buzz.`, 'no');
    } else return;
    render();
    setTimeout(() => { if (t.step === 'teach' && t.teach) app.send(`REC ${t.teach.slot} ${name}`); }, 1200);
  }

  // ---------------------------------------------------------------- render
  function demoSvg(m, big) {
    return `<svg class="move-demo ${big ? 'big' : ''}" viewBox="0 0 100 100" aria-label="${esc(MOVES[m].name)} path">
      <path d="${MOVES[m].path}" class="trail"/>
      <circle r="5" class="spark"><animateMotion dur="${m === 'hit' ? 1.2 : 1.8}s" repeatCount="indefinite" path="${MOVES[m].path}"/></circle>
    </svg>`;
  }
  function replayDemo() {
    const d = $('#trainArea .move-demo');
    if (d) { d.classList.remove('pulse'); void d.offsetWidth; d.classList.add('pulse'); }
  }
  const stars = () => `<div class="stars" aria-label="${t.good} of ${NEEDED}">${Array.from({ length: NEEDED }, (_, k) => `<span class="${k < t.good ? 'on' : ''}">★</span>`).join('')}</div>`;
  const pretend = () => (app.perform ? `<div class="pretend"><span class="muted small">Pretend wand:</span>
      <button class="small" data-pretend="1">do it right</button><button class="small" data-pretend="0.33">too slow</button>
      <button class="small" data-pretend="3">rushed</button><button class="small" data-pretend="wrong">wrong move</button></div>` : '');

  function render() {
    const area = $('#trainArea');
    const m = cur();
    const progress = `<ol class="train-steps">${ORDER.map((x, i) => `<li class="${i < t.move || ['action', 'teach', 'finish'].includes(t.step) ? 'done' : i === t.move && practising() ? 'now' : ''}">${MOVES[x].name}</li>`).join('')}
      <li class="${t.step === 'finish' ? 'done' : ['action', 'teach'].includes(t.step) ? 'now' : ''}">Actions</li></ol>`;
    let html = '';
    if (!app.connected()) {
      html = `<p class="muted">Summon your wand to begin.</p>`;
    } else if (t.step === 'intro') {
      html = `<button class="primary big" data-act="begin">Begin training</button>
        <button class="small" data-act="skip-to-action">I know the moves: go to actions</button>`;
    } else if (t.step === 'show') {
      html = `${demoSvg(m, true)}<button class="primary big" data-act="try">I'm ready</button>`;
    } else if (t.step === 'try' || t.step === 'mastered') {
      html = `<div class="try-row">${demoSvg(m, false)}<div>${stars()}<p class="muted small">${t.step === 'try' ? 'Watching your wand…' : ''}</p></div></div>
        ${t.step === 'try' ? `${pretend()}<button class="small linkish" data-act="skip">Skip this spell</button>` : ''}`;
    } else if (t.step === 'action' || t.step === 'teach') {
      html = `<div class="action-list">${ORDER.map((x) => {
        const g = slotOf(x), s = gestures()[g];
        const b = s && s.b >= 0 ? `sends “${esc(codeName(s.b))}”` : s?.c >= 3 ? 'no action yet' : 'not on the wand yet';
        const teaching = t.step === 'teach' && t.teach?.move === x;
        return `<div class="action-row ${teaching ? 'teaching' : ''}">${demoSvg(x, false)}
          <div><b>${MOVES[x].name}</b><div class="muted small">${teaching ? `wand learning: ${t.teach.count} of ${NEEDED}` : b}</div></div>
          <button class="small primary" data-act="choose" data-move="${x}" ${t.step === 'teach' ? 'disabled' : ''}>${s?.b >= 0 ? 'Change' : 'Choose action'}</button>
        </div>`;
      }).join('')}</div>
      <button class="primary" data-act="finish" ${t.step === 'teach' ? 'disabled' : ''}>Finish training</button>`;
    } else if (t.step === 'finish') {
      html = `<button class="primary" data-act="restart">Practise again</button>`;
    }
    area.innerHTML = progress + html;
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
    if (act === 'begin') { t.move = 0; go('show'); }
    else if (act === 'try') go('try');
    else if (act === 'skip') { t.good = NEEDED; go('mastered'); }
    else if (act === 'skip-to-action') go('action');
    else if (act === 'choose') chooseAction(b.dataset.move);
    else if (act === 'finish') go('finish');
    else if (act === 'restart') { t.move = 0; go('intro'); }
  });

  // ---------------------------------------------------------------- hooks from app.js
  return {
    onLine(tag, p) {
      if (tag === 'F') onFeature(p.slice(0, 5).map(Number));
      else if (tag === 'REC') onRec(+p[0], p[1], +p[2]);
      else if (tag === 'INFO' && (t.step === 'action' || t.step === 'teach')) render();
    },
    onShown(shown) {
      t.shown = shown;
      if (!shown) hush();
      else if (!$('#wizSay').textContent) go(t.step);
      setPractice(practising());
      if (shown) render();
    },
    onConnect() { t.practice = false; render(); setPractice(practising()); },
    onDisconnect() { t.practice = false; if (t.step === 'teach') { t.teach = null; t.step = 'action'; } render(); },
    firstTime: () => store.get('wandTrained') !== '1',
  };
}
