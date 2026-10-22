#!/usr/bin/env node
/**
 * Attach to an already-running headless Edge/Chrome via CDP, load the mock
 * device page, collect console/page errors and layout facts, save a full-page
 * PNG, then exercise the Save button.
 *
 * Uses only Node 24 built-ins (global fetch + WebSocket); no npm packages.
 *
 * Example:
 *   node cdp_probe.mjs --port 9223 --url http://127.0.0.1:18080/ \
 *     --screenshot mobile-full.png --out cdp-report.json
 */

import fs from "node:fs/promises";
import path from "node:path";
import process from "node:process";

function arg(name, fallback) {
  const i = process.argv.indexOf(`--${name}`);
  return i >= 0 && process.argv[i + 1] ? process.argv[i + 1] : fallback;
}
function hasFlag(name) {
  return process.argv.includes(`--${name}`);
}

const cdpPort = Number(arg("port", "9223"));
const targetUrl = arg("url", "http://127.0.0.1:18080/");
const screenshotPath = path.resolve(arg("screenshot", "mobile-full.png"));
const viewportScreenshotArg = arg("viewport-screenshot", "");
const viewportScreenshotPath = viewportScreenshotArg ? path.resolve(viewportScreenshotArg) : "";
const scrollScreenshotArg = arg("scroll-screenshot", "");
const scrollScreenshotPath = scrollScreenshotArg ? path.resolve(scrollScreenshotArg) : "";
const outPath = path.resolve(arg("out", "cdp-report.json"));
const clickSave = !hasFlag("no-save");
const timeoutMs = Number(arg("timeout", "15000"));
const emulatedWidth = Number(arg("width", "390"));
const emulatedHeight = Number(arg("height", "1200"));
const mobileEmulation = !hasFlag("no-mobile-emulation");

const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));

async function getJson(url, attempts = 40) {
  let lastErr;
  for (let i = 0; i < attempts; i++) {
    try {
      const res = await fetch(url);
      if (res.ok) return await res.json();
      lastErr = new Error(`${res.status} ${res.statusText}`);
    } catch (err) {
      lastErr = err;
    }
    await sleep(250);
  }
  throw new Error(`Cannot fetch ${url}: ${lastErr}`);
}

const versionInfo = await getJson(`http://127.0.0.1:${cdpPort}/json/version`);
const targets = await getJson(`http://127.0.0.1:${cdpPort}/json/list`);
const target = targets.find((t) => t.type === "page");
if (!target) throw new Error("No page target found");

const ws = new WebSocket(target.webSocketDebuggerUrl);
await new Promise((resolve, reject) => {
  const timer = setTimeout(() => reject(new Error("WebSocket open timeout")), 10000);
  ws.addEventListener("open", () => {
    clearTimeout(timer);
    resolve();
  });
  ws.addEventListener("error", (event) => {
    clearTimeout(timer);
    reject(new Error(`WebSocket error: ${event?.message || "unknown"}`));
  });
});

let nextId = 1;
const pending = new Map();
const consoleEvents = [];
const exceptions = [];
const logEntries = [];
const eventWaiters = [];

ws.addEventListener("message", (event) => {
  let msg;
  try {
    msg = JSON.parse(typeof event.data === "string" ? event.data : String(event.data));
  } catch {
    return;
  }
  if (msg.id) {
    const waiter = pending.get(msg.id);
    if (waiter) {
      pending.delete(msg.id);
      if (msg.error) waiter.reject(new Error(`${msg.error.message} (${msg.error.code})`));
      else waiter.resolve(msg.result);
    }
    return;
  }
  if (msg.method === "Runtime.consoleAPICalled") {
    const args = (msg.params.args || []).map((a) => {
      if (Object.prototype.hasOwnProperty.call(a, "value")) return String(a.value);
      return a.description || a.type || "";
    });
    consoleEvents.push({
      type: msg.params.type,
      args,
      timestamp: msg.params.timestamp,
    });
  } else if (msg.method === "Runtime.exceptionThrown") {
    const d = msg.params.exceptionDetails || {};
    exceptions.push({
      text: d.text || "",
      description: d.exception?.description || d.exception?.value || "",
      url: d.url || "",
      lineNumber: d.lineNumber,
      columnNumber: d.columnNumber,
    });
  } else if (msg.method === "Log.entryAdded") {
    const e = msg.params.entry || {};
    logEntries.push({
      source: e.source,
      level: e.level,
      text: e.text,
      url: e.url || "",
      lineNumber: e.lineNumber,
    });
  }
  for (let i = eventWaiters.length - 1; i >= 0; i--) {
    if (eventWaiters[i].method === msg.method) {
      eventWaiters[i].resolve(msg.params);
      eventWaiters.splice(i, 1);
    }
  }
});

