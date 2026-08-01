# ESP32 esptool flashing Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let the app's Firmware page flash the `esp32_wroom32` target end-to-end (bundled catalog image or a local `.bin`), the same way it already flashes STM32 boards via DFU — using `esptool` instead of `dfu-util`, dispatched off a manifest `method` field. Also give `app/tools/bundle_firmware.py` an explicit `--all` flag to build every board target in one run.

**Architecture:** The manifest's dormant `method` field (`"dfu"` / `"esptool"`) becomes the single dispatch point, read by both the bundler (which mechanism to validate/produce an image with) and the backend (which flasher class to run). `EsptoolFlasher` is a new sibling to `DfuFlasher` with the same injected-`runner` testing seam; `FlashSession` is generalized to accept either flasher per flash rather than being bound to one at construction. The bundler gains a `merge_esp32_image()` step that folds PlatformIO's four ESP32 build outputs into one flashable file with `esptool merge-bin`, so the manifest/Catalog/UI stay uniform with the STM32 one-file-per-image shape. The frontend adds an explicit serial-port picker (reusing the already-existing `/api/ports` endpoint and its `_KNOWN_BOARDS` ESP32 recognition) everywhere a `method: esptool` image can be flashed, since — unlike DFU — there is no way to detect an ESP32 in bootloader mode by USB identity alone.

**Tech Stack:** Python (FastAPI backend, stdlib-only bundler script), vanilla JS + Alpine.js (frontend), PlatformIO/`esptool` (firmware toolchain).

## Global Constraints

- esptool CLI: use the **hyphenated** subcommands/flags (`write-flash`, `merge-bin`, `--flash-mode`, `--flash-freq`, `--flash-size`) — the underscored spellings still work in the installed v5.3.1 but print a deprecation warning line on every invocation, which would land in the user-visible flash log.
- `--chip`, `--port`, `--baud` are **global** esptool options and must appear *before* the subcommand (`esptool --chip esp32 --port <p> --baud 460800 write-flash 0x0 <file>`), not after it.
- Merge offsets (verified against this repo's real `esp32_wroom32` build and `~/.platformio/platforms/espressif32*/boards/esp32dev.json`): `bootloader.bin` at `0x1000`, `partitions.bin` at `0x8000`, `boot_app0.bin` at `0xe000`, `firmware.bin` at `0x10000`. Flash config: `--flash-mode dio --flash-freq 40m --flash-size 4MB`. Upload baud: `460800`.
- `boot_app0.bin` lives at `~/.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin` on this machine (confirmed present, no version suffix in the directory name).
- The merged image is **sparse**: bytes `0x0`-`0xFFF` are `0xFF` padding. The ESP image magic byte (`0xE9`) that identifies a real merged image is at offset **`0x1000`**, not offset `0`.
- esptool's real `write-flash` progress line (captured from the installed package's own logger, not assumed) is `Writing at 0x00010000 [====>          ]  12.3% 41000/334144 bytes...` — a bracketed bar and a **float** percent with one decimal. There is only ever one progress source (no separate erase-percentage line in the default stub-based path).
- `EsptoolFlasher` must set `NO_COLOR=1` in the subprocess environment — verified that a color-capable `TERM` inherited from the parent process (not tty-ness) is what triggers ANSI escape codes in piped esptool output, and `NO_COLOR=1` reliably suppresses that regardless of `TERM`.
- `esptool` is a pip package (console scripts `esptool` and `esptool.py`, confirmed) — add it to `app/requirements.txt`. Do not assume PlatformIO's own bundled copy at `~/.platformio/packages/tool-esptoolpy/` is available at runtime; the whole point is the packaged app doesn't require PlatformIO.
- `native` PlatformIO env has no `BOARD_HEADER` and must never be treated as a shippable board target.
- Existing bundler default (`DEFAULT_ENV = "blackpill_f411ce"`, bare invocation builds one board) must not change — only `--all` opts into building every board.
- Backend tests never touch real hardware, `dfu-util`, or `esptool` — every flasher class takes an injected `runner` and is tested against canned/fake subprocess output, per this codebase's existing pattern in `test_firmware.py`.

---

## Task 1: Bundler — derive `method` and split image validation

**Files:**
- Modify: `app/tools/bundle_firmware.py:44` (add `FW_MCU_ESP32` build-flag regex helper), `app/tools/bundle_firmware.py:254-275` (split `check_vector_table` into a dispatch)
- Test: `app/tests/test_bundle_firmware.py`

**Interfaces:**
- Produces: `method_for(env: str) -> str` (returns `"esptool"` or `"dfu"`), `check_esp32_image(blob: bytes) -> None` (raises `BundleError`). Both live in `app/tools/bundle_firmware.py` alongside `check_vector_table`.

- [ ] **Step 1: Write the failing tests**

