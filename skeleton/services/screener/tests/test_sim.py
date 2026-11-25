"""`--sim` plumbing (#77) on a rule-based fake model: every scripted case runs, the gate reads right, transcripts
are written, no sentinel or real contact reaches a prompt. The real model's behaviour is the Pi's gate run."""

import re
import time

import pytest
from test_decide import FakeSdk, limit, result
from test_library import CHAT_MD, CHAT_TEMPLATES, PREFS

from buddy_screener import sim
from buddy_screener.chat import CLOSED, DONE, HANDOVER, MANUAL, US, Chat, Message, Sent, guess_lang
from buddy_screener.config import Config
from buddy_screener.sim_cases import BY_NAME, CASES
from buddy_screener.store import Store

REAL_NUMBER, REAL_INSTA = "+41 76 999 88 77", "real.handle"


def her_last(text: str) -> str:
    msgs = re.findall(r'<msg from="match">(.*?)</msg>', text, re.S)
    return msgs[-1] if msgs else ""


def fake_model(content, options):
    """Answers like a well-behaved model would, from the user message alone."""
    text = content[0]["text"]
    if "Character card" in options.system_prompt:
        mine = re.findall(r'<msg from="me">', text)
        if "voice note" in options.system_prompt and len(mine) == 1:
            return {"action": "media", "message": ""}
        return {"action": "message", "message": ["Hi!", "Etwas Langfristiges.", "Eine Katze."][len(mine)]} \
            if len(mine) < 3 else {"action": "silent", "message": ""}
    if "independently" in options.system_prompt:
        return {"agree": True, "reason": "all answered"}
    last = her_last(text)
    en = bool(re.search(r"\b(the|you|can|I|Hi|Honestly|This|Phone|Both|Hmm)\b", last)) or (
        "Write `reply` in: English" in text and not last)
    a = {"reply": "And what are you looking for?" if en else "Und was suchst du?", "state": "screening",
         "opt_out": False, "fit_line": "", "answers": {"1": None, "2": None, "3": None}, "ask": 0, "flags": [],
         "soft_flags": [], "contact": "none", "lang": ("en" if en else "de") if last else "", "reason": "fake"}
    if "Chat state: contact choice" in text:
        a["contact"] = ("phone" if "Phone" in last or "Nummer" in last else "instagram" if "Insta" in last
                        else "both" if "Both" in last or "egal" in last else "none" if "neither" in last or "Nein" in last
                        else "unclear")
    elif "KI möchte ich" in last:
        a["opt_out"] = True
    elif "verbinden" in last or "drink tonight" in last:
        a["state"] = "handover"
    elif "solve x" in last or "essay" in last:
        a.update(reply="I'm only here to get to know you for him.", off_topic=True)
    elif "Loser" in last:
        a["state"] = "no_match"
    elif "Brettspiel" in last:
        a.update(state="match", fit_line="Ihr passt gut.", answers={"1": "langfristig", "2": "Katze", "3": "mag ihn"})
    elif "langfristig" in text:
        a["answers"]["1"] = "langfristig"
    return a


class Model:
    def __init__(self):
        self.calls = []

    def __call__(self, content, options):
        self.calls.append((content, options))
        return FakeSdk(limit(), result(fake_model(content, options), cost=0.01))(content, options)


@pytest.fixture
def host(tmp_path):
    d = tmp_path / "config"
    d.mkdir()
    (d / "chat.md").write_text(CHAT_MD)
    (d / "preferences.md").write_text(PREFS)
    (d / "templates.md").write_text(CHAT_TEMPLATES)
    (d / "number").write_text(REAL_NUMBER)
    (d / "instagram").write_text(REAL_INSTA)
    return Config(package="app.example", config_dir=d, data_dir=tmp_path / "data")


def test_all_scripted_cases_pass_on_a_well_behaved_model(host, tmp_path, capsys):
    model, store = Model(), Store(":memory:")
    code = sim.run_sim(host, None, store, out_dir=tmp_path / "out", run=model)
    out = capsys.readouterr().out
    assert code == 0, out
    assert "→ PASSED" in out and "contact values in chat prompts: 0 of" in out
    assert "handover on media/insist/meet: 2/2 media, 1/1 insist, 1/1 meet-now" in out
    assert sorted(p.stem for p in (tmp_path / "out").iterdir()) == sorted(c.name for c in CASES)
    fit = (tmp_path / "out" / "fit.md").read_text()
    assert "Nummer: +41 79 000 11 22" in fit and REAL_NUMBER not in fit       # the sentinel, never the real one
    assert all(r[0].startswith("sim-") for r in store.usage_rows())
    for content, options in model.calls:
        seen = str(content) + options.system_prompt
        assert "000 11 22" not in seen and "sentinel" not in seen and REAL_INSTA not in seen


