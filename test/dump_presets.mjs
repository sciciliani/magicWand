// Prints every app preset as "<name> <khz> <d1,d2,...>" for test_ir_codec.
import { PRESETS, LIBRARY } from '../app/ir-presets.js';
for (const [brand, buttons] of Object.entries(PRESETS))
  for (const [btn, make] of Object.entries(buttons)) {
    const c = make();
    console.log(`${(brand + '_' + btn).replace(/\s+/g, '_')} ${c.khz} ${c.d.join(',')}`);
  }
for (const [dev, brands] of Object.entries(LIBRARY))
  for (const [brand, b] of Object.entries(brands))
    for (const a of b.actions)
      if (a.make) {
        const c = a.make();
        console.log(`${b.short}_${a.short}_${brand.replace(/\W+/g, '')} ${c.khz} ${c.d.join(',')}`);
      }
