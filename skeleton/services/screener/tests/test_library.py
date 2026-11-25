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


def test_voice_and_about_dev(tmp_path):
    (tmp_path / "preferences.md").write_text("# Prefs\n\n## About Dev (facts)\n- Codes.\n\n## Looking for\nKind.\n")
    (tmp_path / "templates.md").write_text(TEMPLATES + "\n## Voice\nShort and deadpan.\nPasses the turn back.\n")
    lib = Library.load(tmp_path)
    assert lib.voice == "Short and deadpan.\nPasses the turn back." and lib.about_dev == "- Codes."
    assert Library("x", ()).voice == "" and Library("x", ()).about_dev == ""


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

## contact_ask
EN: I think you two are a match. Should I send you his number?
DE: Ich glaube, ihr passt. Soll ich dir seine Nummer schicken?

## contact_ask_again
EN: Should I send you his number?
DE: Soll ich dir seine Nummer schicken?

## contact_choice
With an Instagram file (a note: ignored).
EN: I think you two are a match. Should I send you his number or his Instagram?
DE: Ich glaube, ihr passt. Soll ich dir seine Nummer oder sein Instagram schicken?

## contact_choice_again
EN: His number or his Instagram?
DE: Seine Nummer oder sein Instagram?

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

## question
EN: He asks: "{question}"
DE: Er fragt: «{question}»

## ai_explain
EN: He built this software; AI is a small part of it. Say if you'd rather not.
DE: Er hat diese Software gebaut; KI ist nur ein kleiner Teil. Sag's, wenn du nicht möchtest.

## Like comments
- hot_take | en | bold opinion | Bold. I'm in.
"""

CHAT_MD = """\
# Chat rules

## Screening questions
Ask naturally.
1. **(required)** "What are you looking for?"
   DE: "Was suchst du?"
2. "Pets?"
   DE: "Haustiere?"
3. "This song? https://example.org/track/1"
   DE: "Dieser Song? https://example.org/track/1"
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
    assert t["contact_choice"]["en"] == "I think you two are a match. Should I send you his number or his Instagram?"
    assert set(t) == {"disclosure", "contact_ask", "contact_ask_again", "contact_choice", "contact_choice_again",
                      "contact_number",
                      "contact_instagram", "closing", "opt_out", "question", "ai_explain"}


@pytest.mark.parametrize("old, new, why", [
    ("## opt_out", "## not_opt_out", "## opt_out missing"),
    ("DE: Kein Problem!", "", "no DE: block"),
    ("EN: Not a fit, all the best.", "EN: Not a fit, {number}.", "not allowed here"),
    ("EN: The number: {number}.", "EN: The number.", "needs \\['number'\\]"),
    ("EN: His number or his Instagram?", "EN: His number or {instagram}?", "not allowed here"),   # values: code only
])
def test_broken_chat_templates_refuse_the_file(old, new, why):
    with pytest.raises(LibraryError, match=why):
        parse_chat_templates(CHAT_TEMPLATES.replace(old, new))


def test_fill_is_one_pass():
    assert fill("Hi {name}, {question}", name="{question}", question="ok") == "Hi {question}, ok"


def test_questions():
    qs = parse_questions(CHAT_MD)
    assert [(q.n, q.required) for q in qs] == [(1, True), (2, False), (3, False)]
    assert (qs[0].text, qs[0].de, qs[0].note) == ("What are you looking for?", "Was suchst du?", "")
    assert qs[2].note == "(judge the vibe)" and qs[2].in_lang("de") == "Dieser Song? https://example.org/track/1"
    assert qs[2].in_lang("en") == "This song? https://example.org/track/1"
    with pytest.raises(LibraryError, match="question 2: no DE"):
        parse_questions(CHAT_MD.replace('   DE: "Haustiere?"\n', ""))
    with pytest.raises(LibraryError, match="no \"quoted\""):
        parse_questions(CHAT_MD.replace('2. "Pets?"', "2. Pets?"))
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
    (tmp_path / "instagram").unlink()
    assert Contacts.load(tmp_path).values() == ("+41 79 000 00 00",)   # no instagram file: the number only
    (tmp_path / "number").write_text("\n")
    with pytest.raises(LibraryError, match="number"):
        Contacts.load(tmp_path)
