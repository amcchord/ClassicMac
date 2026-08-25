import RFB from "./novnc/core/rfb.js";

const screen = document.getElementById("screen");
const status = document.getElementById("status");
const machineName = document.getElementById("machine-name");
const message = document.getElementById("message");
const messageTitle = document.getElementById("message-title");
const messageDetail = document.getElementById("message-detail");
const captureButton = document.getElementById("capture");
const scaleButton = document.getElementById("scale");
const fullScreenButton = document.getElementById("fullscreen");
const escapeButton = document.getElementById("escape");
const modifierButtons = [...document.querySelectorAll("button.modifier")];

let configuration;
let rfb;
let reconnectTimer;
let relativePointer = false;
let scaleViewport = true;
let pageClosing = false;
let connected = false;

function setMessage(title, detail, visible = true) {
  messageTitle.textContent = title;
  messageDetail.textContent = detail;
  message.hidden = !visible;
}

function setStatus(text, state = "waiting") {
  status.textContent = text;
  status.dataset.state = state;
  document.body.dataset.status = state;
}

function currentCanvas() {
  return screen.querySelector("canvas");
}

function releaseToolbarModifiers() {
  for (const button of modifierButtons) {
    if (button.classList.contains("active") && rfb) {
      rfb.sendKey(Number(button.dataset.keysym), button.dataset.code, false);
    }
    button.classList.remove("active");
    button.setAttribute("aria-pressed", "false");
  }
}

function updatePointerControls() {
  captureButton.hidden = !relativePointer;
  const captured = document.pointerLockElement === currentCanvas();
  captureButton.textContent = captured ? "Release game mouse" : "Capture game mouse";
  captureButton.classList.toggle("active", captured);
  captureButton.setAttribute("aria-pressed", captured ? "true" : "false");
  if (relativePointer && !captured) {
    setStatus("Mouse capture needed", "attention");
  } else if (relativePointer && captured) {
    setStatus("Mouse captured", "connected");
  } else if (connected) {
    setStatus("Connected", "connected");
  }
}

function requestGameMouse() {
  const canvas = currentCanvas();
  if (!canvas || !relativePointer) return;
  if (document.pointerLockElement === canvas) {
    document.exitPointerLock();
  } else {
    canvas.requestPointerLock();
  }
}

function connect() {
  clearTimeout(reconnectTimer);
  if (!configuration || pageClosing) return;

  setStatus("Connecting…");
  setMessage("Connecting to the Mac…", "The display will appear automatically.");
  connected = false;
  relativePointer = false;
  updatePointerControls();

  try {
    rfb = new RFB(screen, configuration.webSocketURL, { shared: true });
    rfb.scaleViewport = scaleViewport;
    rfb.clipViewport = false;
    rfb.resizeSession = false;
    rfb.focusOnClick = true;
    rfb.qualityLevel = 8;
    rfb.compressionLevel = 2;
    rfb.classicInputHelpers = configuration.classicInputHelpers;

    rfb.addEventListener("connect", () => {
      connected = true;
      setStatus("Connected", "connected");
      setMessage("", "", false);
      rfb.focus();
    });

    rfb.addEventListener("desktopname", (event) => {
      if (!configuration.machineName && event.detail.name) {
        machineName.textContent = event.detail.name;
      }
    });

    rfb.addEventListener("pointertypechange", (event) => {
      relativePointer = event.detail.relative;
      if (!relativePointer && document.pointerLockElement) {
        document.exitPointerLock();
      }
      updatePointerControls();
    });

    rfb.addEventListener("disconnect", () => {
      releaseToolbarModifiers();
      if (document.pointerLockElement) {
        document.exitPointerLock();
      }
      rfb = undefined;
      connected = false;
      relativePointer = false;
      updatePointerControls();
      if (!pageClosing) {
        setStatus("Waiting for the Mac…");
        setMessage(
          "The Mac is reconnecting…",
          "Keep this tab open during a restart."
        );
        reconnectTimer = setTimeout(connect, 1000);
      }
    });

    rfb.addEventListener("securityfailure", () => {
      connected = false;
      setStatus("Display rejected", "error");
      setMessage(
        "The local display was rejected",
        "Close this tab and reopen the screen from ClassicMac."
      );
    });
  } catch (error) {
    setStatus("Waiting for the Mac…");
    setMessage("The Mac is still starting…", "Trying the display again shortly.");
    reconnectTimer = setTimeout(connect, 1000);
  }
}

captureButton.addEventListener("click", requestGameMouse);

screen.addEventListener("mousedown", () => {
  if (relativePointer && document.pointerLockElement !== currentCanvas()) {
    currentCanvas()?.requestPointerLock();
  }
}, true);

document.addEventListener("pointerlockchange", updatePointerControls);

scaleButton.addEventListener("click", () => {
  scaleViewport = !scaleViewport;
  scaleButton.setAttribute("aria-pressed", String(scaleViewport));
  scaleButton.textContent = scaleViewport ? "Fit" : "Actual Size";
  if (rfb) {
    rfb.scaleViewport = scaleViewport;
    rfb.clipViewport = !scaleViewport;
  }
});

fullScreenButton.addEventListener("click", async () => {
  if (document.fullscreenElement) {
    await document.exitFullscreen();
  } else {
    await document.documentElement.requestFullscreen();
  }
});

document.addEventListener("fullscreenchange", () => {
  fullScreenButton.textContent = document.fullscreenElement ? "Exit Fullscreen" : "Fullscreen";
});

escapeButton.addEventListener("click", () => {
  rfb?.sendKey(0xff1b, "Escape");
  rfb?.focus();
});

for (const button of modifierButtons) {
  button.setAttribute("aria-pressed", "false");
  button.addEventListener("click", () => {
    if (!rfb) return;
    const down = !button.classList.contains("active");
    rfb.sendKey(Number(button.dataset.keysym), button.dataset.code, down);
    button.classList.toggle("active", down);
    button.setAttribute("aria-pressed", String(down));
    rfb.focus();
  });
}

window.addEventListener("blur", releaseToolbarModifiers);
window.addEventListener("beforeunload", () => {
  pageClosing = true;
  clearTimeout(reconnectTimer);
  releaseToolbarModifiers();
  rfb?.disconnect();
});

try {
  configuration = {
    machineName: document.querySelector('meta[name="classicmac-machine-name"]')?.content,
    webSocketURL: document.querySelector('meta[name="classicmac-websocket-url"]')?.content,
    classicInputHelpers:
      document.querySelector('meta[name="classicmac-input-helpers"]')?.content === "true",
  };
  if (!configuration.webSocketURL) throw new Error("The display URL is missing");
  machineName.textContent = configuration.machineName || "ClassicMac";
  document.title = `${machineName.textContent} — ClassicMac`;
  connect();
} catch (error) {
  setStatus("Display unavailable", "error");
  setMessage(
    "ClassicMac is no longer serving this display",
    "Reopen the screen from the ClassicMac app."
  );
}
