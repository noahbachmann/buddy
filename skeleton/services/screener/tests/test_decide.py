import asyncio
import base64

import pytest
from claude_agent_sdk import RateLimitEvent, ResultMessage
from claude_agent_sdk.types import RateLimitInfo

from buddy_screener.decide import (BROWSE_SCHEMA, CHECK_SYSTEM, PHOTO_SYSTEM, STAGE_TEXT, BudgetSpent, DecideConfig, DecideError,
                                   Decider, UsageLimited, check_browse, check_photo, check_text, photo_content,
                                   profile_content, system_prompt)
from buddy_screener.library import Library, LikeComment
from buddy_screener.profile import Profile, Section
from buddy_screener.store import Store

LIB = Library("## About Dev\n- Codes, hikes.\n\n## Looking for\nKind people.",
              (LikeComment("travel_far", "de", "travel photo", "wie wiit weg bisch du für das gsi?"),
               LikeComment("hot_take", "en", "bold opinion", "Bold. I'm in, as long as you defend it in public.")),
              "Short, deadpan, passes the turn back.")


def profile() -> Profile:
    return Profile("Alex", [
        Section("photo", 1, "", 0, 900, jpeg=b"\xff\xd8one"),
        Section("prompt", 1, "My hot take\nPineapple belongs on pizza", 950, 1200),
        Section("photo", 2, "", 1300, 2200),        # no crop (hash failed)
        Section("prompt", 2, "Sunday\nHiking", 2300, 2500),
    ], [("Age", "27"), ("Height", "168 cm"), ("", "Zürich")])


def test_profile_content_keeps_capture_order_and_no_name():
    blocks = profile_content(profile())
    assert [b["type"] for b in blocks] == ["text", "image", "text"]
    assert blocks[0]["text"].endswith("photo 1:")
    assert "Age: 27; Height: 168 cm; Zürich" in blocks[0]["text"]
    assert base64.b64decode(blocks[1]["source"]["data"]) == b"\xff\xd8one"
    assert blocks[2]["text"].splitlines() == ["prompt 1: My hot take — Pineapple belongs on pizza",
                                              "photo 2: (photo not available)", "prompt 2: Sunday — Hiking"]
    assert "Alex" not in str(blocks)


def test_text_check_content_has_no_photos():
    blocks = profile_content(profile(), photos=False)
    assert [b["type"] for b in blocks] == ["text"]
    lines = blocks[0]["text"].splitlines()
    assert lines[0] == STAGE_TEXT and "photo 1: (photo)" in lines and "photo 2: (photo)" in lines


def test_check_text():
    assert check_text({"reason": "nothing stated", "dealbreakers": []}) is None
    b = check_text({"reason": "says so", "dealbreakers": [" smokes "]})
    assert (b.decision, b.dealbreakers, b.stage, b.comment_on) == ("skip", ["smokes"], "text", "none")
    with pytest.raises(DecideError):
        check_text(None)


def test_system_prompt_has_preferences_and_library():
    s = system_prompt(LIB)
    assert "## Looking for\nKind people." in s
    assert "- hot_take | en | bold opinion | Bold. I'm in, as long as you defend it in public." in s


def raw(**kw):
    d = {"reason": "Likes hiking.", "decision": "like", "generic": False, "comment_on": "prompt 1", "comment": "hot_take",
         "comment_text": "Bold. Count me in.", "comment_lang": "en", "missing_hook": "", "prompt_langs": ["en", "en"], "soft_flags": [],
         "soft_plus": [], "uncovered": [], "dealbreakers": []}
    return d | kw


def test_check_browse_accepts_a_good_answer():
    b = check_browse(raw(), profile(), LIB)
    assert (b.decision, b.comment_on, b.comment, b.fixes) == ("like", "prompt 1", "hot_take", [])
    assert b.chat_lang == "en" and b.comment_text == "Bold. Count me in."


def flag(f, source, quote):
    return {"flag": f, "source": source, "quote": quote}


