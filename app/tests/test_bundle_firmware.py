"""The release script, driven against a fixture tree with no toolchain.

`bundle_firmware.py` is a standalone script (stdlib only, so it runs from a
bare checkout with no venv), not a package member -- hence the importlib load
below.

Only the one call that needs PlatformIO is faked. Everything else runs for
real against a miniature firmware tree: platformio.ini is really parsed for
`-D BOARD_HEADER`, config.h and the board header are really read, and the
synthetic binaries really go through the vector-table and identity checks.
A fake that stubbed out `plan_entry` would test nothing worth testing.

The cases that matter most are the destructive ones. This script deletes
files, and it is the only thing standing between a manifest and a device's
flash, so "a failed build writes nothing" and "pruning stays inside what a
manifest listed" are the invariants under test.
"""
import hashlib
import importlib.util
import json
from pathlib import Path

import pytest

SCRIPT = Path(__file__).resolve().parents[1] / "tools" / "bundle_firmware.py"


def load_script():
    spec = importlib.util.spec_from_file_location("bundle_firmware", SCRIPT)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


# --- fixture tree -------------------------------------------------------------

STAMP_A = "Jul 27 2026 05:34:14"
STAMP_B = "Jul 27 2026 06:11:02"


def fake_bin(name: str, version: str, board: str, stamp: str) -> bytes:
    """A blob that passes check_vector_table() and check_identity().

    MSP is 0x20020000 (the top of the F411's SRAM, the value a real build
    has) and the reset vector is a Thumb address in flash. The identity
    strings and the __DATE__-shaped stamp are what the script greps back out
    of a real image.
    """
    head = (0x2002_0000).to_bytes(4, "little") + (0x0800_0abd).to_bytes(4, "little")
    body = b"\x00".join(s.encode() for s in (name, version, board, stamp))
    return head + body + b"\x00" * (2048 - len(head) - len(body))


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


@pytest.fixture
def tree(tmp_path, monkeypatch):
    """A repo-shaped fixture the script can be pointed at wholesale."""
    mod = load_script()
    root = tmp_path
    fw = root / "firmware"
    (fw / "include" / "boards").mkdir(parents=True)
    (fw / "src" / "core").mkdir(parents=True)
    (root / "app" / "web").mkdir(parents=True)

    (fw / "include" / "config.h").write_text(
        '#define FW_PROJECT_NAME "silkscreen"\n'
        '#define FW_VERSION "1.0.0"\n')
    (fw / "src" / "core" / "types.h").write_text(
        "constexpr int kProtoVersion = 1;\n")
    (root / "app" / "web" / "app.js").write_text("const APP_VERSION = '1.0.0';\n")

    ini = []
    for board in ("board_a", "board_b"):
        (fw / "include" / "boards" / f"{board}.h").write_text(
            f'#define BOARD_ID "{board}"\n'
            f"#define FEATURE_LED 1\n"
            f"#define FEATURE_DFU 1\n")
        ini.append(f"[env:{board}]\n"
                   f"build_flags = -D BOARD_HEADER='\"boards/{board}.h\"'\n")
    (fw / "platformio.ini").write_text("\n".join(ini))

    monkeypatch.setattr(mod, "ROOT", root)
    monkeypatch.setattr(mod, "FIRMWARE", fw)
    monkeypatch.setattr(mod, "BUNDLE", root / "app" / "firmware")
    return mod


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


def test_merge_esp32_image_invokes_esptool_with_the_right_argv(esp32_tree):
    """The other merge tests' fakes only look at `-o` -- none of them check
    that the offsets/paths/flags actually sent to esptool are right. A
    transposed offset in ESP32_MERGE_LAYOUT, or a flag reverted to a
    deprecated underscored spelling (`merge_bin` instead of `merge-bin`),
    would pass every other test here and only bite at real-flash time."""
    mod = esp32_tree
    build_dir = build_esp32_parts_into(mod, "board_c")

    calls = []

    def recording_esptool(argv):
        calls.append(argv)
        out = Path(argv[argv.index("-o") + 1])
        out.write_bytes(b"\xff" * 0x1000 + b"\xe9" + b"\x00" * 512)
        return 0

    mod.merge_esp32_image("board_c", runner=recording_esptool)

    assert len(calls) == 1
    argv = calls[0]

    assert argv[0:3] == ["esptool", "--chip", "esp32"]
    assert argv[3] == "merge-bin"
    assert "merge_bin" not in argv   # the deprecated underscored spelling

    assert argv[argv.index("--flash-mode") + 1] == "dio"
    assert argv[argv.index("--flash-freq") + 1] == "40m"
    assert argv[argv.index("--flash-size") + 1] == "4MB"

    # Offset/path pairs, in the documented ESP32_MERGE_LAYOUT order.
    tail = argv[argv.index("--flash-size") + 2:]
    pairs = list(zip(tail[0::2], tail[1::2]))
    assert pairs == [
        ("0x1000", str(build_dir / "bootloader.bin")),
        ("0x8000", str(build_dir / "partitions.bin")),
        ("0xe000", str(mod.BOOT_APP0_PATH)),
        ("0x10000", str(build_dir / "firmware.bin")),
    ]


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