Add to `app/tests/test_bundle_firmware.py`, near the top after the existing fixture helpers (after `fake_bin`, before the `tree` fixture is fine — these don't need the fixture tree except where noted):

```python
# --- method derivation ----------------------------------------------------

def test_method_for_a_normal_stm32_env_is_dfu(tree):
    mod = tree
    assert mod.method_for("board_a") == "dfu"


def test_method_for_an_esp32_env_is_esptool(tree):
    mod = tree
    (mod.FIRMWARE / "platformio.ini").write_text(
        (mod.FIRMWARE / "platformio.ini").read_text() +
        "\n[env:board_c]\n"
        "build_flags = -D BOARD_HEADER='\"boards/board_a.h\"' -D FW_MCU_ESP32=1\n")
    assert mod.method_for("board_c") == "esptool"


# --- esp32 image validation -------------------------------------------------

def fake_esp32_merged_image(size=8192) -> bytes:
    """Shaped like a real merge-bin output: 0xFF padding up to 0x1000, then
    the ESP image magic byte -- not a magic byte at offset 0, which a real
    merged image never has."""
    pad = b"\xff" * 0x1000
    body = b"\xe9" + b"\x00" * (size - len(pad) - 1)
    return pad + body


def test_check_esp32_image_accepts_a_plausible_merged_image(tree):
    mod = tree
    mod.check_esp32_image(fake_esp32_merged_image())


def test_check_esp32_image_rejects_a_missing_magic_byte(tree):
    mod = tree
    blob = bytearray(fake_esp32_merged_image())
    blob[0x1000] = 0x00
    with pytest.raises(mod.BundleError, match="0xE9|magic"):
        mod.check_esp32_image(bytes(blob))


def test_check_esp32_image_rejects_a_magic_byte_at_offset_zero(tree):
    """The realistic mistake this guards against: checking blob[0] instead
    of blob[0x1000] would wrongly accept a bare firmware.bin (which DOES
    have 0xE9 at offset 0) as if it were a flashable merged image."""
    mod = tree
    blob = bytearray(fake_esp32_merged_image())
    blob[0] = 0xe9   # looks right at offset 0, but that's not where it counts
    blob[0x1000] = 0x00
    with pytest.raises(mod.BundleError, match="0xE9|magic"):
        mod.check_esp32_image(bytes(blob))


def test_check_esp32_image_rejects_tiny_input(tree):
    mod = tree
    with pytest.raises(mod.BundleError, match="too small"):
        mod.check_esp32_image(b"\x00" * 16)
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `app/.venv/bin/pytest app/tests/test_bundle_firmware.py -v -k "method_for or check_esp32_image"`
Expected: FAIL with `AttributeError: module has no attribute 'method_for'` / `'check_esp32_image'`

- [ ] **Step 3: Implement `method_for()`**

In `app/tools/bundle_firmware.py`, add right after `board_header_path()` (currently ending at line 104):

```python
def method_for(env: str) -> str:
    """"esptool" for an ESP32 env, "dfu" for everything else.

    Read from the same platformio.ini block board_header_path() already
    parses, keyed on the FW_MCU_ESP32 build flag the 2026-08-01 ESP32 design
    introduced to guard architecture-specific driver bodies -- reusing it
    here means there is exactly one place in the tree that says "this env is
    an ESP32", not two that could drift apart.
    """
    ini = (FIRMWARE / "platformio.ini").read_text()
    block = re.search(rf'^\[env:{re.escape(env)}\](.*?)(?=^\[|\Z)', ini, re.M | re.S)
    if not block:
        raise BundleError(f"no [env:{env}] section in firmware/platformio.ini")
    if re.search(r"-D\s+FW_MCU_ESP32\s*=\s*1\b", block.group(1)):
        return "esptool"
    return "dfu"
```

- [ ] **Step 4: Implement `check_esp32_image()`**

In `app/tools/bundle_firmware.py`, right after `check_vector_table()` (currently ending at line 275):

```python
# A merged ESP32 image is sparse: bytes 0x0-0xFFF are 0xFF padding because
# bootloader.bin (the first real content) starts at offset 0x1000, not 0.
# Verified against a real `esptool merge-bin` run -- checking blob[0] would
# reject every genuine merged image AND wrongly accept a bare, unbootable
# firmware.bin (which does have the magic byte at offset 0 on its own).
ESP32_IMAGE_MAGIC_OFFSET = 0x1000
ESP32_IMAGE_MAGIC = 0xE9


def check_esp32_image(blob: bytes) -> None:
    if len(blob) < ESP32_IMAGE_MAGIC_OFFSET + 1:
        raise BundleError(
            f"binary is only {len(blob)} bytes -- too small to contain a "
            f"bootloader image at offset 0x{ESP32_IMAGE_MAGIC_OFFSET:x}")
    if blob[ESP32_IMAGE_MAGIC_OFFSET] != ESP32_IMAGE_MAGIC:
        raise BundleError(
            f"byte at offset 0x{ESP32_IMAGE_MAGIC_OFFSET:x} is "
            f"0x{blob[ESP32_IMAGE_MAGIC_OFFSET]:02x}, not the ESP image magic "
            f"(0x{ESP32_IMAGE_MAGIC:02x}) -- not a raw merged esptool image?")
```

- [ ] **Step 5: Run the tests to verify they pass**

Run: `app/.venv/bin/pytest app/tests/test_bundle_firmware.py -v -k "method_for or check_esp32_image"`
Expected: PASS (5 tests)

- [ ] **Step 6: Commit**

```bash
git add app/tools/bundle_firmware.py app/tests/test_bundle_firmware.py
git commit -m "feat(app): derive bundler method from FW_MCU_ESP32, add ESP32 image check"
```

---

## Task 2: Bundler — merge ESP32 build output and wire it into `plan_entry`/`release`

**Files:**
- Modify: `app/tools/bundle_firmware.py:333-398` (`plan_entry`), module docstring at the top of the file
- Test: `app/tests/test_bundle_firmware.py`

**Interfaces:**
- Consumes: `method_for(env)`, `check_esp32_image(blob)` from Task 1.
- Produces: `merge_esp32_image(env: str, esptool: str = "esptool") -> Path` (returns the merged file's path, raises `BundleError` on any missing input or a nonzero `esptool` exit). `plan_entry()`'s returned dict now has a real, per-env `"method"` value instead of the hardcoded `"dfu"` string, and for an `esptool`-method env its `"file"`/`"size"`/`"sha256"` describe the *merged* binary, not `.pio/build/<env>/firmware.bin`.

- [ ] **Step 1: Write the failing tests**

Add to `app/tests/test_bundle_firmware.py`. These need a fixture tree with an ESP32-shaped env; extend the existing `tree` fixture's board loop is tempting but changes every existing test's assumptions, so build a dedicated helper instead — add this after the `tree` fixture (which ends at line 84):

```python
@pytest.fixture
def esp32_tree(tree, monkeypatch):
    """`tree` plus one esptool-method env with the four PlatformIO output
    files an ESP32 build actually produces, and a fake boot_app0.bin standing
    in for the framework package (never write into the real
    ~/.platformio/packages/ during a test)."""
    mod = tree
    (mod.FIRMWARE / "include" / "boards" / "board_c.h").write_text(
        '#define BOARD_ID "board_c"\n#define FEATURE_LED 1\n')
    with (mod.FIRMWARE / "platformio.ini").open("a") as f:
        f.write("\n[env:board_c]\n"
                "build_flags = -D BOARD_HEADER='\"boards/board_c.h\"' "
                "-D FW_MCU_ESP32=1\n")

    boot_app0 = mod.ROOT / "fake-framework" / "boot_app0.bin"
    boot_app0.parent.mkdir(parents=True)
    boot_app0.write_bytes(b"\xe9" + b"\x00" * 64)
    monkeypatch.setattr(mod, "BOOT_APP0_PATH", boot_app0)
    return mod


def build_esp32_parts_into(mod, env: str, stamp: str = STAMP_A, board: str | None = None):
    """The four files a real `pio run -e <esp32 env>` leaves behind, standing
    in for what merge_esp32_image() reads. firmware.bin alone is what
    check_identity()/embedded_build_date() scan for the project/version/board
    strings and __DATE__ stamp -- those checks work on the merged blob too
    since they scan the whole thing for substrings, so only firmware.bin
    needs the real identity payload."""
    build_dir = mod.FIRMWARE / ".pio" / "build" / env
    build_dir.mkdir(parents=True, exist_ok=True)
    (build_dir / "bootloader.bin").write_bytes(b"\xe9" + b"\x11" * 256)
    (build_dir / "partitions.bin").write_bytes(b"\x00" * 128)
    (build_dir / "firmware.bin").write_bytes(
        fake_bin("silkscreen", "1.0.0", board or env, stamp))
    return build_dir


# --- merging ------------------------------------------------------------------

def test_merge_esp32_image_produces_a_sparse_file_with_magic_at_0x1000(esp32_tree):
    mod = esp32_tree
    build_esp32_parts_into(mod, "board_c")

    def fake_esptool(argv):
        # Stand-in for the real `esptool merge-bin` call: write a
        # minimally-plausible merged shape (padding then magic at 0x1000)
        # rather than actually running the tool.
        out = Path(argv[argv.index("-o") + 1])
        out.write_bytes(b"\xff" * 0x1000 + b"\xe9" + b"\x00" * 512)
        return 0

    merged = mod.merge_esp32_image("board_c", runner=fake_esptool)
    blob = merged.read_bytes()
    assert blob[:0x1000] == b"\xff" * 0x1000
    assert blob[0x1000] == 0xe9


def test_merge_esp32_image_reports_a_failed_merge(esp32_tree):
    mod = esp32_tree
    build_esp32_parts_into(mod, "board_c")

    def failing_esptool(argv):
        return 1

    with pytest.raises(mod.BundleError, match="merge"):
        mod.merge_esp32_image("board_c", runner=failing_esptool)


def test_merge_esp32_image_requires_all_four_inputs(esp32_tree):
    mod = esp32_tree
    # bootloader.bin/partitions.bin never written -- only firmware.bin exists.
    build_dir = mod.FIRMWARE / ".pio" / "build" / "board_c"
    build_dir.mkdir(parents=True)
    (build_dir / "firmware.bin").write_bytes(fake_bin("silkscreen", "1.0.0", "board_c", STAMP_A))

    with pytest.raises(mod.BundleError, match="bootloader.bin"):
        mod.merge_esp32_image("board_c", runner=lambda argv: 0)


# --- plan_entry / release dispatch on method -----------------------------

def test_release_bundles_an_esp32_env_as_the_merged_image(esp32_tree, monkeypatch):
    mod = esp32_tree

    def builder(env, pio):
        build_esp32_parts_into(mod, env)

    def fake_esptool(argv):
        out = Path(argv[argv.index("-o") + 1])
        out.write_bytes(b"\xff" * 0x1000 + b"\xe9" + b"\x00" * 512)
        return 0
    monkeypatch.setattr(mod, "_run_esptool", fake_esptool)

    entries, _ = mod.release(["board_c"], builder=builder)

    assert entries[0]["method"] == "esptool"
    assert entries[0]["board"] == "board_c"
    bundled = (mod.BUNDLE / entries[0]["file"]).read_bytes()
    assert bundled[0x1000] == 0xe9


def test_release_still_bundles_a_dfu_env_as_firmware_bin(tree):
    """Unaffected by the esptool path: same behavior as before this task."""
    mod = tree
    entries, _ = mod.release(["board_a"], builder=builder_for(mod))
    assert entries[0]["method"] == "dfu"


def test_release_rejects_an_esp32_env_missing_boot_app0(esp32_tree, monkeypatch):
    mod = esp32_tree
    monkeypatch.setattr(mod, "BOOT_APP0_PATH", mod.ROOT / "nope" / "boot_app0.bin")

    def builder(env, pio):
        build_esp32_parts_into(mod, env)

    with pytest.raises(mod.BundleError, match="boot_app0"):
        mod.release(["board_c"], builder=builder)
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `app/.venv/bin/pytest app/tests/test_bundle_firmware.py -v -k "merge or esp32"`
Expected: FAIL — `merge_esp32_image` / `BOOT_APP0_PATH` / `_run_esptool` don't exist yet, and `board_c` bundles as `dfu`.

- [ ] **Step 3: Implement `merge_esp32_image()`**

In `app/tools/bundle_firmware.py`, add near the top-level constants (after `DEFAULT_ENV` at line 48):

```python
# Real path on this machine, confirmed present. If the ESP32 platform is
# reinstalled to a different location this needs updating -- there is no
# portable way to derive it without invoking PlatformIO itself, which this
# stdlib-only script deliberately avoids (see the module docstring).
BOOT_APP0_PATH = (Path.home() / ".platformio" / "packages"
                  / "framework-arduinoespressif32" / "tools" / "partitions"
                  / "boot_app0.bin")

# Verified against esp32dev.json (flash_size/flash_mode/f_flash) and a real
# `esptool merge-bin` run against this project's own esp32_wroom32 build.
ESP32_MERGE_LAYOUT = (
    ("0x1000", "bootloader.bin"),
    ("0x8000", "partitions.bin"),
    ("0xe000", None),          # boot_app0.bin, resolved via BOOT_APP0_PATH
    ("0x10000", "firmware.bin"),
)
```

Add `merge_esp32_image()` right after `run_build()` (currently ending at line 236):

```python
def _run_esptool(argv: list[str]) -> int:
    proc = subprocess.run(argv, capture_output=True, text=True)
    if proc.returncode != 0:
        raise BundleError(
            "esptool merge-bin failed:\n" +
            (proc.stdout + proc.stderr).strip())
    return proc.returncode


def merge_esp32_image(env: str, esptool: str = "esptool", runner=None) -> Path:
    """Fold this env's four PlatformIO build outputs into one flashable file.

    `runner` is injected the same way run_build()'s `builder` param is --
    tests replace it with something that writes a fake merged file instead
    of shelling out to a real esptool. Resolved to _run_esptool INSIDE the
    body (a name lookup at call time), not as the default parameter value --
    a default of `runner=_run_esptool` would bind the function object at
    def-time, so a test's `monkeypatch.setattr(mod, "_run_esptool", fake)`
    would silently have no effect on any caller (like plan_entry() below)
    that doesn't pass its own runner explicitly.
    """
    runner = runner or _run_esptool
    build_dir = FIRMWARE / ".pio" / "build" / env
    inputs = []
    for offset, name in ESP32_MERGE_LAYOUT:
        path = build_dir / name if name else BOOT_APP0_PATH
        if not path.is_file():
            raise BundleError(
                f"{path} does not exist -- build {env} first, or (for "
                f"boot_app0.bin) check the ESP32 Arduino platform is "
                f"installed")
        inputs.append((offset, path))

    merged = build_dir / "merged-flash.bin"
    argv = [esptool, "--chip", "esp32", "merge-bin", "-o", str(merged),
            "--flash-mode", "dio", "--flash-freq", "40m", "--flash-size", "4MB"]
    for offset, path in inputs:
        argv += [offset, str(path)]

    runner(argv)
    if not merged.is_file():
        raise BundleError(f"esptool merge-bin did not produce {merged}")
    return merged
```

Note: `test_merge_esp32_image_reports_a_failed_merge`'s `failing_esptool` fake (Step 1) never
writes the output file and never raises — that's enough on its own, because
`merge_esp32_image` treats "the merged file doesn't exist afterward" as the failure signal
(`if not merged.is_file(): raise BundleError(...)`), regardless of what the injected `runner`
returns. The real default runner, `_run_esptool`, separately raises `BundleError` on a nonzero
`esptool` exit code — that's a second, independent path to the same outcome, exercised by
`test_release_bundles_an_esp32_env_as_the_merged_image`'s `monkeypatch.setattr(mod,
"_run_esptool", fake_esptool)` further down, not by this test.

- [ ] **Step 4: Wire `plan_entry()` to dispatch on method**

Replace `app/tools/bundle_firmware.py`'s `plan_entry()` (lines 333-398) with:

```python
def plan_entry(env: str, force: bool = False, build: bool = True,
               pio: str | None = None, builder=run_build) -> dict:
    """Build and validate one env, and return its manifest entry.

    Deliberately writes NOTHING under app/firmware/. Separating "work out what
    this env would contribute" from "write it" is what lets a multi-board run
    fail cleanly: every env goes through here first, so the release either
    lands whole or not at all.

    `builder` is injected the same way `SerialLink` takes `open_port` and
    `DfuFlasher` takes `runner` -- it is the one call that needs a toolchain,
    so the tests replace it and exercise everything else for real.
    """
    method = method_for(env)
    bin_path = bin_path_for(env)

    if build:
        builder(env, pio)

    if not bin_path.is_file():
        raise BundleError(
            f"{bin_path.relative_to(ROOT)} does not exist. Build it first:\n"
            f"    ~/.platformio/penv/bin/pio run -e {env}")

    config = (FIRMWARE / "include" / "config.h").read_text()
    header = board_header_path(env)
    header_text = header.read_text()

    name = read_define(config, "FW_PROJECT_NAME")
    version = read_define(config, "FW_VERSION")
    board = read_define(header_text, "BOARD_ID")
    if not (name and version and board):
        raise BundleError(
            "could not read FW_PROJECT_NAME/FW_VERSION from config.h or "
            f"BOARD_ID from {header.relative_to(ROOT)}")

    # Format check BEFORE check_identity(), same order the pre-existing code
    # already used for the DFU path (check_vector_table() then
    # check_identity()) -- test_a_binary_that_fails_validation_stops_the_
    # whole_release feeds an ELF fixture and asserts the error mentions
    # "stack pointer", which only holds if the vector-table check still runs
    # first. Swapping the order would instead report a missing-identity
    # error for the same fixture, silently changing what that test proves.
    fw_blob = bin_path.read_bytes()

    if method == "esptool":
        image_path = merge_esp32_image(env)
        blob = image_path.read_bytes()
        check_esp32_image(blob)
    else:
        image_path = bin_path
        blob = fw_blob
        check_vector_table(blob)

    # check_identity()/embedded_build_date() scan firmware.bin -- the
    # ESP32 app partition -- for the FW_PROJECT_NAME/FW_VERSION/BOARD_ID
    # strings and the __DATE__ stamp, regardless of whether that's the file
    # actually shipped.
    check_identity(fw_blob, name, version, board)

    # Only meaningful under --no-build: with a build just run, the binary is
    # current by construction and a timestamp comparison can only mislead.
    if not build and not force:
        stale = sources_newer_than(bin_path)
        if stale:
            listed = "\n".join(f"      {p}" for p in stale[:10])
            more = f"\n      ... and {len(stale) - 10} more" if len(stale) > 10 else ""
            raise BundleError(
                f"{len(stale)} source file(s) are newer than the binary:\n{listed}{more}\n"
                f"    Drop --no-build to rebuild and bundle in one step.\n"
                f"    (If the binary really is current -- a touch or a checkout can\n"
                f"     skew mtimes without changing content -- pass --force.)")

    entry = {
        "id": f"{board}-{name}-{version}",
        "board": board,
        "name": name,
        "version": version,
        "built": embedded_build_date(fw_blob),
        "proto": proto_version(),
        "method": method,
        "file": f"{board}/{name}-{version}.bin",
        "size": len(blob),
        "sha256": hashlib.sha256(blob).hexdigest(),
        "notes": ", ".join(enabled_features(header_text)) or "no optional modules",
        "_source": image_path,   # internal only, consumed by release() below
    }
    return entry