function send(method, params = {}, timeout = 20000) {
  const id = nextId++;
  return new Promise((resolve, reject) => {
    const timer = setTimeout(() => {
      pending.delete(id);
      reject(new Error(`CDP timeout: ${method}`));
    }, timeout);
    pending.set(id, {
      resolve: (value) => {
        clearTimeout(timer);
        resolve(value);
      },
      reject: (err) => {
        clearTimeout(timer);
        reject(err);
      },
    });
    ws.send(JSON.stringify({ id, method, params }));
  });
}

async function evaluate(expression, awaitPromise = true) {
  const r = await send("Runtime.evaluate", {
    expression,
    awaitPromise,
    returnByValue: true,
    userGesture: true,
  });
  if (r.exceptionDetails) {
    throw new Error(
      `Runtime.evaluate exception: ${r.exceptionDetails.exception?.description || r.exceptionDetails.text}`,
    );
  }
  return r.result?.value;
}

const scrollSamples = [];
async function waitForReady() {
  const started = Date.now();
  const deadline = started + timeoutMs;
  let last;
  let readyAt = null;
  while (Date.now() < deadline) {
    last = await evaluate(`(() => {
      const info = document.getElementById("deviceInfo");
      const status = document.getElementById("statusLog");
      const lastStatus = status && status.lastElementChild;
      const sr = lastStatus ? lastStatus.getBoundingClientRect() : null;
      return {
        readyState: document.readyState,
        rows: document.querySelectorAll(".point-row").length,
        deviceDisplay: info ? getComputedStyle(info).display : null,
        statusChildren: status ? status.children.length : -1,
        title: document.title,
        scrollX: window.scrollX,
        scrollY: window.scrollY,
        maxScrollY: Math.max(0, document.documentElement.scrollHeight - window.innerHeight),
        innerHeight: window.innerHeight,
        statusRect: sr ? { top: +sr.top.toFixed(1), bottom: +sr.bottom.toFixed(1) } : null,
      };
    })()`);
    const isReady =
      last.readyState === "complete" &&
      last.rows >= 1 &&
      last.deviceDisplay === "block" &&
      last.statusChildren >= 1;
    scrollSamples.push({
      tMs: Date.now() - started,
      scrollY: last.scrollY,
      maxScrollY: last.maxScrollY,
      ready: isReady,
    });
    if (isReady && readyAt === null) {
      readyAt = Date.now() - started;
      await sleep(900);
      last = await evaluate(`(() => {
        const status = document.getElementById("statusLog");
        const lastStatus = status && status.lastElementChild;
        const sr = lastStatus ? lastStatus.getBoundingClientRect() : null;
        return {
          scrollX: window.scrollX,
          scrollY: window.scrollY,
          maxScrollY: Math.max(0, document.documentElement.scrollHeight - window.innerHeight),
          innerHeight: window.innerHeight,
          statusRect: sr ? { top: +sr.top.toFixed(1), bottom: +sr.bottom.toFixed(1) } : null,
        };
      })()`);
      scrollSamples.push({
        tMs: Date.now() - started,
        scrollY: last.scrollY,
        maxScrollY: last.maxScrollY,
        ready: true,
        settled: true,
      });
      return { loaded: true, state: last, readyAt, scrollSamples };
    }
    await sleep(150);
  }
  return { loaded: false, state: last, readyAt, scrollSamples };
}

