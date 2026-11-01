/* Mermaid SVG modal with pointer-centered wheel zoom and drag-to-pan.
 * Works with asynchronously rendered sphinxcontrib-mermaid diagrams.
 * No third-party runtime or image conversion required.
 */
(() => {
  "use strict";
  const SELECTOR = ".mermaid svg, .mermaid-container svg, .mermaid-diagram svg";
  const clamp = (value, lo, hi) => Math.max(lo, Math.min(hi, value));
  let overlay, viewport, stage, zoomLabel, closeButton, active;

  function redraw() {
    if (!active) return;
    stage.style.transform = "translate(" + active.x + "px," + active.y + "px) scale(" + active.zoom + ")";
    zoomLabel.textContent = Math.round(active.zoom * 100) + "%";
  }
  function zoomAt(multiplier, px, py) {
    if (!active) return;
    const next = clamp(active.zoom * multiplier, 0.05, 10);
    const ratio = next / active.zoom;
    active.x = px - (px - active.x) * ratio;
    active.y = py - (py - active.y) * ratio;
    active.zoom = next;
    redraw();
  }
  function fit() {
    if (!active) return;
    const zoom = Math.min((viewport.clientWidth - 48) / active.width,
                          (viewport.clientHeight - 48) / active.height);
    active.zoom = clamp(zoom, 0.05, 10);
    active.x = (viewport.clientWidth - active.width * active.zoom) / 2;
    active.y = (viewport.clientHeight - active.height * active.zoom) / 2;
    redraw();
  }
  function close() {
    if (!active) return;
    const old = active;
    active = null;
    old.placeholder.replaceWith(old.svg);
    if (old.style === null) old.svg.removeAttribute("style");
    else old.svg.setAttribute("style", old.style);
    stage.replaceChildren();
    overlay.hidden = true;
    viewport.classList.remove("diagram-viewer-dragging");
    document.body.style.overflow = old.overflow;
    old.opener.focus({preventScroll: true});
  }
  function button(text, title, fn) {
    const b = document.createElement("button");
    b.type = "button";
    b.textContent = text;
    b.title = title;
    b.setAttribute("aria-label", title);
    b.addEventListener("click", fn);
    return b;
  }
  function setup() {
    if (overlay) return;
    overlay = document.createElement("section");
    overlay.className = "diagram-viewer-overlay";
    overlay.setAttribute("role", "dialog");
    overlay.setAttribute("aria-modal", "true");
    overlay.setAttribute("aria-label", "Zoomable Mermaid diagram");
    overlay.hidden = true;
    const toolbar = document.createElement("div");
    toolbar.className = "diagram-viewer-toolbar";
    const minus = button("−", "Zoom out", () => zoomAt(1 / 1.3, viewport.clientWidth / 2, viewport.clientHeight / 2));
    const plus = button("+", "Zoom in", () => zoomAt(1.3, viewport.clientWidth / 2, viewport.clientHeight / 2));
    zoomLabel = document.createElement("output");
    zoomLabel.className = "diagram-viewer-zoom";
    zoomLabel.setAttribute("aria-label", "Zoom level");
    closeButton = button("×", "Close diagram (Escape)", close);
    toolbar.append(minus, zoomLabel, plus, button("Fit", "Fit diagram (0)", fit), closeButton);
    viewport = document.createElement("div");
    viewport.className = "diagram-viewer-viewport";
    viewport.tabIndex = 0;
    viewport.setAttribute("aria-label", "Wheel to zoom; drag to move; double-click to fit");
    stage = document.createElement("div");
    stage.className = "diagram-viewer-stage";
    viewport.append(stage);
    overlay.append(toolbar, viewport);
    document.body.append(overlay);

    viewport.addEventListener("wheel", (event) => {
      if (!active) return;
      event.preventDefault();
      const rect = viewport.getBoundingClientRect();
      zoomAt(Math.exp(-event.deltaY * 0.002), event.clientX - rect.left, event.clientY - rect.top);
    }, {passive: false});
    viewport.addEventListener("pointerdown", (event) => {
      if (!active || event.button !== 0 || event.target.closest("a")) return;
      event.preventDefault();
      active.pointer = event.pointerId;
      active.startX = event.clientX - active.x;
      active.startY = event.clientY - active.y;
      viewport.setPointerCapture(event.pointerId);
      viewport.classList.add("diagram-viewer-dragging");
    });
    viewport.addEventListener("pointermove", (event) => {
      if (!active || active.pointer !== event.pointerId) return;
      active.x = event.clientX - active.startX;
      active.y = event.clientY - active.startY;
      redraw();
    });
    const endDrag = (event) => {
      if (!active || active.pointer !== event.pointerId) return;
      active.pointer = null;
      viewport.classList.remove("diagram-viewer-dragging");
      if (viewport.hasPointerCapture(event.pointerId)) viewport.releasePointerCapture(event.pointerId);
    };
    viewport.addEventListener("pointerup", endDrag);
    viewport.addEventListener("pointercancel", endDrag);
    viewport.addEventListener("lostpointercapture", () => {
      if (active) active.pointer = null;
      viewport.classList.remove("diagram-viewer-dragging");
    });
    viewport.addEventListener("dblclick", (event) => {event.preventDefault(); fit();});
    document.addEventListener("keydown", (event) => {
      if (!active) return;
      if (event.key === "Escape") {event.preventDefault(); close();}
      else if (event.key === "+" || event.key === "=") {event.preventDefault(); zoomAt(1.3, viewport.clientWidth / 2, viewport.clientHeight / 2);}
      else if (event.key === "-") {event.preventDefault(); zoomAt(1 / 1.3, viewport.clientWidth / 2, viewport.clientHeight / 2);}
      else if (event.key === "0") {event.preventDefault(); fit();}
      else if (event.key === "Tab") {
        const focusables = Array.from(overlay.querySelectorAll("button, [tabindex='0']"));
        const position = focusables.indexOf(document.activeElement);
        if (event.shiftKey && position <= 0) {event.preventDefault(); focusables[focusables.length - 1].focus();}
        else if (!event.shiftKey && position === focusables.length - 1) {event.preventDefault(); focusables[0].focus();}
      }
    });
    window.addEventListener("resize", () => {if (active) fit();});
  }
  function open(svg, opener) {
    if (active) close();
    setup();
    const box = svg.viewBox && svg.viewBox.baseVal;
    const rect = svg.getBoundingClientRect();
    const width = box && box.width > 0 ? box.width : rect.width;
    const height = box && box.height > 0 ? box.height : rect.height;
    if (!(width > 0 && height > 0)) return;
    const placeholder = document.createComment("original Mermaid position");
    svg.before(placeholder);
    active = {
      svg, placeholder, opener, width, height,
      style: svg.getAttribute("style"), overflow: document.body.style.overflow,
      zoom: 1, x: 0, y: 0, pointer: null
    };
    svg.style.width = width + "px";
    svg.style.height = height + "px";
    svg.style.maxWidth = "none";
    svg.style.maxHeight = "none";
    svg.style.display = "block";
    stage.append(svg);
    overlay.hidden = false;
    document.body.style.overflow = "hidden";
    fit();
    closeButton.focus({preventScroll: true});
  }
  function attach() {
    document.querySelectorAll(SELECTOR).forEach((svg) => {
      const source = svg.closest(".mermaid, .mermaid-container, .mermaid-diagram");
      if (!source || source.dataset.diagramViewerReady === "true") return;
      source.dataset.diagramViewerReady = "true";
      source.classList.add("diagram-viewer-source");
      const launch = button("⛶", "Open diagram (wheel zoom, drag to move)", () => {
        const current = source.querySelector("svg");
        if (current) open(current, launch);
      });
      launch.className = "diagram-viewer-open";
      source.append(launch);
    });
  }
  function init() {
    attach();
    let pending = false;
    new MutationObserver(() => {
      if (pending) return;
      pending = true;
      requestAnimationFrame(() => {pending = false; attach();});
    }).observe(document.body, {childList: true, subtree: true});
  }
  if (document.readyState === "loading") document.addEventListener("DOMContentLoaded", init, {once: true});
  else init();
})();
