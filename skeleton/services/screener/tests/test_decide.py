import asyncio
import base64

import pytest
from claude_agent_sdk import RateLimitEvent, ResultMessage
from claude_agent_sdk.types import RateLimitInfo

from buddy_screener.decide import (STAGE_TEXT, BudgetSpent, DecideConfig, DecideError, Decider, UsageLimited,
                                   check_browse, check_text, profile_content, system_prompt)
from buddy_screener.library import Library, LikeComment
from buddy_screener.profile import Profile, Section
from buddy_screener.store import Store

LIB = Library("Prefs: kind people.", (LikeComment("travel_far", "de", "travel photo", "Wie weit weg?"),
                                      LikeComment("hot_take", "en", "bold opinion", "Bold. I'm in.")))


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
    assert "Prefs: kind people." in s
    assert "- hot_take | en | bold opinion | Bold. I'm in." in s


def raw(**kw):
    d = {"reason": "Likes hiking.", "decision": "like", "comment_on": "prompt 1", "comment": "hot_take",
         "missing_hook": "", "prompt_langs": ["en", "en"], "soft_flags": [], "dealbreakers": []}
    return d | kw


def test_check_browse_accepts_a_good_answer():
    b = check_browse(raw(), profile(), LIB)
    assert (b.decision, b.comment_on, b.comment, b.fixes) == ("like", "prompt 1", "hot_take", [])
    assert b.chat_lang == "en"


@pytest.mark.parametrize("langs, chat", [(["en", "de"], "de"), (["en", "de-ch"], "de"), (["other", "other"], "de"),
                                         (["en", "other"], "en")])
def test_chat_language_rule(langs, chat):
    assert check_browse(raw(prompt_langs=langs), profile(), LIB).chat_lang == chat


def test_unknown_comment_becomes_none():
    b = check_browse(raw(comment="made_up"), profile(), LIB)
    assert b.comment == "none" and b.fixes


def test_unknown_section_falls_back_to_the_first_prompt_without_comment():
    b = check_browse(raw(comment_on="prompt 9"), profile(), LIB)
    assert (b.comment_on, b.comment) == ("prompt 1", "none") and b.fixes


def test_dealbreaker_forces_skip():
    b = check_browse(raw(dealbreakers=["wants kids soon"]), profile(), LIB)
    assert (b.decision, b.comment_on, b.comment) == ("skip", "none", "none")


def test_skip_clears_the_choice():
    b = check_browse(raw(decision="skip"), profile(), LIB)
    assert (b.comment_on, b.comment) == ("none", "none")


def test_garbage_is_an_error():
    with pytest.raises(DecideError):
        check_browse(raw(decision="maybe"), profile(), LIB)
    with pytest.raises(DecideError):
        check_browse(None, profile(), LIB)


# --- the call, against a fake SDK -------------------------------------------------------------------------------------

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
    sdk = FakeSdk(limit(), result(raw()))
    b, u = decider(cfg, store, sdk).browse(profile(), LIB)
    assert b.decision == "like" and b.comment == "hot_take"
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
    sdk = FakeSdk(result(raw()))
    with pytest.raises(UsageLimited):
        decider(cfg, store, sdk).browse(profile(), LIB)
    assert sdk.calls == []                                  # no call while limited
    decider(cfg, store, sdk, now=1_000_601).browse(profile(), LIB)   # after the reset: runs again
    assert len(sdk.calls) == 1


def test_daily_cap(cfg, store, monkeypatch):
    monkeypatch.delenv("ANTHROPIC_API_KEY", raising=False)
    sdk = FakeSdk(result(raw(), cost=0.6))
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
        decider(cfg, store, FakeSdk(result(raw()), delay=1.0)).browse(profile(), LIB)


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
                          (limit(five=0.5), result(raw(), cost=0.02))])
    b, u = decider(cfg, store, sdk).decide(profile(), LIB)
    assert (b.decision, b.stage) == ("like", "full") and [images(c) for c, _ in sdk.calls] == [0, 1]
    assert sdk.calls[0][1].system_prompt == sdk.calls[1][1].system_prompt      # one cached prefix for both
    assert (u.task, u.cost, u.five_hour) == ("text+browse", pytest.approx(0.023), 0.5)
    assert [r[0] for r in store.usage_rows()] == ["text", "browse"]


def test_text_first_off(cfg, store, monkeypatch):
    monkeypatch.delenv("ANTHROPIC_API_KEY", raising=False)
    cfg.text_first = False
    sdk = FakeSdk(result(raw()))
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