def test_release_checks_esp32_image_format_before_identity(esp32_tree, monkeypatch):
    """Mirrors test_a_binary_that_fails_validation_stops_the_whole_release's
    technique for the DFU path: firmware.bin here carries none of the
    FW_PROJECT_NAME/FW_VERSION/BOARD_ID strings check_identity() looks for,
    AND the merged image is missing its format magic byte, so BOTH checks
    would fail if reached. Only the check that runs first ever raises.

    A version with valid identity strings would not prove anything here --
    check_identity() would then pass silently regardless of which check ran
    first, and the observed error would be the format error either way. Only
    making both fail lets the assertion tell the two orderings apart.
    """
    mod = esp32_tree

    def builder(env, pio):
        build_dir = mod.FIRMWARE / ".pio" / "build" / env
        build_dir.mkdir(parents=True, exist_ok=True)
        (build_dir / "bootloader.bin").write_bytes(b"\xe9" + b"\x11" * 256)
        (build_dir / "partitions.bin").write_bytes(b"\x00" * 128)
        # No identity strings at all -- see the docstring above.
        (build_dir / "firmware.bin").write_bytes(b"\x00" * 2048)

    def bad_esptool(argv):
        out = Path(argv[argv.index("-o") + 1])
        # Missing the ESP image magic byte at 0x1000.
        out.write_bytes(b"\xff" * 0x1000 + b"\x00" + b"\x00" * 512)
        return 0
    monkeypatch.setattr(mod, "_run_esptool", bad_esptool)

    with pytest.raises(mod.BundleError) as exc_info:
        mod.release(["board_c"], builder=builder)

    msg = str(exc_info.value)
    assert "0xE9" in msg or "magic" in msg or "0x1000" in msg
    assert "does not contain" not in msg   # would mean check_identity() ran first


def build_into(mod, env: str, stamp: str = STAMP_A, board: str | None = None):
    """Put a plausible firmware.bin where a `pio run` would have left one."""
    path = mod.bin_path_for(env)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(fake_bin("silkscreen", "1.0.0", board or env, stamp))
    return path


def builder_for(mod, *, fails: set[str] = frozenset(), stamps: dict | None = None):
    """A stand-in for `pio run` that writes a binary instead of compiling."""
    def builder(env, pio):
        if env in fails:
            raise mod.BundleError(f"`pio run -e {env}` failed:\nsimulated")
        build_into(mod, env, (stamps or {}).get(env, STAMP_A))
    return builder


def manifest(mod) -> dict:
    return json.loads(mod.manifest_path().read_text())


def ids(mod) -> list[str]:
    return [img["id"] for img in manifest(mod)["images"]]


# --- the happy path -----------------------------------------------------------

def test_releases_every_named_env_into_one_manifest(tree):
    mod = tree
    entries, pruned = mod.release(["board_a", "board_b"],
                                  builder=builder_for(mod, stamps={"board_b": STAMP_B}))

    assert [e["id"] for e in entries] == [
        "board_a-silkscreen-1.0.0", "board_b-silkscreen-1.0.0"]
    assert pruned == []
    assert ids(mod) == ["board_a-silkscreen-1.0.0", "board_b-silkscreen-1.0.0"]
    assert manifest(mod)["app_version"] == "1.0.0"

    for img in manifest(mod)["images"]:
        blob = (mod.BUNDLE / img["file"]).read_bytes()
        assert len(blob) == img["size"]
        assert hashlib.sha256(blob).hexdigest() == img["sha256"]

    # Derived from the board header, not typed in anywhere.
    assert manifest(mod)["images"][0]["notes"] == "led, dfu"
    assert manifest(mod)["images"][1]["built"] == STAMP_B


