const cardNameEl = document.getElementById("cardName");
const cardMetaEl = document.getElementById("cardMeta");
const internNameInput = document.getElementById("internName");
const readBtn = document.getElementById("readBtn");
const writeBtn = document.getElementById("writeBtn");
const statusEl = document.getElementById("status");
const modeArduinoBtn = document.getElementById("modeArduino");
const modeNfcBtn = document.getElementById("modeNfc");
const arduinoPanel = document.getElementById("arduinoPanel");
const nfcPanel = document.getElementById("nfcPanel");
const connectBtn = document.getElementById("connectBtn");
const connectionDot = document.getElementById("connectionDot");
const connectionLabel = document.getElementById("connectionLabel");

let isBusy = false;
let mode = "arduino";
let serialPort = null;
let serialReader = null;
let serialReadLoopActive = false;
let lineBuffer = "";
let pendingResponse = null;

function setStatus(message, type = "info") {
  statusEl.textContent = message;
  statusEl.className = `status visible ${type}`;
}

function setBusy(busy) {
  isBusy = busy;
  readBtn.disabled = busy;
  writeBtn.disabled = busy;
}

function setConnection(connected, label = "Not connected") {
  connectionDot.classList.toggle("connected", connected);
  connectionLabel.textContent = label;
  connectBtn.textContent = connected ? "Disconnect" : "Connect Arduino";
}

function decodeRecord(record) {
  if (record.recordType === "text") {
    const decoder = new TextDecoder(record.encoding || "utf-8");
    return decoder.decode(record.data);
  }

  if (record.recordType === "url") {
    const decoder = new TextDecoder();
    return decoder.decode(record.data);
  }

  if (record.recordType === "mime" && record.mediaType === "text/plain") {
    const decoder = new TextDecoder();
    return decoder.decode(record.data);
  }

  return null;
}

function extractNameFromMessage(message) {
  for (const record of message.records) {
    const value = decodeRecord(record);
    if (value && value.trim()) {
      return value.trim();
    }
  }
  return null;
}

function friendlyError(error) {
  switch (error.name) {
    case "NotAllowedError":
      return "Permission denied. Allow access when prompted.";
    case "NotSupportedError":
      return mode === "nfc"
        ? "Web NFC is not available. Use Chrome on Android over HTTPS or localhost."
        : "Web Serial is not supported. Use Chrome or Edge on desktop.";
    case "NotReadableError":
      return "Could not read the tag. Hold the card steady and try again.";
    case "NetworkError":
      return "Lost connection to the tag. Keep the card near the reader and retry.";
    case "AbortError":
      return "Operation cancelled.";
    default:
      return error.message || "Something went wrong. Try again.";
  }
}

function validateName(name) {
  if (!name.trim()) {
    return "Enter an intern name before writing.";
  }
  if (/[\|\r\n\t]/.test(name)) {
    return "Names cannot contain |, tabs, or line breaks.";
  }
  return null;
}

function applyReadResult(name, uid) {
  if (name) {
    cardNameEl.textContent = name;
    internNameInput.value = name;
    cardMetaEl.textContent = uid ? `Card UID: ${uid}` : "Read succeeded";
    setStatus(`Read "${name}" successfully.`, "success");
  } else {
    cardNameEl.textContent = "(empty)";
    cardMetaEl.textContent = uid ? `Card UID: ${uid}` : "Card is blank";
    setStatus("Card found but no name is stored.", "warning");
  }
}

function setMode(nextMode) {
  mode = nextMode;
  const isArduino = mode === "arduino";

  modeArduinoBtn.classList.toggle("active", isArduino);
  modeNfcBtn.classList.toggle("active", !isArduino);
  modeArduinoBtn.setAttribute("aria-selected", String(isArduino));
  modeNfcBtn.setAttribute("aria-selected", String(!isArduino));
  arduinoPanel.hidden = !isArduino;
  nfcPanel.hidden = isArduino;

  if (isArduino) {
    cardMetaEl.textContent = serialPort
      ? "Place a card on the reader, then tap Read or Write"
      : "Connect your Arduino, then tap Read card";
  } else {
    cardMetaEl.textContent = "Tap Read, then hold a card near your phone";
  }

  clearStatus();
}

