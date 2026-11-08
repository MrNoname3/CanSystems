// Fills the placeholders of a configuration template (see installer.py) with a board's own files.
// A placeholder owns exactly one LittleFS block. Its content goes into that block, and its size into
// every metadata commit whose CTZ struct names the block, whose CRC is then recomputed: LittleFS v2
// checksums metadata commits, not data blocks.

const TAG_CTZ_STRUCT = 0x202;
const TAG_FCRC = 0x5ff;
const TAG_INVALID = 0x80000000;
const TAG_SIZE_DELETED = 0x3ff;

const CRC_TABLE = Uint32Array.from({ length: 256 }, (_, index) => {
  let value = index;
  for (let bit = 0; bit < 8; bit++) {
    value = value & 1 ? 0xedb88320 ^ (value >>> 1) : value >>> 1;
  }
  return value;
});

// LittleFS's CRC-32: reflected, seeded with all ones, and no final inversion.
function crc32(bytes) {
  let crc = 0xffffffff;
  for (const byte of bytes) {
    crc = CRC_TABLE[(crc ^ byte) & 0xff] ^ (crc >>> 8);
  }
  return crc >>> 0;
}

export function placeholderMarker(path) {
  return new TextEncoder().encode(`CANSYSTEMS-TEMPLATE ${path}\n`);
}

// The valid commits of one metadata block: where each starts, where its CRC tag sits, and the
// CTZ structs it carries.
function commits(view, base, blockSize) {
  const found = [];
  let offset = 4;                                   // after the revision count
  let start = 0;
  let previous = 0xffffffff;
  let structs = [];
  while (offset + 4 <= blockSize) {
    const tag = (view.getUint32(base + offset, false) ^ previous) >>> 0;
    if (tag & TAG_INVALID) {
      break;
    }
    const type = (tag >>> 20) & 0x7ff;
    const size = tag & 0x3ff;
    const length = size === TAG_SIZE_DELETED ? 0 : size;
    if ((type & 0x700) === 0x500 && type !== TAG_FCRC) {
      const stored = view.getUint32(base + offset + 4, true);
      if (crc32(new Uint8Array(view.buffer, view.byteOffset + base + start, offset + 4 - start)) !== stored) {
        break;                                      // a torn or never-written commit ends the log
      }
      found.push({ start, crcAt: offset, structs });
      start = offset + 4 + length;
      structs = [];
      previous = (tag ^ ((type & 1) << 31)) >>> 0;
    } else {
      if (type === TAG_CTZ_STRUCT && length === 8) {
        structs.push({ at: offset + 4, head: view.getUint32(base + offset + 4, true),
                       size: view.getUint32(base + offset + 8, true) });
      }
      previous = tag;
    }
    offset += 4 + length;
  }
  return found;
}

function startsWith(image, at, prefix) {
  return prefix.every((byte, index) => image[at + index] === byte);
}

// Returns a copy of `image` with each placeholder named in `files` (a Map of path to bytes) filled.
export function fillTemplate(image, blockSize, files) {
  const filled = new Uint8Array(image);
  const view = new DataView(filled.buffer);
  const blockCount = filled.length / blockSize;
  const heads = new Map();
  for (const [path, content] of files) {
    if (content.length === 0 || content.length > blockSize) {
      throw new Error(`${path} has to be 1 to ${blockSize} bytes, is ${content.length}`);
    }
    const marker = placeholderMarker(path);
    const blocks = [];
    for (let block = 0; block < blockCount; block++) {
      if (startsWith(filled, block * blockSize, marker)) {
        blocks.push(block);
      }
    }
    if (blocks.length !== 1) {
      throw new Error(`the template holds ${blocks.length} placeholders for ${path}`);
    }
    heads.set(blocks[0], content);
  }

  const resized = new Set();
  for (let block = 0; block < blockCount; block++) {
    const base = block * blockSize;
    for (const commit of commits(view, base, blockSize)) {
      const named = commit.structs.filter((entry) => heads.has(entry.head) && entry.size === blockSize);
      for (const entry of named) {
        view.setUint32(base + entry.at + 4, heads.get(entry.head).length, true);
        resized.add(entry.head);
      }
      if (named.length > 0) {
        const covered = filled.subarray(base + commit.start, base + commit.crcAt + 4);
        view.setUint32(base + commit.crcAt + 4, crc32(covered), true);
      }
    }
  }
  for (const [head, content] of heads) {
    if (!resized.has(head)) {
      throw new Error(`no metadata names the placeholder in block ${head}`);
    }
    filled.fill(0xff, head * blockSize, (head + 1) * blockSize);
    filled.set(content, head * blockSize);
  }
  return filled;
}