```

`embedded_build_date()` is called on `fw_blob` (the raw `firmware.bin`) rather than the merged blob for the esptool case — the merged blob also contains the same stamp (firmware.bin's bytes are in there unchanged), but scanning the smaller file is simpler and this keeps the "ambiguous timestamp" check meaningful (the merged blob's bootloader region could in principle also match the date regex).

Note the new `"_source"` key: `release()` currently does `shutil.copyfile(bin_path_for(env), dest)` for every entry (line ~458-460), which is wrong for an esptool entry (it must copy the *merged* file, not `firmware.bin`). Update `release()`'s copy loop:

```python
    for env, entry in plans:
        dest = BUNDLE / entry["file"]
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(entry["_source"], dest)
```

(this replaces the existing `shutil.copyfile(bin_path_for(env), dest)` line). And strip `_source` before writing the manifest, since it's an internal `Path` object, not JSON-serializable and not something the manifest should carry — in `release()`, right before `data = {...}` is built:

```python
    for _, entry in plans:
        entry.pop("_source", None)
```

Place this immediately after the `clash` duplicate-id check and before the `if dry_run: return entries, []` line, so `dry_run` output (returned to the caller, potentially printed) also never carries the internal `Path`.

- [ ] **Step 5: Run the tests to verify they pass**

Run: `app/.venv/bin/pytest app/tests/test_bundle_firmware.py -v`
Expected: PASS, all tests (old and new)

- [ ] **Step 6: Update the module docstring**

In `app/tools/bundle_firmware.py`, the top-of-file docstring's usage block (around line 10-14) — no textual change needed yet (that comes in Task 3 with `--all`), but add one sentence after the existing "What lands in the bundle..." paragraph:

```python
An esptool-method env (currently esp32_wroom32) bundles a merged single
binary built from PlatformIO's four separate output files, not a copy of
firmware.bin directly -- see merge_esp32_image().
```

- [ ] **Step 7: Commit**

```bash
git add app/tools/bundle_firmware.py app/tests/test_bundle_firmware.py
git commit -m "feat(app): bundle esp32_wroom32 as a merged single flashable image"
```

---

## Task 3: Bundler — `--all` flag, VS Code task, skill docs

**Files:**
- Modify: `app/tools/bundle_firmware.py:401-513` (`release()`, `main()`), top-of-file docstring
- Modify: `silkscreen.code-workspace`
- Modify: `.claude/skills/bundle-firmware/SKILL.md`
- Test: `app/tests/test_bundle_firmware.py`

**Interfaces:**
- Produces: `all_board_envs() -> list[str]` in `app/tools/bundle_firmware.py`.

- [ ] **Step 1: Write the failing tests**

Add to `app/tests/test_bundle_firmware.py`:

```python
# --- --all -------------------------------------------------------------------

def test_all_board_envs_finds_every_env_with_a_board_header(tree):
    mod = tree
    # native-shaped env: no BOARD_HEADER at all.
    with (mod.FIRMWARE / "platformio.ini").open("a") as f:
        f.write("\n[env:native]\nplatform = native\n")
    assert mod.all_board_envs() == ["board_a", "board_b"]


def test_all_board_envs_includes_an_esptool_env(esp32_tree):
    mod = esp32_tree
    assert mod.all_board_envs() == ["board_a", "board_b", "board_c"]


def test_main_all_flag_builds_every_board(tree, monkeypatch, capsys):
    mod = tree
    monkeypatch.setattr(mod, "run_build", builder_for(mod))
    monkeypatch.setattr("sys.argv", ["bundle_firmware.py", "--all", "--dry-run"])
    rc = mod.main()
    assert rc == 0
    out = capsys.readouterr().out
    assert "board_a-silkscreen-1.0.0" in out
    assert "board_b-silkscreen-1.0.0" in out


def test_all_flag_rejects_explicit_envs_too(tree, monkeypatch, capsys):
    mod = tree
    monkeypatch.setattr("sys.argv", ["bundle_firmware.py", "--all", "board_a"])
    with pytest.raises(SystemExit) as exc:
        mod.main()
    assert exc.value.code != 0
    assert "--all" in capsys.readouterr().err
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `app/.venv/bin/pytest app/tests/test_bundle_firmware.py -v -k "all_board_envs or all_flag"`
Expected: FAIL — `all_board_envs` doesn't exist, `--all` isn't a recognized argument.

- [ ] **Step 3: Implement `all_board_envs()`**

In `app/tools/bundle_firmware.py`, add right after `method_for()` (from Task 1):

```python
def all_board_envs() -> list[str]:
    """Every [env:*] in platformio.ini that names a BOARD_HEADER, in file
    order. `native` (and any other host-side env with no board header) is
    excluded without a name-based blocklist, which would itself go stale --
    exactly the kind of drift this script exists to prevent (see module
    docstring)."""
    ini = (FIRMWARE / "platformio.ini").read_text()
    envs = []
    for m in re.finditer(r'^\[env:([\w-]+)\]', ini, re.M):
        env = m.group(1)
        block = re.search(rf'^\[env:{re.escape(env)}\](.*?)(?=^\[|\Z)', ini, re.M | re.S)
        if block and re.search(r"-D\s+BOARD_HEADER\s*=", block.group(1)):
            envs.append(env)
    return envs
```

- [ ] **Step 4: Wire `--all` into `main()`**

In `app/tools/bundle_firmware.py`'s `main()` (lines 469-484), change the argument parsing:

```python
def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("envs", nargs="*", metavar="ENV", default=[],
                    help=f"PlatformIO envs to build and ship "
                         f"(default: {DEFAULT_ENV})")
    ap.add_argument("--all", action="store_true",
                    help="build every board env with a BOARD_HEADER, "
                         "instead of just the default")
    ap.add_argument("--add", action="store_true",
                    help="merge into the existing manifest instead of "
                         "replacing it with exactly these envs")
    ap.add_argument("--dry-run", action="store_true",
                    help="report what would be bundled, write nothing")
    ap.add_argument("--no-build", action="store_true",
                    help="bundle the existing binaries instead of rebuilding first")
    ap.add_argument("--pio", help="path to the PlatformIO CLI")
    ap.add_argument("--force", action="store_true",
                    help="with --no-build, bundle even if sources look newer")
    args = ap.parse_args()

    # A plain ap.error() check rather than add_mutually_exclusive_group():
    # argparse's mutually-exclusive groups are unreliable for a positional
    # nargs="*" arg (zero-given doesn't consistently register as "used"
    # across versions), so this is deterministic where that would not be.
    if args.all and args.envs:
        ap.error("--all cannot be combined with explicit ENV arguments")

    envs = all_board_envs() if args.all else args.envs

    try:
        entries, pruned = release(envs, dry_run=args.dry_run,
                                  force=args.force, build=not args.no_build,
                                  pio=args.pio, add=args.add)
```

- [ ] **Step 5: Run the tests to verify they pass**

Run: `app/.venv/bin/pytest app/tests/test_bundle_firmware.py -v`
Expected: PASS, all tests.

- [ ] **Step 6: Update the module docstring's usage block**

In `app/tools/bundle_firmware.py`, replace the usage block (currently lines 10-14):

```python
    python app/tools/bundle_firmware.py                    # blackpill_f411ce
    python app/tools/bundle_firmware.py board_a board_b    # the release set
    python app/tools/bundle_firmware.py --all              # every board target
    python app/tools/bundle_firmware.py --add other_board  # merge, don't prune
    python app/tools/bundle_firmware.py --dry-run          # report, change nothing
    python app/tools/bundle_firmware.py --no-build         # bundle what's already built
```

- [ ] **Step 7: Add the VS Code task**

In `silkscreen.code-workspace`, add a new task object right after the existing "Build release firmware" task (which currently ends at line 52 with the closing `},` before `],`):

```json
            {
                "label": "Build ALL release firmware",
                "type": "shell",
                "command": "python3 ${workspaceFolder:silkscreen}/app/tools/bundle_firmware.py --all",
                "problemMatcher": [],
                "presentation": {
                    "panel": "dedicated",
                    "clear": true,
                    "focus": true
                }
            }
```

Insert it as a new element in the `tasks` array, before the closing `]` that currently sits at line 53. No change needed to the `inputs` array (the `--all` task takes no prompt).

- [ ] **Step 8: Update the bundle-firmware skill file**

In `.claude/skills/bundle-firmware/SKILL.md`, replace the usage block:

```
python3 app/tools/bundle_firmware.py                    # builds, then updates app/firmware/
python3 app/tools/bundle_firmware.py board_a board_b    # the whole release set, in one go
python3 app/tools/bundle_firmware.py --all              # every board target in one run
python3 app/tools/bundle_firmware.py --add other_board  # merge, don't prune the rest
python3 app/tools/bundle_firmware.py --dry-run          # report only
```

And append a sentence after the existing "Also the **Build release firmware** task..." line:

```
There is also a no-prompt **Build ALL release firmware** task for `--all`.
```

- [ ] **Step 9: Run the full bundler test suite once more**

Run: `app/.venv/bin/pytest app/tests/test_bundle_firmware.py -v`
Expected: PASS (full file, confirms nothing in Steps 6-8 broke Python syntax elsewhere — Steps 6-8 are docs/JSON only, but this is a cheap final check before committing).

- [ ] **Step 10: Commit**

```bash
git add app/tools/bundle_firmware.py app/tests/test_bundle_firmware.py silkscreen.code-workspace .claude/skills/bundle-firmware/SKILL.md
git commit -m "feat(app): add --all flag to build every board target, plus a VS Code task"
```

---

## Task 4: Backend — `DeviceModel` tracks its own connected port

**Files:**
- Modify: `app/backend/device.py:46-108`
- Test: `app/tests/test_device.py`

**Interfaces:**
- Produces: `DeviceModel.status()["port"]` — the port string passed to the most recent successful `connect()`, or `None`.

- [ ] **Step 1: Write the failing tests**

Add to `app/tests/test_device.py`, near the other `status()`-checking tests (e.g. right after `test_connect_caches_schema_and_values`, around line 89-101):

