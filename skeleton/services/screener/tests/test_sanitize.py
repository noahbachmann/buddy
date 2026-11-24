import pytest

from buddy_screener.sanitize import Leak, forbidden_words, sanitize, sanitize_many
from buddy_screener.screens import Kind, classify
from buddy_screener.uitree import parse

PKG = "org.realapp.android"


def node(desc="", text="", rid="", cls="android.view.View", bounds="[0,0][10,10]", **kw):
    extra = "".join(f' {k}="{v}"' for k, v in kw.items())
    return (f'<node index="0" text="{text}" resource-id="{rid}" class="{cls}" package="{PKG}" content-desc="{desc}"'
            f' clickable="false" enabled="true" bounds="{bounds}"{extra} />')


def dump(*nodes) -> str:
    return ("<?xml version='1.0' encoding='UTF-8' standalone='yes' ?><hierarchy rotation=\"0\">"
            f'<node index="0" text="" resource-id="" class="android.widget.FrameLayout" package="{PKG}" '
            f'content-desc="" bounds="[0,0][1080,2060]">{"".join(nodes)}</node></hierarchy>')


def test_names_replaced_consistently():
    out = sanitize(dump(node(desc="Skip Zoë"), node(desc="Zoë's photo"), node(text="Zoë", cls="android.widget.TextView"),
                        node(desc=" Zoë: hi there. ")), PKG)
    assert "Zoë" not in out and "hi there" not in out
    assert 'content-desc="Skip Alex"' in out and "Alex's photo" in out and 'text="Alex"' in out
    assert 'content-desc=" Alex: Message 1. "' in out


def test_prompts_texts_and_dates_replaced():
    out = sanitize(dump(node(desc="Prompt: My secret. Answer: Cats rule"), node(text="Zürich"),
                        node(text="Fri, 13 Nov 10:54"), node(text="Like photo")), PKG)
    for leak in ("My secret", "Cats rule", "Zürich", "13 Nov"):
        assert leak not in out
    assert "Prompt: Question 1. Answer: Answer 1" in out and 'text="Text 1"' in out and "Today 12:00" in out
    assert 'text="Like photo"' in out


def test_chat_ui_strings_kept_names_and_captions_replaced():
    # #77 walk: the matches list's sections, the chat header, the More menu, a photo caption on the Profile tab
    out = sanitize(dump(node(text="Their turn (1)"),
                        node(desc="Zoë, Verified"), node(desc="Verified"), node(desc="Record voice note"),
                        node(desc="Press back to close"), node(text="Unmatch"), node(text="We Met"),
                        node(desc="Photo prompt: My weekends"), node(desc="Languages Spoken")), PKG)
    assert "Zoë" not in out and "My weekends" not in out
    for kept in ('text="Their turn (1)"', 'content-desc="Alex, Verified"',
                 'content-desc="Verified"', "Record voice note", "Press back to close", 'text="Unmatch"',
                 'text="We Met"', "Photo prompt: Question 1", "Languages Spoken"):
        assert kept in out


def test_package_and_ids_replaced_defaults_dropped():
    out = sanitize(dump(node(rid=f"{PKG}:id/messageComposition")), PKG)
    assert PKG not in out and "realapp" not in out
    assert 'resource-id="app.example:id/messageComposition"' in out
    assert "clickable=" not in out and "index=" not in out


def test_one_mapping_across_a_walk():
    a, b = sanitize_many([dump(node(desc="Prompt: Q one. Answer: A one")),
                          dump(node(desc="Prompt: Q two. Answer: A two"), node(desc="Prompt: Q one. Answer: A one"))],
                         PKG)
    assert "Question 1. Answer: Answer 1" in a and "Question 1. Answer: Answer 1" in b and "Question 2" in b


def test_forbidden_word_refuses_the_fixture():
    # a word from the host list that survives (here: inside a kept resource id) must stop the fixture
    with pytest.raises(Leak) as e:
        sanitize(dump(node(rid=f"{PKG}:id/secretword_banner")), PKG, ["secretword"])
    assert "secretword" not in str(e.value)   # never echoed into logs


def test_forbidden_words_include_the_package_parts():
    assert "realapp" in forbidden_words(PKG, [])
    assert "android" not in forbidden_words(PKG, [])


def test_sanitized_dump_still_classifies():
    xml = dump(node(desc="Matches, 2 new messages", bounds="[648,1970][864,2060]"),
               node(text="Matches", cls="android.widget.TextView", bounds="[60,283][389,384]"))
    # make the tab's wrapper selected: wrap the badge node
    xml = xml.replace('<node index="0" text="" resource-id="" class="android.view.View" package="org.realapp.android" '
                      'content-desc="Matches, 2 new messages"',
                      f'<node class="android.view.View" package="{PKG}" selected="true" bounds="[648,1970][864,2060]">'
                      '<node index="0" text="" resource-id="" class="android.view.View" package="org.realapp.android" '
                      'content-desc="Matches, 2 new messages"', 1)
    xml = xml.replace('bounds="[648,1970][864,2060]" />', 'bounds="[648,1970][864,2060]" /></node>', 1)
    s = classify(parse(sanitize(xml, PKG)), "app.example")
    assert s.kind is Kind.MATCHES and s.tabs["Matches"].badge == 2
