import pytest

from buddy_screener.library import COMMENT_MAX, Library, LibraryError, parse_like_comments, sections

TEMPLATES = """\
# Templates (host only)

## disclosure
EN: Hi {name}!

## Like comments
Format: - id | de|en | fits | text

- travel_far | de | travel photo far away | Wie weit weg ist das?
- hot_take | en | prompt with a bold opinion | Bold. I'm in.

## closing
EN: bye
"""


def test_like_comments_from_their_section_only():
    cs = parse_like_comments(TEMPLATES)
    assert [(c.id, c.lang, c.hint, c.text) for c in cs] == [
        ("travel_far", "de", "travel photo far away", "Wie weit weg ist das?"),
        ("hot_take", "en", "prompt with a bold opinion", "Bold. I'm in."),
    ]
    assert set(sections(TEMPLATES)) == {"disclosure", "like comments", "closing"}


def test_no_section_is_an_empty_library():
    assert parse_like_comments("## disclosure\nEN: hi\n") == []


@pytest.mark.parametrize("line, why", [
    ("- a | de | x", "not '- id"),
    ("- Bad | de | x | y", "lower-case"),
    ("- none | de | x | y", "reserved"),
    ("- a | fr | x | y", "de or en"),
    ("- a | de | x | " + "y" * (COMMENT_MAX + 1), "the app takes"),
    ("- a | de | x | Hi {name}", "placeholders"),
])
def test_broken_lines_refuse_the_file(line, why):
    with pytest.raises(LibraryError, match=why):
        parse_like_comments(f"## Like comments\n{line}\n")


def test_duplicate_ids_refuse_the_file():
    with pytest.raises(LibraryError, match="used twice"):
        parse_like_comments("## Like comments\n- a | de | x | y\n- a | en | x | z\n")


def test_exactly_the_limit_is_fine():
    assert len(parse_like_comments(f"## Like comments\n- a | de | x | {'y' * COMMENT_MAX}\n")[0].text) == COMMENT_MAX


def test_load(tmp_path):
    (tmp_path / "preferences.md").write_text("# Prefs\n\nKind people.\n")
    (tmp_path / "templates.md").write_text(TEMPLATES)
    lib = Library.load(tmp_path)
    assert lib.preferences == "# Prefs\n\nKind people."
    assert lib.comment("hot_take").lang == "en" and lib.comment("nope") is None


def test_missing_file(tmp_path):
    with pytest.raises(LibraryError, match="can't read"):
        Library.load(tmp_path)
