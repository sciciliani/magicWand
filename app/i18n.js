// i18n.js — UI language. English is the source: t('English text') returns the
// translation for the current language, or the English text itself.
// Placeholders: t('Lost {name}.', { name }).
//
// The language follows the browser (en, es, pt, it, de) unless one is picked
// in the header (remembered in this browser).
import es from './lang/es.js?v=2';
import pt from './lang/pt.js?v=2';
import it from './lang/it.js?v=2';
import de from './lang/de.js?v=2';

export const LANGS = { en: 'English', es: 'Español', pt: 'Português', it: 'Italiano', de: 'Deutsch' };
const DICTS = { es, pt, it, de };
// BCP-47 tags for speech and <html lang>
export const SPEECH_TAG = { en: 'en-US', es: 'es-MX', pt: 'pt-BR', it: 'it-IT', de: 'de-DE' };  // US English, Latin American Spanish

const store = {
  get(k) { try { return localStorage.getItem(k); } catch { return null; } },
  set(k, v) { try { if (v == null) localStorage.removeItem(k); else localStorage.setItem(k, v); } catch { /* private mode */ } },
};

function browserLang() {
  const list = (typeof navigator !== 'undefined' && (navigator.languages?.length ? navigator.languages : [navigator.language])) || [];
  for (const l of list) {
    const code = String(l || '').slice(0, 2).toLowerCase();
    if (LANGS[code]) return code;
  }
  return 'en';
}

let picked = store.get('wandLang');
export let lang = LANGS[picked] ? picked : browserLang();
export const langChoice = () => (LANGS[picked] ? picked : 'auto');

export function t(text, vars) {
  let s = (lang !== 'en' && DICTS[lang]?.[text]) || text;
  if (vars) s = s.replace(/\{(\w+)\}/g, (m, k) => (k in vars ? vars[k] : m));
  return s;
}

const listeners = [];
export const onLangChange = (fn) => listeners.push(fn);

export function setLang(choice) {
  picked = choice === 'auto' ? null : choice;
  store.set('wandLang', picked);
  lang = LANGS[picked] ? picked : browserLang();
  applyStatic();
  listeners.forEach((fn) => fn(lang));
}

// Static page text: elements marked data-i18n (innerHTML, English kept in
// data-i18n-src), data-i18n-placeholder, data-i18n-title, data-i18n-alt.
export function applyStatic(root = document) {
  if (typeof document === 'undefined') return;
  document.documentElement.lang = lang;
  root.querySelectorAll('[data-i18n]').forEach((el) => {
    if (!el.dataset.i18nSrc) el.dataset.i18nSrc = el.innerHTML.trim().replace(/\s+/g, ' ');
    el.innerHTML = t(el.dataset.i18nSrc);
  });
  for (const attr of ['placeholder', 'title', 'alt']) {
    root.querySelectorAll(`[data-i18n-${attr}]`).forEach((el) => {
      const key = `i18n${attr[0].toUpperCase()}${attr.slice(1)}`;
      el.setAttribute(attr, t(el.dataset[key]));
    });
  }
}

// Language pickers (<select class="lang-pick">): Auto + the five languages.
export function initPickers() {
  document.querySelectorAll('select.lang-pick').forEach((sel) => {
    sel.innerHTML = `<option value="auto">🌐 Auto</option>` +
      Object.entries(LANGS).map(([k, v]) => `<option value="${k}">${v}</option>`).join('');
    sel.value = langChoice();
    sel.addEventListener('change', () => {
      setLang(sel.value);
      document.querySelectorAll('select.lang-pick').forEach((s) => (s.value = langChoice()));
    });
  });
}

// ASCII-only version of a name (the wand only keeps A-Z a-z 0-9 -): "Círculo" -> "Circulo".
export const ascii = (s) => s.normalize('NFD').replace(/[̀-ͯ]/g, '');

// The same text in every language (to recognize names the wand may hold).
export const allTranslations = (text) => [text, ...Object.values(DICTS).map((d) => d[text]).filter(Boolean)];