def test_soft_positives_apart_from_soft_negatives():
    b = check_browse(raw(soft_flags=[flag("pizza crime", "prompt 1", "pineapple belongs on pizza")],
                         soft_plus=[flag("outdoorsy", "prompt 2", "Hiking"), flag(" ", "prompt 2", "Hiking")],
                         uncovered=["vegan"]), profile(), LIB)
    assert b.soft_flags == ['pizza crime (prompt 1: "pineapple belongs on pizza")']
    assert (b.soft_plus, b.uncovered, b.decision, b.dropped_flags) == (['outdoorsy (prompt 2: "Hiking")'], ["vegan"],
                                                                       "like", [])
    assert set(BROWSE_SCHEMA["required"]) == set(BROWSE_SCHEMA["properties"])


@pytest.mark.parametrize("f", ["tattoos? none stated", "no tattoos visible", "smoking: not mentioned",
                               "Smoker status unknown"])
def test_a_flag_that_only_states_an_absence_is_dropped(f):
    b = check_browse(raw(soft_plus=[flag(f, "details", "Zürich")]), profile(), LIB)
    assert b.soft_plus == [] and b.dropped_flags == [f"absence: {f}"]


def test_a_stated_no_is_kept():
    p = profile()
    p.details.append(("Children", "Don't have children"))
    b = check_browse(raw(soft_plus=[flag("no kids", "details", "don't have children")]), p, LIB)
    assert b.soft_plus == ['no kids (details: "don\'t have children")']


@pytest.mark.parametrize("item, why", [
    (flag("hiker", "prompt 2", ""), "no quote"),
    (flag("hiker", "prompt 7", "Hiking"), "unknown source"),
    (flag("Zürich-based", "details", "lives in Zürich"), "quote not in details"),   # a town alone isn't the rule
    (flag("smoker", "prompt 1", "cigarette in hand"), "quote not in prompt 1"),     # a photo's look as a text quote
    ("smoker", None),                                                               # a bare string: no source
])
def test_a_flag_without_a_verifiable_source_is_dropped(item, why):
    b = check_browse(raw(soft_flags=[item]), profile(), LIB)
    assert b.soft_flags == []
    assert (b.dropped_flags[0].startswith(why) if why else b.dropped_flags == [])


def test_a_photo_flag_needs_only_an_existing_photo():
    b = check_browse(raw(soft_plus=[flag("climbs", "photo 1", "on a climbing wall")]), profile(), LIB)
    assert b.soft_plus == ['climbs (photo 1: "on a climbing wall")']


def test_prompt_states_the_flag_rules():
    s = system_prompt(LIB)
    assert "No quote, no flag." in s and "Never health or another sensitive trait" in s


@pytest.mark.parametrize("text, why", [("call me 079 123 45 67", "a number"), ("@me.here", "a handle"),
                                       ("see www.example.com", "a link"), ("x" * 50, "50 chars > 49")])
def test_a_refused_comment_is_never_replaced_by_the_example(text, why):
    b = check_browse(raw(comment_text=text), profile(), LIB)
    assert (b.decision, b.comment, b.comment_text, b.comment_refused) == ("like", "none", "", [why])


def test_no_comment_is_a_like_without_one():
    b = check_browse(raw(comment="none", comment_text="", missing_hook="a pizza joke"), profile(), LIB)
    assert (b.comment, b.comment_text, b.missing_hook, b.comment_refused) == ("none", "", "a pizza joke", [])


def test_a_free_comment_is_own():
    b = check_browse(raw(comment="none", comment_text="ananas ja, aber nur mit chili"), profile(), LIB)
    assert (b.comment, b.comment_text, b.fixes) == ("own", "ananas ja, aber nur mit chili", [])


def test_the_prompt_has_the_voice_and_the_examples_as_examples():
    s = system_prompt(LIB)
    assert "# The owner's voice\n\nShort, deadpan, passes the turn back." in s
    assert "examples of that voice, not a menu" in s and "At most 49 characters" in s


@pytest.mark.parametrize("text", ["I'm in, as long as you defend it in public",     # 4 words in a row
                                  "bold, i'm in as long as you defend it",          # mostly the same words
                                  "wie wiit weg bisch du gsi?"])                    # the de example in its own words