```python
def test_status_reports_the_connected_port():
    fake = FakeSerial(responder=device_responder())
    dev = DeviceModel(SerialLink(open_port=lambda p: fake))
    dev.connect("/dev/ttyFAKE0")
    assert dev.status()["port"] == "/dev/ttyFAKE0"
    dev.disconnect()


def test_status_port_is_none_before_any_connection():
    dev = DeviceModel(SerialLink(open_port=lambda p: FakeSerial(responder=device_responder())))
    assert dev.status()["port"] is None


def test_status_port_survives_disconnect_like_board_and_fw_do():
    """Matches the existing behavior of board/fw/caps: status() keeps
    reporting the last-known value after a disconnect, not None -- the
    Firmware page's "not connected (last seen: ...)" text relies on exactly
    this pattern for `board` already."""
    fake = FakeSerial(responder=device_responder())
    dev = DeviceModel(SerialLink(open_port=lambda p: fake))
    dev.connect("/dev/ttyFAKE0")
    dev.disconnect()
    assert dev.status()["port"] == "/dev/ttyFAKE0"


def test_status_port_is_cleared_on_a_failed_connect():
    def boom(p):
        raise OSError("no such port")
    dev = DeviceModel(SerialLink(open_port=boom))
    with pytest.raises(DeviceError):
        dev.connect("/dev/ttyGHOST")
    assert dev.status()["port"] is None
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `app/.venv/bin/pytest app/tests/test_device.py -v -k "status_port or status_reports_the_connected"`
Expected: FAIL with `KeyError: 'port'`

- [ ] **Step 3: Implement**

In `app/backend/device.py`, `__init__` (lines 46-52), add `self._port`:

```python
    def __init__(self, link: SerialLink | None = None):
        self._link = link or SerialLink()
        self._schema: list[dict] = []
        self._tlm_schema: list[dict] = []
        self._by_key: dict[str, dict] = {}
        self._values: dict = {}
        self._info: dict = {}
        self._port: str | None = None
```

In `connect()` (lines 58-100), set `self._port` alongside `self._info` on success, and clear it in the rollback branch:

```python
    def connect(self, port: str):
        try:
            self._link.connect(port)
        except Exception as exc:
            raise DeviceError("connect_failed", str(exc)) from exc

        try:
            hello = self._send("hello")
            if hello.get("proto") != PROTO_VERSION:
                raise ProtoMismatch(
                    f"device speaks proto {hello.get('proto')}, "
                    f"this app speaks {PROTO_VERSION}"
                )
            # `fw` stays the display string; name/ver/built/mods are the
            # structured fields the modular firmware added beside it. Older
            # firmware simply omits them and they come through as None.
            self._info = {
                "fw": hello.get("fw"),
                "proto": hello.get("proto"),
                "board": hello.get("board"),
                "name": hello.get("name"),
                "ver": hello.get("ver"),
                "built": hello.get("built"),
                "mods": hello.get("mods", []),
                "caps": hello.get("caps", []),
            }
            self._port = port
            schema = self._send("schema")
            self._schema = schema["params"]
            self._by_key = {p["key"]: p for p in self._schema}
            self._values = self._send("getall")["vals"]
        except Exception:
            self._link.disconnect()
            self._schema, self._tlm_schema = [], []
            self._by_key, self._values, self._info = {}, {}, {}
            self._port = None
            raise