const metricsExpression = `(() => {
  const r = (el) => {
    if (!el) return null;
    const b = el.getBoundingClientRect();
    return {
      x: +b.x.toFixed(1), y: +b.y.toFixed(1),
      left: +b.left.toFixed(1), top: +b.top.toFixed(1),
      width: +b.width.toFixed(1), height: +b.height.toFixed(1),
      right: +b.right.toFixed(1), bottom: +b.bottom.toFixed(1),
    };
  };
  const text = (el) => el ? (el.innerText || el.textContent || "").replace(/\\s+/g, " ").trim() : null;
  const canvas = document.getElementById("led");
  const wrap = canvas && canvas.parentElement;
  const cRect = r(canvas);
  const wRect = r(wrap);
  const save = document.getElementById("saveBtn");
  const deviceInfo = document.getElementById("deviceInfo");
  const allButtons = [...document.querySelectorAll("button")].map((b) => ({
    id: b.id || null,
    text: (b.textContent || "").trim(),
    rect: r(b),
    disabled: b.disabled,
  }));
  const controls = [...document.querySelectorAll("button,input,select")].map((el) => ({
    tag: el.tagName.toLowerCase(),
    id: el.id || null,
    type: el.type || null,
    rect: r(el),
    disabled: !!el.disabled,
    value: el.type === "color" || el.type === "text" || el.type === "time" || el.type === "number" ? el.value : undefined,
  }));
  const outsideHorizontally = controls.filter((c) =>
    c.rect && (c.rect.left < -0.5 || c.rect.right > window.innerWidth + 0.5)
  );
  let pixels = { width: 0, height: 0, green: 0, red: 0, blue: 0, lit: 0, nonBackground: 0 };
  if (canvas) {
    const ctx = canvas.getContext("2d");
    const img = ctx.getImageData(0, 0, canvas.width, canvas.height).data;
    pixels.width = canvas.width;
    pixels.height = canvas.height;
    for (let i = 0; i < img.length; i += 4) {
      const rr = img[i], gg = img[i + 1], bb = img[i + 2];
      if (rr > 8 || gg > 8 || bb > 12) pixels.nonBackground++;
      if (Math.max(rr, gg, bb) > 100) pixels.lit++;
      if (gg > rr + 25 && gg > bb + 25) pixels.green++;
      if (rr > gg + 25 && rr > bb + 25) pixels.red++;
      if (bb > rr + 25 && bb > gg + 25) pixels.blue++;
    }
  }
  const errorsInVisibleText = /Uncaught|TypeError|ReferenceError|SyntaxError|未捕获/.test(document.body.innerText);
  const deviceLines = deviceInfo
    ? [...deviceInfo.querySelectorAll(".dline")].map((line) => text(line))
    : [];
  return {
    title: document.title,
    readyState: document.readyState,
    viewport: {
      innerWidth: window.innerWidth,
      innerHeight: window.innerHeight,
      devicePixelRatio: window.devicePixelRatio,
      visualViewportWidth: window.visualViewport ? +window.visualViewport.width.toFixed(1) : null,
      scrollX: window.scrollX,
      scrollY: window.scrollY,
    },
    document: {
      clientWidth: document.documentElement.clientWidth,
      scrollWidth: document.documentElement.scrollWidth,
      scrollHeight: document.documentElement.scrollHeight,
      bodyScrollWidth: document.body.scrollWidth,
      bodyScrollHeight: document.body.scrollHeight,
      horizontalOverflow: document.documentElement.scrollWidth > window.innerWidth + 0.5 ||
        document.body.scrollWidth > window.innerWidth + 0.5,
    },
    mainRect: r(document.querySelector("main")),
    led: {
      canvasRect: cRect,
      wrapRect: wRect,
      wrapClientWidth: wrap ? wrap.clientWidth : null,
      wrapScrollWidth: wrap ? wrap.scrollWidth : null,
      canvasWidthAttr: canvas ? canvas.width : null,
      canvasHeightAttr: canvas ? canvas.height : null,
      canvasStyle: canvas ? { width: canvas.style.width, height: canvas.style.height } : null,
      fullyInsideWrap: !!(cRect && wRect) &&
        cRect.left >= wRect.left - 0.5 && cRect.right <= wRect.right + 0.5 &&
        cRect.top >= wRect.top - 0.5 && cRect.bottom <= wRect.bottom + 0.5,
      aspectOk: !!(cRect) && Math.abs(cRect.width / cRect.height - 52 / 16) < 0.02,
      pixels,
    },
    pointRows: document.querySelectorAll(".point-row").length,
    pointTimes: [...document.querySelectorAll(".point-row input[type=time]")].map((i) => i.value),
    pointSwitches: [...document.querySelectorAll(".point-row .switch input")].map((i) => i.checked),
    saveButton: save ? {
      exists: true,
      text: (save.textContent || "").trim(),
      disabled: save.disabled,
      rect: r(save),
      fullyVisibleHorizontally: r(save).left >= -0.5 && r(save).right <= window.innerWidth + 0.5,
    } : { exists: false },
    allButtons,
    controls,
    controlsOutsideViewportHorizontally: outsideHorizontally.map((c) => ({ tag: c.tag, id: c.id, rect: c.rect })),
    deviceInfo: {
      display: deviceInfo ? getComputedStyle(deviceInfo).display : null,
      className: deviceInfo ? deviceInfo.className : null,
      text: text(deviceInfo),
      lines: deviceLines,
    },
    statusTexts: [...document.querySelectorAll("#statusLog .status-item")].map((item) => text(item)),
    stageText: text(document.getElementById("stageText")),
    frame: (typeof computeFrame === "function") ? computeFrame() : null,
    configValues: {
      base: document.getElementById("baseInput")?.value,
      tz: document.getElementById("tzInput")?.value,
      warn: document.getElementById("warnInput")?.value,
      countdownColor: document.getElementById("colorCountdown")?.value,
      warningColor: document.getElementById("colorWarning")?.value,
      countupColor: document.getElementById("colorCountup")?.value,
    },
    errorsInVisibleText,
    ledCanvasPresent: !!canvas,
    pointListPresent: !!document.getElementById("pointList"),
    addPointButton: (() => {
      const b = document.getElementById("addPointBtn");
      return b ? { exists: true, text: (b.textContent || "").trim(), rect: r(b), disabled: b.disabled } : { exists: false };
    })(),
  };
})()`;