function clearStatus() {
  statusEl.textContent = "";
  statusEl.className = "status";
}

function handleSerialLine(line) {
  if (line === "READY") {
    setConnection(true, "Arduino ready");
    return;
  }

  if (!pendingResponse) {
    return;
  }

  if (line.startsWith("OK|")) {
    const parts = line.split("|");
    pendingResponse.resolve({
      ok: true,
      uid: parts[1] || "",
      name: parts.slice(2).join("|"),
    });
    pendingResponse = null;
    return;
  }

  if (line.startsWith("ERR|")) {
    pendingResponse.resolve({
      ok: false,
      error: line.slice(4) || "Unknown error",
    });
    pendingResponse = null;
  }
}

async function startSerialReadLoop() {
  if (!serialPort?.readable || serialReadLoopActive) {
    return;
  }

  serialReadLoopActive = true;
  const decoder = new TextDecoder();

  try {
    while (serialPort.readable && serialReadLoopActive) {
      serialReader = serialPort.readable.getReader();

      try {
        while (true) {
          const { value, done } = await serialReader.read();
          if (done) {
            break;
          }

          lineBuffer += decoder.decode(value, { stream: true });
          let newlineIndex = lineBuffer.indexOf("\n");

          while (newlineIndex !== -1) {
            const line = lineBuffer.slice(0, newlineIndex).trim();
            lineBuffer = lineBuffer.slice(newlineIndex + 1);
            if (line) {
              handleSerialLine(line);
            }
            newlineIndex = lineBuffer.indexOf("\n");
          }
        }
      } finally {
        serialReader.releaseLock();
        serialReader = null;
      }
    }
  } catch (error) {
    if (serialReadLoopActive) {
      setConnection(false, "Connection lost");
      setStatus(friendlyError(error), "error");
    }
  } finally {
    serialReadLoopActive = false;
  }
}

async function sendSerialLine(line) {
  if (!serialPort?.writable) {
    throw new Error("Arduino is not connected.");
  }

  const writer = serialPort.writable.getWriter();
  try {
    await writer.write(new TextEncoder().encode(`${line}\n`));
  } finally {
    writer.releaseLock();
  }
}

function waitForSerialResponse(timeoutMs = 15000) {
  return new Promise((resolve, reject) => {
    const timer = setTimeout(() => {
      pendingResponse = null;
      reject(new Error("Timed out waiting for the Arduino. Is a card on the reader?"));
    }, timeoutMs);

    pendingResponse = {
      resolve: (result) => {
        clearTimeout(timer);
        resolve(result);
      },
    };
  });
}

async function connectArduino() {
  if (serialPort) {
    await disconnectArduino();
    return;
  }

  if (!("serial" in navigator)) {
    setStatus("Web Serial is not supported. Use Chrome or Edge on desktop.", "warning");
    return;
  }

  try {
    serialPort = await navigator.serial.requestPort();
    await serialPort.open({ baudRate: 115200 });
    setConnection(true, "Connecting…");
    setStatus("Connected. Waiting for Arduino…", "info");
    lineBuffer = "";
    startSerialReadLoop();
  } catch (error) {
    serialPort = null;
    setConnection(false);
    setStatus(friendlyError(error), "error");
  }
}

async function disconnectArduino() {
  serialReadLoopActive = false;

  if (serialReader) {
    await serialReader.cancel().catch(() => {});
  }

  if (serialPort) {
    await serialPort.close().catch(() => {});
  }

  serialPort = null;
  pendingResponse = null;
  lineBuffer = "";
  setConnection(false);
  setStatus("Arduino disconnected.", "info");
}

async function readViaArduino() {
  if (!serialPort) {
    setStatus("Connect your Arduino first.", "warning");
    return;
  }

  setBusy(true);
  setStatus("Place a card on the reader…", "info");
  cardMetaEl.textContent = "Waiting for card…";

  try {
    await sendSerialLine("READ");
    const result = await waitForSerialResponse();

    if (!result.ok) {
      throw new Error(result.error);
    }

    applyReadResult(result.name, result.uid);
  } catch (error) {
    setStatus(error.message || friendlyError(error), "error");
    cardMetaEl.textContent = "Read failed";
  } finally {
    setBusy(false);
  }
}