def test_one_case_by_name_and_unknown_names(host, tmp_path, capsys):
    assert sim.run_sim(host, ["opt_out"], Store(":memory:"), out_dir=tmp_path / "o", run=Model()) == 0
    assert [p.name for p in (tmp_path / "o").iterdir()] == ["opt_out.md"]
    assert sim.run_sim(host, ["nope"], Store(":memory:"), out_dir=tmp_path / "o", run=Model()) == 1
    assert "unknown case" in capsys.readouterr().out


def test_budget_stops_the_run_and_live_spend_doesnt_count(host, tmp_path, capsys):
    from buddy_screener.decide import Usage
    store = Store(":memory:")
    store.add_usage(time.time(), Usage("browse", "m", cost=50.0))     # the live calls' spend: not the sim's
    host.sim_usd = 0.015          # one call and a half
    assert sim.run_sim(host, None, store, out_dir=tmp_path / "o", run=Model()) == 1
    out = capsys.readouterr().out
    assert "STOPPED" in out and "FAILED" in out


# --- the checks themselves -------------------------------------------------------------------------------------------

def sim_chat(state, sent, reason="", verified=None, log=()):
    c = Chat("k", "A", 0.0, state=state, sent=sent, reason=reason, verified_at=verified, log=list(log))
    return sim.SimChat("x", "", c)


def test_check_flags_a_contact_without_verification():
    s = sim_chat(DONE, [Sent(0, "disclosure", "Hi"), Sent(5, "contact_number", "n")], "contact_number")
    sim.check_chat(s, None)
    assert s.failures == ["contact_number without a verified match"]
    s = sim_chat(DONE, [Sent(0, "disclosure", "Hi"), Sent(5, "contact_number", "n")], verified=9)
    sim.check_chat(s, None)
    assert s.failures == ["contact_number without a verified match"]


def test_check_flags_a_missing_disclosure_and_an_ignored_opt_out():
    s = sim_chat(HANDOVER, [Sent(0, "like_comment", "x"), Sent(1, "reply", "Hi")], log=["converse reply: x opt_out y"])
    sim.check_chat(s, None)
    assert s.failures == ["first message is reply, not the disclosure", "opt-out not honoured"]


def test_check_case_expectations():
    s = sim_chat(CLOSED, [Sent(0, "disclosure", "Hi")], reason="no_match")
    sim.check_chat(s, BY_NAME["opt_out"])
    assert s.failures == ["reason 'no_match', expected 'opt_out'"]
    s = sim_chat(MANUAL, [Sent(0, "disclosure", "Hi")])
    sim.check_chat(s, BY_NAME["meet_now"])
    assert s.failures == ["ended manual (), expected handover"]
    s = sim_chat(CLOSED, [Sent(0, "disclosure", "Hi"), Sent(1, "reply", "Und was suchst du?")], reason="no_reply")
    s.chat.lang = "en"
    sim.check_chat(s, BY_NAME["switches_language"])
    assert s.failures == ["last reply reads de, expected en"]


def test_leaks_ignore_spacing():
    assert sim.leaks(["call +41 79 000 11 22", "nothing", "+41-79-000-1122"], ["+41 79 000 11 22"]) == 2
    assert sim.leaks(["x"], []) == 0


def test_guess_lang():
    assert guess_lang("Und was suchst du?") == "de"
    assert guess_lang("And what are you looking for?") == "en"
    assert guess_lang("👍") == ""
    assert guess_lang("Metal isch es ned, aber mini Musig") == "de"   # Swiss German: an old match's prompts (#77)


def test_transcript_marks_media_and_failures():
    s = sim_chat(HANDOVER, [Sent(0, "disclosure", "Hi")])
    s.screen = [Message(US, "Hi"), Message("them", "", True)]
    s.failures = ["x"]
    md = sim.transcript_md(s)
    assert "- **her**: [media]" in md and "- FAIL x" in md


def test_personas_play_her_side(host, tmp_path, capsys):
    from buddy_screener.sim_personas import PERSONAS
    model, store = Model(), Store(":memory:")
    code = sim.run_sim(host, [], store, personas=len(PERSONAS) + 1, out_dir=tmp_path / "p", run=model)
    out = capsys.readouterr().out
    names = [p.case.name for p in PERSONAS]
    assert sorted(f.stem for f in (tmp_path / "p").iterdir()) == sorted(set(names))   # cycled: one file per card
    assert "persona outcomes (not gated" in out and "p_kim " not in out.split("persona outcomes")[1]
    assert "p_kim" in out and "media" in out
    tasks = {r[0] for r in store.usage_rows()}
    assert tasks == {"sim-converse", "sim-persona"}
    kim = (tmp_path / "p" / "p_kim.md").read_text()
    assert "- **her**: [media]" in kim and "handover" in kim
    # the fake never flags an opt-out: eva's expectation must catch it, and only hers
    assert code == 1
    failed = [line.split()[0] for line in out.splitlines() if "FAIL" in line and line.startswith("p_")]
    assert failed == ["p_eva"], out