let result = null;
try {
  await send("Page.enable");
  await send("Runtime.enable");
  await send("Log.enable");
  await send("Network.enable").catch(() => null);

  // On Windows headless Chromium/Edge clamps --window-size below roughly
  // 500 px wide (a 390 px screenshot is just a crop of a ~492 px layout).
  // Emulation is what actually makes the page see a 390 px CSS viewport.
  await send("Emulation.setDeviceMetricsOverride", {
    width: emulatedWidth,
    height: emulatedHeight,
    deviceScaleFactor: 1,
    mobile: mobileEmulation,
    screenWidth: emulatedWidth,
    screenHeight: emulatedHeight,
    screenOrientation: { type: "portraitPrimary", angle: 0 },
  });
  await send("Emulation.setTouchEmulationEnabled", {
    enabled: mobileEmulation,
    maxTouchPoints: mobileEmulation ? 5 : 0,
  }).catch(() => null);

  await send("Page.navigate", { url: targetUrl });
  const ready = await waitForReady();

  // Capture the page as it naturally settles after load (the status log calls
  // scrollIntoView) before top-aligning it for the normal screenshots.
  let scrollScreenshot = null;
  if (scrollScreenshotPath) {
    try {
      const capture = await send("Page.captureScreenshot", {
        format: "png",
        fromSurface: true,
        captureBeyondViewport: false,
      });
      await fs.mkdir(path.dirname(scrollScreenshotPath), { recursive: true });
      await fs.writeFile(scrollScreenshotPath, Buffer.from(capture.data, "base64"));
      scrollScreenshot = { path: scrollScreenshotPath };
    } catch (err) {
      scrollScreenshot = { path: scrollScreenshotPath, error: String(err) };
    }
  }

  // The page's own status log calls scrollIntoView(), so a screenshot taken
  // right after load starts part-way down the page. Top-align for a clean
  // viewport/full-page capture.
  await evaluate(`(() => {
    window.scrollTo(0, 0);
    return { scrollY: window.scrollY, scrollX: window.scrollX };
  })()`);
  await sleep(350);
  const scrollPosition = await evaluate(`({ x: window.scrollX, y: window.scrollY })`);

  const metrics = await evaluate(metricsExpression);

  let viewportScreenshot = null;
  if (viewportScreenshotPath) {
    try {
      const capture = await send("Page.captureScreenshot", {
        format: "png",
        fromSurface: true,
        captureBeyondViewport: false,
      });
      await fs.mkdir(path.dirname(viewportScreenshotPath), { recursive: true });
      await fs.writeFile(viewportScreenshotPath, Buffer.from(capture.data, "base64"));
      viewportScreenshot = { path: viewportScreenshotPath };
    } catch (err) {
      viewportScreenshot = { path: viewportScreenshotPath, error: String(err) };
    }
  }

  // Full-page screenshot (the mandated viewport shots are taken separately
  // with Edge's --screenshot command).
  let fullPageScreenshot = null;
  try {
    const layout = await send("Page.getLayoutMetrics");
    const content = layout.cssContentSize || layout.contentSize;
    const width = Math.ceil(content.width);
    const height = Math.ceil(content.height);
    const capture = await send("Page.captureScreenshot", {
      format: "png",
      captureBeyondViewport: true,
      fromSurface: true,
      clip: { x: 0, y: 0, width, height, scale: 1 },
    });
    await fs.mkdir(path.dirname(screenshotPath), { recursive: true });
    await fs.writeFile(screenshotPath, Buffer.from(capture.data, "base64"));
    fullPageScreenshot = { path: screenshotPath, width, height };
  } catch (err) {
    fullPageScreenshot = { path: screenshotPath, error: String(err) };
  }

  let save = null;
  if (clickSave) {
    const clickResult = await evaluate(`(() => {
      const b = document.getElementById("saveBtn");
      if (!b) return { clicked: false, reason: "missing" };
      b.scrollIntoView({ block: "center" });
      b.click();
      return { clicked: true };
    })()`);
    const deadline = Date.now() + 10000;
    let saveStatus = [];
    while (Date.now() < deadline) {
      saveStatus = await evaluate(`[
        ...document.querySelectorAll("#statusLog .status-item")
      ].map((x) => (x.innerText || "").replace(/\\s+/g, " ").trim())`);
      if (saveStatus.some((s) => s.includes("配置已保存") || s.includes("保存失败") || s.includes("保存中止"))) break;
      await sleep(200);
    }
    await sleep(500);
    save = {
      clickResult,
      statuses: saveStatus,
      busyButtonDisabled: await evaluate(`document.getElementById("saveBtn").disabled`),
      finalStatuses: await evaluate(`[
        ...document.querySelectorAll("#statusLog .status-item")
      ].map((x) => (x.innerText || "").replace(/\\s+/g, " ").trim())`),
      afterMetrics: await evaluate(metricsExpression),
    };
  }

  result = {
    ok: true,
    browser: {
      product: versionInfo["Browser"],
      protocolVersion: versionInfo["Protocol-Version"],
      userAgent: versionInfo["User-Agent"],
      cdpPort,
    },
    attachedTarget: { id: target.id, url: target.url, title: target.title },
    loaded: ready.loaded,
    loadState: ready.state,
    targetUrl,
    emulation: {
      width: emulatedWidth,
      height: emulatedHeight,
      mobile: mobileEmulation,
      deviceScaleFactor: 1,
    },
    scrollPosition,
    autoScroll: {
      readyAtMs: ready.readyAt,
      atReady: ready.state,
      samples: ready.scrollSamples,
    },
    consoleEvents,
    exceptions,
    logEntries,
    metrics,
    scrollScreenshot,
    viewportScreenshot,
    fullPageScreenshot,
    save,
  };
} catch (err) {
  result = {
    ok: false,
    error: String(err && err.stack ? err.stack : err),
    browser: {
      product: versionInfo["Browser"],
      userAgent: versionInfo["User-Agent"],
    },
    targetUrl,
    consoleEvents,
    exceptions,
    logEntries,
  };
} finally {
  await fs.mkdir(path.dirname(outPath), { recursive: true });
  await fs.writeFile(outPath, JSON.stringify(result, null, 2), "utf8");
  try {
    ws.close();
  } catch {
    /* ignore */
  }
}

