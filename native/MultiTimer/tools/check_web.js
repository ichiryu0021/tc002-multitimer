#!/usr/bin/env node
/* eslint-disable */
/*
 * Ulanzi TC002 MultiTimer v3 - 网页自检
 *
 * 1) 提取 web-src/index.html 的内联 <script>，new Function 编译；
 * 2) 用 DOM / fetch / localStorage 桩真实执行页面脚本；
 * 3) 覆盖 v3 schema、页面/直播槽位、模拟器帧模型、保存链路、v2 迁移/只读、
 *    多 WiFi、自动时钟地址、目标、时区、颜色、XSS、零外链、390px 静态约束。
 *
 * 运行：node native/MultiTimer/tools/check_web.js
 * 退出码：0 = 全绿；1 = 有断言失败；2 = 自检脚本异常。
 */
"use strict";

const fs = require("fs");
const path = require("path");

const ROOT = path.resolve(__dirname, "..");
const HTML_PATH = path.join(ROOT, "web-src", "index.html");
const html = fs.readFileSync(HTML_PATH, "utf8");

/* ========================================================================== */
/* 1. 静态检查：脚本提取 / 编译 / 零外链 / DOM 契约                            */
/* ========================================================================== */

const scriptMatch = /<script>([\s\S]*?)<\/script>/.exec(html);
if (!scriptMatch) {
  console.error("FAIL: 未找到内联 <script>");
  process.exit(1);
}
const SCRIPT = scriptMatch[1];
const PARAM_NAMES = ["document", "window", "fetch", "performance", "requestAnimationFrame", "console"];

let compiledOK = false;
let compileError = "";
try {
  new Function(...PARAM_NAMES, SCRIPT);
  compiledOK = true;
} catch (e) {
  compileError = e && e.message ? e.message : String(e);
}

