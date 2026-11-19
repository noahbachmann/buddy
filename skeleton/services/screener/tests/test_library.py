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


# --- the chat (#77) --------------------------------------------------------------------------------------------------

from buddy_screener.library import ChatRules, Contacts, fill, parse_chat_templates, parse_questions  # noqa: E402

CHAT_TEMPLATES = """\
## disclosure
EN: Hi {name}! I'm an AI assistant,
asking a few questions.
DE: Hi {name}! Ich bin ein
KI-Assistent.

## contact_offer
{fit_line} = generated (a note: ignored)
EN: {fit_line} Phone or Instagram?
DE: {fit_line} Telefon oder Instagram?

## contact_number
EN: The number: {number}.
DE: Die Nummer: {number}.

## contact_instagram
EN: Instagram: {instagram}.
DE: Instagram: {instagram}.

## closing
EN: Not a fit, all the best.
DE: Passt nicht, alles Gute.

## opt_out
EN: No problem!
DE: Kein Problem!

## Like comments
- hot_take | en | bold opinion | Bold. I'm in.
"""

CHAT_MD = """\
# Chat rules

## Screening questions
Ask naturally.
1. **(required)** "What are you looking for?"
2. "Pets?"
3. "This song? https://example.org/track/1"
   (judge the vibe)

## Match rule
Required answered.
"""

PREFS = """\
# Prefs
## About Dev
Likes robots.
## Physical
Not for chats.
## Dealbreakers (hard)
Smoking.
"""


def test_chat_templates_join_wrapped_lines_and_skip_notes():
    t = parse_chat_templates(CHAT_TEMPLATES)
    assert t["disclosure"] == {"en": "Hi {name}! I'm an AI assistant, asking a few questions.",
                               "de": "Hi {name}! Ich bin ein KI-Assistent."}
    assert t["contact_offer"]["en"] == "{fit_line} Phone or Instagram?"
    assert set(t) == {"disclosure", "contact_offer", "contact_number", "contact_instagram", "closing", "opt_out"}


@pytest.mark.parametrize("old, new, why", [
    ("## opt_out", "## not_opt_out", "## opt_out missing"),
    ("DE: Kein Problem!", "", "no DE: block"),
    ("EN: Not a fit, all the best.", "EN: Not a fit, {number}.", "not allowed here"),
    ("EN: The number: {number}.", "EN: The number.", "needs \\['number'\\]"),
    ("EN: {fit_line} Phone", "EN: Phone", "needs \\['fit_line'\\]"),
])
def test_broken_chat_templates_refuse_the_file(old, new, why):
    with pytest.raises(LibraryError, match=why):
        parse_chat_templates(CHAT_TEMPLATES.replace(old, new))


def test_fill_is_one_pass():
    assert fill("Hi {name}, {fit_line}", name="{fit_line}", fit_line="ok") == "Hi {fit_line}, ok"


def test_questions():
    qs = parse_questions(CHAT_MD)
    assert [(q.n, q.required) for q in qs] == [(1, True), (2, False), (3, False)]
    assert qs[0].text == '"What are you looking for?"'
    assert qs[2].text.endswith("(judge the vibe)")
    with pytest.raises(LibraryError, match="required"):
        parse_questions(CHAT_MD.replace("**(required)** ", ""))
    with pytest.raises(LibraryError, match="no numbered"):
        parse_questions("## Screening questions\nnone\n")


def test_chat_rules_load(tmp_path):
    (tmp_path / "chat.md").write_text(CHAT_MD)
    (tmp_path / "preferences.md").write_text(PREFS)
    (tmp_path / "templates.md").write_text(CHAT_TEMPLATES)
    r = ChatRules.load(tmp_path)
    assert r.required == (1,)
    assert r.urls == {"https://example.org/track/1"}
    assert "Likes robots." in r.preferences and "Smoking." in r.preferences and "Not for chats" not in r.preferences
    assert r.rules.startswith("# Chat rules")


def test_chat_rules_need_about_dev(tmp_path):
    (tmp_path / "chat.md").write_text(CHAT_MD)
    (tmp_path / "preferences.md").write_text("## Dealbreakers\nx\n")
    (tmp_path / "templates.md").write_text(CHAT_TEMPLATES)
    with pytest.raises(LibraryError, match="About Dev"):
        ChatRules.load(tmp_path)


def test_contacts_never_in_repr(tmp_path):
    (tmp_path / "number").write_text("+41 79 000 00 00\n")
    (tmp_path / "instagram").write_text("someone\n")
    c = Contacts.load(tmp_path)
    assert c.values() == ("+41 79 000 00 00", "someone")
    assert "000" not in repr(c) and "someone" not in repr(c)