if (!result.ok) {
  console.error(JSON.stringify(result, null, 2));
  process.exitCode = 1;
} else {
  const m = result.metrics;
  console.log(JSON.stringify({
    ok: true,
    browser: result.browser.product,
    loaded: result.loaded,
    emulation: result.emulation,
    viewport: m.viewport,
    autoScroll: {
      readyAtMs: result.autoScroll.readyAtMs,
      finalScrollY: result.autoScroll.samples.at(-1)?.scrollY,
      finalMaxScrollY: result.autoScroll.samples.at(-1)?.maxScrollY,
      samples: result.autoScroll.samples,
    },
    document: m.document,
    pointRows: m.pointRows,
    pointTimes: m.pointTimes,
    saveButton: m.saveButton,
    led: m.led,
    deviceLines: m.deviceInfo.lines,
    statusTexts: m.statusTexts,
    stageText: m.stageText,
    frame: m.frame,
    consoleEvents: result.consoleEvents.length,
    exceptions: result.exceptions.length,
    logEntries: result.logEntries.filter((e) => e.level === "error").length,
    scrollScreenshot: result.scrollScreenshot,
    viewportScreenshot: result.viewportScreenshot,
    fullPageScreenshot: result.fullPageScreenshot,
    saveStatuses: result.save?.statuses || null,
    report: outPath,
  }, null, 2));
}