```

(the `caps` field's comment stays as in the original; only the two `self._port` lines and the surrounding structure shown above are new/changed).

In `status()` (line 107-108):

```python
    def status(self) -> dict:
        return {"state": self._link.state, "port": self._port, **self._info}
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `app/.venv/bin/pytest app/tests/test_device.py -v`
Expected: PASS, full file (confirms no existing `status()` consumer broke — none of the existing tests assert `status()`'s exact key set, only specific keys, so adding `"port"` is additive).

- [ ] **Step 5: Commit**

```bash
git add app/backend/device.py app/tests/test_device.py
git commit -m "feat(app): DeviceModel tracks its own connected port in status()"
```

---

## Task 5: Backend — split image validation, add `EsptoolFlasher`

**Files:**
- Modify: `app/backend/firmware.py:60-84` (rename + split `validate_image`), add `EsptoolFlasher` after `DfuFlasher` (currently ends at line 328)
- Test: `app/tests/test_firmware.py`

**Interfaces:**
- Produces: `validate_dfu_image(blob: bytes) -> None` (renamed from `validate_image`), `validate_esp32_image(blob: bytes) -> None`, `EsptoolFlasher` class with `.flash(path: Path, port: str, on_progress=None) -> None`.
- Consumes: nothing new from other tasks (independent of Tasks 1-4).

- [ ] **Step 1: Write the failing tests**

In `app/tests/test_firmware.py`, change the import line (line 16-19) to add the new names:

```python
from backend.firmware import (
    Catalog, DfuFlasher, EsptoolFlasher, FlashSession, FirmwareError, _Process,
    validate_dfu_image, validate_esp32_image, FLASH_ORIGIN,
)
```

Replace every `validate_image(` call in the existing tests (lines 85-114, the six `test_validate_*` functions) with `validate_dfu_image(`. There is no behavior change in those tests — only the rename.

Add new tests, after the renamed `test_validate_rejects_tiny_and_oversized` (around line 114):

```python
# --- esp32 upload validation ---------------------------------------------------

def make_esp32_image(size=8192) -> bytes:
    """Shaped like a real merge-bin output: 0xFF padding to 0x1000, then the
    ESP image magic byte there, not at offset 0."""
    return b"\xff" * 0x1000 + b"\xe9" + b"\x00" * (size - 0x1001)


def test_validate_esp32_accepts_a_plausible_merged_image():
    validate_esp32_image(make_esp32_image())


def test_validate_esp32_rejects_a_magic_byte_at_offset_zero_only():
    blob = bytearray(make_esp32_image())
    blob[0] = 0xe9
    blob[0x1000] = 0x00
    with pytest.raises(FirmwareError, match="0x1000|offset"):
        validate_esp32_image(bytes(blob))


def test_validate_esp32_rejects_tiny_input():
    with pytest.raises(FirmwareError, match="too small"):
        validate_esp32_image(b"\x00" * 16)


# --- esptool flashing -----------------------------------------------------------

# Captured from the real, installed esptool v5.3.1 package's own logger code
# (esptool/logger.py's progress_bar()), not assumed -- the float percent and
# bracketed bar are real, not the "(NN %)" shape older esptool versions used.
ESPTOOL_WRITE = [
    "esptool v5.3.1",
    "Serial port /dev/ttyUSB0",
    "Connecting....",
    "Uploading stub...",
    "Running stub...",
    "Writing at 0x00010000 [                              ]   0.0% 0/334144 bytes...",
    "Writing at 0x00010000 [==>                           ]  12.3% 41000/334144 bytes...",
    "Writing at 0x00010000 [==============================] 100.0% 334144/334144 bytes...",
    "Hash of data verified.",
    "",
    "Leaving...",
    "Hard resetting via RTS pin...",
]


def test_esptool_flash_builds_the_right_command(tmp_path):
    calls = []
    image = tmp_path / "merged.bin"
    image.write_bytes(make_esp32_image())
    EsptoolFlasher(runner=runner_for(ESPTOOL_WRITE, record=calls)).flash(
        image, "/dev/ttyUSB0")
    assert calls[0] == [
        "esptool", "--chip", "esp32", "--port", "/dev/ttyUSB0",
        "--baud", "460800", "write-flash", "0x0", str(image),
    ]


def test_esptool_flash_reports_write_progress_monotonically(tmp_path):
    seen = []
    image = tmp_path / "merged.bin"
    image.write_bytes(make_esp32_image())
    EsptoolFlasher(runner=runner_for(ESPTOOL_WRITE)).flash(
        image, "/dev/ttyUSB0", on_progress=seen.append)

    writing = [e["pct"] for e in seen if e["op"] == "writing"]
    assert writing == [0, 12, 100]


def test_esptool_flash_passes_through_non_progress_lines(tmp_path):
    seen = []
    image = tmp_path / "merged.bin"
    image.write_bytes(make_esp32_image())
    EsptoolFlasher(runner=runner_for(ESPTOOL_WRITE)).flash(
        image, "/dev/ttyUSB0", on_progress=seen.append)

    plain = [e["line"] for e in seen if e["pct"] is None]
    assert any("Hash of data verified" in line for line in plain)
    assert any("Hard resetting" in line for line in plain)


def test_esptool_flash_raises_on_nonzero_exit(tmp_path):
    image = tmp_path / "merged.bin"
    image.write_bytes(make_esp32_image())
    lines = ["Connecting....", "A fatal error occurred: Failed to connect"]
    with pytest.raises(FirmwareError, match="Failed to connect"):
        EsptoolFlasher(runner=runner_for(lines, rc=2)).flash(image, "/dev/ttyUSB0")


def test_esptool_flash_reports_a_missing_esptool(tmp_path):
    image = tmp_path / "merged.bin"
    image.write_bytes(make_esp32_image())

    def missing(argv):
        raise FileNotFoundError("esptool")
    with pytest.raises(FirmwareError, match="not installed"):
        EsptoolFlasher(runner=missing).flash(image, "/dev/ttyUSB0")


def test_esptool_flash_sets_no_color(monkeypatch):
    """Verified separately (see the design spec) that a color-capable
    inherited TERM makes esptool emit raw ANSI escapes into a piped
    subprocess even with no tty -- NO_COLOR=1 forces plain output
    regardless. Checks the default runner actually sets it, via
    monkeypatch.setattr (auto-restoring) rather than a hand-rolled
    patch/restore -- nothing here needs a real subprocess, just to observe
    what env `_default_esptool_runner` would hand to one."""
    from backend.firmware import _default_esptool_runner
    import subprocess

    captured = {}

    class FakeCompletedPopen:
        def __init__(self, argv, **kwargs):
            captured["env"] = kwargs.get("env")
            self.stdout = None
            self.returncode = 0

        def wait(self):
            return 0

    monkeypatch.setattr(subprocess, "Popen", FakeCompletedPopen)
    _default_esptool_runner(["esptool", "--help"])

    assert captured["env"] is not None
    assert captured["env"].get("NO_COLOR") == "1"
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `app/.venv/bin/pytest app/tests/test_firmware.py -v`
Expected: FAIL — `ImportError` on `EsptoolFlasher`/`validate_dfu_image`/`validate_esp32_image`/`_default_esptool_runner`.

- [ ] **Step 3: Rename and split validation**

In `app/backend/firmware.py`, rename `validate_image` (line 60) to `validate_dfu_image` — same body, just the name (`def validate_dfu_image(blob: bytes) -> None:`). Update its call site in `main.py` is handled in Task 7/8, not here.

Add `validate_esp32_image()` right after it (after the current `validate_image` function body, before `class Catalog` at line 92):

```python
# Same offset/magic-byte reasoning as bundle_firmware.py's check_esp32_image
# (see that function's comment) -- a merged ESP32 image is sparse, with the
# ESP image magic byte at 0x1000, not 0.
ESP32_IMAGE_MAGIC_OFFSET = 0x1000
ESP32_IMAGE_MAGIC = 0xE9


def validate_esp32_image(blob: bytes) -> None:
    """Reject anything that is obviously not a raw merged ESP32 image.

    Applied to UPLOADED files only, same as validate_dfu_image() -- a
    bundled image is covered by its sha256 instead.
    """
    if len(blob) < ESP32_IMAGE_MAGIC_OFFSET + 1:
        raise FirmwareError(
            f"image is only {len(blob)} bytes -- too small to contain a "
            f"bootloader image at offset 0x{ESP32_IMAGE_MAGIC_OFFSET:x}")
    if len(blob) > MAX_IMAGE:
        raise FirmwareError(
            f"image is {len(blob)} bytes, larger than the 512KB flash")
    if blob[ESP32_IMAGE_MAGIC_OFFSET] != ESP32_IMAGE_MAGIC:
        raise FirmwareError(
            f"not a raw merged esptool image: byte at offset "
            f"0x{ESP32_IMAGE_MAGIC_OFFSET:x} is not the ESP image magic "
            f"(0x{ESP32_IMAGE_MAGIC:02x})")
```

- [ ] **Step 4: Implement `EsptoolFlasher`**

In `app/backend/firmware.py`, add after the `DfuFlasher` class (currently ends at line 328, right before `class FlashSession`):

```python
# esptool's real `write-flash` progress line (captured from the installed
# v5.3.1 package's own logger, not assumed): a bracketed bar and a FLOAT
# percent with one decimal --
#   "Writing at 0x00010000 [====>          ]  12.3% 41000/334144 bytes..."
# not the "(NN %)" shape older esptool docs/memory suggest. There is only
# ever one progress source in the default stub-based write path (no separate
# erase-percentage line), so `op` is always "writing" when this matches.
_ESPTOOL_PROGRESS = re.compile(
    r"^Writing at 0x[0-9a-fA-F]+\s+\[.*?\]\s*(?P<pct>\d{1,3}(?:\.\d+)?)%")


def _default_esptool_runner(argv: list[str]) -> _Process:
    # NO_COLOR=1: verified that a color-capable TERM inherited from this
    # process (not tty-ness -- stdout is always a pipe here) makes esptool
    # emit raw ANSI escape codes and \r-based overwrites, which would
    # otherwise land as literal escape bytes in the user-visible log.
    env = dict(os.environ)
    env["NO_COLOR"] = "1"
    return _Process(argv, env=env)


class EsptoolFlasher:
    def __init__(self, runner=None, esptool: str = "esptool"):
        self._run = runner or _default_esptool_runner
        self._esptool = esptool

    def flash(self, path: Path, port: str, on_progress=None) -> None:
        """Write a merged image to `port` and leave esptool's bootloader.

        No devices()/wait_for_device(): unlike a Black Pill in DFU mode, an
        ESP32 in its ROM bootloader has no distinct USB identity to poll
        for. `port` is supplied by the caller (the Firmware page's explicit
        port picker) and esptool performs the reset-into-bootloader
        handshake itself when it opens it.
        """
        argv = [self._esptool, "--chip", "esp32", "--port", port,
                "--baud", "460800", "write-flash", "0x0", str(path)]
        try:
            proc = self._run(argv)
        except FileNotFoundError as exc:
            raise FirmwareError(
                f"{self._esptool} is not installed or not on PATH") from exc
        except OSError as exc:
            raise FirmwareError(f"could not run {self._esptool}: {exc}") from exc

        tail = []
        for line in proc.lines():
            tail.append(line)
            del tail[:-8]
            m = _ESPTOOL_PROGRESS.match(line)
            if on_progress:
                on_progress({
                    "op": "writing" if m else None,
                    "pct": min(100, int(float(m.group("pct")))) if m else None,
                    "line": line,
                })

        if proc.returncode != 0:
            raise FirmwareError(
                "esptool failed (exit {}):\n{}".format(
                    proc.returncode, "\n".join(tail)))
```

`_Process` (the existing class, lines ~163-197) needs one small change to accept an `env` kwarg — it currently always inherits the parent's environment via `subprocess.Popen`'s default. Update its `__init__`:

```python
class _Process:
    def __init__(self, argv: list[str], env: dict | None = None):
        self._proc = subprocess.Popen(
            argv, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
            text=True, bufsize=0, env=env)
```

(`env=None` passed to `Popen` means "inherit the parent's environment", identical to today's behavior when `_default_runner` — the DFU one — constructs a `_Process` without passing `env`; so `DfuFlasher`'s existing behavior and tests are unaffected).

Add `import os` and `import re` to the top of `app/backend/firmware.py` if not already present — check the existing import block (lines 21-27 per the earlier read: `hashlib, json, logging, re, subprocess, threading, time` — `re` is already imported; `os` is not, so add it).

- [ ] **Step 5: Run the tests to verify they pass**

Run: `app/.venv/bin/pytest app/tests/test_firmware.py -v`
Expected: PASS, full file.

- [ ] **Step 6: Commit**

```bash
git add app/backend/firmware.py app/tests/test_firmware.py
git commit -m "feat(app): add EsptoolFlasher and ESP32 image validation"
```

---

## Task 6: Backend — generalize `FlashSession` to accept either flasher

**Files:**
- Modify: `app/backend/firmware.py:330-388` (`FlashSession`)
- Test: `app/tests/test_firmware.py`

**Interfaces:**
- Consumes: `EsptoolFlasher` from Task 5 (only in tests).
- Produces: `FlashSession.start(path, label, flasher=None, wait=True, **flash_kwargs)` — `flasher` defaults to the one passed to `__init__` (unchanged default behavior for every existing DFU call site), `wait=True` keeps the "waiting for a device in DFU mode" phase (unchanged default), extra `flash_kwargs` are forwarded to `flasher.flash(path, on_progress=..., **flash_kwargs)`.

- [ ] **Step 1: Write the failing tests**

Add to `app/tests/test_firmware.py`, after the existing session tests (after `test_session_refuses_a_concurrent_flash`, before `_join`, around line 382):

```python
def test_session_can_flash_with_a_different_flasher_per_call(tmp_path):
    """The esptool path: FlashSession is constructed once (with a DfuFlasher,
    as main.py does today) but a single call can override which flasher and
    whether to wait -- this is what lets one FlashSession/one busy-lock
    serve both mechanisms."""
    image = tmp_path / "merged.bin"
    image.write_bytes(make_image())
    calls = []

    class FakeEsptoolFlasher:
        def flash(self, path, on_progress=None, port=None):
            calls.append(port)
            on_progress({"op": "writing", "pct": 100, "line": "done"})

    dfu = DfuFlasher(runner=runner_for(DFU_LIST))
    events = []
    session = FlashSession(dfu, on_event=events.append)

    session.start(image, "esp32 1.0.0", flasher=FakeEsptoolFlasher(),
                 wait=False, port="/dev/ttyUSB0")
    _join(session)

    assert calls == ["/dev/ttyUSB0"]
    phases = [e["phase"] for e in events]
    assert "waiting" not in phases      # wait=False skips it entirely
    assert phases[-1] == "done"


def test_session_wait_false_skips_the_dfu_wait_for_device_call(tmp_path):
    image = tmp_path / "merged.bin"
    image.write_bytes(make_image())

    class NeverCallMe:
        def wait_for_device(self, **kw):
            raise AssertionError("wait_for_device() must not be called when wait=False")

        def flash(self, path, on_progress=None):
            on_progress({"op": "writing", "pct": 100, "line": "done"})

    session = FlashSession(DfuFlasher(runner=runner_for(DFU_LIST)))
    session.start(image, "x", flasher=NeverCallMe(), wait=False)
    _join(session)
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `app/.venv/bin/pytest app/tests/test_firmware.py -v -k "session_can_flash_with_a_different or wait_false"`
Expected: FAIL — `start()` doesn't accept `flasher=`/`wait=`/`port=` kwargs yet.

- [ ] **Step 3: Implement**

Replace `FlashSession` (lines 330-388) with:

```python
class FlashSession:
    """Owns the flashing thread and enforces that only one runs at a time.

    Separate from the HTTP layer so the concurrency rule is testable without
    FastAPI, and so `main.py` only has to forward events to the Broadcaster.
    Two overlapping writes to one device's flash is the kind of thing that
    must be impossible rather than unlikely.
    """

    def __init__(self, flasher, on_event=None):
        self._flasher = flasher
        self._on_event = on_event or (lambda ev: None)
        self._lock = threading.Lock()
        self._thread: threading.Thread | None = None
        self._busy = False

    @property
    def busy(self) -> bool:
        with self._lock:
            return self._busy

    def start(self, path: Path, label: str, flasher=None, wait: bool = True,
             **flash_kwargs) -> None:
        """Start a flash in the background.

        `flasher` defaults to the one this session was constructed with
        (every existing DFU call site keeps working unchanged). Passing a
        different flasher -- e.g. an EsptoolFlasher -- lets one session/one
        busy-lock serve both mechanisms, since the two overlapping-writes
        invariant is about "one flash at a time in this app", not "one flash
        at a time per mechanism". `wait=False` skips the "waiting for a
        device in DFU mode" phase entirely, for a flasher (like
        EsptoolFlasher) with no such concept -- there is nothing to poll for
        an ESP32's bootloader by USB identity. `flash_kwargs` are forwarded
        to `flasher.flash(path, on_progress=..., **flash_kwargs)`, e.g.
        `port=` for EsptoolFlasher.
        """
        with self._lock:
            if self._busy:
                raise FlashBusy("a firmware flash is already in progress")
            self._busy = True
        self._thread = threading.Thread(
            target=self._run,
            args=(path, label, flasher or self._flasher, wait, flash_kwargs),
            daemon=True)
        self._thread.start()

    def _emit(self, phase: str, **fields):
        try:
            self._on_event({"phase": phase, **fields})
        except Exception:
            log.exception("flash event subscriber raised")

    def _run(self, path: Path, label: str, flasher, wait: bool, flash_kwargs: dict):
        try:
            if wait:
                self._emit("waiting", line=f"waiting for a device in DFU mode ({label})")
                if not flasher.wait_for_device():
                    raise FirmwareError(
                        "no device in DFU mode. Hold BOOT0, tap NRST, release "
                        "BOOT0, then try again.")
            self._emit("flashing", pct=0, line=f"writing {label}")
            flasher.flash(
                path,
                on_progress=lambda ev: self._emit("flashing", **ev),
                **flash_kwargs)
            self._emit("done", pct=100, line="flash complete")
        except FirmwareError as exc:
            self._emit("error", line=str(exc))
        except Exception as exc:                      # never kill the thread silently
            log.exception("unexpected error while flashing")
            self._emit("error", line=f"unexpected error: {exc}")
        finally:
            with self._lock:
                self._busy = False
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `app/.venv/bin/pytest app/tests/test_firmware.py -v`
Expected: PASS, full file — including every pre-existing `FlashSession` test (`test_session_emits_a_full_event_sequence` etc.), unchanged, confirming the default-argument backward compatibility holds.

- [ ] **Step 5: Commit**

```bash
git add app/backend/firmware.py app/tests/test_firmware.py
git commit -m "feat(app): generalize FlashSession to take a flasher per call"
```

---

## Task 7: Backend — wire `/api/firmware/flash` to dispatch on method

**Files:**
- Modify: `app/backend/main.py:15-16` (imports), `:44-45` (`FlashBody`), `:121-129` (`create_app`), `:317-325` (`firmware_flash`)
- Test: `app/tests/test_firmware_api.py`

**Interfaces:**
- Consumes: `EsptoolFlasher` (Task 5), `FlashSession.start(..., flasher=, wait=, **kwargs)` (Task 6), `DeviceModel.status()["port"]` (Task 4).
- Produces: `create_app(..., esptool_flasher: EsptoolFlasher | None = None)`.

- [ ] **Step 1: Write the failing tests**

Add to `app/tests/test_firmware_api.py`. First, extend the `bundle` fixture (lines 26-46) with an esptool-method image — add a third entry to its `images` list:

```python
@pytest.fixture
def bundle(tmp_path):
    blob = make_image()
    esp_blob = b"\xff" * 0x1000 + b"\xe9" + b"\x00" * 512
    (tmp_path / "blackpill_f411ce").mkdir()
    (tmp_path / "blackpill_f411ce" / "silkscreen-1.0.0.bin").write_bytes(blob)
    (tmp_path / "esp32_wroom32").mkdir()
    (tmp_path / "esp32_wroom32" / "silkscreen-1.0.0.bin").write_bytes(esp_blob)
    (tmp_path / "manifest.json").write_text(json.dumps({
        "app_version": "1.0.0",
        "images": [{
            "id": "blackpill_f411ce-silkscreen-1.0.0",
            "board": "blackpill_f411ce", "name": "silkscreen",
            "version": "1.0.0", "built": "Jul 26 2026 15:02:35", "proto": 1,
            "method": "dfu", "file": "blackpill_f411ce/silkscreen-1.0.0.bin",
            "size": len(blob), "sha256": hashlib.sha256(blob).hexdigest(),
            "notes": "led, button, st7789_240x240",
        }, {
            "id": "otherboard-silkscreen-1.0.0",
            "board": "otherboard", "name": "silkscreen",
            "version": "1.0.0", "built": "Jul 26 2026 15:02:35", "proto": 1,
            "method": "dfu", "file": "otherboard/silkscreen-1.0.0.bin",
            "size": len(blob), "sha256": hashlib.sha256(blob).hexdigest(),
            "notes": "led",
        }, {
            "id": "esp32_wroom32-silkscreen-1.0.0",
            "board": "esp32_wroom32", "name": "silkscreen",
            "version": "1.0.0", "built": "Jul 26 2026 15:02:35", "proto": 1,
            "method": "esptool", "file": "esp32_wroom32/silkscreen-1.0.0.bin",
            "size": len(esp_blob), "sha256": hashlib.sha256(esp_blob).hexdigest(),
            "notes": "led, wifi",
        }],
    }))
    return tmp_path
```

Update `make_client()` (lines 51-67) to also construct an `EsptoolFlasher` and pass it through `create_app`:

```python
def make_client(bundle, dfu_lines=DFU_LIST, flash_lines=DFU_DOWNLOAD,
                flash_rc=0, caps=("dfu",), esptool_lines=None, esptool_rc=0):
    fake = FakeSerial(responder=device_responder(caps=caps))
    device = DeviceModel(SerialLink(open_port=lambda p: fake))

    def runner(argv):
        from tests.test_firmware import FakeProcess
        if "-D" in argv:
            return FakeProcess(flash_lines, rc=flash_rc)
        return FakeProcess(dfu_lines)

    def esptool_runner(argv):
        from tests.test_firmware import FakeProcess
        return FakeProcess(esptool_lines or ["Writing at 0x0 [====] 100.0% 1/1 bytes..."],
                           rc=esptool_rc)

    from backend.firmware import EsptoolFlasher
    app = create_app(device,
                     catalog=Catalog(bundle),
                     flasher=DfuFlasher(runner=runner),
                     esptool_flasher=EsptoolFlasher(runner=esptool_runner))
    app.state.fake = fake
    app.state.device = device
    return app
```

Add new tests after the existing `test_flash_rejects_a_corrupted_bundled_image` (around line 187):

```python
# --- esptool flashing -----------------------------------------------------------

def test_flash_an_esptool_image_requires_a_port(client):
    r = client.post("/api/firmware/flash",
                    json={"id": "esp32_wroom32-silkscreen-1.0.0"})
    assert r.status_code == 400
    assert "port" in r.json()["detail"]


def test_flash_an_esptool_image_with_a_port(client):
    r = client.post("/api/firmware/flash",
                    json={"id": "esp32_wroom32-silkscreen-1.0.0",
                          "port": "/dev/ttyUSB0"})
    assert r.status_code == 200
    assert r.json()["ok"] is True
    _settle(client)


def test_flash_an_esptool_image_skips_the_dfu_waiting_phase(client):
    with client.websocket_connect("/ws") as ws:
        assert ws.receive_json()["type"] == "state"
        client.post("/api/firmware/flash",
                    json={"id": "esp32_wroom32-silkscreen-1.0.0",
                          "port": "/dev/ttyUSB0"})
        frames = _drain(ws, until=lambda f: f["data"].get("phase") == "done")
    phases = [f["data"]["phase"] for f in frames]
    assert "waiting" not in phases
    assert phases[0] == "flashing"


def test_flash_releases_the_port_if_currently_connected_on_it(bundle):
    app = make_client(bundle)
    with TestClient(app) as c:
        c.post("/api/connect", json={"port": "/dev/ttyUSB0"})
        assert c.get("/api/status").json()["state"] == "connected"

        c.post("/api/firmware/flash",
              json={"id": "esp32_wroom32-silkscreen-1.0.0", "port": "/dev/ttyUSB0"})
        _settle(c)

        assert c.get("/api/status").json()["state"] == "disconnected"
    app.state.device.disconnect()


def test_flash_leaves_a_different_ports_connection_alone(bundle):
    """Flashing an ESP32 on one port must not disconnect a board connected
    on a DIFFERENT port."""
    app = make_client(bundle)
    with TestClient(app) as c:
        c.post("/api/connect", json={"port": "/dev/ttyACM0"})
        c.post("/api/firmware/flash",
              json={"id": "esp32_wroom32-silkscreen-1.0.0", "port": "/dev/ttyUSB0"})
        _settle(c)
        assert c.get("/api/status").json()["state"] == "connected"
    app.state.device.disconnect()


def test_a_failed_esptool_flash_reports_an_error_frame(bundle):
    app = make_client(bundle, esptool_lines=["A fatal error occurred: no device"],
                      esptool_rc=2)
    with TestClient(app) as c:
        with c.websocket_connect("/ws") as ws:
            assert ws.receive_json()["type"] == "state"
            c.post("/api/firmware/flash",
                   json={"id": "esp32_wroom32-silkscreen-1.0.0", "port": "/dev/ttyUSB0"})
            frames = _drain(ws, until=lambda f: f["data"].get("phase") == "error")
        assert frames[-1]["data"]["phase"] == "error"
    app.state.device.disconnect()
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `app/.venv/bin/pytest app/tests/test_firmware_api.py -v -k esptool`
Expected: FAIL — `create_app()` doesn't accept `esptool_flasher`, `FlashBody` has no `port` field, the route doesn't dispatch.

- [ ] **Step 3: Update imports and `FlashBody`**

In `app/backend/main.py`, lines 15-16:

```python
from .firmware import (
    Catalog, DfuFlasher, EsptoolFlasher, FlashBusy, FlashSession, FirmwareError,
    validate_dfu_image, validate_esp32_image)
```

Lines 44-45:

```python
class FlashBody(BaseModel):
    id: str
    port: str | None = None
```

- [ ] **Step 4: Update `create_app()`**

Lines 121-129:

```python
def create_app(device: DeviceModel | None = None,
               catalog: Catalog | None = None,
               flasher: DfuFlasher | None = None,
               esptool_flasher: EsptoolFlasher | None = None) -> FastAPI:
    device = device or DeviceModel()
    catalog = catalog or Catalog()
    flasher = flasher or DfuFlasher()
    esptool_flasher = esptool_flasher or EsptoolFlasher()
    bus = Broadcaster()
    device.subscribe(bus.publish_threadsafe)
    flash = FlashSession(flasher, on_event=lambda ev: bus.publish_event("flash", ev))
```

- [ ] **Step 5: Add the port-release helper and update the route**

Add a nested helper right before `firmware_catalog` (around line 265, inside `create_app`'s body — same indentation level as the other route functions):

```python
    def _release_if_connected_on(port: str) -> None:
        # Only ESP32/esptool flashing needs this: DFU's board disappears
        # from its serial port on its own (see enter_dfu()) before a DFU
        # flash ever starts, so the app is never holding the port DfuFlasher
        # needs. An ESP32 stays on the same port throughout, so if the app
        # happens to be connected to the very board being flashed, esptool
        # can't also open it -- release it first. A board connected on a
        # DIFFERENT port is left alone.
        if device.status().get("port") == port:
            device.disconnect()
```

Replace `firmware_flash` (lines 317-325):

```python
    @app.post("/api/firmware/flash")
    def firmware_flash(body: FlashBody):
        # verify() re-hashes the file on disk instead of trusting the
        # manifest: the two can drift (a bad merge, a partial checkout), and
        # the cost of being wrong is a board that no longer boots.
        path = catalog.verify(body.id)
        img = catalog.get(body.id)
        label = f"{img['name']} {img['version']} ({img['board']})"
        if img.get("method") == "esptool":
            if not body.port:
                raise FirmwareError("a port is required to flash this image")
            _release_if_connected_on(body.port)
            flash.start(path, label, flasher=esptool_flasher, wait=False,
                       port=body.port)
        else:
            flash.start(path, label)
        return {"ok": True, "id": body.id}
```

- [ ] **Step 6: Run the tests to verify they pass**

Run: `app/.venv/bin/pytest app/tests/test_firmware_api.py -v`
Expected: PASS, full file.

- [ ] **Step 7: Run the whole backend test suite**

Run: `app/.venv/bin/pytest -v`
Expected: PASS (confirms `validate_image` -> `validate_dfu_image` rename didn't break `test_firmware_api.py`'s existing upload tests, which are updated in Task 8 — if any fail here referencing `validate_image`, that's expected until Task 8 lands; note it and continue only if the failures are confined to `flash-upload`-related tests, which Task 8 covers next).

- [ ] **Step 8: Commit**

```bash
git add app/backend/main.py app/tests/test_firmware_api.py
git commit -m "feat(app): dispatch /api/firmware/flash to esptool for esptool-method images"
```

---

## Task 8: Backend — wire `/api/firmware/flash-upload`, sync `docs/api.md`

**Files:**
- Modify: `app/backend/main.py:327-343` (`firmware_flash_upload`)
- Modify: `docs/api.md:22-26`, `:180-192`
- Test: `app/tests/test_firmware_api.py`

**Interfaces:**
- Consumes: `validate_dfu_image`, `validate_esp32_image` (Task 5), `_release_if_connected_on` (Task 7).

- [ ] **Step 1: Write the failing tests**

Add to `app/tests/test_firmware_api.py`, after `test_upload_flash_rejects_junk` (around line 220):

```python
def test_upload_flash_esptool_accepts_a_real_looking_image(client):
    from tests.test_firmware import make_esp32_image
    r = client.post(
        "/api/firmware/flash-upload?filename=merged.bin&method=esptool&port=/dev/ttyUSB0",
        content=make_esp32_image())
    assert r.status_code == 200
    assert r.json()["filename"] == "merged.bin"
    _settle(client)


def test_upload_flash_esptool_requires_a_port(client):
    from tests.test_firmware import make_esp32_image
    r = client.post(
        "/api/firmware/flash-upload?filename=merged.bin&method=esptool",
        content=make_esp32_image())
    assert r.status_code == 400
    assert "port" in r.json()["detail"]


def test_upload_flash_esptool_rejects_a_bad_magic_byte(client):
    r = client.post(
        "/api/firmware/flash-upload?filename=merged.bin&method=esptool&port=/dev/ttyUSB0",
        content=b"\x00" * 8192)
    assert r.status_code == 400
    assert "magic" in r.json()["detail"] or "0x1000" in r.json()["detail"]


def test_upload_flash_defaults_to_dfu_method(client):
    """No `method` param at all -- the existing STM32 Advanced-upload
    behavior from before this feature, unchanged."""
    r = client.post("/api/firmware/flash-upload?filename=firmware.bin",
                    content=make_image())
    assert r.status_code == 200
    _settle(client)
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `app/.venv/bin/pytest app/tests/test_firmware_api.py -v -k upload_flash_esptool`
Expected: FAIL — the route doesn't accept `method`/`port` yet.

- [ ] **Step 3: Implement**

Replace `firmware_flash_upload` (lines 327-343):

```python
    @app.post("/api/firmware/flash-upload")
    async def firmware_flash_upload(request: Request, filename: str = "uploaded image",
                                    method: str = "dfu", port: str | None = None):
        """The Advanced path: flash a .bin the user picked themselves.

        Takes the image as the raw request body rather than a multipart form.
        That avoids a `python-multipart` dependency for a single endpoint, and
        the caller only has to produce bytes — no FormData wrapper, and no
        browser-only type in the app's transport seam (see `Api.flashUpload`).

        Unlike a bundled image there is no checksum to check this against, so
        validate_dfu_image()/validate_esp32_image() is the only thing
        standing between "picked the wrong file out of .pio/build" (or the
        wrong TARGET entirely) and a board that no longer enumerates.
        """
        blob = await request.body()
        if method == "esptool":
            validate_esp32_image(blob)
            if not port:
                raise FirmwareError("a port is required to flash this image")
            _release_if_connected_on(port)
        else:
            validate_dfu_image(blob)

        path = upload_dir / "upload.bin"
        path.write_bytes(blob)

        if method == "esptool":
            flash.start(path, filename, flasher=esptool_flasher, wait=False, port=port)
        else:
            flash.start(path, filename)
        return {"ok": True, "filename": filename, "size": len(blob)}
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `app/.venv/bin/pytest app/tests/test_firmware_api.py -v`
Expected: PASS, full file.

- [ ] **Step 5: Run the whole backend test suite**

Run: `app/.venv/bin/pytest -v`
Expected: PASS, every test file.

- [ ] **Step 6: Sync `docs/api.md`**

In `docs/api.md`, update the route table (lines 25-26):

```
| POST | `/api/firmware/flash` | `{"id": "<catalog id>", "port": "<optional, required for esptool-method images>"}` | `{ok, id}` |
| POST | `/api/firmware/flash-upload?method=dfu\|esptool&port=<required for esptool>` | raw `.bin` bytes | `{ok, filename, size}` |
```

After the existing paragraph ending "...the manifest and the binary are two files that can drift apart." and the `flash-upload` paragraph that follows it (lines 187-192), add:

```markdown
An `esptool`-method image (currently `esp32_wroom32`) is flashed differently under the hood: an
ESP32 in its ROM bootloader has no distinct USB identity to detect the way a Black Pill in DFU
mode does (`0483:df11`), so both `flash` and `flash-upload` require an explicit `port` for such
an image, and skip the `waiting` phase entirely (there is nothing to poll for). If the app is
currently connected to the board on that same port, it is released first so `esptool` can open
it; a connection on a different port is left alone.
```

- [ ] **Step 7: Commit**

```bash
git add app/backend/main.py docs/api.md app/tests/test_firmware_api.py
git commit -m "feat(app): dispatch /api/firmware/flash-upload to esptool, sync docs/api.md"
```

---

## Task 9: Frontend — bundled-image flow gets a port picker

**Files:**
- Modify: `app/web/app.js:70` (`Api.flashBundled`), `:357-480` (firmware store), `:560-593` (`refreshPorts`/port label), `:868-916` (`setDfuPolling`, `fw-flash` handler)
- Modify: `app/web/index.html:421-524` (Firmware page header badge, controls row)

**Interfaces:**
- Consumes: `Api.ports()` (already exists), the manifest's `method` field (already flows through `Catalog.images()`, no backend change needed here).
- Produces: `Alpine.store('firmware').ports`, `.selectedPort`, `.selectedMethod`, `.hasDfuImages`, `.portLabel(p)`, `.refreshPorts()` — new store state/methods other tasks (Task 10) also use.

- [ ] **Step 1: Extract the shared port-label helper**

In `app/web/app.js`, `refreshPorts()` (lines 560-593) currently builds each `<option>`'s label inline. Extract it so the new firmware-page picker can reuse the exact same rule. Add, right before `async function refreshPorts()`:

```js
function portOptionLabel(p) {
  // Every board this template's `match` heuristic doesn't recognize (not
  // one of link.py's _KNOWN_BOARDS) used to render as a bare path,
  // indistinguishable from this environment's own placeholder serial
  // ports (or any other port with nothing plugged in). Any port with a
  // real USB descriptor at least proves *something* is actually
  // connected there, which is worth surfacing even without a name for it.
  return p.board ? `${p.port} (${p.board})`
    : p.vid ? `${p.port} (USB ${p.vid}:${p.pid})`
    : p.port;
}
```

Then simplify `refreshPorts()`'s body to use it — replace the `o.textContent = ...` block (the multi-line ternary) with:

```js
    o.textContent = portOptionLabel(p);
```

- [ ] **Step 2: Add `Api.flashBundled`'s port parameter**

In `app/web/app.js` line 70:

```js
  flashBundled: (id, port) => Api.send('POST', '/api/firmware/flash', port ? { id, port } : { id }),
```

- [ ] **Step 3: Extend the firmware store**

In `app/web/app.js`, the `Alpine.store('firmware', {...})` object (lines 361-480). Add new state alongside the existing `images`/`recommended`/etc. (after `op: null,` around line 369):

```js
    ports: [],
    selectedPort: null,
```

Add new getters, right after `get canEnterDfu()` (lines 412-415):

```js
    get selectedImage() {
      return this.images.find((i) => i.id === this.selected) || null;
    },

    get selectedMethod() {
      return this.selectedImage?.method || 'dfu';
    },

    // Hides the DFU-mode badge/polling text when the catalog has no
    // dfu-method image at all -- an ESP32-only bundle, say -- rather than
    // showing DFU-specific chrome that can never apply to anything selected.
    get hasDfuImages() {
      return this.images.some((i) => i.method !== 'esptool');
    },
```

Replace the existing `canFlash` getter (lines 417-420):

```js
    get canFlash() {
      const img = this.selectedImage;
      if (!img || !img.available || this.busy) return false;
      return img.method === 'esptool' ? !!this.selectedPort : this.dfuPresent;
    },
```

Add `portLabel` and `refreshPorts` methods, right after `async refresh()` (lines 434-443):

```js
    portLabel(p) {
      return portOptionLabel(p);
    },

    async refreshPorts() {
      try {
        this.ports = await Api.ports();
        const stillThere = (port) => this.ports.some((p) => p.port === port);
        if (!stillThere(this.selectedPort)) {
          this.selectedPort = this.ports.find((p) => p.match)?.port || null;
        }
      } catch { /* backend restarting; the next page-enter retries */ }
    },
```

- [ ] **Step 4: Refresh ports on page entry**

In `app/web/app.js`, `setDfuPolling()` (lines 868-878), add a call inside the `on` branch:

```js
function setDfuPolling(on) {
  const store = window.Alpine?.store('firmware');
  if (on && store && !dfuPollTimer) {
    store.syncDevice(connected, deviceInfo);
    store.refresh();
    store.refreshPorts();
    store.pollDfu();
    dfuPollTimer = setInterval(() => store.pollDfu(), 1500);
  } else if (!on && dfuPollTimer) {
    clearInterval(dfuPollTimer);
    dfuPollTimer = null;
  }
}
```

- [ ] **Step 5: Update the `fw-flash` click handler**

In `app/web/app.js`, lines 898-916:

```js
el('fw-flash').addEventListener('click', async () => {
  const store = Alpine.store('firmware');
  const img = store.images.find((i) => i.id === store.selected);
  if (!img) return;
  if (!window.confirm(
      `Flash ${img.name} ${img.version} (${img.board}) to the board?\n\n`
      + 'This overwrites the firmware currently on it.')) return;
  store.begin();
  firmwareLog(`> flash ${img.id}`);
  try {
    await Api.flashBundled(img.id, img.method === 'esptool' ? store.selectedPort : undefined);
  } catch (e) {
    store.onFlashEvent({ phase: 'error', line: e.message });
    showError(e.message);
  }
});
```

(only the `Api.flashBundled(img.id)` call changes, to `Api.flashBundled(img.id, ...)`.)

- [ ] **Step 6: Update `index.html`**

In `app/web/index.html`, the DFU-mode badge (lines 427-429) gets a visibility condition:

```html
              <span class="badge" x-show="$store.firmware.hasDfuImages"
                    :class="$store.firmware.dfuPresent ? 'text-bg-warning' : 'text-bg-secondary'"
                    x-text="$store.firmware.dfuPresent ? 'DFU mode' : 'no DFU device'"></span>
```

Replace the controls row (lines 489-501):

```html
          <div class="d-flex gap-2 flex-wrap align-items-center mb-3">
            <!-- Gated on the device advertising `dfu` in its capabilities:
                 firmware built without FEATURE_DFU (or predating it entirely)
                 would just answer an error. -->
            <button id="fw-enter-dfu" class="btn btn-outline-secondary"
                    x-show="$store.firmware.selectedMethod !== 'esptool'"
                    :disabled="!$store.firmware.canEnterDfu"
                    x-text="$store.firmware.dfuPresent ? 'Already in DFU mode' : 'Reboot to DFU'"></button>

            <!-- Unlike a Black Pill in DFU mode, an ESP32 in its ROM
                 bootloader has no distinct USB identity to detect -- there
                 is no dfu-status equivalent to poll, so the port has to be
                 picked explicitly. -->
            <select id="fw-port" class="form-select form-select-sm w-auto"
                    x-show="$store.firmware.selectedMethod === 'esptool'"
                    x-model="$store.firmware.selectedPort">
              <template x-for="p in $store.firmware.ports" :key="p.port">
                <option :value="p.port" x-text="$store.firmware.portLabel(p)"></option>
              </template>
            </select>

            <button id="fw-flash" class="btn btn-danger"
                    :disabled="!$store.firmware.canFlash">Flash selected firmware</button>
            <span class="text-secondary small"
                  x-show="$store.firmware.selectedMethod !== 'esptool' && !$store.firmware.dfuPresent">
              The board must be in DFU mode first.
            </span>
          </div>
```

- [ ] **Step 7: Verify in a headless browser**

Per CLAUDE.md, this project has no automated UI test suite by design but a real check is still required — "believing you cannot check the UI has cost real defects." Write a throwaway script using the project's own fake-device pattern:

```python
# /tmp/claude-.../scratchpad/verify_fw_port_picker.py
import subprocess, time, sys, threading
sys.path.insert(0, "/home/godar/Projects/stm32/silkscreen/app")
import uvicorn
from backend.device import DeviceModel
from backend.firmware import Catalog
from backend.link import SerialLink
from backend.main import create_app

# Fake ESP32-method catalog, no board connected -- exercises the
# no-connection Firmware page path (the one that matters most, since a
# board needing a flash is often one that can't be talked to).
import json, hashlib, tempfile
from pathlib import Path
bundle = Path(tempfile.mkdtemp())
blob = b"\xff" * 0x1000 + b"\xe9" + b"\x00" * 512
(bundle / "esp32_wroom32").mkdir()
(bundle / "esp32_wroom32" / "silkscreen-1.0.0.bin").write_bytes(blob)
(bundle / "manifest.json").write_text(json.dumps({
    "app_version": "1.0.0",
    "images": [{"id": "esp32_wroom32-silkscreen-1.0.0", "board": "esp32_wroom32",
               "name": "silkscreen", "version": "1.0.0", "built": "x", "proto": 1,
               "method": "esptool", "file": "esp32_wroom32/silkscreen-1.0.0.bin",
               "size": len(blob), "sha256": hashlib.sha256(blob).hexdigest(),
               "notes": "led, wifi"}]}))

app = create_app(DeviceModel(), catalog=Catalog(bundle))
server = uvicorn.Server(uvicorn.Config(app, port=8199, log_level="warning"))
t = threading.Thread(target=server.run, daemon=True)
t.start()
time.sleep(1)

from playwright.sync_api import sync_playwright
with sync_playwright() as p:
    browser = p.chromium.launch(headless=True)
    page = browser.new_page()
    errors = []
    page.on("pageerror", lambda e: errors.append(str(e)))
    page.on("console", lambda m: errors.append(m.text) if m.type == "error" else None)
    page.goto("http://127.0.0.1:8199/")
    page.click("text=Firmware")
    page.wait_for_selector("#fw-port", state="visible", timeout=3000)
    assert page.is_visible("#fw-port"), "port picker should show for an esptool image"
    assert not page.is_visible("#fw-enter-dfu"), "Reboot to DFU should hide for an esptool image"
    print("OK: port picker visible, DFU button hidden, console errors:", errors)
    browser.close()
server.should_exit = True
```

Run: `~/.pwvenv/bin/python3 /tmp/claude-.../scratchpad/verify_fw_port_picker.py`
Expected: `OK: port picker visible, DFU button hidden, console errors: []`

If `#fw-port` never becomes visible, check `x-show`/`x-model` bindings and that `Catalog.images()` is actually returning `method: "esptool"` for the fixture (the catalog fixture here doesn't need the real `app/firmware/` dir — `create_app(..., catalog=Catalog(bundle))` points it at the throwaway one built above).

- [ ] **Step 8: Commit**

```bash
git add app/web/app.js app/web/index.html
git commit -m "feat(app): port picker for esptool-method images on the Firmware page"
```

---

## Task 10: Frontend — Advanced upload gets a target + port picker, DFU-by-hand gains ESP32 text

**Files:**
- Modify: `app/web/app.js:76-77` (`Api.flashUpload`), firmware store (from Task 9), `:917-936` (`fw-upload`/`fw-upload-file` handlers)
- Modify: `app/web/index.html:528-564` ("Getting into DFU mode by hand", Advanced section)

**Interfaces:**
- Consumes: `store.ports`, `store.portLabel()`, `store.refreshPorts()` (Task 9).
- Produces: `Alpine.store('firmware').uploadTarget`, `.uploadPort`, `.canUpload`.

- [ ] **Step 1: Update `Api.flashUpload`**

In `app/web/app.js`, lines 76-77:

```js
  // The image goes up as the raw request body, not multipart: that keeps the
  // backend free of a python-multipart dependency. Takes bytes rather than the
  // File itself even though fetch would accept a File as a body directly --
  // a File cannot cross an IPC boundary, and this would otherwise be the one
  // method whose signature pins the UI to an HTTP transport.
  flashUpload: (bytes, filename, method = 'dfu', port = null) => {
    const params = new URLSearchParams({ filename, method });
    if (port) params.set('port', port);
    return Api.sendBody(`/api/firmware/flash-upload?${params}`, bytes);
  },
```

- [ ] **Step 2: Add upload target/port state to the firmware store**

In `app/web/app.js`, in the `Alpine.store('firmware', {...})` object, alongside `selectedPort` (added in Task 9):

```js
    uploadTarget: 'dfu',
    uploadPort: null,
```

Add a `canUpload` getter next to `canFlash`:

```js
    get canUpload() {
      if (this.busy) return false;
      return this.uploadTarget === 'esptool' ? !!this.uploadPort : this.dfuPresent;
    },
```

Extend `refreshPorts()` (added in Task 9) to also default `uploadPort`:

```js
    async refreshPorts() {
      try {
        this.ports = await Api.ports();
        const stillThere = (port) => this.ports.some((p) => p.port === port);
        if (!stillThere(this.selectedPort)) {
          this.selectedPort = this.ports.find((p) => p.match)?.port || null;
        }
        if (!stillThere(this.uploadPort)) {
          this.uploadPort = this.ports.find((p) => p.match)?.port || null;
        }
      } catch { /* backend restarting; the next page-enter retries */ }
    },
```

(this replaces the version of `refreshPorts()` written in Task 9, Step 3 — same function, one more `if` block).

- [ ] **Step 3: Update the upload handlers**

In `app/web/app.js`, `el('fw-upload')`'s click handler (line 917) is unchanged. `el('fw-upload-file')`'s change handler (lines 919-936):

```js
el('fw-upload-file').addEventListener('change', async (ev) => {
  const file = ev.target.files[0];
  ev.target.value = '';   // so re-picking the same file fires `change` again
  if (!file) return;
  const store = Alpine.store('firmware');
  if (!window.confirm(
      `Flash ${file.name} to the board?\n\n`
      + 'Nothing verifies this file matches your board.')) return;
  store.begin();
  firmwareLog(`> flash ${file.name} (${file.size} bytes)`);
  try {
    await Api.flashUpload(await file.arrayBuffer(), file.name,
                          store.uploadTarget,
                          store.uploadTarget === 'esptool' ? store.uploadPort : null);
  } catch (e) {
    store.onFlashEvent({ phase: 'error', line: e.message });
    showError(e.message);
  }
});
```

- [ ] **Step 4: Update `index.html`**

In `app/web/index.html`, after the "Getting into DFU mode by hand" section's existing paragraph (right after line 540, before the `<details>` block starting at line 542), add:

```html
          <p class="text-secondary small">
            For an ESP32 board: hold the <strong>BOOT</strong> (sometimes labeled
            <strong>IO0</strong>) button until <code>esptool</code> reports
            "Writing at...". Only needed if the specific board lacks auto-reset
            circuitry, or if flashing hangs at "Connecting....".
          </p>
```

Replace the Advanced section's file-picker button block (lines 552-562):

```html
              <p class="small text-secondary">
                For a locally built STM32 image this is
                <code>firmware/.pio/build/&lt;env&gt;/firmware.bin</code> — the
                <code>.bin</code>, not the <code>.elf</code> or <code>.hex</code> beside it.
                For ESP32 it's the merged image <code>bundle_firmware.py</code> itself
                produces, or the output of <code>esptool merge-bin</code> run by hand.
              </p>
              <div class="d-flex gap-2 flex-wrap align-items-center mb-2">
                <select id="fw-target" class="form-select form-select-sm w-auto"
                        x-model="$store.firmware.uploadTarget">
                  <option value="dfu">STM32 (DFU)</option>
                  <option value="esptool">ESP32 (esptool)</option>
                </select>
                <select id="fw-upload-port" class="form-select form-select-sm w-auto"
                        x-show="$store.firmware.uploadTarget === 'esptool'"
                        x-model="$store.firmware.uploadPort">
                  <template x-for="p in $store.firmware.ports" :key="p.port">
                    <option :value="p.port" x-text="$store.firmware.portLabel(p)"></option>
                  </template>
                </select>
              </div>
              <button id="fw-upload" class="btn btn-outline-danger btn-sm"
                      :disabled="!$store.firmware.canUpload">
                Choose a .bin and flash…
              </button>
              <input id="fw-upload-file" type="file" accept=".bin,application/octet-stream"
                     class="d-none">
```

- [ ] **Step 5: Verify in a headless browser**

Extend the Task 9 verification script (or write a fresh one reusing the same fake-catalog setup) to also open the "Advanced: flash a local file" `<details>` and check the new controls:

```python
    page.click("text=Advanced: flash a local file")
    page.select_option("#fw-target", "esptool")
    page.wait_for_selector("#fw-upload-port", state="visible", timeout=3000)
    assert page.is_visible("#fw-upload-port")
    page.select_option("#fw-target", "dfu")
    assert not page.is_visible("#fw-upload-port")
    print("OK: advanced upload target/port picker toggles correctly")
```

Run: `~/.pwvenv/bin/python3 /tmp/claude-.../scratchpad/verify_fw_port_picker.py`
Expected: both OK lines print, `errors` list empty.

- [ ] **Step 6: Commit**

```bash
git add app/web/app.js app/web/index.html
git commit -m "feat(app): Advanced upload supports ESP32/esptool with a target+port picker"
```

---

## Task 11: Real-hardware verification

Not a code task — this project's standing rule is that green tests alone don't close out a
flashing-path change (`_notes/todo.md`, `docs/architecture.md`). No steps here are automated;
work through them by hand against the real `esp32_wroom32` board (CP2102 bridge already observed
at `/dev/ttyUSB1` on this machine during planning).

- [ ] Build and bundle the real image: `python3 app/tools/bundle_firmware.py --all` from the repo
  root. Confirm `app/firmware/manifest.json` lists `esp32_wroom32-silkscreen-1.0.0` with
  `"method": "esptool"`, and `app/firmware/esp32_wroom32/silkscreen-1.0.0.bin` exists.
- [ ] Confirm the bundled file is actually flashable: `esptool --chip esp32 --port /dev/ttyUSB1
  --baud 460800 write-flash 0x0 app/firmware/esp32_wroom32/silkscreen-1.0.0.bin` from a shell
  (not through the app), then `pio device monitor -b 115200` and confirm the boot log
  (`boot: silkscreen 1.0.0 (esp32_wroom32) built ...`).
- [ ] Start the backend (`app/.venv/bin/uvicorn backend.main:app --port 8080` from `app/`) and
  open the Firmware page in a real browser. Confirm the ESP32 image shows a port dropdown, not
  a "must be in DFU mode" message, and that `/dev/ttyUSB1` appears labeled `(ESP32)`.
- [ ] Pick the port, click "Flash selected firmware", confirm the progress bar actually moves
  (not stuck at 0% — this is what would break silently if the regex from Task 5 doesn't match
  real hardware output exactly as it matched the captured-format tests) and the log shows real
  `esptool` lines with no raw ANSI escape bytes visible.
- [ ] Confirm the board reboots into the new firmware and the app's Connect page picks it back up
  without a manual restart.
- [ ] Repeat via the Advanced upload path with a manually-built merged `.bin` (`esptool merge-bin`
  run by hand against `.pio/build/esp32_wroom32/*` and the framework's `boot_app0.bin`), to
  exercise the path that has no manifest/sha256 to lean on.
- [ ] If the app was left connected to the ESP32 before starting a flash from the Firmware page,
  confirm the Connect-page state flips to disconnected within a couple of seconds (via the
  existing telemetry watchdog — no new frontend code should be needed for this, see
  `startWatchdog()` in `app.js`) rather than staying stuck showing "connected" to a board whose
  port was just handed to `esptool`.
- [ ] Update `_notes/todo.md`'s "Second board target" entry (the one narrowed 2026-07-27) to
  note the `method != dfu` dispatch is no longer untested/unbuilt — mark it however the rest of
  that file's completed items are marked, and add a one-line summary of this whole feature to
  `CHANGELOG.md` under a new dated entry, matching the existing entries' style.