def test_no_envs_falls_back_to_the_default(tree, monkeypatch):
    """The zero-argument form documented in CLAUDE.md and readme.md."""
    mod = tree
    monkeypatch.setattr(mod, "DEFAULT_ENV", "board_a")
    entries, _ = mod.release([], builder=builder_for(mod))
    assert [e["board"] for e in entries] == ["board_a"]


def test_dry_run_writes_nothing(tree):
    mod = tree
    entries, pruned = mod.release(["board_a"], dry_run=True,
                                  builder=builder_for(mod))
    assert len(entries) == 1
    assert pruned == []
    assert not mod.manifest_path().exists()
    assert not (mod.BUNDLE / "board_a").exists()


# --- rebuild vs --add ---------------------------------------------------------

def test_rebuild_prunes_an_image_the_previous_release_shipped(tree):
    mod = tree
    mod.release(["board_a", "board_b"], builder=builder_for(mod))
    assert (mod.BUNDLE / "board_b" / "silkscreen-1.0.0.bin").is_file()

    _, pruned = mod.release(["board_a"], builder=builder_for(mod))

    assert ids(mod) == ["board_a-silkscreen-1.0.0"]
    assert [p.name for p in pruned] == ["silkscreen-1.0.0.bin"]
    assert not (mod.BUNDLE / "board_b" / "silkscreen-1.0.0.bin").exists()
    # the board directory it emptied goes too
    assert not (mod.BUNDLE / "board_b").exists()
    # ...while the board still shipping is untouched
    assert (mod.BUNDLE / "board_a" / "silkscreen-1.0.0.bin").is_file()


def test_add_keeps_the_previous_release(tree):
    mod = tree
    mod.release(["board_a"], builder=builder_for(mod))
    _, pruned = mod.release(["board_b"], add=True, builder=builder_for(mod))

    assert pruned == []
    assert ids(mod) == ["board_a-silkscreen-1.0.0", "board_b-silkscreen-1.0.0"]
    assert (mod.BUNDLE / "board_a" / "silkscreen-1.0.0.bin").is_file()
    assert (mod.BUNDLE / "board_b" / "silkscreen-1.0.0.bin").is_file()


def test_re_releasing_the_same_env_replaces_its_entry_in_place(tree):
    mod = tree
    mod.release(["board_a"], builder=builder_for(mod))
    mod.release(["board_a"], builder=builder_for(mod, stamps={"board_a": STAMP_B}))

    assert ids(mod) == ["board_a-silkscreen-1.0.0"]
    assert manifest(mod)["images"][0]["built"] == STAMP_B
    # Rewritten, not pruned: the file is a keeper because the new manifest
    # names it too.
    assert (mod.BUNDLE / "board_a" / "silkscreen-1.0.0.bin").is_file()


def test_pruning_never_touches_a_file_no_manifest_listed(tree):
    mod = tree
    mod.release(["board_a", "board_b"], builder=builder_for(mod))
    stray = mod.BUNDLE / "board_b" / "hand-placed.bin"
    stray.write_bytes(b"mine")

    mod.release(["board_a"], builder=builder_for(mod))

    assert stray.read_bytes() == b"mine"
    # and the directory survives precisely because it isn't empty
    assert (mod.BUNDLE / "board_b").is_dir()


def test_prune_ignores_an_entry_pointing_outside_the_bundle(tree):
    mod = tree
    mod.release(["board_a"], builder=builder_for(mod))
    outsider = mod.BUNDLE.parent / "not-ours.bin"
    outsider.write_bytes(b"keep")

    data = manifest(mod)
    data["images"].append({"id": "evil", "file": "../not-ours.bin"})
    mod.manifest_path().write_text(json.dumps(data))

    mod.release(["board_b"], builder=builder_for(mod))

    assert outsider.read_bytes() == b"keep"


# --- all or nothing -----------------------------------------------------------