def test_a_copy_of_an_example_is_refused(text):
    lang = "de-ch" if "bisch" in text else "en"
    b = check_browse(raw(comment_text=text, comment_lang=lang, prompt_langs=["de-ch", "de-ch"]), profile(), LIB)
    assert b.comment_text == "" and b.comment_refused[0].startswith("copies ")


@pytest.mark.parametrize("langs, lang, ok", [(["de-ch", "de"], "de-ch", True), (["de-ch", "de"], "en", True),
                                             (["en", "en"], "de-ch", False), (["en", "en"], "de", False),
                                             (["de", "de"], "de", True), (["de", "de"], "none", False)])
def test_comment_language_rule(langs, lang, ok):
    b = check_browse(raw(prompt_langs=langs, comment_text="pineapple, bold move", comment_lang=lang), profile(), LIB)
    assert bool(b.comment_text) == ok


def test_standard_german_is_fine_in_a_german_chat():
    b = check_browse(raw(prompt_langs=["de", "de"], comment_text="das ist nicht dein Ernst", comment_lang="de"),
                     profile(), LIB)
    assert b.comment_text == "das ist nicht dein Ernst" and b.comment_refused == []


@pytest.mark.parametrize("langs, chat", [(["en", "de"], "de"), (["en", "de-ch"], "de"), (["other", "other"], "en"),
                                         (["en", "other"], "en"), (["en", "en", "de"], "en")])
def test_chat_language_rule(langs, chat):
    assert check_browse(raw(prompt_langs=langs), profile(), LIB).chat_lang == chat


def test_unknown_comment_becomes_own():
    b = check_browse(raw(comment="made_up"), profile(), LIB)
    assert (b.comment, b.comment_text) == ("own", "Bold. Count me in.") and b.fixes


def test_unknown_section_falls_back_to_the_first_prompt_without_comment():
    b = check_browse(raw(comment_on="prompt 9"), profile(), LIB)
    assert (b.comment_on, b.comment, b.comment_text) == ("prompt 1", "none", "") and b.fixes


def test_dealbreaker_forces_skip():
    b = check_browse(raw(dealbreakers=["wants kids soon"]), profile(), LIB)
    assert (b.decision, b.comment_on, b.comment) == ("skip", "none", "none")


def test_a_like_on_a_profile_it_called_generic_is_a_skip():
    b = check_browse(raw(generic=True), profile(), LIB)
    assert (b.decision, b.comment_on, b.comment) == ("skip", "none", "none")
    assert b.fixes == ["like on a profile it called generic → skip"]


def test_skip_clears_the_choice():
    b = check_browse(raw(decision="skip"), profile(), LIB)
    assert (b.comment_on, b.comment) == ("none", "none")


def test_garbage_is_an_error():
    with pytest.raises(DecideError):
        check_browse(raw(decision="maybe"), profile(), LIB)
    with pytest.raises(DecideError):
        check_browse(None, profile(), LIB)


# --- the call, against a fake SDK -------------------------------------------------------------------------------------

PLAIN = raw(comment="none", comment_text="", comment_lang="none")   # a like without a comment: no check call


def result(structured=None, is_error=False, status=None, cost=0.012):
    return ResultMessage(subtype="success", duration_ms=4200, duration_api_ms=4000, is_error=is_error, num_turns=2,
                         session_id="s", total_cost_usd=cost, structured_output=structured, api_error_status=status,
                         usage={"input_tokens": 10, "cache_read_input_tokens": 3000, "cache_creation_input_tokens": 50,
                                "output_tokens": 400})


def limit(status="allowed", five=0.4, resets=2_000_000_000):
    info = RateLimitInfo(status=status, resets_at=resets,
                         raw={"unifiedWindows": {"five_hour": {"utilization": five}, "seven_day": {"utilization": 0.1}}})
    return RateLimitEvent(info, "u", "s")


