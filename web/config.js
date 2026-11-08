// Firmware plus configuration: fills the chosen board's configuration template with what the form
// holds and offers it to ESP Web Tools as a blob: manifest. Nothing typed here leaves the browser.
import { fillTemplate } from "./fspatch.mjs";

const form = document.getElementById("config-form");
const button = document.getElementById("config-install");
const status = document.getElementById("config-status");
const encoder = new TextEncoder();
let description = null;
let blobUrls = [];

const field = (name) => form.elements.namedItem(name);

function withdraw(message) {
  blobUrls.forEach((url) => URL.revokeObjectURL(url));
  blobUrls = [];
  button.removeAttribute("manifest");
  button.hidden = true;
  status.textContent = message;
}

function showDeviceFields() {
  const device = description?.devices[field("device").value];
  field("tube").closest("label").hidden = !device?.files.includes("/config/tube.json");
}

function serverJson(rules) {
  const values = {
    mqttUserName: field("mqttUserName").value,
    mqttPassword: field("mqttPassword").value,
    mqttServerUrl: field("mqttServerUrl").value,
    mqttServerPort: Number(field("mqttServerPort").value),
    haDiscovery: field("haDiscovery").checked,
    ssid: field("ssid").value,
    password: field("password").value,
  };
  const ordered = {};
  for (const key of rules.fields) {
    const value = values[key];
    if (value === "") {
      if (rules.required.includes(key)) {
        throw new Error(`${key} is required`);
      }
      continue;
    }
    const maximum = rules.maxBytes[key];
    if (maximum !== undefined && (encoder.encode(value).length > maximum || value.includes("\0"))) {
      throw new Error(`${key} may hold at most ${maximum} UTF-8 bytes`);
    }
    ordered[key] = value;
  }
  if (!Number.isInteger(ordered.mqttServerPort) || ordered.mqttServerPort < 1 || ordered.mqttServerPort > 65535) {
    throw new Error("mqttServerPort must be 1-65535");
  }
  return JSON.stringify(ordered);
}

function caBundle() {
  if (field("ca").value === "default") {
    return description.caBundle;
  }
  const pem = field("caCustom").value.trim();
  if (!pem.includes("-----BEGIN CERTIFICATE-----")) {
    throw new Error("the CA bundle has to be PEM certificates");
  }
  return `${pem}\n`;
}

function contents(device) {
  const files = new Map();
  for (const path of device.files) {
    if (path === "/config/server.json") {
      files.set(path, encoder.encode(serverJson(description.serverJson)));
    } else if (path === "/config/mosq-ca.crt") {
      files.set(path, encoder.encode(caBundle()));
    } else if (path === "/config/tube.json") {
      files.set(path, encoder.encode(JSON.stringify({ tube: Number(field("tube").value) })));
    } else {
      throw new Error(`this page cannot fill ${path}`);
    }
  }
  return files;
}

async function fetchOk(url) {
  const response = await fetch(url);
  if (!response.ok) {
    throw new Error(`${url}: ${response.status}`);
  }
  return response;
}

async function prepare() {
  const env = field("device").value;
  const device = description.devices[env];
  const files = contents(device);
  const template = new Uint8Array(await (await fetchOk(`firmware/${device.image}`)).arrayBuffer());
  const image = fillTemplate(template, device.blockSize, files);
  const manifestUrl = new URL(`manifests/${env}.json`, location.href);
  const manifest = await (await fetchOk(manifestUrl)).json();
  const imageUrl = URL.createObjectURL(new Blob([image]));
  for (const build of manifest.builds) {
    build.parts = build.parts.map((part) => ({ ...part, path: new URL(part.path, manifestUrl).href }));
    build.parts.push({ path: imageUrl, offset: device.offset });
  }
  const manifestBlob = URL.createObjectURL(new Blob([JSON.stringify(manifest)], { type: "application/json" }));
  blobUrls = [imageUrl, manifestBlob];
  button.setAttribute("manifest", manifestBlob);
  button.hidden = false;
  status.textContent = "Ready: connect the board and install.";
}

form.addEventListener("input", () => {
  showDeviceFields();
  withdraw("");
});
form.addEventListener("submit", (event) => {
  event.preventDefault();
  withdraw("Preparing…");
  prepare().catch((error) => withdraw(`Not prepared: ${error.message}`));
});
field("caFile").addEventListener("change", async () => {
  const file = field("caFile").files[0];
  if (file) {
    field("caCustom").value = await file.text();
    field("ca").value = "custom";
    withdraw("");
  }
});

fetchOk("firmware/cansystems-config.json")
  .then((response) => response.json())
  .then((loaded) => {
    description = loaded;
    showDeviceFields();
    form.querySelector("fieldset").disabled = false;
  })
  .catch((error) => withdraw(`Configuration is unavailable: ${error.message}`));
