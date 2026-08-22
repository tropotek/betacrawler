// Parses Terminal page commands into DeviceModel calls. No generic
// passthrough op exists on the wire, so every command is dispatched through
// DeviceModel's schema-validated terminal* methods rather than raw bytes.
import { DeviceError } from './device-model.js';
import { dumpIni } from './settings-ini.js';

export const HELP_TEXT = [
  'Commands:',
  "  get <key>            read a parameter's current value",
  '  set <key> <value>    set a parameter',
  '  save                 commit current values to flash',
  '  defaults             reset all values to firmware defaults',
  '  revert               discard unsaved changes, reload from flash',
  '  list                 list every setting with its valid values',
  '  dump                 print all settings as INI text (copy it to a file;',
  "                       'Restore from INI' feeds it back)",
  '  help                 show this text',
].join('\n');

export class TerminalResult {
  constructor(ok, friendly, rawSent = '', rawRecv = '', dirty = null) {
    this.ok = ok;
    this.friendly = friendly;
    this.rawSent = rawSent;
    this.rawRecv = rawRecv;
    this.dirty = dirty;
  }
}

function validValues(spec) {
  const kind = spec.type;
  if (kind === 'u8') return `${spec.min}..${spec.max}`;
  if (kind === 'enum') return spec.options.join('|');
  if (kind === 'str') return `max ${spec.maxlen} chars`;
  return '';
}

function listText(schema) {
  if (!schema.length) return 'No settings — is a device connected?';
  const rows = schema.map((spec) => {
    const label = spec.label || spec.key;
    const cells = [spec.key, spec.unit ? `${label} (${spec.unit})` : label,
                   spec.type, validValues(spec), `(default: ${spec.def})`];
    return { group: spec.group || '', cells };
  });
  const widths = rows[0].cells.map((_, i) => Math.max(...rows.map((r) => r.cells[i].length)));
  const order = [];
  const byGroup = new Map();
  for (const row of rows) {
    if (!byGroup.has(row.group)) { byGroup.set(row.group, []); order.push(row.group); }
    byGroup.get(row.group).push(row.cells);
  }
  const lines = ['Settings — change one with: set <key> <value>'];
  for (const group of order) {
    lines.push('');
    lines.push(group);
    for (const cells of byGroup.get(group)) {
      const padded = cells.map((c, i) => c.padEnd(widths[i])).join('  ');
      lines.push(`  ${padded}`.trimEnd());
    }
  }
  return lines.join('\n');
}

export async function run(device, command) {
  const parts = command.split(/\s+/).filter(Boolean);
  if (!parts.length) return new TerminalResult(false, "ERROR: empty command. Type 'help' for a list.");
  const [cmd, ...args] = parts;

  if (cmd !== 'help' && device.status().state !== 'connected') {
    return new TerminalResult(false, 'ERROR: not connected. Connect a device first.');
  }

  try {
    if (cmd === 'help') {
      if (args.length) return new TerminalResult(false, 'ERROR: usage: help');
      return new TerminalResult(true, HELP_TEXT);
    }
    if (cmd === 'get') {
      if (args.length !== 1) return new TerminalResult(false, 'ERROR: usage: get <key>');
      const { sent, recv, val } = await device.terminalGet(args[0]);
      return new TerminalResult(true, `${args[0]} = ${val}`, sent, recv);
    }
    if (cmd === 'set') {
      if (args.length !== 2) return new TerminalResult(false, 'ERROR: usage: set <key> <value>');
      const [key, rawVal] = args;
      const { sent, recv, val } = await device.terminalSet(key, rawVal);
      return new TerminalResult(true, `OK: ${key} = ${val}`, sent, recv, true);
    }
    if (cmd === 'save') {
      if (args.length) return new TerminalResult(false, 'ERROR: usage: save');
      const { sent, recv } = await device.terminalSave();
      return new TerminalResult(true, 'OK: saved to flash', sent, recv, false);
    }
    if (cmd === 'defaults') {
      if (args.length) return new TerminalResult(false, 'ERROR: usage: defaults');
      const { sent, recv } = await device.terminalDefaults();
      return new TerminalResult(true, 'OK: reset to defaults', sent, recv, true);
    }
    if (cmd === 'revert') {
      if (args.length) return new TerminalResult(false, 'ERROR: usage: revert');
      const { sent, recv, src } = await device.terminalRevert();
      const msg = src === 'flash'
        ? 'OK: reloaded settings from flash'
        : 'OK: no saved settings on this board — loaded defaults instead';
      return new TerminalResult(true, msg, sent, recv, src !== 'flash');
    }
    if (cmd === 'list') {
      if (args.length) return new TerminalResult(false, 'ERROR: usage: list');
      return new TerminalResult(true, listText(device.schema().params));
    }
    if (cmd === 'dump') {
      if (args.length) return new TerminalResult(false, 'ERROR: usage: dump');
      const { sent, recv, vals } = await device.terminalGetAll();
      const text = dumpIni(device.schema().params, vals, device.status());
      return new TerminalResult(true, text, sent, recv);
    }
    return new TerminalResult(false, `ERROR: unknown command '${cmd}'. Type 'help' for a list.`);
  } catch (exc) {
    if (exc instanceof DeviceError) return new TerminalResult(false, `ERROR: ${exc.message}`);
    throw exc;
  }
}
