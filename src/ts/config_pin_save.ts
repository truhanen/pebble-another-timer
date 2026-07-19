// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 Tuomas Airaksinen

// Clay's customFn: runs inside the generated config webview (not in pkjs) —
// Clay serializes this function's *source* into that page's HTML via
// `tosource`, so it must be fully self-contained (no closures over pkjs
// modules/scope; only `this` (the ClayConfig instance) and DOM/window
// globals are available at runtime).
export default function pinSaveButtonToBottom(this: any): void {
  const clayConfig = this;
  clayConfig.on(clayConfig.EVENTS.AFTER_BUILD, () => {
    let submitEl: Element | null =
      document.querySelector('.submit-button, [type="submit"], .component-submit');
    if (!submitEl) {
      const allBtns = document.querySelectorAll('button, input[type="submit"]');
      for (let i = 0; i < allBtns.length; i++) {
        const el = allBtns[i] as HTMLButtonElement | HTMLInputElement;
        const txt = el.textContent || (el as HTMLInputElement).value || '';
        if (txt.trim().toLowerCase() === 'save') { submitEl = el; break; }
      }
    }
    if (!submitEl) { return; }

    let wrapper: Element | null = submitEl;
    while (wrapper && wrapper.tagName !== 'LI' && wrapper.tagName !== 'BODY') {
      wrapper = wrapper.parentElement;
    }
    const fixedEl = (wrapper && wrapper.tagName === 'LI' ? wrapper : submitEl) as HTMLElement;
    const scrollContainer = fixedEl.parentElement;

    // Deferred so layout has settled and offsetHeight below is accurate.
    setTimeout(() => {
      const bg = (scrollContainer && window.getComputedStyle(scrollContainer).backgroundColor) ||
        window.getComputedStyle(document.body).backgroundColor || '#1c1c1c';
      fixedEl.style.cssText = 'position:fixed;bottom:0;left:0;right:0;z-index:1000;' +
        'margin:0;border-top:1px solid #333;background:' + bg + ';';
      const btnH = fixedEl.offsetHeight || 56;
      document.body.style.cssText += 'margin:0;padding:0;overflow:hidden;height:100vh;';
      document.documentElement.style.cssText += 'overflow:hidden;height:100vh;';
      if (scrollContainer) {
        scrollContainer.style.cssText += 'overflow-y:auto;' +
          'height:calc(100vh - ' + btnH + 'px);' +
          'box-sizing:border-box;display:block;';
      }
    }, 0);
  });
}
