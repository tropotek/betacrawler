// INI codec for whole-device settings backup and restore. Mirrors
// app/backend/settings_ini.py's dump_ini()/parse_ini(). Pure text in, pure
// text out: no device, no type coercion -- DeviceModel.terminalSet() already
// knows how to coerce and validate a raw string against the schema.
export const GENERAL_SECTION = 'general';

// Local time to seconds, matching Python's datetime.now().isoformat(timespec=
// 'seconds') -- a dump taken from either app should read the same.
function localIsoSeconds(d) {
  const p = (n) => String(n).padStart(2, '0');
  return `${d.getFullYear()}-${p(d.getMonth() + 1)}-${p(d.getDate())}`
       + `T${p(d.getHours())}:${p(d.getMinutes())}:${p(d.getSeconds())}`;
}

function splitKey(key) {
  const dot = key.indexOf('.');
  if (dot === -1) return [GENERAL_SECTION, key];
  return [key.slice(0, dot), key.slice(dot + 1)];
}

export function dumpIni(schema, values, info) {
  const lines = ['; betacrawler settings dump'];
  const identParts = [info.fw, info.board ? `(${info.board})` : null].filter(Boolean);
  if (identParts.length) lines.push(`; firmware: ${identParts.join(' ')}`);
  lines.push(`; dumped: ${localIsoSeconds(new Date())}`);
  lines.push('; restore with the Terminal page\'s "Restore from INI" button');

  const order = [];
  const bySection = new Map();
  for (const spec of schema) {
    const key = spec.key;
    if (!(key in values)) continue;
    const [section, option] = splitKey(key);
    if (!bySection.has(section)) { bySection.set(section, []); order.push(section); }
    bySection.get(section).push([option, values[key]]);
  }
  for (const section of order) {
    lines.push('');
    lines.push(`[${section}]`);
    for (const [option, value] of bySection.get(section)) lines.push(`${option} = ${value}`);
  }
  return lines.join('\n') + '\n';
}

export function parseIni(text, knownKeys = []) {
  const known = new Set(knownKeys);
  const seenSections = new Set();
  const seenOptionsInSection = new Map();
  const pairs = [];
  let currentSection = null;

  const lines = text.split(/\r\n|\n/);
  for (let i = 0; i < lines.length; i++) {
    const raw = lines[i];
    const line = raw.trim();
    if (!line) continue;
    if (line.startsWith(';') || line.startsWith('#')) continue;

    const sectionMatch = /^\[(.+)\]$/.exec(line);
    if (sectionMatch) {
      const name = sectionMatch[1].trim();
      if (seenSections.has(name)) throw new Error(`duplicate section '${name}' at line ${i + 1}`);
      seenSections.add(name);
      seenOptionsInSection.set(name, new Set());
      currentSection = name;
      continue;
    }

    if (currentSection === null) {
      throw new Error(`option outside a section at line ${i + 1}: ${raw}`);
    }

    const kv = /^([^=:]+)[=:](.*)$/.exec(line);
    if (!kv) throw new Error(`could not parse line ${i + 1}: ${raw}`);

    const option = kv[1].trim();
    const value = kv[2].trim();
    const seen = seenOptionsInSection.get(currentSection);
    if (seen.has(option)) {
      throw new Error(`duplicate option '${option}' in section '${currentSection}' at line ${i + 1}`);
    }
    seen.add(option);

    let key = `${currentSection}.${option}`;
    if (currentSection === GENERAL_SECTION && !known.has(key)) key = option;
    pairs.push([key, value]);
  }
  return pairs;
}
