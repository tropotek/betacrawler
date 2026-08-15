import json
from pathlib import Path

_REPO = Path(__file__).resolve().parents[2]
GOLDEN = _REPO / "firmware" / "test" / "golden" / "schema.json"
PROFILE = _REPO / "app" / "backend" / "sim_profile.json"


def test_sim_profile_matches_the_firmware_golden_fixture():
    """The simulator's schema is the real firmware's, or it is a lie.

    Regenerate the fixture with `pio test -e native` in firmware/, then
    `cp firmware/test/golden/schema.json app/backend/sim_profile.json`.
    """
    assert json.loads(PROFILE.read_text()) == json.loads(GOLDEN.read_text())