class FakeSdk:
    """Every call yields `messages`; `script` = one message tuple per call instead."""
    def __init__(self, *messages, delay=0.0, script=None):
        self.messages, self.delay, self.calls, self.script = messages, delay, [], list(script or [])

    def __call__(self, content, options):
        self.calls.append((content, options))
        messages = self.script.pop(0) if self.script else self.messages

        async def gen():
            if self.delay:
                await asyncio.sleep(self.delay)
            for m in messages:
                yield m
        return gen()


@pytest.fixture
def cfg(tmp_path):
    return DecideConfig("claude-sonnet-5-5", "medium", 5.0, 1.0, tmp_path / "token", tmp_path / "limit_until",
                        tmp_path / "work")


@pytest.fixture
def store():
    return Store(":memory:", clock=lambda: 1_000_000.0)


def decider(cfg, store, sdk, now=1_000_000.0):
    return Decider(cfg, store, run=sdk, clock=lambda: now)


def test_browse_runs_locked_down_and_logs_usage(cfg, store, monkeypatch):
    monkeypatch.delenv("ANTHROPIC_API_KEY", raising=False)
    cfg.token_file.write_text("tok-123\n")
    sdk = FakeSdk(limit(), result(PLAIN))
    b, u = decider(cfg, store, sdk).browse(profile(), LIB)
    assert b.decision == "like" and b.comment == "none"
    assert (u.input, u.cache_read, u.cache_write, u.output, u.cost, u.ms, u.five_hour) == (10, 3000, 50, 400, 0.012,
                                                                                          4200, 0.4)
    assert store.cost_since(0) == pytest.approx(0.012)
    o = sdk.calls[0][1]
    assert o.tools == [] and o.setting_sources == [] and o.strict_mcp_config and o.skills == []
    assert o.verbatim_prompts and o.extra_args == {"no-session-persistence": None}
    assert o.env["CLAUDE_CODE_OAUTH_TOKEN"] == "tok-123" and o.model == "claude-sonnet-5-5"
    assert o.output_format["schema"]["required"]


def test_api_key_in_env_is_refused(cfg, store, monkeypatch):
    monkeypatch.setenv("ANTHROPIC_API_KEY", "sk-x")
    with pytest.raises(DecideError, match="ANTHROPIC_API_KEY"):
        decider(cfg, store, FakeSdk())


def test_usage_limit_pauses_until_the_reset(cfg, store, monkeypatch):
    monkeypatch.delenv("ANTHROPIC_API_KEY", raising=False)
    d = decider(cfg, store, FakeSdk(limit("rejected", resets=1_000_600), result(is_error=True, status=429)))
    with pytest.raises(UsageLimited) as e:
        d.browse(profile(), LIB)
    assert e.value.until == 1_000_600 and cfg.limit_file.read_text().strip() == "1000600"
    sdk = FakeSdk(result(PLAIN))
    with pytest.raises(UsageLimited):
        decider(cfg, store, sdk).browse(profile(), LIB)
    assert sdk.calls == []                                  # no call while limited
    decider(cfg, store, sdk, now=1_000_601).browse(profile(), LIB)   # after the reset: runs again
    assert len(sdk.calls) == 1


def test_daily_cap(cfg, store, monkeypatch):
    monkeypatch.delenv("ANTHROPIC_API_KEY", raising=False)
    sdk = FakeSdk(result(PLAIN, cost=0.6))
    d = decider(cfg, store, sdk)
    d.browse(profile(), LIB)
    d.browse(profile(), LIB)
    with pytest.raises(BudgetSpent):
        d.browse(profile(), LIB)
    assert len(sdk.calls) == 2


def test_error_result_and_timeout(cfg, store, monkeypatch):
    monkeypatch.delenv("ANTHROPIC_API_KEY", raising=False)
    with pytest.raises(DecideError, match="no structured output"):
        decider(cfg, store, FakeSdk(result(None))).browse(profile(), LIB)
    cfg.timeout_s = 0.05
    with pytest.raises(DecideError, match="no answer within"):
        decider(cfg, store, FakeSdk(result(PLAIN), delay=1.0)).browse(profile(), LIB)


def images(content):
    return sum(b["type"] == "image" for b in content)


