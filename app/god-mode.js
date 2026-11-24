// God mode: hidden developer page (English only, not translated on purpose).
// Open the app with ?god_mode (remembered in this browser; ?god_mode=0 hides it
// again), or tap the ✦ logo 7 times. First page: the buzzer's sound lab.
//
// Protocol (0.6.3): SOUND, SOUND <name>, SOUND on|off|vol|pitch|castsound|events,
// SOUND tone|sweep|stop. Light ring (0.7.0): LIGHT, LIGHT <effect>|walk|on|off|bright|events|px|stop.
// See docs/PROTOCOL.md.

const $ = (s) => document.querySelector(s);
const SOUNDS = [
  ['cast', '✨', 'Cast'], ['lightning', '⚡', 'Lightning'], ['glow', '🌟', 'Glow'], ['error', '✖', 'Error'],
  ['learned', '🔔', 'Learned'], ['wake', '🌅', 'Wake up'], ['lowbat', '🪫', 'Low battery'],
];
const LEN = { cast: 240, lightning: 270, glow: 492, error: 210, learned: 234, wake: 100, lowbat: 312 };  // ms (sounds_data.h)
// Light effects: same shapes and timings as light.cpp, so the preview ring shows what the wand shows.
const LIGHTS = [
  ['cast', '✨', 'Cast'], ['lightning', '⚡', 'Lightning'], ['glow', '🌟', 'Glow'], ['error', '✖', 'Error'],
  ['learned', '🔔', 'Learned'], ['wake', '🌅', 'Wake up'], ['lowbat', '🪫', 'Low battery'], ['walk', '🔄', 'Walk (test)'],
];
const C = { gold: [255, 150, 30], white: [190, 210, 255], violet: [170, 80, 255], red: [255, 20, 10], green: [30, 255, 60],
  blue: [40, 110, 255], amber: [255, 90, 0], dim: [120, 120, 120] };
const sc = (c, k) => c.map((v) => v * k);
const ramp = (t, a, b) => (t <= a ? 0 : t >= b ? 1 : ((t - a) / (b - a)) ** 2);
const on = (t, a, b) => t >= a && t < b;
/** Pixels at time t (ms) of an effect, or null when it's over. */
function lightFrame(name, t, N, pix) {
  const px = Array.from({ length: N }, () => null);
  switch (name) {
    case 'cast': { if (t >= 340) return null; const h = Math.floor(t * N / 320) % N; px[h] = C.gold; px[(h + N - 1) % N] = sc(C.gold, 70 / 256); return px; }
    case 'lightning': { if (t >= 240) return null; const k = on(t, 0, 60) ? 1 : on(t, 110, 190) ? 140 / 256 : 0; for (let j = 0; j < 3; j++) px[Math.floor(j * N / 3)] = sc(C.white, k); return px; }
    case 'glow': { if (t >= 500) return null; const k = t < 180 ? ramp(t, 0, 180) : 1 - ramp(t, 180, 500); for (let j = 0; j < 3; j++) px[j] = sc(C.violet, j === 1 ? k : k * 0.6); return px; }
    case 'error': case 'learned': { if (t >= 300) return null; if (on(t, 0, 100) || on(t, 180, 280)) { const c = name === 'error' ? C.red : C.green; px[0] = c; px[N >> 1] = c; } return px; }
    case 'wake': { if (t >= 260) return null; px[0] = sc(C.blue, t < 100 ? ramp(t, 0, 100) : 1 - ramp(t, 100, 260)); return px; }
    case 'lowbat': { if (t >= 560) return null; if (on(t, 0, 80) || on(t, 240, 320) || on(t, 480, 560)) px[0] = C.amber; return px; }
    case 'walk': { if (t >= N * 150) return null; px[Math.floor(t / 150)] = C.dim; return px; }
    case 'px': { if (t >= pix.ms) return null; px[pix.i % N] = pix.rgb; return px; }
  }
  return null;
}
const FINDER = Array.from({ length: 17 }, (_, i) => 1000 + i * 250);
const SOUND_CENTRE = 2000;  // Hz: roughly where the sound tables sit (pitch 0)

function storedFlag() { try { return localStorage.getItem('godMode') === '1'; } catch { return false; } }
function storeFlag(on) { try { on ? localStorage.setItem('godMode', '1') : localStorage.removeItem('godMode'); } catch { /* private */ } }

/** Should the tab be visible? ?god_mode turns it on (and remembers), ?god_mode=0 off. */
export function godModeWanted() {
  const q = new URLSearchParams(location.search);
  if (q.has('god_mode')) storeFlag(!['0', 'false', 'off'].includes(q.get('god_mode')));
  return storedFlag();
}

