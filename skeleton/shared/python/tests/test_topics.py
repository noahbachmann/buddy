import pytest

from buddy_shared import topics


def test_names_from_json():
    assert topics.ears_state == "buddy/ears/state"
    assert topics.status("ears") == "buddy/ears/status"
    assert all(t["topic"].startswith("buddy/") for t in topics.SPEC)


def test_unknown_name_raises():
    with pytest.raises(AttributeError):
        topics.ears_stat  # noqa: B018


@pytest.mark.parametrize(
    "filter, topic, expected",
    [  # same cases as the C++ matches()
        ("buddy/+/status", "buddy/ears/status", True),
        ("buddy/#", "buddy/ears/state", True),
        ("buddy/#", "buddy", True),
        ("#", "anything/at/all", True),
        ("buddy/ears/state", "buddy/ears/state", True),
        ("buddy/+/status", "buddy/ears/state", False),
        ("buddy/+", "buddy/ears/state", False),
        ("buddy/ears/state/x", "buddy/ears/state", False),
    ],
)
def test_matches(filter, topic, expected):
    assert topics.matches(filter, topic) is expected