def test_text_first_skips_without_photos(cfg, store, monkeypatch):
    monkeypatch.delenv("ANTHROPIC_API_KEY", raising=False)
    sdk = FakeSdk(script=[(result({"reason": "says so", "dealbreakers": ["smokes"]}, cost=0.003),)])
    b, u = decider(cfg, store, sdk).decide(profile(), LIB)
    assert (b.decision, b.stage) == ("skip", "text") and len(sdk.calls) == 1 and images(sdk.calls[0][0]) == 0
    assert u.task == "text" and u.cost == 0.003


def test_text_first_passes_to_the_full_call(cfg, store, monkeypatch):
    monkeypatch.delenv("ANTHROPIC_API_KEY", raising=False)
    sdk = FakeSdk(script=[(result({"reason": "none stated", "dealbreakers": []}, cost=0.003),),
                          (limit(five=0.5), result(PLAIN, cost=0.02))])
    b, u = decider(cfg, store, sdk).decide(profile(), LIB)
    assert (b.decision, b.stage) == ("like", "full") and [images(c) for c, _ in sdk.calls] == [0, 1]
    assert sdk.calls[0][1].system_prompt == sdk.calls[1][1].system_prompt      # one cached prefix for both
    assert (u.task, u.cost, u.five_hour) == ("text+browse", pytest.approx(0.023), 0.5)
    assert [r[0] for r in store.usage_rows()] == ["text", "browse"]


def test_text_first_off(cfg, store, monkeypatch):
    monkeypatch.delenv("ANTHROPIC_API_KEY", raising=False)
    cfg.text_first = False
    sdk = FakeSdk(result(PLAIN))
    decider(cfg, store, sdk).decide(profile(), LIB)
    assert len(sdk.calls) == 1 and images(sdk.calls[0][0]) == 1


def test_a_video_is_listed_but_never_commented_on():
    p = profile()
    p.sections.insert(1, Section("video", 1, "", 910, 940))
    text = " ".join(b["text"] for b in profile_content(p) if b["type"] == "text")
    assert "video 1: (a video: can't be shown, can't be commented on)" in text
    b = check_browse(raw(comment_on="video 1"), p, LIB)
    assert b.comment_on == "prompt 1" and b.comment == "none" and b.fixes


def test_a_video_prompt_names_its_question():
    p = profile()
    p.sections.insert(1, Section("video", 1, "Typical Sunday", 910, 940))
    text = " ".join(b["text"] for b in profile_content(p) if b["type"] == "text")
    assert 'video 1: (a video answering the prompt "Typical Sunday": can\'t be shown' in text


def test_a_poll_is_listed_but_never_commented_on():
    p = profile()
    p.sections.insert(1, Section("poll", 1, "Pick our first getaway", 910, 940))
    text = " ".join(b["text"] for b in profile_content(p) if b["type"] == "text")
    assert 'poll 1: (a poll asking "Pick our first getaway"; its options below can be liked one by one)' in text
    b = check_browse(raw(comment_on="poll 1"), p, LIB)
    assert b.comment_on == "prompt 1" and b.comment == "none" and b.fixes


# --- the photo check (#83) --------------------------------------------------------------------------------------------

def checked_profile() -> Profile:
    p = profile()
    p.sections[0].check_jpeg = b"\xff\xd8big-one"
    p.sections[2].check_jpeg = b"\xff\xd8big-two"
    return p


def test_photo_content_is_photos_only():
    blocks = photo_content(checked_profile())
    assert [b["type"] for b in blocks] == ["text", "image", "text", "image"]
    assert [b["text"] for b in blocks if b["type"] == "text"] == ["photo 1:", "photo 2:"]
    assert base64.b64decode(blocks[3]["source"]["data"]) == b"\xff\xd8big-two"
    assert "Pineapple" not in str(blocks) and "Zürich" not in str(blocks)
    assert photo_content(profile()) == []                               # no check crops: no call


@pytest.mark.parametrize("raw_, out", [({"verdict": "yes", "photo": "photo 2"}, ("yes", "photo 2")),
                                       ({"verdict": "yes", "photo": "prompt 1"}, ("yes", "")),
                                       ({"verdict": "cant_tell", "photo": "photo 1"}, ("cant_tell", ""))])
