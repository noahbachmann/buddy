import json
from pathlib import Path

import pytest

from buddy_shared import topics

CASES = json.loads((Path(__file__).parents[2] / "tests/cases/topic_matches.json").read_text())["cases"]


def test_names_from_json():
    assert topics.ears_state == "buddy/ears/state"
    assert topics.status("ears") == "buddy/ears/status"
    assert all(t["topic"].startswith("buddy/") for t in topics.SPEC)


def test_unknown_name_raises():
    with pytest.raises(AttributeError):
        topics.ears_stat  # noqa: B018


@pytest.mark.parametrize("filter, topic, expected", CASES)
def test_matches(filter, topic, expected):
    assert topics.matches(filter, topic) is expected
