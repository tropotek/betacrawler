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

SCRIPT = Path(__file__).resolve().parent / "bundle_firmware.py"


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
    (root / "web-app" / "js").mkdir(parents=True)

    (fw / "include" / "config.h").write_text(
        '#define FW_PROJECT_NAME "betacrawler"\n'
        '#define FW_VERSION "1.0.0"\n')
    (fw / "src" / "core" / "types.h").write_text(
        "constexpr int kProtoVersion = 1;\n")
    (root / "web-app" / "js" / "app.js").write_text("const APP_VERSION = '1.0.0';\n")

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
    monkeypatch.setattr(mod, "BUNDLES", [root / "dest_a" / "firmware",
                                         root / "dest_b" / "firmware"])
    return mod


# --- plan_entry / release ----------------------------------------------------

def test_release_bundles_an_env_as_firmware_bin(tree):
    mod = tree
    entries, _ = mod.release(["board_a"], builder=builder_for(mod))
    assert entries[0]["method"] == "dfu"


def build_into(mod, env: str, stamp: str = STAMP_A, board: str | None = None):
    """Put a plausible firmware.bin where a `pio run` would have left one."""
    path = mod.bin_path_for(env)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(fake_bin("betacrawler", "1.0.0", board or env, stamp))
    return path


def builder_for(mod, *, fails: set[str] = frozenset(), stamps: dict | None = None):
    """A stand-in for `pio run` that writes a binary instead of compiling."""
    def builder(env, pio):
        if env in fails:
            raise mod.BundleError(f"`pio run -e {env}` failed:\nsimulated")
        build_into(mod, env, (stamps or {}).get(env, STAMP_A))
    return builder


def manifest(mod) -> dict:
    return json.loads(mod.manifest_path(mod.BUNDLES[0]).read_text())


def ids(mod) -> list[str]:
    return [img["id"] for img in manifest(mod)["images"]]


# --- the happy path -----------------------------------------------------------

def test_releases_every_named_env_into_one_manifest(tree):
    mod = tree
    entries, pruned = mod.release(["board_a", "board_b"],
                                  builder=builder_for(mod, stamps={"board_b": STAMP_B}))

    assert [e["id"] for e in entries] == [
        "board_a-betacrawler-1.0.0", "board_b-betacrawler-1.0.0"]
    assert pruned == []
    assert ids(mod) == ["board_a-betacrawler-1.0.0", "board_b-betacrawler-1.0.0"]
    assert manifest(mod)["app_version"] == "1.0.0"

    for img in manifest(mod)["images"]:
        blob = (mod.BUNDLES[0] / img["file"]).read_bytes()
        assert len(blob) == img["size"]
        assert hashlib.sha256(blob).hexdigest() == img["sha256"]

    # Derived from the board header, not typed in anywhere.
    assert manifest(mod)["images"][0]["notes"] == "led, dfu"
    assert manifest(mod)["images"][1]["built"] == STAMP_B


def test_release_resolves_run_build_at_call_time(tree, monkeypatch):
    """`builder` must be looked up when the function RUNS, not bound as a
    def-time default.

    A `builder=run_build` default captures the function object at import, so
    `monkeypatch.setattr(mod, "run_build", fake)` has no effect on any caller
    that omits `builder=` -- which would shell out to a real PlatformIO from
    a test.
    """
    mod = tree
    monkeypatch.setattr(mod, "run_build", builder_for(mod))
    monkeypatch.setattr(mod, "find_pio", lambda: "/nonexistent/pio")
    entries, _ = mod.release(["board_a"], dry_run=True)
    assert [e["board"] for e in entries] == ["board_a"]


def test_plan_entry_resolves_run_build_at_call_time(tree, monkeypatch):
    mod = tree
    monkeypatch.setattr(mod, "run_build", builder_for(mod))
    entry = mod.plan_entry("board_a", pio="/nonexistent/pio")
    assert entry["board"] == "board_a"


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
    assert not mod.manifest_path(mod.BUNDLES[0]).exists()
    assert not (mod.BUNDLES[0] / "board_a").exists()


# --- rebuild vs --add ---------------------------------------------------------

def test_rebuild_prunes_an_image_the_previous_release_shipped(tree):
    mod = tree
    mod.release(["board_a", "board_b"], builder=builder_for(mod))
    assert (mod.BUNDLES[0] / "board_b" / "betacrawler-1.0.0.bin").is_file()

    _, pruned = mod.release(["board_a"], builder=builder_for(mod))

    assert ids(mod) == ["board_a-betacrawler-1.0.0"]
    # one per destination
    assert [p.name for p in pruned] == ["betacrawler-1.0.0.bin"] * 2
    assert not (mod.BUNDLES[0] / "board_b" / "betacrawler-1.0.0.bin").exists()
    # the board directory it emptied goes too
    assert not (mod.BUNDLES[0] / "board_b").exists()
    # ...while the board still shipping is untouched
    assert (mod.BUNDLES[0] / "board_a" / "betacrawler-1.0.0.bin").is_file()