def test_check_photo(raw_, out):
    assert check_photo(raw_, profile()) == out


def test_check_photo_needs_a_verdict():
    with pytest.raises(DecideError):
        check_photo({"verdict": "maybe", "photo": "none"}, profile())


def photo_cfg(cfg):
    cfg.text_first, cfg.photo_check, cfg.photo_model = False, True, "claude-opus-5-5"
    return cfg


def test_photo_yes_skips_without_the_full_call(cfg, store, monkeypatch):
    monkeypatch.delenv("ANTHROPIC_API_KEY", raising=False)
    sdk = FakeSdk(script=[(result({"verdict": "yes", "photo": "photo 2"}, cost=0.01),)])
    b, u = decider(photo_cfg(cfg), store, sdk).decide(checked_profile(), LIB)
    assert (b.decision, b.stage, b.reason, b.dealbreakers, b.photo_check, b.photo_ref) == (
        "skip", "photo", "physical preference", ["physical preference"], "yes", "photo 2")
    assert len(sdk.calls) == 1 and sdk.calls[0][1].system_prompt == PHOTO_SYSTEM
    assert sdk.calls[0][1].model == "claude-opus-5-5" and u.task == "photo"
    assert [r[:2] for r in store.usage_rows()] == [("photo", "claude-opus-5-5")]


@pytest.mark.parametrize("verdict", ["cant_tell", "no"])
def test_photo_cant_tell_or_no_leaves_the_decision(cfg, store, monkeypatch, verdict):
    monkeypatch.delenv("ANTHROPIC_API_KEY", raising=False)
    sdk = FakeSdk(script=[(result({"verdict": verdict, "photo": "none"}, cost=0.01),), (result(PLAIN, cost=0.02),)])
    b, u = decider(photo_cfg(cfg), store, sdk).decide(checked_profile(), LIB)
    assert (b.decision, b.stage, b.photo_check) == ("like", "full", verdict)
    assert sdk.calls[1][1].model == "claude-sonnet-5-5"                 # the full call keeps the decide model
    assert (u.task, u.cost) == ("photo+browse", pytest.approx(0.03))


def test_text_then_photo_then_full(cfg, store, monkeypatch):
    monkeypatch.delenv("ANTHROPIC_API_KEY", raising=False)
    photo_cfg(cfg).text_first = True
    sdk = FakeSdk(script=[(result({"reason": "none", "dealbreakers": []}),), (result({"verdict": "no", "photo": ""}),),
                          (result(PLAIN),)])
    b, u = decider(cfg, store, sdk).decide(checked_profile(), LIB)
    assert b.decision == "like" and u.task == "text+photo+browse"


def test_no_check_crops_no_photo_call(cfg, store, monkeypatch):
    monkeypatch.delenv("ANTHROPIC_API_KEY", raising=False)
    sdk = FakeSdk(result(PLAIN))
    b, _ = decider(photo_cfg(cfg), store, sdk).decide(profile(), LIB)
    assert len(sdk.calls) == 1 and b.photo_check == ""


# --- the like comment's checks and second try (#84) -------------------------------------------------------------------

OK = {"copies": "none", "invented": []}


def comment_cfg(cfg):
    cfg.text_first, cfg.check_model = False, "claude-haiku-5-5"
    return cfg


def test_a_free_comment_passes_the_check_call(cfg, store, monkeypatch):
    monkeypatch.delenv("ANTHROPIC_API_KEY", raising=False)
    sdk = FakeSdk(script=[(result(raw(comment="none", comment_text="ananas? mutig. verteidigsch das au live?",
                                      comment_lang="de-ch", prompt_langs=["de-ch", "de-ch"])),), (result(OK),)])
    b, u = decider(comment_cfg(cfg), store, sdk).browse(profile(), LIB)
    assert (b.comment, b.comment_text, b.comment_refused) == ("own", "ananas? mutig. verteidigsch das au live?", [])
    check = sdk.calls[1]
    assert check[1].model == "claude-haiku-5-5" and "- Codes, hikes." in check[1].system_prompt
    assert check[1].system_prompt.startswith(CHECK_SYSTEM[:40]) and images(check[0]) == 0
    assert "Her section (prompt 1): My hot take — Pineapple belongs on pizza" in check[0][0]["text"]
    assert u.task == "browse+comment_check" and [r[0] for r in store.usage_rows()] == ["browse", "comment_check"]