async function writeViaArduino() {
  if (!serialPort) {
    setStatus("Connect your Arduino first.", "warning");
    return;
  }

  const name = internNameInput.value.trim();
  const validationError = validateName(name);
  if (validationError) {
    setStatus(validationError, "warning");
    internNameInput.focus();
    return;
  }

  setBusy(true);
  setStatus(`Place a card on the reader to write "${name}"…`, "info");

  try {
    await sendSerialLine(`WRITE|${name}`);
    const result = await waitForSerialResponse();

    if (!result.ok) {
      throw new Error(result.error);
    }

    cardNameEl.textContent = name;
    cardMetaEl.textContent = result.uid ? `Card UID: ${result.uid}` : "Write succeeded";
    setStatus(`Wrote "${name}" to the card.`, "success");
  } catch (error) {
    setStatus(error.message || friendlyError(error), "error");
  } finally {
    setBusy(false);
  }
}

async function readViaNfc() {
  if (!("NDEFReader" in window)) {
    setStatus("Web NFC is not supported in this browser.", "warning");
    return;
  }

  setBusy(true);
  setStatus("Hold an intern card near your phone…", "info");
  cardMetaEl.textContent = "Scanning…";

  const ndef = new NDEFReader();
  const controller = new AbortController();
  const timeout = setTimeout(() => controller.abort(), 30000);

  try {
    await ndef.scan({ signal: controller.signal });

    ndef.addEventListener("reading", ({ serialNumber, message }) => {
      clearTimeout(timeout);

      const name = extractNameFromMessage(message);
      if (name) {
        applyReadResult(name, serialNumber);
      } else {
        cardNameEl.textContent = "(empty or unsupported format)";
        cardMetaEl.textContent = `Tag ID: ${serialNumber || "unknown"}`;
        setStatus("Tag found, but no readable text name was stored.", "warning");
      }

      setBusy(false);
    });

    ndef.addEventListener("readingerror", () => {
      clearTimeout(timeout);
      setStatus("Failed to read the tag. Try again.", "error");
      cardMetaEl.textContent = "Read failed";
      setBusy(false);
    });
  } catch (error) {
    clearTimeout(timeout);
    setStatus(friendlyError(error), "error");
    cardMetaEl.textContent = "Tap Read, then hold a card near your device";
    setBusy(false);
  }
}

async function writeViaNfc() {
  if (!("NDEFReader" in window)) {
    setStatus("Web NFC is not supported in this browser.", "warning");
    return;
  }

  const name = internNameInput.value.trim();
  const validationError = validateName(name);
  if (validationError) {
    setStatus(validationError, "warning");
    internNameInput.focus();
    return;
  }

  setBusy(true);
  setStatus(`Hold a writable card near your phone to write "${name}"…`, "info");

  const ndef = new NDEFReader();

  try {
    await ndef.write({
      records: [{ recordType: "text", data: name }],
      overwrite: true,
    });

    cardNameEl.textContent = name;
    cardMetaEl.textContent = "Last write succeeded";
    setStatus(`Wrote "${name}" to the card.`, "success");
  } catch (error) {
    setStatus(friendlyError(error), "error");
  } finally {
    setBusy(false);
  }
}

async function readCard() {
  if (isBusy) return;
  if (mode === "arduino") {
    await readViaArduino();
  } else {
    await readViaNfc();
  }
}

async function writeCard() {
  if (isBusy) return;
  if (mode === "arduino") {
    await writeViaArduino();
  } else {
    await writeViaNfc();
  }
}

modeArduinoBtn.addEventListener("click", () => setMode("arduino"));
modeNfcBtn.addEventListener("click", () => setMode("nfc"));
connectBtn.addEventListener("click", connectArduino);
readBtn.addEventListener("click", readCard);
writeBtn.addEventListener("click", writeCard);

setMode("arduino");
