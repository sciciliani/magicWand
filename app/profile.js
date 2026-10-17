// profile.js — what this browser remembers about each wand, keyed by the
// wand's chip ID (INFO "uid": it survives renames and factory resets):
//   profile: your name, setup done, home devices, knight battle codes
//   backups: the last 5 full copies of the wand (spells, IR codes, settings)
// Everything is in localStorage: it stays in this browser only.

const get = (k) => { try { return JSON.parse(localStorage.getItem(k) || 'null'); } catch { return null; } };
const put = (k, v) => { try { localStorage.setItem(k, JSON.stringify(v)); } catch { /* private mode or full */ } };

export const MAX_BACKUPS = 5;

/** The ID the app uses for a wand: its chip ID, else Chrome's device id, else its name. */
export function wandId(info, link) {
  if (info?.uid) return info.uid;
  if (link?.device?.id) return 'dev:' + link.device.id;
  return 'name:' + (info?.name || link?.name || 'wand');
}

/** True when the wand still has its factory name (Wand-XXXX = end of the chip ID). */
export function factoryNamed(info) {
  if (!info?.name) return false;
  if (info.uid) return info.name.toUpperCase() === `WAND-${info.uid.slice(-4).toUpperCase()}`;
  return /^Wand-[0-9A-F]{4}$/i.test(info.name);
}

/** Nothing on it: factory name, no spells, no IR codes (new, or just reset). */
export function wandIsEmpty(info) {
  return factoryNamed(info) && !(info.g || []).some((g) => g.c) && !(info.c || []).some((c) => c.k);
}

export function loadProfile(id) {
  return { owner: '', setupDone: false, devices: [], knight: null, spells: {}, ...(get('wandProfile:' + id) || {}), id };
}
export function saveProfile(p) { put('wandProfile:' + p.id, p); }

export function listBackups(id) { return get('wandBackups:' + id) || []; }
export function addBackup(id, backup) {
  const list = [{ ...backup, ts: Date.now() }, ...listBackups(id)].slice(0, MAX_BACKUPS);
  put('wandBackups:' + id, list);
  return list;
}

// The wand to reconnect to on the next visit.
export const lastWand = {
  get: () => get('lastWand'),
  set: (v) => put('lastWand', v),
};
