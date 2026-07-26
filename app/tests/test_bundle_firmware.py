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
