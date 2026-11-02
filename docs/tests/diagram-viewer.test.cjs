'use strict';
const assert = require('node:assert/strict');
const {readFileSync} = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const {test} = require('node:test');

function makeViewer() {
  const elements = [];
  class Element {
    constructor(tag) {
      this.tag = tag;
      this.events = new Map();
      this.attrs = new Map();
      this.captured = new Set();
      this.dataset = {};
      this.style = {};
      this.children = [];
      const classes = new Set();
      this.classList = {
        add: (name) => classes.add(name),
        remove: (name) => classes.delete(name),
        contains: (name) => classes.has(name),
      };
      this.clientWidth = 400;
      this.clientHeight = 300;
      elements.push(this);
    }
    addEventListener(name, cb) {
      if (!this.events.has(name)) this.events.set(name, []);
      this.events.get(name).push(cb);
    }
    dispatch(name, details = {}) {
      let prevented = false;
      const event = {
        type: name, target: this, button: 0, pointerType: 'touch',
        preventDefault() { prevented = true; },
        ...details,
      };
      for (const cb of this.events.get(name) || []) cb(event);
      return prevented;
    }
    setAttribute(k, v) { this.attrs.set(k, v); }
    getAttribute(k) { return this.attrs.get(k) ?? null; }
    hasAttribute(k) { return this.attrs.has(k); }
    removeAttribute(k) { this.attrs.delete(k); }
    append(...nodes) { this.children.push(...nodes); }
    replaceChildren(...nodes) { this.children = [...nodes]; }
    before() {}
    replaceWith() {}
    focus() {}
    getBoundingClientRect() { return {left: 0, top: 0, width: 400, height: 300}; }
    closest(selector) { return this === svg && selector === '.mermaid-diagram' ? source : null; }
    querySelector(selector) { return this === source && selector.includes('svg') ? svg : null; }
    setPointerCapture(id) { this.captured.add(id); }
    hasPointerCapture(id) { return this.captured.has(id); }
    releasePointerCapture(id) { this.captured.delete(id); }
  }

  const source = new Element('source');
  const svg = new Element('svg');
  svg.viewBox = {baseVal: {width: 1000, height: 800}};
  const document = {
    readyState: 'complete',
    body: new Element('body'),
    createElement: (tag) => new Element(tag),
    createComment: () => new Element('comment'),
    querySelectorAll: () => [svg],
    addEventListener() {},
  };
  const script = readFileSync(path.join(__dirname, '../_static/diagram-viewer.js'), 'utf8');
  vm.runInNewContext(script, {
    document, window: {addEventListener() {}},
    MutationObserver: class { observe() {} },
    requestAnimationFrame: (cb) => cb(),
  });
  source.dispatch('click', {pointerType: 'mouse', target: source});
  const viewport = elements.find((element) => element.className === 'diagram-viewer-viewport');
  const stage = elements.find((element) => element.className === 'diagram-viewer-stage');
  assert.ok(viewport && stage, 'Viewer must open with a viewport and stage');
  const transform = () => {
    const match = stage.style.transform.match(/^translate\(([-\d.]+)px,([-\d.]+)px\) scale\(([-\d.]+)\)$/);
    assert.ok(match, stage.style.transform);
    return {x: Number(match[1]), y: Number(match[2]), zoom: Number(match[3])};
  };
  const pointer = (name, id, x, y, pointerType = 'touch') => viewport.dispatch(name, {
    pointerId: id, pointerType, clientX: x, clientY: y, target: viewport,
  });
  return {viewport, stage, transform, pointer};
}

function near(actual, expected) {
  assert.ok(Math.abs(actual - expected) < 1e-7, actual + ' != ' + expected);
}

test('pinch scales around finger midpoint; one-finger pan resumes after lift', () => {
  const {viewport, pointer, transform} = makeViewer();
  const initial = transform();
  pointer('pointerdown', 1, 100, 100);
  pointer('pointerdown', 2, 200, 100);
  pointer('pointermove', 2, 300, 100);
  const zoomed = transform();
  near(zoomed.zoom, initial.zoom * 2);
  near(zoomed.x, 200 - (150 - initial.x) * 2);
  near(zoomed.y, 100 - (100 - initial.y) * 2);
  pointer('pointerup', 2, 300, 100);
  pointer('pointermove', 1, 120, 130);
  const panned = transform();
  near(panned.zoom, zoomed.zoom);
  near(panned.x, zoomed.x + 20);
  near(panned.y, zoomed.y + 30);
  assert.ok(viewport.classList.contains('diagram-viewer-dragging'));
  pointer('pointercancel', 1, 120, 130);
  assert.ok(!viewport.classList.contains('diagram-viewer-dragging'));
  assert.equal(viewport.captured.size, 0);
});

test('two-finger movement translates without changing zoom', () => {
  const {pointer, transform} = makeViewer();
  const initial = transform();
  pointer('pointerdown', 1, 100, 100);
  pointer('pointerdown', 2, 200, 100);
  pointer('pointermove', 1, 120, 120);
  pointer('pointermove', 2, 220, 120);
  const moved = transform();
  near(moved.zoom, initial.zoom);
  near(moved.x, initial.x + 20);
  near(moved.y, initial.y + 20);
});

test('desktop wheel zoom remains centered under the cursor', () => {
  const {viewport, transform} = makeViewer();
  const initial = transform();
  const prevented = viewport.dispatch('wheel', {deltaY: -100, clientX: 120, clientY: 80});
  const next = transform();
  const ratio = Math.exp(0.2);
  assert.equal(prevented, true);
  near(next.zoom, initial.zoom * ratio);
  near(next.x, 120 - (120 - initial.x) * ratio);
  near(next.y, 80 - (80 - initial.y) * ratio);
});

test('ignore secondary mouse buttons and unrelated pointer moves', () => {
  const {viewport, pointer, transform} = makeViewer();
  const initial = transform();
  viewport.dispatch('pointerdown', {pointerId: 8, pointerType: 'mouse', button: 2, clientX: 50, clientY: 50});
  pointer('pointermove', 8, 110, 150, 'mouse');
  assert.deepEqual(transform(), initial);
  assert.equal(viewport.captured.size, 0);
});