def test_add_keeps_the_previous_release(tree):
    mod = tree
    mod.release(["board_a"], builder=builder_for(mod))
    _, pruned = mod.release(["board_b"], add=True, builder=builder_for(mod))

    assert pruned == []
    assert ids(mod) == ["board_a-betacrawler-1.0.0", "board_b-betacrawler-1.0.0"]
    assert (mod.BUNDLES[0] / "board_a" / "betacrawler-1.0.0.bin").is_file()
    assert (mod.BUNDLES[0] / "board_b" / "betacrawler-1.0.0.bin").is_file()


def test_re_releasing_the_same_env_replaces_its_entry_in_place(tree):
    mod = tree
    mod.release(["board_a"], builder=builder_for(mod))
    mod.release(["board_a"], builder=builder_for(mod, stamps={"board_a": STAMP_B}))

    assert ids(mod) == ["board_a-betacrawler-1.0.0"]
    assert manifest(mod)["images"][0]["built"] == STAMP_B
    # Rewritten, not pruned: the file is a keeper because the new manifest
    # names it too.
    assert (mod.BUNDLES[0] / "board_a" / "betacrawler-1.0.0.bin").is_file()


def test_pruning_never_touches_a_file_no_manifest_listed(tree):
    mod = tree
    mod.release(["board_a", "board_b"], builder=builder_for(mod))
    stray = mod.BUNDLES[0] / "board_b" / "hand-placed.bin"
    stray.write_bytes(b"mine")

    mod.release(["board_a"], builder=builder_for(mod))

    assert stray.read_bytes() == b"mine"
    # and the directory survives precisely because it isn't empty
    assert (mod.BUNDLES[0] / "board_b").is_dir()


def test_prune_ignores_an_entry_pointing_outside_the_bundle(tree):
    mod = tree
    mod.release(["board_a"], builder=builder_for(mod))
    outsider = mod.BUNDLES[0].parent / "not-ours.bin"
    outsider.write_bytes(b"keep")

    data = manifest(mod)
    data["images"].append({"id": "evil", "file": "../not-ours.bin"})
    mod.manifest_path(mod.BUNDLES[0]).write_text(json.dumps(data))

    mod.release(["board_b"], builder=builder_for(mod))

    assert outsider.read_bytes() == b"keep"


# --- all or nothing -----------------------------------------------------------

def test_one_failed_env_leaves_the_previous_release_intact(tree):
    mod = tree
    mod.release(["board_a"], builder=builder_for(mod))
    before = mod.manifest_path(mod.BUNDLES[0]).read_text()

    with pytest.raises(mod.BundleError, match="board_b"):
        mod.release(["board_a", "board_b"],
                    builder=builder_for(mod, fails={"board_b"}))

    assert mod.manifest_path(mod.BUNDLES[0]).read_text() == before
    assert (mod.BUNDLES[0] / "board_a" / "betacrawler-1.0.0.bin").is_file()
    assert not (mod.BUNDLES[0] / "board_b").exists()


def test_a_binary_that_fails_validation_stops_the_whole_release(tree):
    mod = tree
    mod.release(["board_a"], builder=builder_for(mod))
    before = mod.manifest_path(mod.BUNDLES[0]).read_text()

    def bad_builder(env, pio):
        path = mod.bin_path_for(env)
        path.parent.mkdir(parents=True, exist_ok=True)
        # An ELF, the realistic mistake -- no valid vector table.
        path.write_bytes(b"\x7fELF" + b"\x00" * 2044)

    with pytest.raises(mod.BundleError, match="stack pointer"):
        mod.release(["board_a", "board_b"], builder=bad_builder)

    assert mod.manifest_path(mod.BUNDLES[0]).read_text() == before


def test_identity_mismatch_is_caught_before_anything_is_written(tree):
    mod = tree

    def stale_builder(env, pio):
        path = mod.bin_path_for(env)
        path.parent.mkdir(parents=True, exist_ok=True)
        # Built from a different board header than the env claims.
        path.write_bytes(fake_bin("betacrawler", "1.0.0", "some_other_board", STAMP_A))

    with pytest.raises(mod.BundleError, match="BOARD_ID"):
        mod.release(["board_a"], builder=stale_builder)

    assert not mod.manifest_path(mod.BUNDLES[0]).exists()


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

    assert not mod.manifest_path(mod.BUNDLES[0]).exists()