def test_one_failed_env_leaves_the_previous_release_intact(tree):
    mod = tree
    mod.release(["board_a"], builder=builder_for(mod))
    before = mod.manifest_path().read_text()

    with pytest.raises(mod.BundleError, match="board_b"):
        mod.release(["board_a", "board_b"],
                    builder=builder_for(mod, fails={"board_b"}))

    assert mod.manifest_path().read_text() == before
    assert (mod.BUNDLE / "board_a" / "silkscreen-1.0.0.bin").is_file()
    assert not (mod.BUNDLE / "board_b").exists()


def test_a_binary_that_fails_validation_stops_the_whole_release(tree):
    mod = tree
    mod.release(["board_a"], builder=builder_for(mod))
    before = mod.manifest_path().read_text()

    def bad_builder(env, pio):
        path = mod.bin_path_for(env)
        path.parent.mkdir(parents=True, exist_ok=True)
        # An ELF, the realistic mistake -- no valid vector table.
        path.write_bytes(b"\x7fELF" + b"\x00" * 2044)

    with pytest.raises(mod.BundleError, match="stack pointer"):
        mod.release(["board_a", "board_b"], builder=bad_builder)

    assert mod.manifest_path().read_text() == before


def test_identity_mismatch_is_caught_before_anything_is_written(tree):
    mod = tree

    def stale_builder(env, pio):
        path = mod.bin_path_for(env)
        path.parent.mkdir(parents=True, exist_ok=True)
        # Built from a different board header than the env claims.
        path.write_bytes(fake_bin("silkscreen", "1.0.0", "some_other_board", STAMP_A))

    with pytest.raises(mod.BundleError, match="BOARD_ID"):
        mod.release(["board_a"], builder=stale_builder)

    assert not mod.manifest_path().exists()


# --- argument checking --------------------------------------------------------

def test_an_env_named_twice_is_rejected(tree):
    mod = tree
    with pytest.raises(mod.BundleError, match="more than once"):
        mod.release(["board_a", "board_a"], builder=builder_for(mod))


def test_two_envs_producing_the_same_image_id_are_rejected(tree):
    mod = tree
    # board_b's header made to claim board_a's identity: both envs would
    # write the same file, and the manifest would describe only the winner.
    (mod.FIRMWARE / "include" / "boards" / "board_b.h").write_text(
        '#define BOARD_ID "board_a"\n#define FEATURE_LED 1\n')

    with pytest.raises(mod.BundleError, match="same image id"):
        mod.release(["board_a", "board_b"],
                    builder=lambda env, pio: build_into(mod, env, board="board_a"))

    assert not mod.manifest_path().exists()


def test_unknown_env_is_reported(tree):
    mod = tree
    with pytest.raises(mod.BundleError, match=r"no \[env:nope\]"):
        mod.release(["nope"], builder=builder_for(mod))


def test_a_corrupt_existing_manifest_stops_the_release(tree):
    mod = tree
    mod.BUNDLE.mkdir(parents=True)
    mod.manifest_path().write_text("{not json")

    with pytest.raises(mod.BundleError, match="not valid JSON"):
        mod.release(["board_a"], builder=builder_for(mod))


# --- the build stamp must be truthful in a RELEASED image ----------------------
# __DATE__/__TIME__ are frozen into version.cpp's object file when it last
# compiled, and SCons will not recompile a TU whose inputs are unchanged. Since
# FW_VERSION stays 1.0.0 by project policy, the stamp is the app's ONLY way to
# tell a running board apart from a bundled image -- so the release build has to
# force that one object to rebuild.

def test_force_version_rebuild_removes_the_stale_object(tree):
    mod = tree
    obj = mod.FIRMWARE / ".pio" / "build" / "board_a" / "src" / "core" / "version.cpp.o"
    obj.parent.mkdir(parents=True)
    obj.write_bytes(b"stale")
    other = obj.parent / "dispatch.cpp.o"
    other.write_bytes(b"keep")

    removed = mod.force_version_rebuild("board_a")

    assert removed == [obj]
    assert not obj.exists()
    assert other.exists()     # only version.cpp is re-stamped, not a full rebuild


def test_force_version_rebuild_tolerates_a_tree_that_has_never_been_built(tree):
    """A clean checkout has no .pio at all, and that is the normal first run."""
    mod = tree
    assert mod.force_version_rebuild("board_a") == []

    # Build directory present but no object file yet -- same requirement.
    (mod.FIRMWARE / ".pio" / "build" / "board_a").mkdir(parents=True)
    assert mod.force_version_rebuild("board_a") == []


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