export function initGodMode({ send, connected, info, toast, showTab }) {
  const btn = $('#godTabBtn');
  let state = null;   // last SOUND {json}
  let findTimer = 0, findI = -1, foundHz = 0;

  const show = (on) => {
    btn.hidden = !on;
    storeFlag(on);
    if (!on && !$('#tab-god').hidden) showTab('live');
  };
  show(godModeWanted());
  // 7 taps on the logo within 3 s toggles the page
  let taps = [];
  $('.logo')?.addEventListener('click', () => {
    const now = Date.now();
    taps = taps.filter((t) => now - t < 3000).concat(now);
    if (taps.length >= 7) {
      taps = [];
      show(btn.hidden);
      toast(btn.hidden ? 'God mode off' : '⚡ God mode on');
      if (!btn.hidden) showTab('god');
    }
  });

  // ---- frequency slider: log scale 100..12000 Hz on 0..1000
  const hzFromPos = (p) => Math.round(100 * Math.pow(120, p / 1000) / 10) * 10;
  const posFromHz = (hz) => Math.round(1000 * Math.log(hz / 100) / Math.log(120));
  const setHz = (hz) => {
    hz = Math.min(12000, Math.max(100, Math.round(hz)));
    $('#godHz').value = posFromHz(hz);
    $('#godHzNum').value = hz;
    $('#godHzOut').textContent = `${hz} Hz`;
  };
  setHz(2700);
  $('#godHz').addEventListener('input', () => setHz(hzFromPos(+$('#godHz').value)));
  $('#godHzNum').addEventListener('change', () => setHz(+$('#godHzNum').value || 2700));

  // ---- pads
  const pads = $('#godPads');
  for (const [name, icon, label] of SOUNDS) {
    const b = document.createElement('button');
    b.dataset.sound = name;
    b.innerHTML = `<b>${icon}</b><span>${label}</span>`;
    b.addEventListener('click', () => play(name));
    pads.append(b);
  }
  function glow(name) {
    const b = pads.querySelector(`[data-sound="${name}"]`);
    if (!b) return;
    b.classList.add('playing');
    setTimeout(() => b.classList.remove('playing'), (LEN[name] || 300) + 150);
  }
  function play(name) {
    if (!connected()) return;
    send(`SOUND ${name}`);
    glow(name);
  }
  let allTimers = [];
  $('#godAll').addEventListener('click', () => {
    allTimers.forEach(clearTimeout);
    allTimers = SOUNDS.map(([name], i) => setTimeout(() => play(name), i * 900));
  });
  $('#godStop').addEventListener('click', () => {
    allTimers.forEach(clearTimeout);
    stopFinder();
    send('SOUND stop');
  });

  // ---- settings (each is saved on the wand)
  const set = (line) => { if (connected()) { send(line); send('SOUND'); } };
  $('#godOn').addEventListener('change', (e) => set(`SOUND ${e.target.checked ? 'on' : 'off'}`));
  $('#godVol').addEventListener('input', () => ($('#godVolOut').textContent = `${$('#godVol').value} %`));
  $('#godVol').addEventListener('change', () => { set(`SOUND vol ${$('#godVol').value}`); play('cast'); });
  const pitchText = (v) => (v == 0 ? '0 (as designed)' : `${v > 0 ? '+' : ''}${v} semitone${Math.abs(v) === 1 ? '' : 's'}`);
  $('#godPitch').addEventListener('input', () => ($('#godPitchOut').textContent = pitchText(+$('#godPitch').value)));
  $('#godPitch').addEventListener('change', () => { set(`SOUND pitch ${$('#godPitch').value}`); play('cast'); });
  $('#godCast').addEventListener('change', (e) => {
    set(`SOUND castsound ${e.target.value}`);
    if (e.target.value !== 'random') play(e.target.value);
  });
  document.querySelectorAll('.god-events input[data-ev]').forEach((cb) => cb.addEventListener('change', () => {
    let mask = 0;
    document.querySelectorAll('.god-events input[data-ev]').forEach((x) => { if (x.checked) mask |= 1 << +x.dataset.ev; });
    set(`SOUND events ${mask}`);
  }));

  // ---- tone / sweep
  const num = (sel, lo, hi, dflt) => Math.min(hi, Math.max(lo, Math.round(+$(sel).value || dflt)));
  $('#godTone').addEventListener('click', () => {
    if (!connected()) return;
    send(`SOUND tone ${num('#godHzNum', 100, 12000, 2700)} ${num('#godMs', 10, 5000, 500)} ${num('#godDuty', 1, 50, 50)}`);
  });
  $('#godSweep').addEventListener('click', () => {
    if (!connected()) return;
    send(`SOUND sweep ${num('#godSw0', 100, 12000, 500)} ${num('#godSw1', 100, 12000, 6000)} ${num('#godSwMs', 20, 5000, 3000)}`);
  });

  // ---- loudest-frequency finder: one step every 600 ms (350 ms tone, then a gap)
  function stopFinder() {
    clearInterval(findTimer);
    findTimer = 0;
    $('#godFind').textContent = findI >= 0 ? 'Again' : 'Start';
    $('#godFound').disabled = true;
  }
  function stepFinder() {
    findI++;
    if (findI >= FINDER.length) {
      stopFinder();
      $('#godFindNote').textContent = 'Done. Press Start to hear it again.';
      return;
    }
    const hz = FINDER[findI];
    $('#godFindHz').textContent = `${hz} Hz`;
    send(`SOUND tone ${hz} 350 50`);
  }
  $('#godFind').addEventListener('click', () => {
    if (!connected()) return;
    if (findTimer) { stopFinder(); return; }
    findI = -1;
    $('#godFindNote').textContent = '';
    $('#godFind').textContent = 'Stop';
    $('#godFound').disabled = false;
    stepFinder();
    findTimer = setInterval(stepFinder, 600);
  });
  $('#godFound').addEventListener('click', () => {
    // the tone you reacted to is usually the one that just played
    foundHz = FINDER[Math.max(0, findI)];
    stopFinder();
    setHz(foundHz);
    const st = suggestedPitch(foundHz);
    $('#godUsePitch').disabled = false;
    $('#godUsePitch').textContent = `Use as pitch (${st > 0 ? '+' : ''}${st})`;
    $('#godFindHz').textContent = `${foundHz} Hz ✓`;
    $('#godFindNote').textContent = 'Set in the tone generator. Fine-tune it there with the slider.';
  });
  // Don't push sounds all the way to the peak: half-way keeps them musical.
  const suggestedPitch = (hz) => Math.max(-12, Math.min(12, Math.round(6 * Math.log2(hz / SOUND_CENTRE))));
  $('#godUsePitch').addEventListener('click', () => {
    if (!foundHz) return;
    const st = suggestedPitch(foundHz);
    $('#godPitch').value = st;
    $('#godPitchOut').textContent = pitchText(st);
    set(`SOUND pitch ${st}`);
    play('cast');
  });

  // ---- light ring
  let light = null, anim = 0;
  const ring = $('#godRing');
  const LEDS = () => light?.leds || 8;
  function buildRing() {
    ring.innerHTML = '';
    const n = LEDS();
    for (let i = 0; i < n; i++) {
      const a = (i / n) * 2 * Math.PI - Math.PI / 2;  // LED 0 at the top, clockwise
      const b = document.createElement('button');
      b.style.left = `${50 + 40 * Math.cos(a)}%`;
      b.style.top = `${50 + 40 * Math.sin(a)}%`;
      b.textContent = i;
      b.title = `LED ${i}`;
      b.addEventListener('click', () => {
        if (!connected()) return;
        const hex = $('#godPxColor').value.slice(1);
        send(`LIGHT px ${i} ${hex} 1500`);
        preview('px', { i, ms: 1500, rgb: [0, 2, 4].map((k) => parseInt(hex.slice(k, k + 2), 16)) });
      });
      ring.append(b);
    }
  }
  function paint(px) {
    const max = light?.maxlit || 3, k = 0.35 + 0.65 * ((light?.bright ?? 60) / 100);
    let lit = 0;
    [...ring.children].forEach((b, i) => {
      let c = px?.[i];
      if (c && !c.some((v) => v > 0)) c = null;
      if (c && lit >= max) c = null;  // the wand never lights more than maxlit
      if (c) lit++;
      const [r, g, bl] = c ? c.map((v) => Math.round(Math.min(255, v * k))) : [0, 0, 0];
      b.classList.toggle('lit', !!c);
      b.style.background = c ? `rgb(${r},${g},${bl})` : '';
      b.style.boxShadow = c ? `0 0 14px rgb(${r},${g},${bl})` : '';
    });
  }
  function preview(name, pix) {
    cancelAnimationFrame(anim);
    const t0 = performance.now();
    const step = () => {
      const px = lightFrame(name, performance.now() - t0, LEDS(), pix);
      paint(px);
      if (px) anim = requestAnimationFrame(step);
    };
    step();
  }
  buildRing();
  const lpads = $('#godLightPads');
  for (const [name, icon, label] of LIGHTS) {
    const b = document.createElement('button');
    b.innerHTML = `<b>${icon}</b><span>${label}</span>`;
    b.addEventListener('click', () => { if (!connected()) return; send(`LIGHT ${name}`); preview(name); });
    lpads.append(b);
  }
  $('#godLightStop').addEventListener('click', () => { send('LIGHT stop'); cancelAnimationFrame(anim); paint(null); });
  const setL = (line) => { if (connected()) { send(line); send('LIGHT'); } };
  $('#godLightOn').addEventListener('change', (e) => setL(`LIGHT ${e.target.checked ? 'on' : 'off'}`));
  $('#godBright').addEventListener('input', () => ($('#godBrightOut').textContent = `${$('#godBright').value} %`));
  $('#godBright').addEventListener('change', () => { setL(`LIGHT bright ${$('#godBright').value}`); send('LIGHT cast'); preview('cast'); });
  document.querySelectorAll('.god-light-events input').forEach((cb) => cb.addEventListener('change', () => {
    let mask = 0;
    document.querySelectorAll('.god-light-events input').forEach((x) => { if (x.checked) mask |= 1 << +x.dataset.lev; });
    setL(`LIGHT events ${mask}`);
  }));
  function renderLight() {
    const ok = connected() && !!light && light.hw !== 0;
    document.querySelectorAll('#godRing button, #godLightPads button, #godLightStop, #godLightOn, #godBright, #godPxColor, .god-light-events input')
      .forEach((x) => (x.disabled = !ok));
    if (!light) { $('#godLightHw').textContent = connected() ? 'needs firmware 0.7.0' : '—'; return; }
    if (ring.children.length !== LEDS()) buildRing();
    $('#godLightHw').textContent = light.info || '—';
    $('#godLightOn').checked = !!light.on;
    $('#godBright').value = light.bright;
    $('#godBrightOut').textContent = `${light.bright} %`;
    document.querySelectorAll('.god-light-events input').forEach((x) => (x.checked = !!(light.events & (1 << +x.dataset.lev))));
  }

  // ---- state from the wand
  function render() {
    const i = info();
    const note = $('#godNote');
    const old = i && !('buzzer' in i);
    const enabled = connected() && !!state && state.hw !== 0;
    note.hidden = true;
    if (!connected()) { note.hidden = false; note.textContent = 'Connect a wand to use the sound lab.'; }
    else if (old) { note.hidden = false; note.textContent = `This wand runs ${i.fw}. The sound lab needs firmware 0.6.3 or newer.`; }
    else if (i && i.buzzer === 0) { note.hidden = false; note.textContent = 'This firmware was built without a buzzer (HAS_BUZZER 0 in config.h).'; }
    document.querySelectorAll('#tab-god button, #tab-god input, #tab-god select').forEach((el) => {
      if (el.id === 'godFound' || el.id === 'godUsePitch' || el.closest('.god-light, .god-light-events, #godLightPads') || el.id === 'godLightStop') return;
      el.disabled = !enabled;
    });
    renderLight();
    if (!state) return;
    $('#godHw').textContent = state.info || '—';
    $('#godOn').checked = !!state.on;
    $('#godVol').value = state.vol;
    $('#godVolOut').textContent = `${state.vol} %`;
    $('#godPitch').value = state.pitch;
    $('#godPitchOut').textContent = pitchText(state.pitch);
    $('#godCast').value = state.cast;
    document.querySelectorAll('.god-events input[data-ev]').forEach((x) => (x.checked = !!(state.events & (1 << +x.dataset.ev))));
    $('#godRaw').textContent = JSON.stringify({ sound: state, light }, null, 1);
  }

  return {
    onShown(shown) {
      if (shown && connected()) { send('SOUND'); send('LIGHT'); }
      if (!shown) stopFinder();
      render();
    },
    onInfo() { if (!$('#tab-god').hidden && connected()) { send('SOUND'); send('LIGHT'); } render(); },
    onDisconnect() { state = null; light = null; stopFinder(); render(); },
    onLine(tag, p) {
      if (tag !== 'SOUND' && tag !== 'LIGHT') return;
      const raw = p.join(' ');
      let v = null;
      try { v = JSON.parse(raw); } catch { $('#godRaw').textContent = raw; }
      if (tag === 'SOUND') state = v; else light = v;
      render();
    },
  };
}