def test_unknown_env_is_reported(tree):
    mod = tree
    with pytest.raises(mod.BundleError, match=r"no \[env:nope\]"):
        mod.release(["nope"], builder=builder_for(mod))


def test_a_corrupt_existing_manifest_stops_the_release(tree):
    mod = tree
    mod.BUNDLES[0].mkdir(parents=True)
    mod.manifest_path(mod.BUNDLES[0]).write_text("{not json")

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


def test_all_board_envs_excludes_native_even_with_a_board_header(tree):
    """Regression test for the real firmware/platformio.ini shape: the real
    [env:native] section DOES define -D BOARD_HEADER (deliberately -- the
    native suite's fixtures need to describe the real device's parameter set,
    per the comment on that section), so BOARD_HEADER alone can't tell it
    apart from a shippable board env. Caught by running `--all` against the
    real repo, where it tried (and failed) to `pio run -e native`."""
    mod = tree
    with (mod.FIRMWARE / "platformio.ini").open("a") as f:
        f.write(
            "\n[env:native]\n"
            "platform = native\n"
            "build_flags =\n"
            "    -D BOARD_HEADER='\"boards/board_a.h\"'\n")
    assert mod.all_board_envs() == ["board_a", "board_b"]


def test_main_all_flag_builds_every_board(tree, monkeypatch, capsys):
    mod = tree
    monkeypatch.setattr(mod, "run_build", builder_for(mod))
    monkeypatch.setattr("sys.argv", ["bundle_firmware.py", "--all", "--dry-run"])
    rc = mod.main()
    assert rc == 0
    out = capsys.readouterr().out
    assert "board_a-betacrawler-1.0.0" in out
    assert "board_b-betacrawler-1.0.0" in out


def test_all_flag_rejects_explicit_envs_too(tree, monkeypatch, capsys):
    mod = tree
    monkeypatch.setattr("sys.argv", ["bundle_firmware.py", "--all", "board_a"])
    with pytest.raises(SystemExit) as exc:
        mod.main()
    assert exc.value.code != 0
    assert "--all" in capsys.readouterr().err


# --- two destinations and the source fingerprint -------------------------------

def test_release_writes_every_destination_identically(tree):
    mod = tree
    mod.release(["board_a"], builder=builder_for(mod))

    texts = [mod.manifest_path(b).read_text() for b in mod.BUNDLES]
    assert texts[0] == texts[1]
    blobs = [(b / "board_a" / "betacrawler-1.0.0.bin").read_bytes() for b in mod.BUNDLES]
    assert blobs[0] == blobs[1]


def test_manifest_records_the_firmware_source_fingerprint(tree):
    mod = tree
    mod.release(["board_a"], builder=builder_for(mod))

    data = manifest(mod)
    assert data["fw_source_sha256"] == mod.fw_source_sha256()
    assert list(data) == ["app_version", "fw_source_sha256", "images"]


def test_source_fingerprint_changes_when_a_source_file_changes(tree):
    mod = tree
    before = mod.fw_source_sha256()
    (mod.FIRMWARE / "src" / "extra.cpp").write_text("// new\n")
    assert mod.fw_source_sha256() != before


def test_source_fingerprint_changes_when_a_source_file_is_renamed(tree):
    mod = tree
    src = mod.FIRMWARE / "src" / "extra.cpp"
    src.write_text("// new\n")
    before = mod.fw_source_sha256()
    src.rename(mod.FIRMWARE / "src" / "renamed.cpp")
    assert mod.fw_source_sha256() != before


def test_source_fingerprint_ignores_files_that_never_reach_the_binary(tree):
    mod = tree
    (mod.FIRMWARE / "test").mkdir()
    before = mod.fw_source_sha256()
    (mod.FIRMWARE / "test" / "test_thing.cpp").write_text("// not built into anything\n")
    assert mod.fw_source_sha256() == before


def test_prune_applies_to_every_destination(tree):
    mod = tree
    mod.release(["board_a", "board_b"], builder=builder_for(mod))
    mod.release(["board_a"], builder=builder_for(mod))

    for bundle in mod.BUNDLES:
        assert (bundle / "board_a" / "betacrawler-1.0.0.bin").is_file()
        assert not (bundle / "board_b").exists()


def test_prune_removes_a_stray_image_from_one_destination_only(tree):
    mod = tree
    mod.release(["board_a", "board_b"], builder=builder_for(mod))
    # Delete board_b from the second destination by hand, then re-release
    # board_a: each destination is pruned against its OWN manifest, so the
    # one that still has the file loses it and the other is already clean.
    (mod.BUNDLES[1] / "board_b" / "betacrawler-1.0.0.bin").unlink()

    mod.release(["board_a"], builder=builder_for(mod))

    for bundle in mod.BUNDLES:
        assert not (bundle / "board_b").exists()