@pytest.mark.parametrize("verdict, why", [({"copies": "hot_take", "invented": []}, "translates hot_take"),
                                          ({"copies": "none", "invented": ["a fellow duck spotter"]},
                                           "invents a fellow duck spotter")])
def test_a_translation_or_invented_fact_gets_one_second_try(cfg, store, monkeypatch, verdict, why):
    monkeypatch.delenv("ANTHROPIC_API_KEY", raising=False)
    retry = {"comment": "none", "comment_text": "pizza hawaii? we need to talk", "comment_lang": "en",
             "missing_hook": ""}
    sdk = FakeSdk(script=[(result(raw()),), (result(verdict),), (result(retry),), (result(OK),)])
    b, u = decider(comment_cfg(cfg), store, sdk).browse(profile(), LIB)
    assert (b.comment, b.comment_text, b.comment_refused) == ("own", "pizza hawaii? we need to talk", [why])
    second = sdk.calls[2]
    assert why in second[0][-1]["text"] and images(second[0]) == 1          # the profile again, photos too
    assert second[1].system_prompt == sdk.calls[0][1].system_prompt           # the cached prefix
    assert u.task == "browse+comment_check+comment+comment_check"


def test_refused_twice_is_no_comment(cfg, store, monkeypatch):
    monkeypatch.delenv("ANTHROPIC_API_KEY", raising=False)
    copy = {"comment": "hot_take", "comment_text": "I'm in, as long as you defend it in public", "comment_lang": "en",
            "missing_hook": ""}
    sdk = FakeSdk(script=[(result(raw()),), (result({"copies": "none", "invented": ["me too"]}),), (result(copy),)])
    b, _ = decider(comment_cfg(cfg), store, sdk).browse(profile(), LIB)
    assert (b.decision, b.comment, b.comment_text) == ("like", "none", "")
    assert b.comment_refused == ["invents me too", "copies hot_take"] and b.missing_hook == "comment refused: copies hot_take"
    assert len(sdk.calls) == 3                                                # no third try, never the example


def test_a_code_refusal_goes_straight_to_the_second_try(cfg, store, monkeypatch):
    monkeypatch.delenv("ANTHROPIC_API_KEY", raising=False)
    sdk = FakeSdk(script=[(result(raw(comment_text="call 079 123 45 67")),),
                          (result({"comment": "none", "comment_text": "", "comment_lang": "none",
                                   "missing_hook": "a pizza debate"}),)])
    b, _ = decider(comment_cfg(cfg), store, sdk).browse(profile(), LIB)
    assert (b.comment_text, b.comment_refused, b.missing_hook) == ("", ["a number"], "a pizza debate")
    assert len(sdk.calls) == 2


def test_a_poll_option_can_be_liked_with_a_comment():
    p = profile()
    p.sections[2:2] = [Section("poll", 1, "Pick our first getaway", 1210, 1600, lo=1210, hi=1600),
                       Section("option", 1, "Lisbon", 1300, 1400, lo=1300, hi=1400),
                       Section("option", 2, "A hut in the Alps", 1420, 1520, lo=1420, hi=1520)]
    text = " ".join(b["text"] for b in profile_content(p) if b["type"] == "text")
    assert 'option 2: "A hut in the Alps" (an option of poll 1: liking it comments on this option)' in text
    b = check_browse(raw(comment_on="option 2", comment_text="the hut, if it has a woodstove"), p, LIB)
    assert (b.comment_on, b.comment_text, b.fixes) == ("option 2", "the hut, if it has a woodstove", [])
    assert p.poll_of(p.find("option 2")).ref == "poll 1"