const EXTERNAL_PATTERNS = [
  ["<script src>", /<script[^>]+src\s*=/i],
  ["<link>", /<link\b/i],
  ["@import", /@import\b/i],
  ["CSS url(http)", /url\(\s*["']?\s*(?:https?:)?\/\//i],
  ["媒体外链", /<(?:img|iframe|video|audio|source|embed|object)\b[^>]*(?:src|data)\s*=/i],
  ["ES import()", /\bimport\s*\(/i],
  ["ES import from", /\bimport\s+[^;]*from\b/i],
  ["new Worker", /\bnew\s+Worker\s*\(/i],
  ["fetch 字面外链", /\bfetch\s*\(\s*["']https?:/i],
  ["<a href=http>", /<a\b[^>]*href\s*=\s*["']https?:/i]
];
const externalRefs = [];
EXTERNAL_PATTERNS.forEach(function (entry) {
  if (entry[1].test(html) || entry[1].test(SCRIPT)) externalRefs.push(entry[0]);
});

const REQUIRED_IDS = [
  "v2Banner",
  "reloadBtn", "deviceInfo",
  "netSsid", "netIp",
  "wifiFallback", "wifiFallbackSsid", "wifiFallbackHint",
  "wifiEditor", "wifiList", "addWifiBtn", "wifiSaveBtn", "wifiHint", "timeSyncBtn",
  "ntpServerInput", "tzSelect", "tzMinutesInput", "tzLabel",
  "colorClock", "hexClock", "colorCountdown", "hexCountdown", "colorWarning", "hexWarning",
  "colorDim", "hexDim", "warnInput",
  "targetList", "addTargetBtn", "removeExpiredBtn",
  "pageCount", "pageList", "addPageBtn", "pageHint", "dirtyBadge",
  "pageEditor", "pageTitle", "pageTitleError", "pageShortTitle", "pageShortTitleError",
  "pageType", "pageTypeHint", "pageEnabled", "defaultPageChk", "setCurrentPageBtn",
  "liveEditor", "livePageMetric", "liveRows",
  "liveSource", "liveUrlRow", "liveUrl", "liveRefresh", "liveTimeout", "liveSourceHint",
  "led", "simMeta", "simPageSelect", "simLiveState", "followChk", "manualBox",
  "simDate", "simTime", "playBtn", "speedSel",
  "saveBtn", "saveError", "statusLog"
];
const missingIds = REQUIRED_IDS.filter(function (id) {
  return html.indexOf('id="' + id + '"') < 0;
});
const allHtmlIds = [];
{
  const re = /\bid="([^"]+)"/g;
  let match;
  while ((match = re.exec(html)) !== null) allHtmlIds.push(match[1]);
}
const duplicateHtmlIds = allHtmlIds.filter(function (id, index) {
  return allHtmlIds.indexOf(id) !== index;
});

const innerHTMLAssignments = [];
{
  const re = /\.innerHTML\s*=\s*([^;\n]+)/g;
  let match;
  while ((match = re.exec(SCRIPT)) !== null) innerHTMLAssignments.push(match[1].trim());
}
const unsafeHTMLAssignments = innerHTMLAssignments.filter(function (value) {
  return value !== '""' && value !== "''";
});

const inlineHandlers = /<\w+[^>]*\son[a-z]+\s*=/i.test(html);
const dangerousAPIs = /\b(?:document\.write|insertAdjacentHTML|outerHTML\s*=|eval\s*\()/.test(SCRIPT);

const cssText = /<style>([\s\S]*?)<\/style>/.exec(html)[1];
const cssFixedWidths = [];
{
  const re = /(?:^|[;{]\s*)width\s*:\s*([0-9]+(?:\.[0-9]+)?)px/g;
  let match;
  while ((match = re.exec(cssText)) !== null) cssFixedWidths.push(Number(match[1]));
}
const tooWideCss = cssFixedWidths.filter(function (width) { return width > 340; });
const cssChecks = {
  no100vw: cssText.indexOf("100vw") < 0,
  noFixedOver340: tooWideCss.length === 0,
  hasMinWidth0: /min-width\s*:\s*0\s*;/.test(cssText),
  hasGridMinmax: /grid-template-columns\s*:\s*minmax\(0,\s*1fr\)/.test(cssText),
  hasLedOverflowHidden: /\.led-wrap[\s\S]*?overflow\s*:\s*hidden/.test(cssText),
  hasBreakAll: /(?:word-break\s*:\s*break-all|overflow-wrap\s*:\s*anywhere)/.test(cssText),
  hasMediaQuery: /@media\s*\(max-width/.test(cssText)
};

/* ========================================================================== */
/* 2. DOM 桩                                                                  */
/* ========================================================================== */

function makeCtx(recorder) {
  return {
    font: "",
    fillStyle: "",
    shadowColor: "",
    shadowBlur: 0,
    textBaseline: "",
    textAlign: "",
    clearRect: function () {},
    fillRect: function () {},
    setTransform: function () {},
    beginPath: function () {},
    arc: function () {},
    fill: function () {},
    fillText: function (text) { recorder.push(String(text)); },
    measureText: function (text) { return { width: String(text).length * 9 }; },
    getImageData: function (x, y, w, h) { return { data: new Uint8ClampedArray(w * h * 4) }; }
  };
}

function makeEl(tag, doc) {
  const el = {
    tag: tag,
    tagName: String(tag).toUpperCase(),
    id: "",
    children: [],
    childNodes: [],
    handlers: {},
    style: {},
    dataset: {},
    value: "",
    checked: false,
    disabled: false,
    readOnly: false,
    hidden: false,
    type: "",
    placeholder: "",
    maxLength: 0,
    min: "",
    max: "",
    step: "",
    title: "",
    className: "",
    parentElement: null,
    _text: "",
    _html: "",
    _attrs: {},
    classList: {
      add: function () {},
      remove: function () {},
      toggle: function () {}
    },
    addEventListener: function (type, fn) {
      (this.handlers[type] = this.handlers[type] || []).push(fn);
    },
    removeEventListener: function () {},
    appendChild: function (child) {
      child.parentElement = this;
      this.children.push(child);
      this.childNodes.push(child);
      return child;
    },
    removeChild: function (child) {
      const index = this.children.indexOf(child);
      if (index >= 0) this.children.splice(index, 1);
      child.parentElement = null;
      return child;
    },
    scrollIntoView: function () {
      if (doc && doc._scrollCalls) doc._scrollCalls.push(this.id || this.tag);
    },
    focus: function () { if (doc) doc.activeElement = this; },
    blur: function () { if (doc && doc.activeElement === this) doc.activeElement = null; },
    click: function () { dispatch(this, "click"); },
    setAttribute: function (name, value) { this._attrs[name] = String(value); },
    getAttribute: function (name) {
      return Object.prototype.hasOwnProperty.call(this._attrs, name) ? this._attrs[name] : null;
    },
    removeAttribute: function (name) { delete this._attrs[name]; },
    getContext: function () {
      if (!this._ctx) this._ctx = makeCtx(doc._fillTexts);
      return this._ctx;
    }
  };

  Object.defineProperty(el, "textContent", {
    get: function () { return this._text; },
    set: function (value) { this._text = String(value); },
    configurable: true
  });
  Object.defineProperty(el, "innerHTML", {
    get: function () { return this._html; },
    set: function (value) {
      this._html = String(value);
      this.children = [];
      this.childNodes = [];
    },
    configurable: true
  });
  Object.defineProperty(el, "firstChild", {
    get: function () { return this.children.length ? this.children[0] : null; },
    configurable: true
  });
  Object.defineProperty(el, "lastElementChild", {
    get: function () { return this.children.length ? this.children[this.children.length - 1] : null; },
    configurable: true
  });
  return el;
}

function dispatch(el, type, event) {
  const handlers = (el.handlers[type] || []).slice();
  for (let i = 0; i < handlers.length; i++) handlers[i].call(el, event || {});
}

function descendants(el, result) {
  result = result || [];
  (el.children || []).forEach(function (child) {
    result.push(child);
    descendants(child, result);
  });
  return result;
}

function textOf(el) {
  return [el._text || ""]
    .concat(descendants(el).map(function (child) { return child._text || ""; }))
    .join(" ");
}

function inputsOf(el) {
  return descendants(el).filter(function (child) { return child.tag === "input"; });
}

function buttonsOf(el) {
  return descendants(el).filter(function (child) { return child.tag === "button"; });
}

function inputOfType(row, type) {
  return inputsOf(row).filter(function (input) { return input.type === type; })[0] || null;
}

function buttonOfText(row, text) {
  return buttonsOf(row).filter(function (button) { return button.textContent === text; })[0] || null;
}

function hasClass(el, name) {
  return new RegExp("(^|\\s)" + name + "(\\s|$)").test(el.className || "");
}

function dispatchInput(el, type) {
  dispatch(el, type || "input");
}

/* ========================================================================== */
/* 3. 沙箱                                                                   */
/* ========================================================================== */

function localParts(epoch, tzMinutes) {
  const d = new Date((Math.floor(epoch) + tzMinutes * 60) * 1000);
  const pad = function (n) { return (n < 10 ? "0" : "") + n; };
  return {
    date: d.getUTCFullYear() + "-" + pad(d.getUTCMonth() + 1) + "-" + pad(d.getUTCDate()),
    time: pad(d.getUTCHours()) + ":" + pad(d.getUTCMinutes()) + ":" + pad(d.getUTCSeconds())
  };
}

function fixturePages() {
  return [
    { id: "p0", type: "clock", title: "时钟", shortTitle: "CLOCK", enabled: true },
    { id: "p1", type: "clock_countdown", title: "时钟+倒计时", shortTitle: "CLOCK+CD", enabled: true },
    { id: "p2", type: "countdown", title: "倒计时", shortTitle: "COUNTDOWN", enabled: true },
    {
      id: "p3", type: "live", title: "直播", shortTitle: "LIVE", enabled: true, metric: "online",
      rooms: [
        { platform: "bilibili", room: "", label: "", enabled: false, metric: "" },
        { platform: "douyin", room: "", label: "", enabled: false, metric: "" }
      ]
    }
  ];
}

function mergeFixtureConfig(options) {
  const source = options || {};
  const config = Object.assign({
    version: 3,
    tzOffsetMinutes: source.tz === undefined ? 480 : source.tz,
    warnSeconds: 30,
    ntpServer: "",
    colors: { clock: "#ABCDEF", countdown: "#112233", warning: "#FF2020", dim: "#808080" },
    display: { splitBottomWhenIdle: "date" },
    targets: [],
    live: { source: "off", url: "", refreshSec: 30, timeoutMs: 3000 },
    defaultPageId: "p0",
    pages: fixturePages()
  }, source.config || {});
  config.colors = Object.assign({ clock: "#ABCDEF", countdown: "#112233", warning: "#FF2020", dim: "#808080" },
    (source.config && source.config.colors) || {});
  config.display = Object.assign({ splitBottomWhenIdle: "date" },
    (source.config && source.config.display) || {});
  config.live = Object.assign({ source: "off", url: "", refreshSec: 30, timeoutMs: 3000 },
    (source.config && source.config.live) || {});
  config.targets = (source.config && source.config.targets) || [];
  config.pages = (source.config && source.config.pages) || fixturePages();
  if (!config.defaultPageId || !config.pages.some(function (p) { return p.id === config.defaultPageId; })) {
    config.defaultPageId = config.pages[0] ? config.pages[0].id : "p0";
  }
  return config;
}

function makeFixture(options) {
  options = options || {};
  const now = Math.floor(Date.now() / 1000);
  const tz = options.tz === undefined ? 480 : options.tz;
  const local = localParts(now, tz);
  const config = mergeFixtureConfig(options);
  const defaultPage = config.pages.filter(function (p) { return p.id === config.defaultPageId; })[0] ||
    config.pages[0] || { id: "p0", type: "clock", title: "时钟" };
  const pageIndex = Math.max(1, config.pages.indexOf(defaultPage) + 1);
  const generatedPage = {
    id: defaultPage.id,
    index: pageIndex,
    count: config.pages.length,
    type: defaultPage.type,
    title: defaultPage.title,
    defaultId: config.defaultPageId
  };
  const device = Object.assign({
    ip: "192.168.8.219",
    ssid: "Ulanzi_TC002",
    wifi: true,
    uptimeSec: 90061,
    timeUtc: now,
    timeValid: true,
    tzOffsetMinutes: tz,
    localTime: local.date + " " + local.time,
    lastSyncUtc: now,
    page: generatedPage,
    capabilities: {
      configVersion: 3,
      pages: true,
      liveStatus: true,
      liveSource: true,
      pageSwitch: true
    }
  }, options.device || {});
  if (options.device && options.device.page) {
    device.page = Object.assign({}, generatedPage, options.device.page);
  }
  return { ok: true, config: config, device: device };
}

function defaultFixture() {
  const now = Math.floor(Date.now() / 1000);
  const tz = 480;
  const first = localParts(now + 3600, tz);
  const second = localParts(now + 7200, tz);
  return makeFixture({
    config: {
      targets: [
        { date: first.date, time: first.time, enabled: true },
        { date: second.date, time: second.time, enabled: true }
      ]
    }
  });
}

function defaultWifiFixture() {
  return {
    ok: true,
    current: { ssid: "Ulanzi_TC002", ip: "192.168.8.219", wifi: true },
    max: 8,
    networks: [
      { ssid: "Ulanzi_TC002", enabled: true, hasPassword: true },
      { ssid: "Office", enabled: true, hasPassword: true }
    ]
  };
}

function makeResponse(result) {
  const status = result && result.status !== undefined ? result.status : 200;
  const data = result && result.data !== undefined ? result.data : { ok: true };
  const badJson = !!(result && result.badJson);
  return {
    status: status,
    json: function () {
      if (badJson) return Promise.reject(new Error("invalid json"));
      return Promise.resolve(data);
    }
  };
}

function okResponse(data) {
  return {
    status: 200,
    json: function () { return Promise.resolve(data === undefined ? { ok: true } : data); }
  };
}

function pageInfoFromBody(body) {
  const pages = body && Array.isArray(body.pages) ? body.pages : [];
  const page = pages.filter(function (p) { return p.id === (body && body.defaultPageId); })[0] || pages[0];
  if (!page) return { id: "p0", index: 1, count: 1, type: "clock", title: "时钟", defaultId: "p0" };
  return {
    id: page.id,
    index: pages.indexOf(page) + 1,
    count: pages.length,
    type: page.type,
    title: page.title,
    defaultId: body.defaultPageId || page.id
  };
}

function defaultResponse(req, ctx, options) {
  if (req.method === "GET" && /\/api\/config$/.test(req.url)) {
    const fixture = options.fixture;
    const data = typeof fixture === "function"
      ? fixture(req, ctx)
      : (fixture || defaultFixture());
    return { status: 200, data: data };
  }
  if (req.method === "POST" && /\/api\/config$/.test(req.url)) {
    let body = null;
    try { body = JSON.parse(req.body); } catch (e) { body = null; }
    ctx.savedBodies.push(body);
    return { status: 200, data: { ok: true, page: pageInfoFromBody(body) } };
  }
  if (req.method === "POST" && /\/api\/page$/.test(req.url)) {
    let body = null;
    try { body = JSON.parse(req.body); } catch (e) { body = null; }
    ctx.pageBodies.push(body);
    const id = body && typeof body.id === "string" ? body.id : "";
    return {
      status: 200,
      data: { ok: true, page: { id: id, index: 1, count: 4, type: "clock", title: "页面", defaultId: id } }
    };
  }
  if (req.method === "POST" && /\/api\/time$/.test(req.url)) {
    let body = null;
    try { body = JSON.parse(req.body); } catch (e) { body = null; }
    ctx.timeBodies.push(body);
    return { status: 200, data: { ok: true } };
  }
  if (req.method === "GET" && /\/api\/wifi$/.test(req.url)) {
    const wifiFixture = options.wifiFixture;
    const data = typeof wifiFixture === "function"
      ? wifiFixture(req, ctx)
      : (wifiFixture || defaultWifiFixture());
    return { status: 200, data: data };
  }
  if (req.method === "POST" && /\/api\/wifi\/list$/.test(req.url)) {
    let body = null;
    try { body = JSON.parse(req.body); } catch (e) { body = null; }
    ctx.wifiListBodies.push(body);
    const count = body && Array.isArray(body.networks) ? body.networks.length : 0;
    return { status: 200, data: { ok: true, count: count, message: "saved, reconnecting" } };
  }
  if (req.method === "POST" && /\/api\/wifi$/.test(req.url)) {
    let body = null;
    try { body = JSON.parse(req.body); } catch (e) { body = null; }
    ctx.wifiBodies.push(body);
    return { status: 200, data: { ok: true, message: "saved, reconnecting" } };
  }
  return { status: 404, data: { ok: false, error: "unexpected url " + req.url } };
}

function wifiResponse(data) {
  return { status: 200, data: data || defaultWifiFixture() };
}

function makeSandbox(options) {
  options = options || {};

  const calls = [];
  const confirmCalls = [];
  const scrollCalls = [];
  const fillTexts = [];
  const createdTags = [];
  const windowHandlers = {};
  const ctx = {
    calls: calls,
    savedBodies: [],
    timeBodies: [],
    wifiBodies: [],
    wifiListBodies: [],
    pageBodies: [],
    confirmCalls: confirmCalls,
    windowHandlers: windowHandlers
  };

  const storage = {};
  Object.keys(options.storage || {}).forEach(function (key) {
    storage[key] = String(options.storage[key]);
  });

  const registry = {};
  const doc = {
    _fillTexts: fillTexts,
    _scrollCalls: scrollCalls,
    activeElement: null,
    visibilityState: "visible",
    hidden: false,
    body: null,
    getElementById: function (id) {
      if (!registry[id]) {
        const el = makeEl("div", this);
        el.id = id;
        if (id === "led") {
          el.tag = "canvas";
          el.tagName = "CANVAS";
          el.parentElement = { clientWidth: 400, clientHeight: 120 };
        }
        registry[id] = el;
      }
      return registry[id];
    },
    createElement: function (tag) {
      createdTags.push(String(tag));
      return makeEl(tag, this);
    },
    querySelectorAll: function (selector) {
      if (selector === "[data-busy]") {
        return Object.keys(registry)
          .map(function (key) { return registry[key]; })
          .filter(function (el) { return el.dataset.busy === true; });
      }
      return [];
    }
  };
  doc.body = makeEl("body", doc);

  const win = {
    location: {
      protocol: options.protocol || "http:",
      origin: "http://192.168.8.219:8080"
    },
    devicePixelRatio: 1,
    addEventListener: function (type, fn) {
      (windowHandlers[type] = windowHandlers[type] || []).push(fn);
    },
    removeEventListener: function () {},
    confirm: function (message) {
      confirmCalls.push(String(message));
      return options.confirmResult !== false;
    },
    localStorage: {
      getItem: function (key) {
        return Object.prototype.hasOwnProperty.call(storage, key) ? storage[key] : null;
      },
      setItem: function (key, value) { storage[key] = String(value); },
      removeItem: function (key) { delete storage[key]; }
    }
  };

  let requestFrameCallback = null;
  let timestamp = 1000;

  function requestAnimationFrameStub(callback) {
    requestFrameCallback = callback;
    return 1;
  }

  function fetchStub(url, init) {
    const record = {
      url: String(url),
      method: (init && init.method) || "GET",
      body: init && init.body,
      index: calls.length
    };
    calls.push(record);

    let result;
    try {
      result = options.handler
        ? options.handler(record, ctx)
        : defaultResponse(record, ctx, options);
    } catch (e) {
      return Promise.reject(e);
    }
    if (result && typeof result.then === "function") return result;
    if (result && result.reject) return Promise.reject(new Error(result.reject));
    return Promise.resolve(makeResponse(result));
  }

  const performanceStub = { now: function () { return Date.now(); } };

  let runtimeError = null;
  try {
    const fn = new Function(...PARAM_NAMES, SCRIPT);
    fn(doc, win, fetchStub, performanceStub, requestAnimationFrameStub, console);
  } catch (e) {
    runtimeError = e;
  }

  function pump(turns) {
    let promise = Promise.resolve();
    for (let i = 0; i < (turns || 60); i++) promise = promise.then(function () {});
    return promise;
  }

  function frame() {
    timestamp += 1000;
    if (typeof requestFrameCallback === "function") {
      const callback = requestFrameCallback;
      requestFrameCallback = null;
      callback(timestamp);
    }
  }

  function fireWindowEvent(type, event) {
    const handlers = (windowHandlers[type] || []).slice();
    handlers.forEach(function (fn) { fn.call(win, event || {}); });
  }

  return {
    doc: doc,
    win: win,
    registry: registry,
    storage: storage,
    calls: calls,
    ctx: ctx,
    confirmCalls: confirmCalls,
    scrollCalls: scrollCalls,
    fillTexts: fillTexts,
    createdTags: createdTags,
    runtimeError: runtimeError,
    api: win.__multitimer,
    pump: pump,
    frame: frame,
    fireWindowEvent: fireWindowEvent,
    rows: function () { return targetRowsOf(this); },
    rowInputs: function (row) { return inputsOf(row); },
    rowButtons: function (row) { return buttonsOf(row); }
  };
}

async function settle(sb, turns) {
  await sb.pump(turns || 60);
}

function sleep(ms) {
  return new Promise(function (resolve) { setTimeout(resolve, ms); });
}

const WIFI_REFRESH_WAIT_MS = 2900;

function sandboxError(sb) {
  if (!sb.runtimeError) return null;
  return "页面脚本执行异常: " + (sb.runtimeError.stack || sb.runtimeError.message || sb.runtimeError);
}

/* ========================================================================== */
/* 4. 断言框架                                                               */
/* ========================================================================== */

const results = [];
const sandboxes = [];

function assert(condition, message) {
  if (!condition) throw new Error(message || "断言失败");
}

function assertEqual(actual, expected, message) {
  if (actual !== expected) {
    throw new Error((message || "值不相等") + "：期望 " + JSON.stringify(expected) + "，实际 " + JSON.stringify(actual));
  }
}

function assertMatch(value, regexp, message) {
  if (!regexp.test(String(value))) {
    throw new Error((message || "格式不匹配") + "：正则 " + regexp + "，实际 " + JSON.stringify(value));
  }
}

function assertIncludes(haystack, needle, message) {
  if (String(haystack).indexOf(needle) < 0) {
    throw new Error((message || "缺少子串") + "：期望包含 " + JSON.stringify(needle) + "，实际 " + JSON.stringify(haystack));
  }
}

function check(name, fn) {
  try {
    fn();
    results.push({ name: name, ok: true });
  } catch (e) {
    results.push({ name: name, ok: false, err: e && e.message ? e.message : String(e) });
  }
}

async function checkAsync(name, fn) {
  try {
    await fn();
    results.push({ name: name, ok: true });
  } catch (e) {
    results.push({ name: name, ok: false, err: e && e.message ? e.message : String(e) });
  }
}

function newSandbox(options) {
  const sb = makeSandbox(options);
  sandboxes.push(sb);
  return sb;
}

function requireApi(sb) {
  const err = sandboxError(sb);
  assert(!err, err);
  assert(sb.api, "页面未暴露 window.__multitimer 自检出口");
  return sb.api;
}

function targetRowsOf(sb) {
  return sb.registry.targetList.children.slice();
}

function pageRows(sb) {
  return sb.registry.pageList.children.slice();
}

function pageRowTitle(row) {
  return descendants(row).filter(function (el) { return hasClass(el, "page-title"); })[0] || null;
}

function pageRowOfTitle(sb, title) {
  return pageRows(sb).filter(function (row) {
    const el = pageRowTitle(row);
    return el && el.textContent === title;
  })[0] || null;
}

function pageBtn(row, text) {
  return buttonsOf(row).filter(function (button) { return button.textContent === text; })[0] || null;
}

function liveSlots(sb) {
  return sb.registry.liveRows.children.slice();
}

function slotInputs(slot) {
  return inputsOf(slot);
}

function slotRoomInput(slot) {
  return slotInputs(slot).filter(function (input) {
    return hasClass(input, "live-room-input");
  })[0] || null;
}

function slotLabelInput(slot) {
  return slotInputs(slot).filter(function (input) {
    return hasClass(input, "live-label-input");
  })[0] || null;
}

function slotSelects(slot) {
  return descendants(slot).filter(function (el) { return el.tag === "select"; });
}

function slotPlatform(slot) {
  return slotSelects(slot)[0] || null;
}

function slotMetric(slot) {
  return slotSelects(slot)[1] || null;
}

function slotBadge(slot) {
  return descendants(slot).filter(function (el) { return hasClass(el, "badge"); })[0] || null;
}

function formOptionValues(select) {
  return select.children.map(function (option) { return option.value; });
}

function setManualTime(sb, date, time) {
  const registry = sb.registry;
  registry.followChk.checked = false;
  dispatch(registry.followChk, "change");
  registry.simDate.value = date;
  registry.simTime.value = time;
  dispatch(registry.simDate, "change");
  dispatch(registry.simTime, "change");
}

function selectPage(sb, id) {
  const ok = requireApi(sb).selectPage(id);
  assert(ok, "selectPage 失败：" + id);
  sb.frame();
  return ok;
}

function setTarget(sb, rowIndex, date, time) {
  const row = sb.rows()[rowIndex];
  const dateInput = inputOfType(row, "date");
  const timeInput = inputOfType(row, "time");
  assert(dateInput && timeInput, "目标行缺少 date/time 输入");
  dateInput.value = date;
  dispatch(dateInput, "input");
  dispatch(dateInput, "change");
  timeInput.value = time;
  dispatch(timeInput, "input");
  dispatch(timeInput, "change");
}

function badges(sb) {
  return sb.rows().map(function (row) {
    const badge = descendants(row).filter(function (el) { return hasClass(el, "badge"); })[0];
    return badge ? badge.textContent : "";
  });
}

function wifiRows(sb) {
  return sb.registry.wifiList.children.filter(function (row) {
    return /(^|\s)wifi-row(\s|$)/.test(row.className || "");
  });
}

function wifiSsidInput(row) {
  return inputsOf(row).filter(function (input) {
    return /(^|\s)wifi-ssid-input(\s|$)/.test(input.className || "");
  })[0] || null;
}

function wifiPassInput(row) {
  return inputsOf(row).filter(function (input) {
    return /(^|\s)wifi-pass-input(\s|$)/.test(input.className || "");
  })[0] || null;
}

function wifiCurrentBadge(row) {
  return descendants(row).filter(function (el) {
    return /(^|\s)badge-current(\s|$)/.test(el.className || "") && /\bon\b/.test(el.className || "");
  })[0] || null;
}

function wifiRowBySsid(sb, ssid) {
  return wifiRows(sb).filter(function (row) {
    const input = wifiSsidInput(row);
    return input && input.value === ssid;
  })[0] || null;
}

function statusText(sb) {
  return textOf(sb.registry.statusLog);
}

/* ========================================================================== */
/* 5. 主流程                                                                 */
/* ========================================================================== */

async function main() {
  /* ------------------------------ 静态检查 ------------------------------ */
  check("1. 内联 <script> 通过 new Function 编译", function () {
    assert(compiledOK, "编译失败：" + compileError);
  });

  check("2. 零外链 / 零 CDN / 无 ES module / 无 Worker", function () {
    assertEqual(externalRefs.length, 0, "发现外链：" + externalRefs.join("、"));
  });

  check("3. 页面包含全部必需 DOM id（≥60）且无重复 id", function () {
    assert(REQUIRED_IDS.length >= 60, "必需 id 数量断言已过期：" + REQUIRED_IDS.length);
    assertEqual(missingIds.length, 0, "缺少 id：" + missingIds.join("、"));
    assertEqual(duplicateHtmlIds.length, 0, "重复 id：" + duplicateHtmlIds.join("、"));
  });

  check("4. 不用 innerHTML 写用户数据；无内联事件 / document.write / eval", function () {
    assertEqual(unsafeHTMLAssignments.length, 0,
      "innerHTML 只能用于清空，发现：" + JSON.stringify(unsafeHTMLAssignments));
    assert(!inlineHandlers, "发现内联 on* 事件属性");
    assert(!dangerousAPIs, "发现 document.write / insertAdjacentHTML / outerHTML / eval");
  });

  check("5. 390px 静态约束：无 100vw、无固定宽 >340、min-width:0、LED 不溢出", function () {
    assert(cssChecks.no100vw, "CSS 不得使用 100vw");
    assert(cssChecks.noFixedOver340, "CSS 固定宽超过 340px：" + tooWideCss.join(","));
    assert(cssChecks.hasMinWidth0, "缺少 min-width:0");
    assert(cssChecks.hasGridMinmax, "直播/网格布局缺少 minmax(0,1fr)");
    assert(cssChecks.hasLedOverflowHidden, "LED 容器缺少 overflow:hidden");
    assert(cssChecks.hasBreakAll, "缺少长文本换行规则");
    assert(cssChecks.hasMediaQuery, "缺少窄屏媒体查询");
  });

  check("6. 内嵌 TomThumb 字形与固件度量一致（16:38:29 scale2 收字距后 47px）", function () {
    const helper = newSandbox({ protocol: "file:" });
    const api = requireApi(helper);
    assertEqual(api.measurePixelText("16:38:29", 2, 1), 47, "时钟字宽应与固件一致");
    assertEqual(api.measurePixelText("NO LIVE", 1, 0), 19, "NO LIVE 字宽不符");
    assert(api.measurePixelText("B25", 1, 0) + 1 + api.measurePixelText("1.2w", 1, 0) <= 52,
      "直播行 label+value 应能放进 52px");
    assert(api.TOM_THUMB.w && api.TOM_THUMB.y && api.TOM_THUMB.d, "缺少 w/y/d 字形");
  });

  check("7. 平台域名只用于粘贴解析，不进入 fetch/链接", function () {
    assert(!/fetch\s*\([^)]*(?:bilibili|douyin|kuaishou)/i.test(SCRIPT),
      "不得 fetch 平台域名");
    assert(!/<a\b[^>]*href\s*=\s*["'][^"']*(?:bilibili|douyin|kuaishou)/i.test(html),
      "不得生成平台外链");
    assert(SCRIPT.indexOf("live.bilibili.com") >= 0, "缺少 B 站链接解析字符串");
  });

  /* ------------------------------ 基础沙箱 A ------------------------------ */
  const sbA = newSandbox();
  await settle(sbA, 80);

  check("8. 页面脚本在 DOM/fetch 桩中完整执行，无异常", function () {
    const err = sandboxError(sbA);
    assert(!err, err || "");
  });

  check("9. 首次加载 GET {同源}/api/config 与 /api/wifi，地址默认同源", function () {
    const gets = sbA.calls.filter(function (call) { return call.method === "GET"; });
    assert(gets.length >= 2, "应发起配置与 WiFi 两个 GET");
    assertEqual(gets[0].url, "http://192.168.8.219:8080/api/config", "配置 GET 地址不符");
    const wifiGets = gets.filter(function (call) { return /\/api\/wifi$/.test(call.url); });
    assertEqual(wifiGets.length, 1, "初始应恰好 GET 一次 /api/wifi");
  });

  check("10. v3 配置回填：version 3 / 4 页 / dim / display / live / 默认页", function () {
    const api = requireApi(sbA);
    const draft = api.getDraft();
    assertEqual(draft.version, 3, "version 必须为 3");
    assertEqual(draft.pages.length, 4, "默认应 4 页");
    assertEqual(draft.defaultPageId, "p0", "默认页应为 p0");
    assertEqual(draft.colors.dim, "#808080", "colors.dim 未回填");
    assertEqual(draft.display.splitBottomWhenIdle, "date", "display.splitBottomWhenIdle 未回填");
    assertEqual(draft.live.source, "off", "默认直播源应为 off");
    assertEqual(draft.live.refreshSec, 30, "refreshSec 默认 30");
    assertEqual(draft.live.timeoutMs, 3000, "timeoutMs 默认 3000");
    assertEqual(sbA.registry.tzMinutesInput.value, "480", "tz 未回填");
    assertEqual(sbA.registry.tzLabel.textContent, "UTC+08:00", "UTC 标签不符");
    assertEqual(sbA.registry.warnInput.value, "30", "warnSeconds 未回填");
    assertEqual(sbA.registry.colorDim.value.toLowerCase(), "#808080", "dim 颜色未回填");
    assertEqual(sbA.registry.colorCountup.value.toLowerCase(), "#00bfff", "正计时颜色未回填");
    assertEqual(sbA.registry.liveSource.value, "off", "直播源未回填");
    assertEqual(sbA.registry.pageCount.textContent, "4/12", "页面计数不符");
  });

  check("11. 页面列表：4 行/1-based 序号/类型徽标/默认/当前/排序边界", function () {
    const rows = pageRows(sbA);
    assertEqual(rows.length, 4, "页面行数不符");
    assertIncludes(textOf(rows[0]), "第 1 页", "序号应为 1-based");
    assertIncludes(textOf(rows[1]), "第 2 页", "序号应为 1-based");
    assertIncludes(textOf(rows[0]), "时钟", "标题不符");
    assertIncludes(textOf(rows[0]), "默认", "默认徽标缺失");
    assertIncludes(textOf(rows[0]), "当前", "当前徽标缺失");
    assert(pageBtn(rows[0], "↑").disabled, "首行上移应禁用");
    assert(!pageBtn(rows[0], "↓").disabled, "首行下移应可用");
    assert(pageBtn(rows[3], "↓").disabled, "末行下移应禁用");
    assert(!pageBtn(rows[0], "✕").disabled, "多页时删除应可用");
    assertEqual(pageBtn(rows[0], "编辑").textContent, "编辑", "缺少编辑按钮");
  });

  check("12. 页面编辑表单回填：标题/短名/类型/启用/默认；非 live 隐藏直播编辑", function () {
    assertEqual(sbA.registry.pageTitle.value, "时钟", "标题未回填");
    assertEqual(sbA.registry.pageShortTitle.value, "CLOCK", "短名未回填");
    assertEqual(sbA.registry.pageType.value, "clock", "类型未回填");
    assertEqual(sbA.registry.pageEnabled.checked, true, "启用状态未回填");
    assertEqual(sbA.registry.defaultPageChk.checked, true, "默认页勾选未回填");
    assertEqual(sbA.registry.liveEditor.style.display, "none", "非 live 页应隐藏直播编辑");
    assertEqual(sbA.registry.setCurrentPageBtn.disabled, false, "已保存页应可设当前页");
  });

  check("13. 选中倒计时页：编辑区/模拟器下拉/当前页预览同步", function () {
    selectPage(sbA, "p2");
    assertEqual(sbA.registry.pageType.value, "countdown", "编辑区类型未同步");
    assertEqual(sbA.registry.simPageSelect.value, "p2", "模拟器下拉未同步");
    assertEqual(requireApi(sbA).getFrame().pageType, "countdown", "预览页类型不符");
    assertEqual(sbA.registry.pageCount.textContent, "4/12", "页面计数不应变化");
  });

  check("14. 时钟页帧：HH:MM:SS、scale2、baseline13、colors.clock", function () {
    selectPage(sbA, "p0");
    setManualTime(sbA, "2026-10-23", "11:00:00");
    const frame = requireApi(sbA).getFrame();
    assertEqual(frame.pageType, "clock", "应为时钟页");
    assertEqual(frame.text, "11:00:00", "时钟文本不符");
    assertEqual(frame.runs.length, 1, "时钟页应只有一个 run");
    assertEqual(frame.runs[0].scale, 2, "时钟 scale 应为 2");
    assertEqual(frame.runs[0].baseline, 13, "时钟 baseline 应为 13");
    assertEqual(frame.runs[0].color, "#ABCDEF", "时钟颜色不符");
    assertIncludes(frame.label, "第 1/4 页", "meta 页码不符");
  });

  check("15. 时间无效帧：--:--:-- / dim 色 / invalid（直播页不受影响）", function () {
    const api = requireApi(sbA);
    const page = api.getDraft().pages[0];
    const frame = api.buildPageFrame(1792658309, page, api.getDraft(), { timeValid: false });
    assertEqual(frame.text, "--:--:--", "无效时间占位不符");
    assertEqual(frame.color, "#808080", "无效时间应使用 colors.dim");
    assertEqual(frame.mode, "invalid", "模式应为 invalid");
  });

  /* ------------------------------ 帧模型：分屏 / 倒计时 / 直播 ------------------------------ */
  const fixedTargets = [{ date: "2026-10-23", time: "12:00:00", enabled: true }];
  const sbFrames = newSandbox({
    fixture: makeFixture({ config: { targets: fixedTargets, warnSeconds: 30 } })
  });
  await settle(sbFrames, 60);
  setManualTime(sbFrames, "2026-10-23", "11:00:00");

  check("16. 分屏帧：上下两条 run、基线 7/14、scale1、共用同一 now", function () {
    selectPage(sbFrames, "p1");
    const frame = requireApi(sbFrames).getFrame();
    assertEqual(frame.pageType, "clock_countdown", "应为分屏页");
    assertEqual(frame.runs.length, 2, "分屏应有 2 个 run");
    assertEqual(frame.runs[0].text, "11:00:00", "上半时钟不符");
    assertEqual(frame.runs[0].scale, 1, "上半 scale 应为 1");
    assertEqual(frame.runs[0].baseline, 7, "上半 baseline 应为 7");
    assertEqual(frame.runs[0].color, "#ABCDEF", "上半应使用时钟色");
    assertEqual(frame.runs[1].text, "01:00:00", "下半倒计时不符");
    assertEqual(frame.runs[1].scale, 1, "下半 scale 应为 1");
    assertEqual(frame.runs[1].baseline, 14, "下半 baseline 应为 14");
    assertEqual(frame.runs[1].color, "#112233", "下半应使用倒计时色");
    assertEqual(frame.runs[0].boxWidth, 52, "分屏宽度应为 52");
    assertEqual(frame.runs[1].boxWidth, 52, "分屏宽度应为 52");
    assertIncludes(frame.label, "11:00:00 / 01:00:00", "分屏 meta 应同时包含上下时间");
  });

  check("17. 分屏无目标：始终显示 NO COUNTDOWN（dim），旧 blank 设置被忽略", function () {
    const api = requireApi(sbFrames);
    const config = api.getDraft();
    const noTargetConfig = api.normalizeConfig(Object.assign({}, config, { targets: [] }));
    const dateFrame = api.buildPageFrame(api.dateTimeToEpoch("2026-10-23", "11:00:00", 480),
      noTargetConfig.pages[1], noTargetConfig, { timeValid: true });
    assertEqual(dateFrame.runs.length, 2, "无目标应有下半 run");
    assertEqual(dateFrame.runs[1].text, "NO COUNTDOWN", "无目标应显示 NO COUNTDOWN");
    assertEqual(dateFrame.runs[1].color, "#808080", "NO COUNTDOWN 应使用 dim");
    const blankConfig = api.normalizeConfig(Object.assign({}, config, {
      display: { splitBottomWhenIdle: "blank" },
      targets: []
    }));
    const blankFrame = api.buildPageFrame(api.dateTimeToEpoch("2026-10-23", "11:00:00", 480),
      blankConfig.pages[1], blankConfig, { timeValid: true });
    assertEqual(blankFrame.runs.length, 2, "旧 blank 设置不得再清空下半");
    assertEqual(blankFrame.runs[1].text, "NO COUNTDOWN", "旧 blank 设置也应显示 NO COUNTDOWN");
  });

  check("18. 倒计时页：普通/预警/远目标/无目标显示 NO COUNTDOWN", function () {
    const api = requireApi(sbFrames);
    selectPage(sbFrames, "p2");
    let frame = api.getFrame();
    assertEqual(frame.pageType, "countdown", "应为倒计时页");
    assertEqual(frame.text, "01:00:00", "11:00 距 12:00 应为 01:00:00");
    assertEqual(frame.mode, "countdown", "应为倒计时模式");
    assertEqual(frame.runs[0].scale, 2, "普通倒计时 scale 应为 2");
    assertEqual(frame.runs[0].baseline, 13, "普通倒计时 baseline 应为 13");

    setManualTime(sbFrames, "2026-10-23", "11:59:50");
    frame = api.getFrame();
    assertEqual(frame.text, "00:00:10", "剩余 10 秒文本不符");
    assertEqual(frame.warning, true, "剩余<=warnSeconds 应预警");
    assertEqual(frame.color, "#FF2020", "预警色不符");

    setManualTime(sbFrames, "2026-10-23", "11:00:00");
    const farConfig = api.normalizeConfig({
      version: 3, tzOffsetMinutes: 480, warnSeconds: 30,
      colors: { clock: "#ABCDEF", countdown: "#112233", warning: "#FF2020", dim: "#808080" },
      display: { splitBottomWhenIdle: "date" },
      targets: [{ date: "2026-10-27", time: "16:00:00", enabled: true }],
      live: { source: "off", url: "", refreshSec: 30, timeoutMs: 3000 },
      defaultPageId: "p2",
      pages: [
        { id: "p2", type: "countdown", title: "倒计时", shortTitle: "COUNTDOWN", enabled: true }
      ]
    });
    const farFrame = api.buildPageFrame(api.dateTimeToEpoch("2026-10-23", "12:00:00", 480),
      farConfig.pages[0], farConfig, { timeValid: true });
    assertEqual(farFrame.text, "4d04:00:00", "远目标文本不符");
    assertEqual(farFrame.runs[0].scale, 1, "远目标应小号");
    assertEqual(farFrame.runs[0].baseline, 10, "远目标 baseline 应为 10");

    const noTargetConfig = api.normalizeConfig(Object.assign({}, farConfig, { targets: [] }));
    const noTargetFrame = api.buildPageFrame(api.dateTimeToEpoch("2026-10-23", "11:00:00", 480),
      noTargetConfig.pages[0], noTargetConfig, { timeValid: true });
    assertEqual(noTargetFrame.mode, "clock", "倒计时页无目标应显示 NO COUNTDOWN");
    assertEqual(noTargetFrame.text, "NO COUNTDOWN", "无目标文本不符");
    assertEqual(noTargetFrame.runs[0].scale, 1, "NO COUNTDOWN 应为 scale1");
    assertEqual(noTargetFrame.runs[0].baseline, 10, "NO COUNTDOWN baseline 应为 10");
  });

  const liveRooms = [
    { platform: "bilibili", room: "12345", label: "", enabled: true, metric: "" },
    { platform: "douyin", room: "88776655", label: "", enabled: true, metric: "" }
  ];
  const sbLive = newSandbox({
    fixture: makeFixture({
      config: {
        pages: [
          { id: "p0", type: "clock", title: "时钟", shortTitle: "CLOCK", enabled: true },
          {
            id: "p3", type: "live", title: "直播", shortTitle: "LIVE", enabled: true,
            metric: "online", rooms: liveRooms
          }
        ],
        defaultPageId: "p0"
      }
    })
  });
  await settle(sbLive, 60);

  check("19. 直播帧：上下两行/8px 行高/baseline 6/14/label+value/平台色", function () {
    const api = requireApi(sbLive);
    const frame = api.getPageFrame("p3", 1792658309, "sample");
    assertEqual(frame.pageType, "live", "应为直播页");
    assertEqual(frame.cells.length, 2, "两个槽位应生成两个 cell");
    assertEqual(frame.cells[0].rect.y, 0, "第一行 y 应为 0");
    assertEqual(frame.cells[0].rect.h, 8, "行高应为 8");
    assertEqual(frame.cells[1].rect.y, 8, "第二行 y 应为 8");
    assertEqual(frame.cells[1].rect.h, 8, "行高应为 8");
    assertEqual(frame.cells[0].rect.w, 52, "行宽应为 52");
    assertEqual(frame.cells[0].baseline, 6, "第一行 baseline 6");
    assertEqual(frame.cells[1].baseline, 14, "第二行 baseline 14");
    assertEqual(frame.cells[0].label, "B45", "B 站 label 应为 B45");
    assertEqual(frame.cells[1].label, "D55", "抖音 label 应为 D55");
    assertEqual(frame.cells[0].value, "1.2w", "B 站示例值应为 1.2w");
    assertEqual(frame.cells[1].value, "8765", "抖音示例值应为 8765");
    assertEqual(frame.cells[0].labelColor, "#FF6699", "B 站平台色不符");
    assertEqual(frame.cells[1].labelColor, "#FFFFFF", "抖音平台色不符");
    assertEqual(frame.cells[0].valueColor, "#FFFFFF", "示例 value 应为白色");
    assertIncludes(frame.label || api.frameMeta(frame, api.getDraft()), "示例数据", "meta 应标注示例数据");
  });

  check("20. 直播默认无数据：value -- / dim；整页无启用行 NO LIVE", function () {
    const api = requireApi(sbLive);
    const frame = api.getPageFrame("p3", 1792658309, "none");
    assertEqual(frame.cells.length, 2, "无数据仍应有两个槽位");
    assertEqual(frame.cells[0].value, "--", "无数据应为 --");
    assertEqual(frame.cells[0].valueColor, "#808080", "无数据 value 应为 dim");
    assertEqual(frame.cells[0].state, "unsupported", "无数据状态不符");

    const noLiveConfig = api.normalizeConfig({
      version: 3, tzOffsetMinutes: 480, warnSeconds: 10,
      colors: { clock: "#FFFFFF", countdown: "#00FF00", warning: "#FF2020", dim: "#808080" },
      display: { splitBottomWhenIdle: "date" },
      targets: [],
      live: { source: "off", url: "", refreshSec: 30, timeoutMs: 3000 },
      defaultPageId: "p0",
      pages: [
        { id: "p0", type: "clock", title: "时钟", shortTitle: "CLOCK", enabled: true },
        { id: "p1", type: "live", title: "直播", shortTitle: "LIVE", enabled: true, metric: "online",
          rooms: [
            { platform: "bilibili", room: "", label: "", enabled: false, metric: "" },
            { platform: "douyin", room: "", label: "", enabled: false, metric: "" }
          ] }
      ]
    });
    const noLiveFrame = api.buildPageFrame(1792658309, noLiveConfig.pages[1], noLiveConfig, {});
    assertEqual(noLiveFrame.cells.length, 0, "全停用不应有 cell");
    assertEqual(noLiveFrame.runs.length, 1, "全停用应有一个 NO LIVE run");
    assertEqual(noLiveFrame.runs[0].text, "NO LIVE", "NO LIVE 文本不符");
    assertEqual(noLiveFrame.runs[0].color, "#808080", "NO LIVE 应为 dim");
    assertEqual(noLiveFrame.runs[0].baseline, 10, "NO LIVE baseline 10");
  });

  check("21. 直播空槽：启用但房间号为空显示 label + --，不显示 NO LIVE", function () {
    const api = requireApi(sbLive);
    const config = api.getDraft();
    const page = config.pages[1];
    const copy = JSON.parse(JSON.stringify(page));
    copy.rooms[1].room = "";
    const frame = api.buildPageFrame(1792658309, copy, config, { livePreview: "sample" });
    assertEqual(frame.cells.length, 2, "空槽仍应占一行");
    assertEqual(frame.cells[1].value, "--", "空槽 value 应为 --");
    assertEqual(frame.cells[1].label, "D", "空槽 label 应为平台字母");
    assertEqual(frame.cells[1].state, "empty", "空槽状态应为 empty");
  });

  check("22. 直播数值格式：1234 / 1.2w / 112.3w / 9.9y / 非法 -> --", function () {
    const api = requireApi(sbLive);
    assertEqual(api.formatLiveValue(0), "0", "0 是有效值");
    assertEqual(api.formatLiveValue(999), "999", "999 不符");
    assertEqual(api.formatLiveValue(1234), "1234", "1234 不符");
    assertEqual(api.formatLiveValue(12345), "1.2w", "12345 应为 1.2w");
    assertEqual(api.formatLiveValue(1200000), "120w", "1200000 应为 120w");
    assertEqual(api.formatLiveValue(987654321), "9.9y", "987654321 应为 9.9y");
    assertEqual(api.formatLiveValue(-1), "--", "负数应为 --");
    assertEqual(api.formatLiveValue("abc"), "--", "非数字应为 --");
    assertEqual(api.formatLiveValue(null), "--", "缺失应为 --");
    assert(api.formatLiveValue(12345).length <= 6, "直播 value 不得超过 6 字符");
  });

  check("23. 直播帧像素：label 用平台色、value 用白色，且无越界像素", function () {
    const api = requireApi(sbLive);
    const frame = api.getPageFrame("p3", 1792658309, "sample");
    const pixels = api.framePixels(frame);
    assertEqual(pixels.length, 52 * 16, "像素数组应为 52×16");
    assert(pixels.indexOf("#FF6699") >= 0, "应出现 B 站标签色");
    assert(pixels.indexOf("#FFFFFF") >= 0, "应出现白色 value");
    frame.cells.forEach(function (cell) {
      assert(cell.rect.x >= 0 && cell.rect.x + cell.rect.w <= 52, "cell 横向越界");
      assert(cell.rect.y >= 0 && cell.rect.y + cell.rect.h <= 16, "cell 纵向越界");
    });
    assert(api.measurePixelText("B45", 1, 0) + 1 + api.measurePixelText("1.2w", 1, 0) <= 52,
      "label+gap+value 应不超过 52");
  });

  /* ------------------------------ 页面列表 / 编辑器 ------------------------------ */
  const sbPages = newSandbox();
  await settle(sbPages, 60);

  check("24. 页面标题输入即时更新列表与草稿，不重建编辑输入；脏标记出现/消失", function () {
    const api = requireApi(sbPages);
    sbPages.registry.pageTitle.value = "时钟主页";
    dispatch(sbPages.registry.pageTitle, "input");
    const draft = api.getDraft();
    assertEqual(draft.pages[0].title, "时钟主页", "draft 标题未更新");
    const row = pageRows(sbPages)[0];
    assertEqual(pageRowTitle(row).textContent, "时钟主页", "列表标题未更新");
    assertEqual(sbPages.registry.pageTitle.value, "时钟主页", "输入框不应被重建");
    assertEqual(sbPages.registry.dirtyBadge.style.display, "inline-block", "应显示未保存修改");
    assertEqual(sbPages.registry.pageTitleError.textContent, "", "合法标题不应报错");

    sbPages.registry.pageTitle.value = "时钟";
    dispatch(sbPages.registry.pageTitle, "input");
    assertEqual(sbPages.registry.dirtyBadge.style.display, "none", "恢复后应清除脏标记");
  });

  await checkAsync("25. 标题校验：>16 字符 / 控制字符 / >48 UTF-8 字节均拦截保存", async function () {
    const api = requireApi(sbPages);
    const before = sbPages.ctx.savedBodies.length;
    sbPages.registry.pageTitle.value = "A".repeat(17);
    dispatch(sbPages.registry.pageTitle, "input");
    assertIncludes(sbPages.registry.pageTitleError.textContent, "16", "缺少 16 字符提示");
    assertIncludes(sbPages.registry.pageTitle.className, "invalid-t", "标题应高亮");
    dispatch(sbPages.registry.saveBtn, "click");
    await settle(sbPages, 10);
    assertEqual(sbPages.ctx.savedBodies.length, before, "非法标题不得 POST");
    assertIncludes(statusText(sbPages), "标题", "状态日志应包含标题错误");

    sbPages.registry.pageTitle.value = "a\nb";
    dispatch(sbPages.registry.pageTitle, "input");
    assertIncludes(sbPages.registry.pageTitleError.textContent, "控制字符", "缺少控制字符提示");

    sbPages.registry.pageTitle.value = "😀".repeat(16);
    dispatch(sbPages.registry.pageTitle, "input");
    assertIncludes(sbPages.registry.pageTitleError.textContent, "48", "缺少 UTF-8 字节提示");

    sbPages.registry.pageTitle.value = "时钟";
    dispatch(sbPages.registry.pageTitle, "input");
    assertEqual(sbPages.registry.pageTitleError.textContent, "", "恢复后应清除标题错误");
    assertEqual(api.getDraft().pages[0].title, "时钟", "draft 未恢复");
  });

  check("26. 切页短名校验：>8 / 非 ASCII 报错；合法值保留", function () {
    sbPages.registry.pageShortTitle.value = "ABCDEFGHI";
    dispatch(sbPages.registry.pageShortTitle, "input");
    assertIncludes(sbPages.registry.pageShortTitleError.textContent, "8", "缺少长度提示");
    sbPages.registry.pageShortTitle.value = "时钟";
    dispatch(sbPages.registry.pageShortTitle, "input");
    assertIncludes(sbPages.registry.pageShortTitleError.textContent, "ASCII", "缺少 ASCII 提示");
    sbPages.registry.pageShortTitle.value = "CLK";
    dispatch(sbPages.registry.pageShortTitle, "input");
    assertEqual(sbPages.registry.pageShortTitleError.textContent, "", "合法短名不应报错");
    assertEqual(requireApi(sbPages).getDraft().pages[0].shortTitle, "CLK", "draft 短名未更新");
    sbPages.registry.pageShortTitle.value = "CLOCK";
    dispatch(sbPages.registry.pageShortTitle, "input");
  });

  await checkAsync("27. 类型切换：clock -> live 建 2 槽位；live -> clock 确认并保留 rows（payload 省略）", async function () {
    const api = requireApi(sbPages);
    selectPage(sbPages, "p0");
    const beforeConfirm = sbPages.confirmCalls.length;
    sbPages.registry.pageType.value = "live";
    dispatch(sbPages.registry.pageType, "change");
    assertEqual(api.getDraft().pages[0].type, "live", "类型未切换");
    assertEqual(sbPages.registry.liveEditor.style.display, "block", "live 应显示直播编辑");
    assertEqual(liveSlots(sbPages).length, 2, "直播页应固定 2 个槽位");
    assertEqual(sbPages.confirmCalls.length, beforeConfirm, "clock -> live 不应确认");

    const slot = liveSlots(sbPages)[0];
    slotRoomInput(slot).value = "12345";
    dispatch(slotRoomInput(slot), "input");
    dispatch(slotRoomInput(slot), "change");
    const enabled = inputsOf(slot).filter(function (input) { return input.type === "checkbox"; })[0];
    enabled.checked = true;
    dispatch(enabled, "change");
    assertEqual(api.getDraft().pages[0].rooms[0].room, "12345", "房间号未写入 draft");

    const confirmBefore = sbPages.confirmCalls.length;
    sbPages.registry.pageType.value = "clock";
    dispatch(sbPages.registry.pageType, "change");
    assertEqual(sbPages.confirmCalls.length, confirmBefore + 1, "live -> clock 有直播间时应确认");
    assertEqual(api.getDraft().pages[0].type, "clock", "确认后类型应切换");
    assertEqual(sbPages.registry.liveEditor.style.display, "none", "clock 应隐藏直播编辑");
    assertEqual(api.getDraft().pages[0].rooms[0].room, "12345", "切回 clock 后 rows 应保留");
    const payload = api.buildPayload(api.getDraft());
    assert(!("rooms" in payload.pages[0]), "非 live 页 payload 不得包含 rooms");
  });

  await checkAsync("28. 类型切换取消：live -> clock 确认取消时保持 live", async function () {
    const sbCancel = newSandbox({ confirmResult: false });
    await settle(sbCancel, 60);
    const api = requireApi(sbCancel);
    selectPage(sbCancel, "p0");
    sbCancel.registry.pageType.value = "live";
    dispatch(sbCancel.registry.pageType, "change");
    const slot = liveSlots(sbCancel)[0];
    slotRoomInput(slot).value = "99";
    dispatch(slotRoomInput(slot), "input");
    const enabled = inputsOf(slot).filter(function (input) { return input.type === "checkbox"; })[0];
    enabled.checked = true;
    dispatch(enabled, "change");

    sbCancel.registry.pageType.value = "clock";
    dispatch(sbCancel.registry.pageType, "change");
    assertEqual(sbCancel.confirmCalls.length, 1, "应请求确认一次");
    assertEqual(api.getDraft().pages[0].type, "live", "取消后类型应保持 live");
    assertEqual(sbCancel.registry.pageType.value, "live", "下拉应回滚为 live");
  });

  check("29. 新增页面：追加/生成唯一 id/自动选中/聚焦标题；达到 12 页禁用", function () {
    const sbAdd = newSandbox();
    requireApi(sbAdd);
    dispatch(sbAdd.registry.addPageBtn, "click");
    const api = requireApi(sbAdd);
    assertEqual(api.getDraft().pages.length, 5, "新增后应有 5 页");
    const page = api.getDraft().pages[4];
    assertEqual(page.id, "p4", "新页 id 应为首个空闲 p4");
    assertEqual(page.title, "新页面", "新页标题不符");
    assertEqual(api.getSelectedPageId(), "p4", "新页应自动选中");
    assertEqual(sbAdd.registry.pageTitle.value, "新页面", "编辑区未切换新页");
    assertEqual(api.getPages().filter(function (p) { return p.id === "p4"; }).length, 1, "id 不唯一");

    while (api.getDraft().pages.length < 12) dispatch(sbAdd.registry.addPageBtn, "click");
    assertEqual(api.getDraft().pages.length, 12, "应能到 12 页");
    assertEqual(sbAdd.registry.addPageBtn.disabled, true, "12 页后新增应禁用");
    dispatch(sbAdd.registry.addPageBtn, "click");
    assertEqual(api.getDraft().pages.length, 12, "第 13 页必须被拦截");
    assertIncludes(sbAdd.registry.pageHint.textContent, "12 页", "应提示上限");
  });

  await checkAsync("30. 删除页面：仅剩 1 页禁用；删除默认页自动改默认；选中相邻页", async function () {
    const sbDel = newSandbox();
    const api = requireApi(sbDel);
    selectPage(sbDel, "p2");
    assertEqual(pageRows(sbDel).length, 4, "初始页面数不符");
    dispatch(pageBtn(pageRows(sbDel)[2], "✕"), "click");
    assertEqual(api.getDraft().pages.length, 3, "应删除一页");
    assertEqual(api.getDraft().pages[2].id, "p3", "删除后顺序不符");
    assertEqual(api.getSelectedPageId(), "p3", "删除选中页后应选后一页");
    assertEqual(sbDel.registry.pageTitle.value, "直播", "编辑区未切到相邻页");

    const sbOne = newSandbox({
      fixture: makeFixture({
        config: {
          pages: [{ id: "p0", type: "clock", title: "唯一页", shortTitle: "ONE", enabled: true }],
          defaultPageId: "p0"
        }
      })
    });
    await settle(sbOne, 60);
    selectPage(sbOne, "p0");
    assertEqual(pageRows(sbOne).length, 1, "单页 fixture 不符");
    assert(pageBtn(pageRows(sbOne)[0], "✕").disabled, "仅剩 1 页时删除应禁用");

    const sbDefault = newSandbox();
    const apiDefault = requireApi(sbDefault);
    selectPage(sbDefault, "p1");
    sbDefault.registry.defaultPageChk.checked = true;
    dispatch(sbDefault.registry.defaultPageChk, "change");
    assertEqual(apiDefault.getDraft().defaultPageId, "p1", "默认页未改为 p1");
    dispatch(pageBtn(pageRows(sbDefault)[1], "✕"), "click");
    assertEqual(apiDefault.getDraft().pages.length, 3, "删除默认页后页数不符");
    assertEqual(apiDefault.getDraft().defaultPageId, "p2", "删除默认页后应顺延到后一页");
  });

  await checkAsync("31. 删除带直播间的页面需确认；取消不删除", async function () {
    const rooms = [
      { platform: "bilibili", room: "12345", label: "", enabled: true, metric: "" },
      { platform: "douyin", room: "", label: "", enabled: false, metric: "" }
    ];
    const sbLiveDel = newSandbox({
      confirmResult: false,
      fixture: makeFixture({
        config: {
          pages: [
            { id: "p0", type: "clock", title: "时钟", shortTitle: "CLOCK", enabled: true },
            { id: "p1", type: "live", title: "直播", shortTitle: "LIVE", enabled: true,
              metric: "online", rooms: rooms }
          ],
          defaultPageId: "p0"
        }
      })
    });
    await settle(sbLiveDel, 60);
    selectPage(sbLiveDel, "p1");
    const before = requireApi(sbLiveDel).getDraft().pages.length;
    dispatch(pageBtn(pageRows(sbLiveDel)[1], "✕"), "click");
    assertEqual(sbLiveDel.confirmCalls.length, 1, "应请求确认");
    assertIncludes(sbLiveDel.confirmCalls[0], "1 个直播间", "确认文案应包含配置数");
    assertEqual(requireApi(sbLiveDel).getDraft().pages.length, before, "取消后不得删除");
  });

  check("32. 上移/下移：顺序交换、首末按钮禁用、选中页按 id 跟随", function () {
    const sbMove = newSandbox();
    const api = requireApi(sbMove);
    selectPage(sbMove, "p0");
    let rows = pageRows(sbMove);
    assertEqual(pageRowTitle(rows[0]).textContent, "时钟", "初始第一页不符");
    dispatch(pageBtn(rows[0], "↓"), "click");
    rows = pageRows(sbMove);
    assertEqual(api.getDraft().pages[0].id, "p1", "下移后顺序不符");
    assertEqual(api.getDraft().pages[1].id, "p0", "下移后顺序不符");
    assertEqual(api.getSelectedPageId(), "p0", "选中页应跟随 id");
    assertEqual(sbMove.registry.pageTitle.value, "时钟", "编辑区不应跳到别页");
    assertEqual(pageRowTitle(rows[1]).textContent, "时钟", "列表未更新顺序");
    dispatch(pageBtn(rows[1], "↑"), "click");
    assertEqual(api.getDraft().pages[0].id, "p0", "上移未恢复");
    assert(pageBtn(pageRows(sbMove)[0], "↑").disabled, "首行上移应禁用");
    assert(pageBtn(pageRows(sbMove)[3], "↓").disabled, "末行下移应禁用");
  });

  check("33. 默认页：勾选后唯一徽标；取消勾选被拒绝；切换只改 defaultPageId", function () {
    const sbDefault = newSandbox();
    const api = requireApi(sbDefault);
    assertEqual(api.getDraft().defaultPageId, "p0", "初始默认页不符");
    selectPage(sbDefault, "p2");
    sbDefault.registry.defaultPageChk.checked = true;
    dispatch(sbDefault.registry.defaultPageChk, "change");
    assertEqual(api.getDraft().defaultPageId, "p2", "默认页未切换");
    assertIncludes(textOf(pageRows(sbDefault)[2]), "默认", "新默认页缺少徽标");
    assert(textOf(pageRows(sbDefault)[0]).indexOf("默认") < 0, "旧默认页不应再有徽标");

    sbDefault.registry.defaultPageChk.checked = false;
    dispatch(sbDefault.registry.defaultPageChk, "change");
    assertEqual(sbDefault.registry.defaultPageChk.checked, true, "默认页勾选不应被取消");
    assertEqual(api.getDraft().defaultPageId, "p2", "默认页不应变化");
  });

  check("34. 启用/停用页面：列表徽标与 off 样式；payload enabled 字段", function () {
    const sbEnable = newSandbox();
    const api = requireApi(sbEnable);
    sbEnable.registry.pageEnabled.checked = false;
    dispatch(sbEnable.registry.pageEnabled, "change");
    assertEqual(api.getDraft().pages[0].enabled, false, "draft enabled 未更新");
    assert(hasClass(pageRows(sbEnable)[0], "off"), "停用页应加 off 样式");
    assertIncludes(textOf(pageRows(sbEnable)[0]), "停用", "列表应显示停用徽标");
    assertEqual(api.buildPayload(api.getDraft()).pages[0].enabled, false, "payload enabled 不符");
    sbEnable.registry.pageEnabled.checked = true;
    dispatch(sbEnable.registry.pageEnabled, "change");
  });

  await checkAsync("35. 设为此页：POST /api/page {id}，不保存配置；新页禁用", async function () {
    const sbCurrent = newSandbox();
    await settle(sbCurrent, 60);
    const api = requireApi(sbCurrent);
    selectPage(sbCurrent, "p1");
    const beforeSaved = sbCurrent.ctx.savedBodies.length;
    dispatch(sbCurrent.registry.setCurrentPageBtn, "click");
    await settle(sbCurrent, 30);
    assertEqual(sbCurrent.ctx.pageBodies.length, 1, "应 POST 一次 /api/page");
    assertEqual(sbCurrent.ctx.pageBodies[0].id, "p1", "切页请求 id 不符");
    assertEqual(sbCurrent.ctx.savedBodies.length, beforeSaved, "设为此页不得保存配置");
    assertEqual(api.getDevicePageId(), "p1", "设备当前页未更新");
    assertIncludes(textOf(pageRows(sbCurrent)[1]), "当前", "当前徽标未移动");
    assertIncludes(statusText(sbCurrent), "设备当前页已切换", "缺少切页成功提示");

    dispatch(sbCurrent.registry.addPageBtn, "click");
    const newPageId = api.getSelectedPageId();
    assertEqual(sbCurrent.registry.setCurrentPageBtn.disabled, true, "未保存新页应禁用设为此页");
    const pagePosts = sbCurrent.ctx.pageBodies.length;
    dispatch(sbCurrent.registry.setCurrentPageBtn, "click");
    await settle(sbCurrent, 10);
    assertEqual(sbCurrent.ctx.pageBodies.length, pagePosts, "禁用时不得发请求");
    assertIncludes(statusText(sbCurrent), "尚未保存到设备", "缺少新页提示");
    assert(newPageId, "新页 id 缺失");
  });

  check("36. 模拟器页面下拉与列表选中双向同步", function () {
    const sbSync = newSandbox();
    const api = requireApi(sbSync);
    sbSync.registry.simPageSelect.value = "p1";
    dispatch(sbSync.registry.simPageSelect, "change");
    assertEqual(api.getSelectedPageId(), "p1", "下拉未同步选中页");
    assertEqual(sbSync.registry.pageType.value, "clock_countdown", "编辑区未同步");
    assert(hasClass(pageRows(sbSync)[1], "selected"), "列表选中态未同步");
    dispatch(pageBtn(pageRows(sbSync)[2], "编辑"), "click");
    assertEqual(sbSync.registry.simPageSelect.value, "p2", "列表选中未同步下拉");
  });

  check("37. 页面标题 XSS：<img>/<script> 只作文本，不创建元素", function () {
    const sbXssPage = newSandbox();
    const api = requireApi(sbXssPage);
    const xss = '<img src=x onerror=alert(1)><script>alert(2)</script>';
    sbXssPage.registry.pageTitle.value = xss;
    dispatch(sbXssPage.registry.pageTitle, "input");
    assertEqual(api.getDraft().pages[0].title, xss, "标题应原样保存为文本");
    assertEqual(pageRowTitle(pageRows(sbXssPage)[0]).textContent, xss, "列表应按文本展示");
    assert(sbXssPage.createdTags.indexOf("img") < 0, "不得创建 img");
    assert(sbXssPage.createdTags.indexOf("script") < 0, "不得创建 script");
  });

  /* ------------------------------ 直播槽位编辑器 ------------------------------ */
  const sbLiveEdit = newSandbox();
  await settle(sbLiveEdit, 60);
  selectPage(sbLiveEdit, "p3");

  check("38. 直播固定 2 槽位：平台/指标/房间号/label/启用/空槽 --", function () {
    const slots = liveSlots(sbLiveEdit);
    assertEqual(slots.length, 2, "直播页应固定 2 个槽位");
    slots.forEach(function (slot) {
      assert(slotPlatform(slot), "缺少平台 select");
      assert(slotMetric(slot), "缺少指标 select");
      assert(slotRoomInput(slot), "缺少房间号输入");
      assert(slotLabelInput(slot), "缺少 label 输入");
      assert(inputsOf(slot).some(function (input) { return input.type === "checkbox"; }), "缺少启用开关");
    });
    const firstEnabled = inputsOf(slots[0]).filter(function (input) { return input.type === "checkbox"; })[0];
    firstEnabled.checked = true;
    dispatch(firstEnabled, "change");
    assertEqual(slotBadge(liveSlots(sbLiveEdit)[0]).textContent, "--", "启用但空房间应显示 --");
    assertEqual(slotBadge(liveSlots(sbLiveEdit)[1]).textContent, "--", "停用空槽也应显示 --");
    assertIncludes(textOf(liveSlots(sbLiveEdit)[1]), "已停用", "停用槽位提示应说明已停用");
    assertEqual(formOptionValues(sbLiveEdit.registry.livePageMetric).join(","),
      "online,likes,fans,status,title,none", "页默认指标选项不符");
  });

  check("39. 指标随平台联动：重建下拉/平台文案/不支持时回退", function () {
    const api = requireApi(sbLiveEdit);
    let slot = liveSlots(sbLiveEdit)[0];
    slotPlatform(slot).value = "bilibili";
    dispatch(slotPlatform(slot), "change");
    assertEqual(slotMetric(slot).children[1].textContent, "在线/人气", "B 站 online 文案不符");
    slotMetric(slot).value = "likes";
    dispatch(slotMetric(slot), "change");

    slotPlatform(slot).value = "douyin";
    dispatch(slotPlatform(slot), "change");
    assertEqual(slotMetric(slot).children[1].textContent, "在线观众", "抖音 online 文案不符");
    assertEqual(api.getDraft().pages[3].rooms[0].platform, "douyin", "平台未写入 draft");

    const original = api.LIVE_CAPABILITIES.kuaishou;
    try {
      api.LIVE_CAPABILITIES.kuaishou = ["online"];
      slotMetric(slot).value = "likes";
      dispatch(slotMetric(slot), "change");
      slotPlatform(slot).value = "kuaishou";
      dispatch(slotPlatform(slot), "change");
      assertEqual(api.getDraft().pages[3].rooms[0].metric, "online", "不支持指标应回退到首个可用项");
      assertIncludes(textOf(slot), "已切换为", "缺少回退提示");
      assertEqual(formOptionValues(slotMetric(slot)).join(","), ",online", "指标下拉应只含可用项");
    } finally {
      api.LIVE_CAPABILITIES.kuaishou = original;
    }
  });

  check("40. 房间号规则：B 1..12 数字 / D 1..20 数字 / K 1..32 字母数字_-", function () {
    const api = requireApi(sbLiveEdit);
    assert(api.roomFormatOk("bilibili", "21452505"), "B 站 8 位数字应合法");
    assert(!api.roomFormatOk("bilibili", "1234567890123"), "B 站 13 位应非法");
    assert(!api.roomFormatOk("bilibili", "abc"), "B 站字母应非法");
    assert(api.roomFormatOk("douyin", "12345678901234567890"), "抖音 20 位应合法");
    assert(!api.roomFormatOk("douyin", "123456789012345678901"), "抖音 21 位应非法");
    assert(api.roomFormatOk("kuaishou", "3x8k2m9"), "快手字母数字应合法");
    assert(api.roomFormatOk("kuaishou", "3x_8-k"), "快手 _- 应合法");
    assert(!api.roomFormatOk("kuaishou", "3x 8k"), "快手空格应非法");
    assert(!api.roomFormatOk("kuaishou", "a".repeat(33)), "快手 33 位应非法");
  });

  check("41. 粘贴分享链接提取：三平台 + query/hash；短链/错平台拒绝", function () {
    const api = requireApi(sbLiveEdit);
    assertEqual(api.parseRoomLink("bilibili", "https://live.bilibili.com/21452505?share_source=copy#x").value,
      "21452505", "B 站链接提取失败");
    assertEqual(api.parseRoomLink("douyin", "https://live.douyin.com/88776655/?x=1").value,
      "88776655", "抖音链接提取失败");
    assertEqual(api.parseRoomLink("kuaishou", "https://live.kuaishou.com/u/3x8k2m9?f=1").value,
      "3x8k2m9", "快手链接提取失败");
    assert(api.parseRoomLink("bilibili", "https://b23.tv/abc").ok === false, "B 站短链应拒绝");
    assert(api.parseRoomLink("douyin", "https://v.douyin.com/abc").ok === false, "抖音短链应拒绝");
    assert(api.parseRoomLink("bilibili", "https://live.douyin.com/12345").ok === false, "错平台链接应拒绝");
    assert(api.parseRoomLink("bilibili", "hello world").ok === false, "无效文本应拒绝");
  });

  check("42. paste 事件尽力提取链接并写入房间号", function () {
    const slot = liveSlots(sbLiveEdit)[0];
    slotPlatform(slot).value = "bilibili";
    dispatch(slotPlatform(slot), "change");
    const input = slotRoomInput(slot);
    let prevented = false;
    dispatch(input, "paste", {
      clipboardData: { getData: function () { return "https://live.bilibili.com/21452505?x=1"; } },
      preventDefault: function () { prevented = true; }
    });
    assertEqual(input.value, "21452505", "paste 未提取房间号");
    assertEqual(requireApi(sbLiveEdit).getDraft().pages[3].rooms[0].room, "21452505", "draft 未写入");
    assert(prevented, "提取成功应阻止默认粘贴");
  });

  await checkAsync("43. 同页重复房间：两行标红/保存拦截/错误含页码行号", async function () {
    const sbDup = newSandbox();
    await settle(sbDup, 60);
    selectPage(sbDup, "p3");
    const slots = liveSlots(sbDup);
    slots.forEach(function (slot, index) {
      const enabled = inputsOf(slot).filter(function (input) { return input.type === "checkbox"; })[0];
      enabled.checked = true;
      dispatch(enabled, "change");
      slotPlatform(slot).value = "bilibili";
      dispatch(slotPlatform(slot), "change");
      slotRoomInput(slot).value = "12345";
      dispatch(slotRoomInput(slot), "input");
      dispatch(slotRoomInput(slot), "change");
    });
    assertIncludes(slotBadge(liveSlots(sbDup)[0]).textContent, "重复", "第一行应标重复");
    assertIncludes(slotBadge(liveSlots(sbDup)[1]).textContent, "重复", "第二行应标重复");
    const before = sbDup.ctx.savedBodies.length;
    dispatch(sbDup.registry.saveBtn, "click");
    await settle(sbDup, 20);
    assertEqual(sbDup.ctx.savedBodies.length, before, "重复房间不得 POST");
    assertIncludes(statusText(sbDup), "第 4 页第 2 个直播间与第 1 个重复", "错误缺少页码/行号");
  });

  await checkAsync("44. 不同平台同数字允许；保存 payload 保留 2 个槽位", async function () {
    const sbDiff = newSandbox();
    await settle(sbDiff, 60);
    selectPage(sbDiff, "p3");
    const slots = liveSlots(sbDiff);
    slots.forEach(function (slot, index) {
      const enabled = inputsOf(slot).filter(function (input) { return input.type === "checkbox"; })[0];
      enabled.checked = true;
      dispatch(enabled, "change");
      slotPlatform(slot).value = index === 0 ? "bilibili" : "douyin";
      dispatch(slotPlatform(slot), "change");
      slotRoomInput(slot).value = "12345";
      dispatch(slotRoomInput(slot), "input");
      dispatch(slotRoomInput(slot), "change");
    });
    dispatch(sbDiff.registry.saveBtn, "click");
    await settle(sbDiff, 80);
    assertEqual(sbDiff.ctx.savedBodies.length, 1, "不同平台同数字应允许保存");
    const rooms = sbDiff.ctx.savedBodies[0].pages[3].rooms;
    assertEqual(rooms.length, 2, "payload 应固定 2 个槽位");
    assertEqual(rooms[0].platform, "bilibili", "第 1 槽平台不符");
    assertEqual(rooms[1].platform, "douyin", "第 2 槽平台不符");
    assertEqual(rooms[0].room, "12345", "第 1 槽房间号不符");
    assertEqual(rooms[1].room, "12345", "第 2 槽房间号不符");
  });

  await checkAsync("45. 跨页重复允许并提示共用数据，不阻塞保存", async function () {
    const rooms = [
      { platform: "bilibili", room: "12345", label: "", enabled: true, metric: "" },
      { platform: "douyin", room: "", label: "", enabled: false, metric: "" }
    ];
    const sbCross = newSandbox({
      fixture: makeFixture({
        config: {
          pages: [
            { id: "p0", type: "live", title: "直播一", shortTitle: "L1", enabled: true, metric: "online", rooms: rooms },
            { id: "p1", type: "live", title: "直播二", shortTitle: "L2", enabled: true, metric: "online", rooms: rooms }
          ],
          defaultPageId: "p0"
        }
      })
    });
    await settle(sbCross, 60);
    selectPage(sbCross, "p0");
    assertIncludes(textOf(liveSlots(sbCross)[0]), "已在第 2 页使用", "缺少跨页提示");
    const errors = requireApi(sbCross).validatePageBasics(requireApi(sbCross).getDraft().pages[0], 0);
    assertEqual(errors.length, 0, "跨页重复不应是校验错误");
  });

  check("46. 19/20 位抖音房间号按字符串原样保存，无精度丢失", function () {
    const api = requireApi(sbLiveEdit);
    const config = api.normalizeConfig({
      version: 3, tzOffsetMinutes: 480, warnSeconds: 10,
      colors: { clock: "#FFFFFF", countdown: "#00FF00", warning: "#FF2020", dim: "#808080" },
      display: { splitBottomWhenIdle: "date" },
      targets: [],
      live: { source: "off", url: "", refreshSec: 30, timeoutMs: 3000 },
      defaultPageId: "p0",
      pages: [
        { id: "p0", type: "live", title: "直播", shortTitle: "LIVE", enabled: true, metric: "online",
          rooms: [
            { platform: "douyin", room: "1234567890123456789", label: "D89", enabled: true, metric: "online" },
            { platform: "douyin", room: "12345678901234567890", label: "", enabled: true, metric: "" }
          ] }
      ]
    });
    const payload = api.buildPayload(config);
    assertEqual(payload.pages[0].rooms[0].room, "1234567890123456789", "19 位房间号被改动");
    assertEqual(payload.pages[0].rooms[1].room, "12345678901234567890", "20 位房间号被改动");
    assertEqual(typeof payload.pages[0].rooms[1].room, "string", "房间号必须为字符串");
  });

  await checkAsync("47. label ≤3 ASCII：合法值进入 payload；非 ASCII 拦截保存", async function () {
    const sbLabel = newSandbox();
    await settle(sbLabel, 60);
    selectPage(sbLabel, "p3");
    const slot = liveSlots(sbLabel)[0];
    slotLabelInput(slot).value = "B25";
    dispatch(slotLabelInput(slot), "input");
    assertEqual(requireApi(sbLabel).buildPayload(requireApi(sbLabel).getDraft()).pages[3].rooms[0].label,
      "B25", "label 未进入 payload");
    slotLabelInput(slot).value = "中";
    dispatch(slotLabelInput(slot), "input");
    const before = sbLabel.ctx.savedBodies.length;
    dispatch(sbLabel.registry.saveBtn, "click");
    await settle(sbLabel, 20);
    assertEqual(sbLabel.ctx.savedBodies.length, before, "非 ASCII label 不得 POST");
    assertIncludes(statusText(sbLabel), "label", "缺少 label 错误");
  });

  await checkAsync("48. 全停用直播页可保存，payload 仍含 2 个 enabled:false 槽位", async function () {
    const sbEmptyLive = newSandbox();
    await settle(sbEmptyLive, 60);
    dispatch(sbEmptyLive.registry.saveBtn, "click");
    await settle(sbEmptyLive, 80);
    assertEqual(sbEmptyLive.ctx.savedBodies.length, 1, "全停用直播页应允许保存");
    const rooms = sbEmptyLive.ctx.savedBodies[0].pages[3].rooms;
    assertEqual(rooms.length, 2, "payload 槽位数不符");
    rooms.forEach(function (room) { assertEqual(room.enabled, false, "默认槽位应停用"); });
  });

  await checkAsync("49. 页默认指标写入 payload；行 metric 空=跟随页默认", async function () {
    const sbMetric = newSandbox();
    await settle(sbMetric, 60);
    selectPage(sbMetric, "p3");
    sbMetric.registry.livePageMetric.value = "status";
    dispatch(sbMetric.registry.livePageMetric, "change");
    assertEqual(requireApi(sbMetric).getDraft().pages[3].metric, "status", "页默认指标未更新");
    const slots = liveSlots(sbMetric);
    const enabled = inputsOf(slots[0]).filter(function (input) { return input.type === "checkbox"; })[0];
    enabled.checked = true;
    dispatch(enabled, "change");
    slotRoomInput(slots[0]).value = "12345";
    dispatch(slotRoomInput(slots[0]), "input");
    dispatch(sbMetric.registry.saveBtn, "click");
    await settle(sbMetric, 80);
    const page = sbMetric.ctx.savedBodies[0].pages[3];
    assertEqual(page.metric, "status", "payload 页默认指标不符");
    assertEqual(page.rooms[0].metric, "", "行 metric 空应保持空（跟随页默认）");
  });

  check("50. 直播数据源 UI：off/bridge/hybrid/push；bridge/hybrid 才显示 URL", function () {
    const select = sbLiveEdit.registry.liveSource;
    assertEqual(formOptionValues(select).join(","), "off,bridge,hybrid,push", "数据源选项不符");
    select.value = "bridge";
    dispatch(select, "change");
    assertEqual(sbLiveEdit.registry.liveUrlRow.style.display, "block", "bridge 应显示 URL");
    select.value = "hybrid";
    dispatch(select, "change");
    assertEqual(sbLiveEdit.registry.liveUrlRow.style.display, "block", "hybrid 应显示 URL");
    assertIncludes(sbLiveEdit.registry.liveSourceHint.textContent, "B 站", "hybrid 提示应提到 B 站直连");
    select.value = "push";
    dispatch(select, "change");
    assertEqual(sbLiveEdit.registry.liveUrlRow.style.display, "none", "push 应隐藏 URL");
    select.value = "off";
    dispatch(select, "change");
    assertEqual(sbLiveEdit.registry.liveUrlRow.style.display, "none", "off 应隐藏 URL");
  });

  await checkAsync("51. 中继 URL：可留空；https/空格/超长拦截；refresh/timeout 越界拦截", async function () {
    const sbSource = newSandbox();
    await settle(sbSource, 60);
    selectPage(sbSource, "p3");
    sbSource.registry.liveSource.value = "bridge";
    dispatch(sbSource.registry.liveSource, "change");
    sbSource.registry.liveUrl.value = "";
    dispatch(sbSource.registry.liveUrl, "input");
    dispatch(sbSource.registry.saveBtn, "click");
    await settle(sbSource, 80);
    assertEqual(sbSource.ctx.savedBodies.length, 1, "bridge URL 留空应允许保存");
    assertEqual(sbSource.ctx.savedBodies[0].live.url, "", "空 URL 应提交空串");

    /* 保存后的回读会按 fixture 把 source 重置为 off，后续 URL 校验前重新选 bridge。 */
    sbSource.registry.liveSource.value = "bridge";
    dispatch(sbSource.registry.liveSource, "change");
    sbSource.registry.liveUrl.value = "https://192.168.31.10:8081/live";
    dispatch(sbSource.registry.liveUrl, "input");
    assertIncludes(sbSource.registry.liveUrl.className, "invalid-t", "https URL 应高亮");
    let before = sbSource.ctx.savedBodies.length;
    dispatch(sbSource.registry.saveBtn, "click");
    await settle(sbSource, 20);
    assertEqual(sbSource.ctx.savedBodies.length, before, "https URL 不得 POST");
    assertIncludes(statusText(sbSource), "只接受 http://", "缺少 http 提示");

    sbSource.registry.liveUrl.value = "http://ok/live path";
    dispatch(sbSource.registry.liveUrl, "input");
    dispatch(sbSource.registry.saveBtn, "click");
    await settle(sbSource, 20);
    assertIncludes(statusText(sbSource), "无空格", "缺少空格错误提示");

    sbSource.registry.liveUrl.value = "http://" + "a".repeat(160);
    dispatch(sbSource.registry.liveUrl, "input");
    dispatch(sbSource.registry.saveBtn, "click");
    await settle(sbSource, 20);
    assertIncludes(statusText(sbSource), "160", "缺少超长提示");

    sbSource.registry.liveUrl.value = "";
    dispatch(sbSource.registry.liveUrl, "input");
    sbSource.registry.liveRefresh.value = "10";
    dispatch(sbSource.registry.liveRefresh, "input");
    dispatch(sbSource.registry.saveBtn, "click");
    await settle(sbSource, 20);
    assertIncludes(statusText(sbSource), "15..300", "缺少刷新周期错误");
    assertIncludes(sbSource.registry.liveRefresh.className, "invalid-t", "刷新周期应高亮");

    sbSource.registry.liveRefresh.value = "30";
    dispatch(sbSource.registry.liveRefresh, "input");
    sbSource.registry.liveTimeout.value = "999";
    dispatch(sbSource.registry.liveTimeout, "input");
    dispatch(sbSource.registry.saveBtn, "click");
    await settle(sbSource, 20);
    assertIncludes(statusText(sbSource), "1000..5000", "缺少超时错误");
  });

  /* ------------------------------ v3 payload / 规范化 / 迁移 ------------------------------ */
  check("52. buildPayload 精确：version3 / 字段顺序 / colors.dim / display / live / 页面结构", function () {
    const api = requireApi(sbA);
    const payload = api.buildPayload(api.getDraft());
    assertEqual(Object.keys(payload).join(","),
      "version,tzOffsetMinutes,warnSeconds,ntpServer,colors,display,targets,live,defaultPageId,pages",
      "payload 顶层字段顺序不符");
    assertEqual(payload.version, 3, "version 必须为 3");
    assertEqual(Object.keys(payload.colors).sort().join(","), "clock,countdown,countup,dim,warning", "colors 字段不符");
    assertEqual(Object.keys(payload.display).join(","), "splitBottomWhenIdle", "display 字段不符");
    assertEqual(Object.keys(payload.live).sort().join(","), "refreshSec,source,timeoutMs,url", "live 字段不符");
    assertEqual(payload.live.source, "off", "默认 live.source 不符");
    assertEqual(payload.defaultPageId, "p0", "默认页不符");
    assertEqual(payload.pages.length, 4, "页面数不符");
    assertEqual(Object.keys(payload.pages[0]).sort().join(","),
      "enabled,id,shortTitle,title,type", "非 live 页字段不符");
    assertEqual(Object.keys(payload.pages[3]).sort().join(","),
      "enabled,id,metric,rooms,shortTitle,title,type", "live 页字段不符");
    assertEqual(Object.keys(payload.pages[3].rooms[0]).sort().join(","),
      "enabled,label,metric,platform,room", "直播槽位字段不符");
    assertEqual(payload.pages[3].rooms.length, 2, "live 页必须恰好 2 个槽位");
    const text = JSON.stringify(payload);
    ["_note", "_touched", "_metricNote", "selectedPageId", "password", "previewState"].forEach(function (key) {
      assert(text.indexOf('"' + key + '"') < 0, "payload 不得包含 UI/隐私字段：" + key);
    });
    assertEqual(payload.targets.length, 2, "fixture 目标数不符");
  });

  check("53. 最坏 v3 payload（12 页 / 24 槽位 / 12 目标 / 16 字标题）< 16 KB", function () {
    const api = requireApi(sbA);
    const pages = [];
    for (let i = 0; i < 12; i++) {
      pages.push({
        id: "p" + i,
        type: "live",
        title: "中文标题十六个字符测试页面",
        shortTitle: "LIVE" + i,
        enabled: true,
        metric: "online",
        rooms: [
          { platform: "bilibili", room: "123456789012", label: "B12", enabled: true, metric: "online" },
          { platform: "kuaishou", room: "3x8k2m9_abcdefghijklmnop", label: "K89", enabled: true, metric: "status" }
        ]
      });
    }
    const targets = [];
    for (let i = 0; i < 12; i++) {
      targets.push({ date: "2026-10-" + String(20 + (i % 9)).padStart(2, "0"), time: "12:34:56", enabled: true });
    }
    const config = api.normalizeConfig({
      version: 3, tzOffsetMinutes: 480, warnSeconds: 10,
      colors: { clock: "#FFFFFF", countdown: "#00FF00", warning: "#FF2020", dim: "#808080" },
      display: { splitBottomWhenIdle: "date" },
      targets: targets,
      live: { source: "hybrid", url: "http://192.168.31.10:8081/live", refreshSec: 30, timeoutMs: 3000 },
      defaultPageId: "p0", pages: pages
    });
    const size = api.payloadSizeBytes(config);
    assert(size < 16384, "payload 体积超 16KB：" + size);
    assertEqual(api.buildPayload(config).pages.length, 12, "页数不符");
    assertEqual(api.buildPayload(config).pages[0].rooms.length, 2, "槽位数不符");
    assertEqual(api.MAX_CONFIG_BODY, 16384, "体限常量应为 16384");
  });

  check("54. normalizeConfig 默认模板：4 页 / p0..p3 / dim / display / live off / 30s / 3000ms", function () {
    const api = requireApi(sbA);
    const defaults = api.normalizeConfig(null);
    assertEqual(defaults.version, 3, "默认 version 不符");
    assertEqual(defaults.pages.length, 4, "默认应 4 页");
    assertEqual(defaults.pages.map(function (p) { return p.id; }).join(","), "p0,p1,p2,p3", "默认 id 不符");
    assertEqual(defaults.pages.map(function (p) { return p.type; }).join(","),
      "clock,clock_countdown,countdown,live", "默认类型不符");
    assertEqual(defaults.defaultPageId, "p0", "默认页不符");
    assertEqual(defaults.colors.dim, "#808080", "默认 dim 不符");
    assertEqual(defaults.display.splitBottomWhenIdle, "date", "默认分屏底部不符");
    assertEqual(defaults.live.source, "off", "默认直播源不符");
    assertEqual(defaults.live.refreshSec, 30, "默认刷新周期不符");
    assertEqual(defaults.live.timeoutMs, 3000, "默认超时不符");
    assertEqual(defaults.targets.length, 0, "默认目标应为空");
  });

  check("55. v2 -> v3 网页侧迁移：3 页、保留 tz/warn/colors/targets、默认 p0", function () {
    const api = requireApi(sbA);
    const now = Math.floor(Date.now() / 1000);
    const first = localParts(now + 3600, 345);
    const migrated = api.normalizeConfig({
      version: 2,
      tzOffsetMinutes: 345,
      warnSeconds: 5,
      ntpServer: "ntp.example.com:123",
      colors: { clock: "#010203", countdown: "#040506", warning: "#070809" },
      targets: [{ date: first.date, time: first.time, enabled: true }]
    });
    assertEqual(migrated.version, 3, "迁移后 version 应为 3");
    assertEqual(migrated.pages.length, 3, "v2 迁移应补 3 页");
    assertEqual(migrated.pages.map(function (p) { return p.type; }).join(","),
      "clock,clock_countdown,countdown", "迁移页面类型不符");
    assertEqual(migrated.defaultPageId, "p0", "迁移默认页不符");
    assertEqual(migrated.tzOffsetMinutes, 345, "迁移时区丢失");
    assertEqual(migrated.warnSeconds, 5, "迁移预警丢失");
    assertEqual(migrated.ntpServer, "ntp.example.com:123", "迁移 NTP 丢失");
    assertEqual(migrated.colors.clock, "#010203", "迁移颜色丢失");
    assertEqual(migrated.colors.dim, "#808080", "迁移应补 dim");
    assertEqual(migrated.targets.length, 1, "迁移目标丢失");
    assertEqual(migrated.live.source, "off", "迁移直播源应为 off");
    assertEqual(api.detectConfigVersion({ version: 2 }), 2, "版本识别不符");
  });

  check("56. 非法页面/id/类型/default 归一化：未知类型回退 clock、id 唯一、default 回退第一页", function () {
    const api = requireApi(sbA);
    const rawPages = [];
    for (let i = 0; i < 14; i++) {
      rawPages.push({
        id: i === 0 ? "BAD ID" : (i === 1 ? "same" : (i === 2 ? "same" : "p" + i)),
        type: i === 0 ? "unknown" : "clock",
        title: "页面" + i,
        shortTitle: "S" + i,
        enabled: true
      });
    }
    const config = api.normalizeConfig({
      version: 3, tzOffsetMinutes: 480, warnSeconds: 10,
      colors: {}, display: {},
      targets: [], live: {},
      defaultPageId: "missing", pages: rawPages
    });
    assertEqual(config.pages.length, 12, "超过 12 页应截断");
    const ids = config.pages.map(function (p) { return p.id; });
    ids.forEach(function (id) { assert(api.pageIdOk(id), "归一化 id 非法：" + id); });
    assertEqual(new Set(ids).size, ids.length, "归一化后 id 应唯一");
    assertEqual(config.pages[0].type, "clock", "未知类型应回退 clock");
    assertEqual(config.defaultPageId, config.pages[0].id, "非法默认页应回退第一页");
  });

  check("57. 直播槽位归一化：补 2 槽、平台/指标缺省、字符串房间号、label 过滤", function () {
    const api = requireApi(sbA);
    const config = api.normalizeConfig({
      version: 3, tzOffsetMinutes: 480, warnSeconds: 10,
      colors: {}, display: {},
      targets: [], live: {},
      defaultPageId: "p0",
      pages: [{
        id: "p0", type: "live", title: "直播", shortTitle: "LIVE", enabled: true,
        metric: "bogus",
        rooms: [{ platform: "bogus", room: "12345678901234567890", label: "TOOLONG", enabled: true, metric: "bogus" }]
      }]
    });
    const page = config.pages[0];
    assertEqual(page.metric, "online", "页默认指标应回退 online");
    assertEqual(page.rooms.length, 2, "不足 2 槽应补齐");
    assertEqual(page.rooms[0].platform, "bilibili", "非法平台应回退 bilibili");
    assertEqual(page.rooms[0].room, "12345678901234567890", "房间号应按字符串保留");
    assertEqual(page.rooms[0].label, "", "超长非 ASCII label 应被清空");
    assertEqual(page.rooms[0].metric, "", "非法行指标应回退空");
    assertEqual(page.rooms[1].enabled, false, "补齐槽位应停用");
  });

  /* ------------------------------ 旧固件 / 保存链路 ------------------------------ */
  const sbLegacy = newSandbox({
    fixture: makeFixture({
      config: {
        version: 2,
        tzOffsetMinutes: 345,
        warnSeconds: 5,
        colors: { clock: "#010203", countdown: "#040506", warning: "#070809" },
        targets: []
      }
    })
  });
  await settle(sbLegacy, 80);

  check("58. v2 配置：3 页迁移、只读横幅、保存禁用、不发 POST", function () {
    const api = requireApi(sbLegacy);
    assertEqual(api.getDraft().pages.length, 3, "v2 应迁移为 3 页");
    assertEqual(sbLegacy.registry.pageCount.textContent, "3/12", "v2 页数不符");
    assertEqual(sbLegacy.registry.tzMinutesInput.value, "345", "v2 时区未保留");
    assertEqual(sbLegacy.registry.v2Banner.style.display, "block", "缺少 v2 只读横幅");
    assertIncludes(sbLegacy.registry.v2Banner.textContent, "v2", "横幅文案不符");
    assertEqual(sbLegacy.registry.saveBtn.disabled, true, "v2 模式保存应禁用");
    assertEqual(api.getDraft().live.source, "off", "v2 迁移直播源应为 off");
    dispatch(sbLegacy.registry.saveBtn, "click");
    assertEqual(sbLegacy.ctx.savedBodies.length, 0, "v2 模式不得 POST");
    assertIncludes(statusText(sbLegacy), "设备返回 v2 配置", "缺少只读提示");
  });

  let save400GetCount = 0;
  const sb400 = newSandbox({
    handler: function (req, ctx) {
      if (req.method === "GET" && /\/api\/config$/.test(req.url)) {
        save400GetCount++;
        return { status: 200, data: defaultFixture() };
      }
      if (req.method === "POST" && /\/api\/config$/.test(req.url)) {
        ctx.savedBodies.push(JSON.parse(req.body));
        return { status: 400, data: { ok: false, error: "v2 config is not accepted, refresh the page" } };
      }
      return defaultResponse(req, ctx, {});
    }
  });
  await settle(sb400, 60);

  await checkAsync("59. 保存 400（v2 page）：完整错误 + 刷新提示、不回读、不对时、dirty 保留", async function () {
    sb400.registry.pageTitle.value = "改了标题";
    dispatch(sb400.registry.pageTitle, "input");
    const beforeGet = save400GetCount;
    dispatch(sb400.registry.saveBtn, "click");
    await settle(sb400, 60);
    assertEqual(sb400.ctx.savedBodies.length, 1, "应尝试保存一次");
    assertEqual(save400GetCount, beforeGet, "400 后不应回读配置");
    assertEqual(sb400.ctx.timeBodies.length, 0, "400 后不应静默对时");
    const status = statusText(sb400);
    assertIncludes(status, "v2 config is not accepted", "错误详情丢失");
    assertIncludes(status, "请刷新页面", "缺少刷新提示");
    assertEqual(sb400.registry.dirtyBadge.style.display, "inline-block", "失败后应保留 dirty");
    assertEqual(sb400.registry.saveBtn.disabled, false, "失败后保存按钮应恢复");
    assertIncludes(sb400.registry.saveError.textContent, "保存失败", "缺少错误条");
  });

  await checkAsync("60. 保存 500：完整错误、不回读、不对时、按钮恢复", async function () {
    let getCount = 0;
    const sb500 = newSandbox({
      handler: function (req, ctx) {
        if (req.method === "GET" && /\/api\/config$/.test(req.url)) {
          getCount++;
          return { status: 200, data: defaultFixture() };
        }
        if (req.method === "POST" && /\/api\/config$/.test(req.url)) {
          ctx.savedBodies.push(JSON.parse(req.body));
          return { status: 500, data: { ok: false, error: "disk full <b>fatal</b>" } };
        }
        return defaultResponse(req, ctx, {});
      }
    });
    await settle(sb500, 40);
    dispatch(sb500.registry.saveBtn, "click");
    await settle(sb500, 40);
    assertEqual(sb500.ctx.savedBodies.length, 1, "应尝试保存一次");
    assertEqual(getCount, 1, "500 后不应回读配置");
    assertEqual(sb500.ctx.timeBodies.length, 0, "500 后不应对时");
    const status = statusText(sb500);
    assertIncludes(status, "HTTP 500", "缺少 HTTP 状态");
    assertIncludes(status, "disk full <b>fatal</b>", "错误详情必须完整展示");
    assertEqual(sb500.registry.saveBtn.disabled, false, "失败后按钮应恢复");
    assertIncludes(sb500.ctx.calls[0].url, "/api/config", "首个请求应为配置");
  });

  await checkAsync("61. 保存网络错误：错误展示、按钮恢复、不抛异常", async function () {
    const sbNet = newSandbox({
      handler: function (req, ctx) {
        if (req.method === "POST" && /\/api\/config$/.test(req.url)) {
          ctx.savedBodies.push(JSON.parse(req.body));
          return { reject: "network unreachable" };
        }
        return defaultResponse(req, ctx, {});
      }
    });
    await settle(sbNet, 40);
    dispatch(sbNet.registry.saveBtn, "click");
    await settle(sbNet, 40);
    assert(!sandboxError(sbNet), sandboxError(sbNet) || "");
    assertIncludes(statusText(sbNet), "network unreachable", "缺少网络错误详情");
    assertEqual(sbNet.registry.saveBtn.disabled, false, "网络错误后按钮应恢复");
  });

  /* 保存成功：POST -> GET -> POST time，回读 canonical，清 dirty */
  let saveGetCount = 0;
  const saveTargets = (function () {
    const now = Math.floor(Date.now() / 1000);
    const first = localParts(now + 86400, 480);
    const second = localParts(now + 172800, 480);
    return [
      { date: first.date, time: first.time, enabled: true },
      { date: second.date, time: second.time, enabled: true }
    ];
  })();
  const sbSave = newSandbox({
    fixture: function () {
      saveGetCount++;
      return makeFixture({
        config: {
          tzOffsetMinutes: saveGetCount > 1 ? 540 : 480,
          warnSeconds: saveGetCount > 1 ? 5 : 30,
          colors: saveGetCount > 1
            ? { clock: "#010203", countdown: "#040506", warning: "#070809", dim: "#0A0B0C" }
            : { clock: "#ABCDEF", countdown: "#112233", warning: "#FF2020", dim: "#808080" },
          targets: saveTargets
        }
      });
    }
  });
  await settle(sbSave, 60);
  const NOTE_TEXT = "主题演讲";

  await checkAsync("62. 保存 v3 链路：POST -> GET 回读 -> 静默对时；不提交备注/密码", async function () {
    const note = inputOfType(sbSave.rows()[0], "text");
    note.value = NOTE_TEXT;
    dispatch(note, "input");
    dispatch(sbSave.registry.saveBtn, "click");
    await settle(sbSave, 120);

    assertEqual(sbSave.ctx.savedBodies.length, 1, "应发起 1 次 POST /api/config");
    const body = sbSave.ctx.savedBodies[0];
    assertEqual(body.version, 3, "POST 必须为 version 3");
    assertEqual(body.pages.length, 4, "payload 页面数不符");
    assertEqual(body.live.source, "off", "payload live.source 不符");
    assertEqual(body.colors.dim, "#808080", "payload colors.dim 不符");
    assert(JSON.stringify(body).indexOf(NOTE_TEXT) < 0, "备注不得提交到设备");

    const calls = sbSave.calls;
    const postConfigIndex = calls.findIndex(function (call) {
      return call.method === "POST" && /\/api\/config$/.test(call.url);
    });
    const getAfterIndex = calls.findIndex(function (call, index) {
      return index > postConfigIndex && call.method === "GET" && /\/api\/config$/.test(call.url);
    });
    const postTimeIndex = calls.findIndex(function (call, index) {
      return index > getAfterIndex && call.method === "POST" && /\/api\/time$/.test(call.url);
    });
    assert(postConfigIndex >= 0, "缺少 POST /api/config");
    assert(getAfterIndex > postConfigIndex, "保存后应重新 GET /api/config");
    assert(postTimeIndex > getAfterIndex, "重新加载后应静默 POST /api/time");
    assertEqual(sbSave.ctx.timeBodies.length, 1, "静默对时应恰好 1 次");
    assertEqual(sbSave.registry.tzMinutesInput.value, "540", "回读 canonical 未生效");
    assertEqual(sbSave.registry.warnInput.value, "5", "回读 warnSeconds 未生效");
    assertEqual(sbSave.registry.dirtyBadge.style.display, "none", "保存成功后应清除 dirty");
    assertIncludes(statusText(sbSave), "配置已保存", "缺少保存成功提示");
    assertIncludes(statusText(sbSave), "已重新加载设备配置", "缺少回读提示");
  });

  await checkAsync("63. 聚合校验：全部错误一次列出、不发 POST、聚焦/高亮首个非法控件", async function () {
    const sbBad = newSandbox();
    await settle(sbBad, 60);
    sbBad.registry.tzMinutesInput.value = "900";
    dispatch(sbBad.registry.tzMinutesInput, "input");
    sbBad.registry.warnInput.value = "601";
    dispatch(sbBad.registry.warnInput, "input");
    sbBad.registry.colorClock.value = "#12345";
    dispatch(sbBad.registry.colorClock, "input");
    sbBad.registry.ntpServerInput.value = "bad host";
    dispatch(sbBad.registry.ntpServerInput, "input");
    const target = sbBad.rows()[0];
    inputOfType(target, "date").value = "";
    dispatch(inputOfType(target, "date"), "input");
    inputOfType(target, "time").value = "";
    dispatch(inputOfType(target, "time"), "input");
    sbBad.registry.pageTitle.value = "A".repeat(17);
    dispatch(sbBad.registry.pageTitle, "input");
    sbBad.registry.liveSource.value = "bridge";
    dispatch(sbBad.registry.liveSource, "change");
    sbBad.registry.liveUrl.value = "https://x";
    dispatch(sbBad.registry.liveUrl, "input");
    sbBad.registry.liveRefresh.value = "10";
    dispatch(sbBad.registry.liveRefresh, "input");
    sbBad.registry.liveTimeout.value = "999";
    dispatch(sbBad.registry.liveTimeout, "input");

    dispatch(sbBad.registry.saveBtn, "click");
    await settle(sbBad, 20);
    assertEqual(sbBad.ctx.savedBodies.length, 0, "校验失败不得 POST");
    const status = statusText(sbBad);
    ["时区偏移无效", "预警秒数无效", "颜色格式无效", "NTP 服务器格式无效",
      "目标日期无效", "目标时间无效", "标题", "只接受 http://", "15..300", "1000..5000"]
      .forEach(function (line) { assertIncludes(status, line, "缺少错误：" + line); });
    assertIncludes(sbBad.registry.saveError.textContent, "保存中止", "缺少错误条");
    assert(sbBad.registry.tzMinutesInput.className.indexOf("invalid-t") >= 0, "tz 未高亮");
    assert(sbBad.registry.pageTitle.className.indexOf("invalid-t") >= 0, "标题未高亮");
    assert(sbBad.scrollCalls.length === 0, "错误展示不得自动滚动");
  });

  check("64. dirty / beforeunload / 重新加载确认取消；保存失败保留 dirty", function () {
    const sbDirty = newSandbox({ confirmResult: false });
    const api = requireApi(sbDirty);
    sbDirty.registry.pageTitle.value = "未保存标题";
    dispatch(sbDirty.registry.pageTitle, "input");
    assertEqual(api.isDirty(), true, "修改后应为 dirty");
    assertEqual(sbDirty.registry.dirtyBadge.style.display, "inline-block", "dirty 徽标未显示");

    let prevented = false;
    const event = {
      preventDefault: function () { prevented = true; },
      returnValue: ""
    };
    sbDirty.fireWindowEvent("beforeunload", event);
    assert(prevented, "dirty 时 beforeunload 应 preventDefault");
    assertEqual(event.returnValue, "", "beforeunload 应设置 returnValue");

    const beforeCalls = sbDirty.calls.length;
    dispatch(sbDirty.registry.reloadBtn, "click");
    assertEqual(sbDirty.confirmCalls.length, 1, "重新加载应确认一次");
    assertEqual(sbDirty.calls.length, beforeCalls, "取消确认后不应发请求");
    assertEqual(api.getDraft().pages[0].title, "未保存标题", "取消后不应丢弃草稿");
  });

  await checkAsync("65. 配置版本能力：applyConfig 对 v3 关闭只读；切换页面不清 dirty", async function () {
    const sbDirty2 = newSandbox();
    await settle(sbDirty2, 60);
    const api = requireApi(sbDirty2);
    sbDirty2.registry.pageTitle.value = "临时标题";
    dispatch(sbDirty2.registry.pageTitle, "input");
    const dirtyBefore = api.isDirty();
    selectPage(sbDirty2, "p2");
    assertEqual(api.isDirty(), dirtyBefore, "切换页面不应改变 dirty");
    assertEqual(sbDirty2.registry.v2Banner.style.display, "none", "v3 不应显示 v2 横幅");
    assertEqual(sbDirty2.registry.saveBtn.disabled, false, "v3 保存应可用");
  });

  /* ------------------------------ 目标（v2 回归） ------------------------------ */
  const badgeNow = Math.floor(Date.now() / 1000);
  const badgeTargets = [
    (function () { const p = localParts(badgeNow - 3600, 480); return { date: p.date, time: p.time, enabled: true }; })(),
    (function () { const p = localParts(badgeNow + 1800, 480); return { date: p.date, time: p.time, enabled: true }; })(),
    (function () { const p = localParts(badgeNow + 7200, 480); return { date: p.date, time: p.time, enabled: true }; })(),
    (function () { const p = localParts(badgeNow + 10800, 480); return { date: p.date, time: p.time, enabled: false }; })()
  ];
  const sbBadge = newSandbox({ fixture: makeFixture({ config: { targets: badgeTargets } }) });
  await settle(sbBadge, 40);

  check("66. 目标行结构：开关/date/time step=1/备注/上移/下移/删除", function () {
    const row = sbBadge.rows()[0];
    const dateInput = inputOfType(row, "date");
    const timeInput = inputOfType(row, "time");
    assert(dateInput && timeInput, "缺少 date/time 输入");
    assertEqual(timeInput.step, "1", "target time 必须 step=1（秒）");
    assertEqual(dateInput.min, "2000-01-01", "date min 不符");
    assertEqual(dateInput.max, "2099-12-31", "date max 不符");
    assertEqual(inputOfType(row, "text").maxLength, 40, "备注长度限制不符");
    assert(buttonOfText(row, "↑"), "缺少上移按钮");
    assert(buttonOfText(row, "↓"), "缺少下移按钮");
    assert(buttonOfText(row, "✕"), "缺少删除按钮");
    assertMatch(timeInput.value, /^\d{2}:\d{2}:\d{2}$/, "时间输入必须显示秒");
  });

  check("67. 目标状态徽标：已过期 / 进行中 / 等待中 / 已停用", function () {
    const texts = badges(sbBadge);
    assertEqual(texts[0], "已过期", "第 1 行应为已过期：" + texts.join(" | "));
    assertEqual(texts[1], "进行中", "第 2 行应为进行中：" + texts.join(" | "));
    assertEqual(texts[2], "等待中", "第 3 行应为等待中：" + texts.join(" | "));
    assertEqual(texts[3], "已停用", "第 4 行应为已停用：" + texts.join(" | "));
    assert(hasClass(sbBadge.rows()[3], "off"), "停用行应有 off 样式");
    assertIncludes(textOf(sbBadge.rows()[1]), "剩余", "进行中应显示剩余时间");
  });

  await checkAsync("68. 目标上移/下移/删除/最多 12/移除已过期", async function () {
    const targetKey = function (row) {
      return inputOfType(row, "date").value + " " + inputOfType(row, "time").value;
    };
    let rows = sbBadge.rows();
    const firstKey = targetKey(rows[0]);
    const secondKey = targetKey(rows[1]);
    dispatch(buttonOfText(rows[0], "↓"), "click");
    rows = sbBadge.rows();
    assertEqual(targetKey(rows[0]), secondKey, "下移后首行不符");
    dispatch(buttonOfText(rows[1], "↑"), "click");
    assertEqual(targetKey(sbBadge.rows()[0]), firstKey, "上移未恢复");
    const beforeDelete = sbBadge.rows().length;
    dispatch(buttonOfText(sbBadge.rows()[1], "✕"), "click");
    assertEqual(sbBadge.rows().length, beforeDelete - 1, "删除后行数不符");

    const sbLimit = newSandbox({
      fixture: makeFixture({
        config: {
          targets: (function () {
            const p = localParts(Math.floor(Date.now() / 1000) + 3600, 480);
            return [{ date: p.date, time: p.time, enabled: true }];
          })()
        }
      })
    });
    await settle(sbLimit, 40);
    while (sbLimit.rows().length < 12) dispatch(sbLimit.registry.addTargetBtn, "click");
    assertEqual(sbLimit.rows().length, 12, "应能加到 12 个");
    assertEqual(sbLimit.registry.addTargetBtn.disabled, true, "12 个后添加应禁用");
    dispatch(sbLimit.registry.addTargetBtn, "click");
    assertEqual(sbLimit.rows().length, 12, "第 13 个必须被拦截");

    const now = Math.floor(Date.now() / 1000);
    const past = localParts(now - 7200, 480);
    const future = localParts(now + 5400, 480);
    const sbExpired = newSandbox({
      fixture: makeFixture({
        config: {
          targets: [
            { date: past.date, time: past.time, enabled: true },
            { date: future.date, time: future.time, enabled: true },
            { date: "", time: "", enabled: false }
          ]
        }
      })
    });
    await settle(sbExpired, 40);
    dispatch(sbExpired.registry.removeExpiredBtn, "click");
    assertEqual(sbExpired.rows().length, 2, "应只移除 1 个已过期目标");
    assertIncludes(statusText(sbExpired), "已移除 1 个已过期目标", "缺少移除提示");
  });

  /* ------------------------------ 时区 / 设备 / 自动对时 ------------------------------ */
  check("69. 常用时区下拉包含 -12..+14 与半小时/45 分钟项", function () {
    const values = formOptionValues(sbA.registry.tzSelect);
    [-720, -660, -600, -570, -540, -480, -420, -360, -300, -240, -210, -180, -120, -60,
      0, 60, 120, 180, 210, 240, 270, 300, 330, 345, 360, 390, 420, 480, 525, 540, 570,
      600, 630, 660, 720, 765, 780, 840].forEach(function (minutes) {
      assert(values.indexOf(String(minutes)) >= 0, "时区列表缺少分钟偏移 " + minutes);
    });
    assert(values.indexOf("custom") >= 0, "缺少“自定义”选项");
  });

  check("70. 设备信息：IP/SSID/时间有效性/本地时间/UTC±HH:MM/最近对时/运行时长", function () {
    const text = textOf(sbA.registry.deviceInfo);
    assertIncludes(text, "192.168.8.219", "缺少 IP");
    assertIncludes(text, "Ulanzi_TC002", "缺少 SSID");
    assertIncludes(text, "时间有效性", "缺少时间有效性");
    assertIncludes(text, "设备本地时间", "缺少设备本地时间");
    assertIncludes(text, "UTC+08:00", "缺少 UTC±HH:MM");
    assertIncludes(text, "最近对时", "缺少最近对时");
    assertIncludes(text, "1 天 01:01:01", "uptime 格式不符");
    assertMatch(text, /\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}/, "设备本地时间必须含秒");
    assertEqual(sbA.registry.netSsid.textContent, "Ulanzi_TC002", "netSsid 不符");
    assertEqual(sbA.registry.netIp.textContent, "192.168.8.219", "netIp 不符");
  });

  await checkAsync("71. 设备时间偏差 >2 秒自动对时一次；阈值边界严格", async function () {
    const autoNow = Math.floor(Date.now() / 1000);
    const sbAuto = newSandbox({ fixture: makeFixture({ device: { timeUtc: autoNow - 3600 } }) });
    await settle(sbAuto, 80);
    const posts = sbAuto.ctx.timeBodies;
    assertEqual(posts.length, 1, "自动对时次数不符");
    assertEqual(Object.keys(posts[0]).join(","), "epoch", "对时请求体字段不符");
    assert(Math.abs(posts[0].epoch - Math.floor(Date.now() / 1000)) <= 5, "epoch 应为浏览器当前秒");
    assertIncludes(statusText(sbAuto), "已自动对时", "缺少自动对时提示");
    assertIncludes(textOf(sbAuto.registry.deviceInfo), "有效", "对时后设备时间应有效");

    const api = requireApi(sbAuto);
    const now = 1792658309;
    assertEqual(api.shouldAutoSync(now - 2, now), false, "偏差 2 秒不应自动对时");
    assertEqual(api.shouldAutoSync(now - 3, now), true, "偏差 3 秒应自动对时");
    assertEqual(api.shouldAutoSync(now + 2, now), false, "快 2 秒不应自动对时");
    assertEqual(api.shouldAutoSync(0, now), true, "设备时间无效应自动对时");
    assertEqual(api.AUTO_SYNC_THRESHOLD, 2, "阈值常量应为 2");
    assertEqual(sbAuto.ctx.timeBodies.length, 1, "阈值测试不应新增对时请求");
  });

  await checkAsync("72. 时间无效 + 自动对时失败：--:--:-- / bad 样式 / 完整错误", async function () {
    const sbInvalid = newSandbox({
      fixture: makeFixture({ device: { timeUtc: 0, timeValid: false, lastSyncUtc: 0 } }),
      handler: function (req, ctx) {
        if (req.method === "POST" && /\/api\/time$/.test(req.url)) {
          ctx.timeBodies.push(JSON.parse(req.body));
          return { status: 500, data: { ok: false, error: "clock write failed" } };
        }
        return defaultResponse(req, ctx, { fixture: makeFixture({ device: { timeUtc: 0, timeValid: false, lastSyncUtc: 0 } }) });
      }
    });
    await settle(sbInvalid, 80);
    const text = textOf(sbInvalid.registry.deviceInfo);
    assertIncludes(text, "未同步", "应标注未同步");
    assertIncludes(text, "--:--:--", "无效时间应显示 --:--:--");
    assert(hasClass(sbInvalid.registry.deviceInfo, "bad"), "未同步应使用 bad 样式");
    assertIncludes(statusText(sbInvalid), "HTTP 500", "错误缺少 HTTP 状态");
    assertIncludes(statusText(sbInvalid), "clock write failed", "错误缺少服务端信息");
    const frame = requireApi(sbInvalid).getFrame();
    assertEqual(frame.text, "--:--:--", "模拟器应显示 --:--:--");
    assertEqual(frame.color, "#808080", "无效颜色应为 dim");
  });

  await checkAsync("73. 立即对时：POST /api/time {epoch}、按钮状态与成功提示", async function () {
    const before = sbA.ctx.timeBodies.length;
    dispatch(sbA.registry.timeSyncBtn, "click");
    assertEqual(sbA.registry.timeSyncBtn.disabled, true, "请求期间按钮应禁用");
    await settle(sbA, 30);
    assertEqual(sbA.ctx.timeBodies.length, before + 1, "对时请求次数不符");
    const body = sbA.ctx.timeBodies[sbA.ctx.timeBodies.length - 1];
    assertEqual(Object.keys(body).join(","), "epoch", "请求体字段不符");
    assertIncludes(statusText(sbA), "设备时间已校准为", "缺少对时成功提示");
    assertEqual(sbA.registry.timeSyncBtn.disabled, false, "请求结束后按钮应恢复");
  });

  /* ------------------------------ WiFi（v2 回归） ------------------------------ */
  const WIFI_SECRET = "s3cret-WiFi-Pass!";
  const WIFI_GET_SECRET = "server-must-not-render";
  let wifiFixtureCount = 0;
  const wifiFixture = function () {
    wifiFixtureCount++;
    const networks = [
      { ssid: "Ulanzi_TC002", enabled: true, hasPassword: true, password: WIFI_GET_SECRET },
      { ssid: "Office", enabled: true, hasPassword: true }
    ];
    if (wifiFixtureCount > 1) networks.push({ ssid: "Cafe", enabled: true, hasPassword: false });
    return { ok: true, current: { ssid: "Ulanzi_TC002", ip: "192.168.8.219", wifi: true }, max: 8, networks: networks };
  };
  const sbWifi = newSandbox({ wifiFixture: wifiFixture });
  await settle(sbWifi, 100);

  check("74. WiFi 列表：多网络/开关/密码框/占位/当前徽标/密码不回显", function () {
    const rows = wifiRows(sbWifi);
    assertEqual(rows.length, 2, "应渲染 2 个 WiFi 网络");
    assertEqual(sbWifi.registry.wifiEditor.style.display, "block", "应显示列表编辑器");
    assertEqual(sbWifi.registry.wifiFallback.style.display, "none", "应隐藏只读回退行");
    assertEqual(sbWifi.registry.wifiFallbackSsid.readOnly, true, "回退 SSID 必须只读");
    assertEqual(wifiSsidInput(rows[0]).value, "Ulanzi_TC002", "第 1 个 SSID 不符");
    assertEqual(wifiSsidInput(rows[1]).value, "Office", "第 2 个 SSID 不符");
    assertEqual(wifiPassInput(rows[0]).type, "password", "密码框必须为 password");
    assertEqual(wifiPassInput(rows[0]).maxLength, 64, "密码长度上限应为 64");
    assertEqual(wifiPassInput(rows[0]).value, "", "GET 返回的密码不得回显");
    assertEqual(wifiPassInput(rows[0]).placeholder, "留空保留原密码", "已有密码占位不符");
    assert(wifiCurrentBadge(rows[0]), "当前网络应有徽标");
    assert(!wifiCurrentBadge(rows[1]), "非当前网络不应有徽标");
    assert(buttonOfText(rows[0], "↑").disabled, "首行上移应禁用");
    assert(buttonOfText(rows[1], "↓").disabled, "末行下移应禁用");
    const visible = statusText(sbWifi) + "\n" +
      descendants(sbWifi.registry.wifiList).map(function (el) { return el.value || el.textContent || ""; }).join("\n");
    assert(visible.indexOf(WIFI_GET_SECRET) < 0, "GET 中夹带的密码不得出现在页面");
  });

  await checkAsync("75. WiFi 添加/上移/停用/保存：开放网络确认、请求体、成功后回读清空密码", async function () {
    let rows = wifiRows(sbWifi);
    wifiPassInput(rows[0]).value = WIFI_SECRET;
    dispatch(wifiPassInput(rows[0]), "input");
    dispatch(sbWifi.registry.addWifiBtn, "click");
    rows = wifiRows(sbWifi);
    assertEqual(rows.length, 3, "添加后应有 3 行");
    assertEqual(wifiPassInput(rows[2]).placeholder, "密码（开放网络可留空）", "新行占位不符");
    wifiSsidInput(rows[2]).value = "Cafe";
    dispatch(wifiSsidInput(rows[2]), "input");
    dispatch(buttonOfText(rows[2], "↑"), "click");
    rows = wifiRows(sbWifi);
    assertEqual(wifiSsidInput(rows[1]).value, "Cafe", "上移后顺序不符");
    assertEqual(wifiPassInput(rows[0]).value, WIFI_SECRET, "移动行不应丢失密码");
    const office = wifiRowBySsid(sbWifi, "Office");
    const officeCheckbox = inputOfType(office, "checkbox");
    officeCheckbox.checked = false;
    dispatch(officeCheckbox, "change");

    const beforeConfirm = sbWifi.confirmCalls.length;
    dispatch(sbWifi.registry.wifiSaveBtn, "click");
    assertEqual(sbWifi.ctx.wifiListBodies.length, 1, "确认后应立即 POST 一次");
    assertEqual(sbWifi.confirmCalls.length, beforeConfirm + 1, "应弹一次确认");
    assertIncludes(sbWifi.confirmCalls[beforeConfirm], "无密码/开放网络", "缺少开放网络提示");
    assertIncludes(sbWifi.confirmCalls[beforeConfirm], "Cafe", "确认应列出 Cafe");
    assert(sbWifi.confirmCalls[beforeConfirm].indexOf(WIFI_SECRET) < 0, "确认框不得包含密码");

    const body = sbWifi.ctx.wifiListBodies[0];
    assertEqual(body.networks.length, 3, "请求体网络数不符");
    assertEqual(body.networks[0].password, WIFI_SECRET, "已有网络新密码应提交");
    assertEqual(Object.keys(body.networks[1]).sort().join(","), "enabled,ssid", "开放网络不得携带空密码");
    assert(!("password" in body.networks[2]), "留空密码应省略字段");
    assertEqual(body.networks[2].enabled, false, "停用状态未提交");
    await sleep(WIFI_REFRESH_WAIT_MS);
    await settle(sbWifi, 100);
    const allPasswords = descendants(sbWifi.registry.wifiList).filter(function (el) { return el.type === "password"; });
    allPasswords.forEach(function (input) { assertEqual(input.value, "", "保存成功后密码框应清空"); });
    assertIncludes(statusText(sbWifi), "WiFi 列表已保存（3 个网络）", "缺少保存成功提示");
    assert(statusText(sbWifi).indexOf(WIFI_SECRET) < 0, "状态日志不得包含 WiFi 密码");
    assert(JSON.stringify(sbWifi.storage).indexOf(WIFI_SECRET) < 0, "密码不得写入 localStorage");
  });

  await checkAsync("76. WiFi 校验：空/重复/超长 SSID、超长密码、取消确认均不 POST", async function () {
    const sbValidateWifi = newSandbox({ confirmResult: false });
    await settle(sbValidateWifi, 80);
    let rows = wifiRows(sbValidateWifi);
    const first = wifiSsidInput(rows[0]);
    const second = wifiSsidInput(rows[1]);

    first.value = "   ";
    dispatch(first, "input");
    dispatch(sbValidateWifi.registry.wifiSaveBtn, "click");
    assertEqual(sbValidateWifi.ctx.wifiListBodies.length, 0, "空 SSID 不得 POST");
    assertIncludes(statusText(sbValidateWifi), "名称不能为空", "缺少空 SSID 错误");

    first.value = "DupNet";
    second.value = "DupNet";
    dispatch(first, "input");
    dispatch(second, "input");
    dispatch(sbValidateWifi.registry.wifiSaveBtn, "click");
    assertEqual(sbValidateWifi.ctx.wifiListBodies.length, 0, "重复 SSID 不得 POST");
    assertIncludes(statusText(sbValidateWifi), "WiFi 名称重复：DupNet", "缺少重复错误");

    first.value = "A".repeat(33);
    second.value = "Other";
    dispatch(first, "input");
    dispatch(second, "input");
    dispatch(sbValidateWifi.registry.wifiSaveBtn, "click");
    assertIncludes(statusText(sbValidateWifi), "超过 32 个字符", "缺少 SSID 长度错误");

    first.value = "Valid";
    dispatch(first, "input");
    rows = wifiRows(sbValidateWifi);
    wifiPassInput(rows[0]).value = "p".repeat(65);
    dispatch(wifiPassInput(rows[0]), "input");
    dispatch(sbValidateWifi.registry.wifiSaveBtn, "click");
    assertIncludes(statusText(sbValidateWifi), "密码超过 64 个字符", "缺少密码长度错误");

    wifiPassInput(wifiRows(sbValidateWifi)[0]).value = "";
    dispatch(wifiPassInput(wifiRows(sbValidateWifi)[0]), "input");
    const beforeConfirm = sbValidateWifi.confirmCalls.length;
    dispatch(sbValidateWifi.registry.wifiSaveBtn, "click");
    assertEqual(sbValidateWifi.confirmCalls.length, beforeConfirm + 1, "校验通过后应确认");
    assertEqual(sbValidateWifi.ctx.wifiListBodies.length, 0, "取消确认后不得 POST");
  });

  await checkAsync("77. WiFi 最多 8 个：满额禁用/删除恢复/允许空列表", async function () {
    const eight = [];
    for (let i = 0; i < 8; i++) {
      eight.push({ ssid: "Net" + i, enabled: i % 2 === 0, hasPassword: i % 2 === 0 });
    }
    const sbMaxWifi = newSandbox({
      wifiFixture: { ok: true, current: { ssid: "Net0", ip: "192.168.8.219", wifi: true }, max: 8, networks: eight }
    });
    await settle(sbMaxWifi, 80);
    assertEqual(wifiRows(sbMaxWifi).length, 8, "应渲染 8 个 WiFi");
    assertEqual(sbMaxWifi.registry.addWifiBtn.disabled, true, "8 个后添加应禁用");
    dispatch(buttonOfText(wifiRows(sbMaxWifi)[7], "✕"), "click");
    assertEqual(wifiRows(sbMaxWifi).length, 7, "删除后应剩 7 个");
    assertEqual(sbMaxWifi.registry.addWifiBtn.disabled, false, "少于 8 个后应恢复");
    while (wifiRows(sbMaxWifi).length > 0) dispatch(buttonOfText(wifiRows(sbMaxWifi)[0], "✕"), "click");
    assertIncludes(textOf(sbMaxWifi.registry.wifiList), "暂无 WiFi", "删空后应显示空状态");
    dispatch(sbMaxWifi.registry.wifiSaveBtn, "click");
    assertEqual(sbMaxWifi.ctx.wifiListBodies[0].networks.length, 0, "应提交 networks:[]");
  });

  await checkAsync("78. WiFi 读取失败：只读当前 SSID + 完整错误，对时仍可用", async function () {
    const sbWifiFail = newSandbox({
      handler: function (req, ctx) {
        if (req.method === "GET" && /\/api\/wifi$/.test(req.url)) {
          return { status: 500, data: { ok: false, error: "wifi read failed" } };
        }
        return defaultResponse(req, ctx, {});
      }
    });
    await settle(sbWifiFail, 100);
    assertEqual(sbWifiFail.registry.wifiEditor.style.display, "none", "失败时应隐藏编辑器");
    assertEqual(sbWifiFail.registry.wifiFallback.style.display, "block", "失败时应显示只读行");
    assertEqual(sbWifiFail.registry.wifiFallbackSsid.value, "Ulanzi_TC002", "应回退显示当前 SSID");
    assertEqual(sbWifiFail.registry.wifiSaveBtn.disabled, true, "失败时保存应禁用");
    assertIncludes(statusText(sbWifiFail), "HTTP 500", "缺少 HTTP 状态");
    assertIncludes(statusText(sbWifiFail), "wifi read failed", "缺少服务端错误");
    dispatch(sbWifiFail.registry.timeSyncBtn, "click");
    await settle(sbWifiFail, 40);
    assertIncludes(statusText(sbWifiFail), "设备时间已校准为", "WiFi 失败时对时仍应可用");
  });

  await checkAsync("79. WiFi 保存失败：完整错误/密码不回显不落盘/保存按钮恢复", async function () {
    const failSecret = "never-log-this-wifi-password";
    const sbWifiSaveFail = newSandbox({
      handler: function (req, ctx) {
        if (req.method === "GET" && /\/api\/wifi$/.test(req.url)) return wifiResponse();
        if (req.method === "POST" && /\/api\/wifi\/list$/.test(req.url)) {
          ctx.wifiListBodies.push(JSON.parse(req.body));
          return { status: 500, data: { ok: false, error: "wifi nvs <b>full</b>" } };
        }
        return defaultResponse(req, ctx, {});
      }
    });
    await settle(sbWifiSaveFail, 80);
    const pass = wifiPassInput(wifiRows(sbWifiSaveFail)[0]);
    pass.value = failSecret;
    dispatch(pass, "input");
    dispatch(sbWifiSaveFail.registry.wifiSaveBtn, "click");
    await settle(sbWifiSaveFail, 90);
    assertEqual(sbWifiSaveFail.ctx.wifiListBodies.length, 1, "应尝试保存一次");
    assertIncludes(statusText(sbWifiSaveFail), "wifi nvs <b>full</b>", "缺少完整错误");
    assert(statusText(sbWifiSaveFail).indexOf(failSecret) < 0, "状态日志不得回显密码");
    assert(JSON.stringify(sbWifiSaveFail.storage).indexOf(failSecret) < 0, "密码不得写入 localStorage");
    assertEqual(wifiPassInput(wifiRows(sbWifiSaveFail)[0]).value, failSecret, "失败后应保留输入");
    assertEqual(sbWifiSaveFail.registry.wifiSaveBtn.disabled, false, "失败后按钮应恢复");
  });

  /* ------------------------------ 多地址 / file:// / 错误路径 ------------------------------ */
  await checkAsync("80. 时钟地址自动同源：不保存 IP、不显示地址列表、刷新仍请求当前设备", async function () {
    const sbAuto = newSandbox();
    await settle(sbAuto, 100);
    const api = requireApi(sbAuto);
    assertEqual(api.getBase(), "http://192.168.8.219:8080", "自动地址应为当前同源");
    assertEqual(sbAuto.storage["multitimer.bases.v2"], undefined, "不得再保存 IP 地址列表");
    assertIncludes(textOf(sbAuto.registry.deviceInfo), "192.168.8.219", "设备信息应显示当前 IP");

    const before = sbAuto.calls.length;
    dispatch(sbAuto.registry.reloadBtn, "click");
    await settle(sbAuto, 100);
    const recent = sbAuto.calls.slice(before);
    const configGets = recent.filter(function (call) {
      return call.method === "GET" && /\/api\/config$/.test(call.url);
    });
    assertEqual(configGets.length, 1, "刷新应 GET config 一次");
    assertEqual(configGets[0].url, "http://192.168.8.219:8080/api/config", "刷新请求地址不符");
  });

  await checkAsync("81. file://：没有设备地址时不发请求，离线预览仍可用", async function () {
    const sbFile = newSandbox({ protocol: "file:" });
    await settle(sbFile, 60);
    assertEqual(requireApi(sbFile).getBase(), "", "file:// 不应有设备地址");
    assertEqual(sbFile.calls.length, 0, "file:// 无地址不应发请求");
    assertEqual(requireApi(sbFile).getFrame().pageType, "clock", "默认应可离线预览时钟");
  });

  await checkAsync("82. GET 失败/500/非法 JSON：错误日志 + 默认模板 + 模拟器仍可用", async function () {
    const sbOffline = newSandbox({
      handler: function (req, ctx) {
        if (req.method === "GET" && /\/api\/config$/.test(req.url)) return { reject: "network unreachable" };
        return defaultResponse(req, ctx, {});
      }
    });
    const sb500 = newSandbox({
      handler: function (req, ctx) {
        if (req.method === "GET" && /\/api\/config$/.test(req.url)) {
          return { status: 500, data: { ok: false, error: "config read failed" } };
        }
        return defaultResponse(req, ctx, {});
      }
    });
    const sbJson = newSandbox({
      handler: function (req, ctx) {
        if (req.method === "GET" && /\/api\/config$/.test(req.url)) return { status: 200, badJson: true, data: null };
        return defaultResponse(req, ctx, {});
      }
    });
    await Promise.all([settle(sbOffline, 40), settle(sb500, 40), settle(sbJson, 40)]);
    assertIncludes(statusText(sbOffline), "无法连接时钟", "缺少连接失败提示");
    assertIncludes(statusText(sbOffline), "network unreachable", "缺少网络错误详情");
    assertIncludes(statusText(sb500), "HTTP 500", "缺少 500 状态");
    assertIncludes(statusText(sb500), "config read failed", "缺少服务端错误");
    assertIncludes(statusText(sbJson), "无响应体或响应不是 JSON", "缺少非法 JSON 提示");
    [sbOffline, sb500, sbJson].forEach(function (sb) {
      assert(!sandboxError(sb), sandboxError(sb) || "");
      assertEqual(requireApi(sb).getDraft().version, 3, "失败后应回退 v3 默认模板");
      assertEqual(requireApi(sb).getFrame().pageType, "clock", "失败后模拟器仍应可用");
      assertEqual(sb.scrollCalls.length, 0, "错误日志不得自动滚动");
    });
  });

  /* ------------------------------ XSS / 不可信文本 ------------------------------ */
  const XSS_NOTE = "<b>note</b><script>alert(2)</script>";
  const XSS_SSID = "<img src=x onerror=alert(1)>";
  await checkAsync("83. XSS：SSID/备注/设备错误/直播房间号按文本渲染，不产生元素", async function () {
    const xssDate = localParts(Math.floor(Date.now() / 1000) + 7200, 480);
    const sbXss = newSandbox({
      fixture: makeFixture({
        device: { ssid: XSS_SSID },
        config: {
          targets: [{ date: xssDate.date, time: xssDate.time, enabled: true }]
        }
      }),
      storage: {
        "multitimer.notes.v2": JSON.stringify({ [(xssDate.date + " " + xssDate.time)]: XSS_NOTE })
      },
      handler: function (req, ctx) {
        if (req.method === "GET" && /\/api\/config$/.test(req.url)) {
          return { status: 200, data: makeFixture({
            device: { ssid: XSS_SSID },
            config: { targets: [{ date: xssDate.date, time: xssDate.time, enabled: true }] }
          }) };
        }
        if (req.method === "POST" && /\/api\/config$/.test(req.url)) {
          ctx.savedBodies.push(JSON.parse(req.body));
          return { status: 500, data: { ok: false, error: "<b>bad</b><script>alert(3)</script>" } };
        }
        return defaultResponse(req, ctx, {});
      }
    });
    await settle(sbXss, 60);
    assertEqual(sbXss.registry.netSsid.textContent, XSS_SSID, "SSID 应按文本展示");
    assertIncludes(textOf(sbXss.registry.deviceInfo), XSS_SSID, "设备信息应按文本展示 SSID");
    const note = inputOfType(sbXss.rows()[0], "text");
    assertEqual(note.value, XSS_NOTE, "本地备注应原样回填");
    assert(sbXss.createdTags.indexOf("img") < 0, "不得创建 img");
    assert(sbXss.createdTags.indexOf("script") < 0, "不得创建 script");

    dispatch(sbXss.registry.saveBtn, "click");
    await settle(sbXss, 40);
    const status = statusText(sbXss);
    assertIncludes(status, "<b>bad</b><script>alert(3)</script>", "错误详情必须按文本完整展示");
    const pres = descendants(sbXss.registry.statusLog).filter(function (el) { return el.tag === "pre"; });
    assert(pres.length > 0, "错误详情应放在 pre");
    assertEqual(pres[pres.length - 1]._html, "", "错误详情不得通过 innerHTML 写入");

    const evilRoom = "<img src=x onerror=alert(4)>";
    const sbRoomXss = newSandbox({
      fixture: makeFixture({
        config: {
          pages: [
            { id: "p0", type: "clock", title: "时钟", shortTitle: "CLOCK", enabled: true },
            { id: "p1", type: "live", title: "直播", shortTitle: "LIVE", enabled: true, metric: "online",
              rooms: [
                { platform: "bilibili", room: evilRoom, label: "", enabled: true, metric: "" },
                { platform: "douyin", room: "", label: "", enabled: false, metric: "" }
              ] }
          ],
          defaultPageId: "p0"
        }
      })
    });
    await settle(sbRoomXss, 60);
    selectPage(sbRoomXss, "p1");
    assertEqual(slotRoomInput(liveSlots(sbRoomXss)[0]).value, evilRoom, "房间号应按文本回显");
    assert(sbRoomXss.createdTags.indexOf("img") < 0, "房间号不得生成 img");
  });

  /* ------------------------------ 纯函数 / 模拟器控制 ------------------------------ */
  check("84. UTC±HH:MM / 日期时间换算 / HH:MM 补秒 / 闰年边界", function () {
    const api = requireApi(sbA);
    assertEqual(api.fmtUtcOffset(480), "UTC+08:00", "480 不符");
    assertEqual(api.fmtUtcOffset(-570), "UTC-09:30", "-570 不符");
    assertEqual(api.fmtUtcOffset(345), "UTC+05:45", "345 不符");
    assertEqual(api.fmtUtcOffset(765), "UTC+12:45", "765 不符");
    assertEqual(api.fmtUtcOffset(-720), "UTC-12:00", "-720 不符");
    assertEqual(api.fmtUtcOffset(840), "UTC+14:00", "840 不符");
    assertEqual(api.normalizeDateInput("2024-02-29"), "2024-02-29", "闰年 2/29 应合法");
    assertEqual(api.normalizeDateInput("2026-02-29"), "", "平年 2/29 应非法");
    assertEqual(api.normalizeDateInput("2100-01-01"), "", "2100 应超出 2000..2099");
    assertEqual(api.normalizeTimeInput("19:25"), "19:25:00", "HH:MM 应补 :00");
    assertEqual(api.normalizeTimeInput("24:00:00"), "", "24:00:00 应非法");
    const utc = Date.UTC(2026, 9, 23, 11, 25, 0) / 1000;
    assertEqual(api.dateTimeToEpoch("2026-10-23", "19:25:00", 480), utc, "+08:00 换算不符");
    assertEqual(api.dateTimeToEpoch("2026-10-23", "19:25:00", 345),
      Date.UTC(2026, 9, 23, 13, 40, 0) / 1000, "+05:45 换算不符");
    const back = api.epochToLocalParts(utc, 480);
    assertEqual(back.date, "2026-10-23", "往返日期不符");
    assertEqual(back.time, "19:25:00", "往返时间不符");
    assertEqual(api.formatCountdown(359999).text, "99:59:59", "99:59:59 边界不符");
    assertEqual(api.formatCountdown(360000).text, "4d04:00:00", "100h 边界不符");
    assertEqual(api.formatCountdown(360000).smallFont, true, "100h 应小号");
  });

  check("85. evaluateStage：预警边界/到点切目标/停用与过期跳过", function () {
    const api = requireApi(sbA);
    const tz = 480;
    const targetEpoch = api.dateTimeToEpoch("2026-10-23", "12:00:00", tz);
    const config = api.defaultConfig();
    config.tzOffsetMinutes = tz;
    config.warnSeconds = 10;
    config.targets = [
      { date: "2026-10-23", time: "12:00:00", enabled: true },
      { date: "2026-10-23", time: "13:00:00", enabled: true },
      { date: "2026-10-22", time: "08:00:00", enabled: true }
    ];
    const atWarn = api.evaluateStage(targetEpoch - 10, config);
    assertEqual(atWarn.kind, "countdown", "应倒计时");
    assertEqual(atWarn.warning, true, "剩余==warnSeconds 应预警");
    assertEqual(atWarn.targetTime, "12:00:00", "应选最近未来目标");
    assertEqual(api.evaluateStage(targetEpoch - 11, config).warning, false, "剩余 11 不应预警");
    const atTarget = api.evaluateStage(targetEpoch, config);
    assertEqual(atTarget.remainSeconds, 0, "到点剩余 0");
    assertEqual(atTarget.warning, true, "到点仍在预警范围");
    const after = api.evaluateStage(targetEpoch + 1, config);
    assertEqual(after.targetTime, "13:00:00", "到点后应切下一个目标");
    assertEqual(after.remainSeconds, 3599, "切下一个目标剩余不符");

    const disabled = api.defaultConfig();
    disabled.targets = [{ date: "2026-10-23", time: "12:00:00", enabled: false }];
    assertEqual(api.evaluateStage(targetEpoch - 100, disabled).kind, "clock", "停用目标应忽略");
  });

  await checkAsync("86. 模拟器播放倍速：10x 三帧 +30 秒，暂停后停止", async function () {
    const sbPlay = newSandbox({
      fixture: makeFixture({ config: { targets: [{ date: "2026-10-23", time: "12:00:00", enabled: true }] } })
    });
    await settle(sbPlay, 40);
    selectPage(sbPlay, "p2");
    setManualTime(sbPlay, "2026-10-23", "11:00:00");
    sbPlay.frame();
    sbPlay.registry.speedSel.value = "10";
    dispatch(sbPlay.registry.speedSel, "change");
    dispatch(sbPlay.registry.playBtn, "click");
    assertEqual(sbPlay.registry.playBtn.textContent, "⏸ 暂停", "播放后按钮文案不符");
    const start = requireApi(sbPlay).getState().manualEpoch;
    sbPlay.frame();
    sbPlay.frame();
    sbPlay.frame();
    const afterPlay = requireApi(sbPlay).getState().manualEpoch;
    assertEqual(Math.round(afterPlay - start), 30, "10x 播放三帧应前进 30 秒");
    assertEqual(requireApi(sbPlay).getFrame().text, "00:59:30", "播放后倒计时不符");
    dispatch(sbPlay.registry.playBtn, "click");
    const paused = requireApi(sbPlay).getState().manualEpoch;
    sbPlay.frame();
    sbPlay.frame();
    assertEqual(requireApi(sbPlay).getState().manualEpoch, paused, "暂停后不应继续前进");
  });

  check("87. 分屏同一 now：改时区后上下两半一起按新时区重算", function () {
    const api = requireApi(sbFrames);
    const epoch = api.dateTimeToEpoch("2026-10-23", "11:00:00", 480);
    const base = {
      version: 3, warnSeconds: 30,
      colors: { clock: "#ABCDEF", countdown: "#112233", warning: "#FF2020", dim: "#808080" },
      display: { splitBottomWhenIdle: "date" },
      targets: [{ date: "2026-10-23", time: "12:00:00", enabled: true }],
      live: { source: "off", url: "", refreshSec: 30, timeoutMs: 3000 },
      defaultPageId: "p1",
      pages: [{ id: "p1", type: "clock_countdown", title: "分屏", shortTitle: "SPLIT", enabled: true }]
    };
    const config480 = api.normalizeConfig(Object.assign({}, base, { tzOffsetMinutes: 480 }));
    const config540 = api.normalizeConfig(Object.assign({}, base, { tzOffsetMinutes: 540 }));
    const frame480 = api.buildPageFrame(epoch, config480.pages[0], config480, { timeValid: true });
    const frame540 = api.buildPageFrame(epoch, config540.pages[0], config540, { timeValid: true });
    assertEqual(frame480.now, epoch, "now 不应被改写");
    assertEqual(frame540.now, epoch, "now 不应被改写");
    assertEqual(frame480.runs[0].text, "11:00:00", "tz480 上半不符");
    assertEqual(frame480.runs[1].text, "01:00:00", "tz480 下半不符");
    assertEqual(frame540.runs[0].text, "12:00:00", "tz540 上半不符");
    assertEqual(frame540.runs[1].text, "00:00:00", "tz540 下半不符");
    assertEqual(frame480.runs[0].baseline, 7, "上半基线不符");
    assertEqual(frame480.runs[1].baseline, 14, "下半基线不符");
  });

  check("88. 全部沙箱无运行期异常、无 scrollIntoView、状态日志不自动滚动", function () {
    const failures = sandboxes.map(sandboxError).filter(Boolean);
    assertEqual(failures.length, 0, failures.join(" | "));
    const offenders = sandboxes.filter(function (sb) { return sb.scrollCalls.length > 0; });
    assertEqual(offenders.length, 0, "以下沙箱触发了滚动：" +
      offenders.map(function (sb) { return sb.scrollCalls.join(","); }).join(" | "));
  });

  await checkAsync("89. 状态日志保留历史、错误完整、从不自动滚动", async function () {
    const before = sbA.registry.statusLog.children.length;
    sbA.registry.tzMinutesInput.value = "900";
    dispatch(sbA.registry.tzMinutesInput, "input");
    dispatch(sbA.registry.saveBtn, "click");
    await settle(sbA, 10);
    assert(sbA.registry.statusLog.children.length > before, "状态日志应追加");
    assertIncludes(textOf(sbA.registry.statusLog.children[0]), "已加载时钟配置", "最早状态应保留");
    assertIncludes(statusText(sbA), "保存中止", "缺少保存中止提示");
    assertEqual(sbA.scrollCalls.length, 0, "状态日志不得触发自动滚动");
    sbA.registry.tzMinutesInput.value = "480";
    dispatch(sbA.registry.tzMinutesInput, "input");
  });

  check("90. 所有 v3 时间/日期字段格式：HH:MM:SS / YYYY-MM-DD", function () {
    const api = requireApi(sbA);
    const payload = api.buildPayload(api.getDraft());
    payload.targets.forEach(function (target) {
      assertMatch(target.date, /^\d{4}-\d{2}-\d{2}$/, "target date 格式不符");
      assertMatch(target.time, /^\d{2}:\d{2}:\d{2}$/, "target time 必须含秒");
    });
    payload.live.refreshSec = 30;
    const frame = api.getFrame();
    frame.runs.forEach(function (run) {
      if (/^\d{2}:\d{2}$/.test(run.text)) {
        throw new Error("run 文本缺少秒：" + run.text);
      }
    });
    assertMatch(api.epochToLocalParts(1792658309, 480).time, /^\d{2}:\d{2}:\d{2}$/, "本地时间格式不符");
  });
  printReport();
}


/* ========================================================================== */
/* 6. 报告                                                                   */
/* ========================================================================== */

function printReport() {
  const passed = results.filter(function (result) { return result.ok; }).length;
  console.log("================ Ulanzi TC002 v3 网页自检 ================");
  results.forEach(function (result) {
    console.log((result.ok ? "PASS " : "FAIL ") + result.name +
      (result.ok ? "" : "\n       -> " + result.err));
  });
  console.log("----------------------------------------------------------");
  console.log(passed + "/" + results.length + " 通过");
  process.exit(passed === results.length ? 0 : 1);
}

main().catch(function (error) {
  console.error("自检脚本异常:", error && error.stack ? error.stack : error);
  process.exit(2);
});
