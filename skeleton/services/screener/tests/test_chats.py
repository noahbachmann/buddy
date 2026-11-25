"""The chat driver (#77) on the fake phone walking the chat fixtures, with a scripted Talker: which matches it takes,
hold and resume, Dev's edits, unmatch, limits. The model's behaviour is `--sim`'s gate."""

import pytest
from test_library import CHAT_MD, CHAT_TEMPLATES, PREFS

from buddy_screener import chats
from buddy_screener.chat import (CLOSED, FOREIGN, HANDOVER, LIKED, MANUAL, NEW, SCREENING, THEM, US, Chat, ChatLimits,
                                 Converse, Sent, Verdict)
from buddy_screener.chats import ChatConfig, ChatDriver
from buddy_screener.decide import DecideError, UsageLimited
from buddy_screener.navigator import Navigator
from buddy_screener.store import Store
from fake_chat_phone import FakeChatPhone
from fixture_files import PACKAGE

NUMBER, INSTA = "+41 76 999 88 77", "real.handle"
T0 = 1_800_000_000.0
DISCLOSURE = "Hi Alex! I'm an AI assistant, asking a few questions."


class Talker:
    def __init__(self, *answers: Converse):
        self.answers = list(answers)
        self.calls: list[tuple[str, list]] = []
        self.fail: Exception | None = None

    def converse(self, rules, task, state, lang, profile, answered, transcript):
        self.calls.append((task, transcript, profile))
        if self.fail:
            raise self.fail
        return (self.answers.pop(0) if self.answers else Converse("Und sonst?", "screening", lang="en")), [], None

    def verify(self, rules, profile, transcript):
        return Verdict(True), None


class Rig:
    def __init__(self, tmp_path, talker: Talker, mode=chats.LIVE, ignore=("Sam", "Robin", "Text 24", "Text 27")):
        d = tmp_path / "config"
        d.mkdir(exist_ok=True)
        (d / "chat.md").write_text(CHAT_MD)
        (d / "preferences.md").write_text(PREFS)
        (d / "templates.md").write_text(CHAT_TEMPLATES)
        (d / "number").write_text(NUMBER)
        (d / "instagram").write_text(INSTA)
        (d / "chat-ignore").write_text("".join(f"{n}\n" for n in ignore) + "# a comment\n")
        self.phone = FakeChatPhone("matches-turns")
        self.nav = Navigator(self.phone, PACKAGE, pause=lambda s: None, log=lambda s: None)
        self.t = T0
        self.store = Store(":memory:", clock=lambda: self.t)
        self.talker = talker
        self.holds, self.notes, self.paused = [], [], False
        self.d = ChatDriver(ChatConfig(mode=mode), self.nav, talker, self.store, d, hold=self.hold,
                            notify=self.notes.append, mode=lambda: "needs_you" if self.paused else "active",
                            limits=ChatLimits(), clock=lambda: self.t, log=lambda s: None)

    def hold(self, kind, row, why):
        self.holds.append(kind)
        self.paused = True

    def resume(self):
        self.paused = False
        return self.d.step()

    def like(self, name="Alex", comment="Text 3", kind="prompt"):
        self.store.save_chat(Chat(f"liked:{name}", name, T0 - 3600, state=LIKED, profile="Age: 22; prompt 1: Q — A",
                                  like_comment=comment, liked_kind=kind, prompt_langs=["en", "en"]))


def ask() -> Converse:
    return Converse("Cool.", "screening", lang="en", ask=1)


def opener() -> Converse:
    # her message answers our like comment: the bot answers first (Dev, 2026-11-24); a question asked is dropped
    return Converse("He loves indie.", "screening", lang="en", ask=1)


OPENER = "He loves indie."


def test_our_like_is_linked_and_the_chat_starts_with_the_disclosure(tmp_path):
    r = Rig(tmp_path, Talker(opener()))
    r.like()
    assert r.d.step() == 600.0
    # her message replies to our like comment: answered first, then the introduction, then we wait for her
    assert r.phone.ours == [OPENER, DISCLOSURE]
    c = r.store.chat("Alex")
    assert c.state == NEW and c.like_comment == "Text 3" and r.store.liked("Alex") == []
    task, transcript, profile = r.talker.calls[0]
    assert task == "opener" and transcript == [(US, "Text 3"), (THEM, "Message 1")]
    assert profile.startswith("Age: 22")
    assert r.phone.screen == "feed-01" and r.notes == []   # back to the feed
    assert all(a[2] != "Text 24" for a in r.phone.actions if a[0] == "tap")   # ignored rows never opened


def test_a_second_pass_waits_for_her(tmp_path):
    r = Rig(tmp_path, Talker(opener()))
    r.like()
    r.d.step()
    n = len(r.phone.ours)
    r.phone.screen = "matches-turns"
    r.t += 601
    r.d.step()
    # the fake still lists the row under "Your turn": opened, but nothing new from her → no call, nothing sent
    assert len(r.phone.ours) == n and len(r.talker.calls) == 1


def test_an_old_match_we_only_liked_reads_her_profile_tab(tmp_path):
    r = Rig(tmp_path, Talker(opener()), ignore=("Alex", "Sam", "Text 24", "Text 27"))
    r.d.step()
    c = r.store.chat("Robin")
    assert c.state == NEW and c.like_comment == "Text 7" and c.liked_kind == "photo"
    assert "Profile details:" in c.profile and "Robin" not in c.profile
    assert c.prompt_langs == ["de"] and c.lang == "en"   # placeholders read as nothing → German; her message: English
    assert any(a[2] == "Profile" for a in r.phone.actions if a[0] == "tap")
    assert r.phone.ours[0] == OPENER and r.phone.ours[1].startswith("Hi Robin!")


def test_two_likes_with_her_name_leave_the_match_to_dev(tmp_path):
    r = Rig(tmp_path, Talker(), ignore=("Sam", "Robin", "Text 24", "Text 27"))
    r.store.save_chat(Chat("liked:a", "Alex", T0, state=LIKED, like_comment="Text 3", liked_kind="prompt"))
    r.store.save_chat(Chat("liked:b", "Alex", T0, state=LIKED, like_comment="Text 3", liked_kind="prompt"))
    r.d.step()
    assert r.store.chat("Alex").state == FOREIGN and r.phone.ours == [] and r.talker.calls == []
    assert r.notes == [chats.NOTICE[FOREIGN]] and "Alex" not in r.notes[0]
    r.t += 601
    r.phone.screen = "matches-turns"
    r.d.step()
    assert len(r.notes) == 1   # never again


def test_a_like_whose_comment_differs_is_not_ours(tmp_path):
    r = Rig(tmp_path, Talker())
    r.like(comment="something else")
    r.d.step()
    assert r.store.chat("Alex").state == FOREIGN and r.phone.ours == []


def test_hold_types_without_sending_then_resume_sends(tmp_path):
    r = Rig(tmp_path, Talker(opener()), mode=chats.HOLD)
    r.like()
    r.d.step()
    assert r.holds == [chats.HOLD_CHAT_MESSAGE] and r.phone.ours == [] and r.phone.field == OPENER
    assert r.store.chat("Alex").held["text"] == OPENER
    assert r.d.step() == 5.0 and r.phone.ours == []          # still held: nothing moves
    r.resume()
    assert r.phone.ours == [OPENER]
    assert r.holds == [chats.HOLD_CHAT_MESSAGE] * 2           # the next one (the introduction) typed and held
    assert r.phone.field == DISCLOSURE
    r.resume()
    assert r.phone.ours == [OPENER, DISCLOSURE] and len(r.holds) == 2   # then it waits for her
    assert [a[2] for a in r.phone.sent()] == ["Send", "Send"]


def test_dev_editing_the_held_message_sends_his_wording_and_the_bot_carries_on(tmp_path):
    r = Rig(tmp_path, Talker(opener()), mode=chats.HOLD)
    r.like()
    r.d.step()
    r.phone.field = "He loves indie, and old soul too."
    r.resume()
    c = r.store.chat("Alex")
    assert r.phone.ours == ["He loves indie, and old soul too."] and c.state == NEW
    assert [(s.kind, s.dev) for s in c.sent] == [("like_comment", False), ("opener", True)]
    assert c.held["template"] == "disclosure" and chats.NOTICE[MANUAL] not in r.notes   # the next one held


def test_dev_sending_his_own_edit_himself_counts_as_the_held_message(tmp_path):
    r = Rig(tmp_path, Talker(opener()), mode=chats.HOLD)
    r.like()
    r.d.step()
    r.phone.ours.append("Indie, yes!")   # Dev rewrote it and tapped send on the phone
    r.phone.field, r.phone.screen = "", "chat-comment"
    r.resume()
    c = r.store.chat("Alex")
    assert c.state == NEW and c.sent[-1].kind == "opener" and c.sent[-1].text == "Indie, yes!" and c.sent[-1].dev


def test_dev_writing_more_than_the_held_message_makes_the_chat_his_and_asks_back(tmp_path):
    backs = []
    r = Rig(tmp_path, Talker(opener()), mode=chats.HOLD)
    r.d.ask_back = backs.append
    r.like()
    r.d.step()
    r.phone.ours += ["Hey, it's me", "the human"]
    r.phone.field, r.phone.screen = "", "chat-comment"
    r.resume()
    c = r.store.chat("Alex")
    assert c.state == MANUAL and c.manual_from == NEW and backs == ["Alex"] and r.phone.sent() == []


def test_a_manual_chat_given_back_adopts_devs_messages_and_continues(tmp_path):
    r = Rig(tmp_path, Talker(opener()), mode=chats.HOLD)
    r.d.back_file = tmp_path / "give-back"
    r.like()
    r.d.step()
    r.phone.ours += ["Hey, it's me", "the human"]
    r.phone.field, r.phone.screen = "", "chat-comment"
    r.resume()
    assert r.store.chat("Alex").state == MANUAL
    r.d.back_file.write_text("Alex\n")
    r.phone.screen = "matches-turns"
    r.d.step()
    c = r.store.chat("Alex")
    assert not r.d.back_file.exists() and c.state == NEW and c.manual_from == ""
    assert [s.kind for s in c.sent if s.dev] == ["dev", "dev"]
    assert c.held["template"] == "disclosure"   # the pass went on: still no introduction → typed and held


def test_dev_sending_the_held_message_himself_counts_as_ours(tmp_path):
    r = Rig(tmp_path, Talker(opener()), mode=chats.HOLD)
    r.like()
    r.d.step()
    r.phone.ours.append(r.phone.field)   # Dev tapped send on the phone
    r.phone.field, r.phone.screen = "", "chat-comment"
    r.resume()
    c = r.store.chat("Alex")
    assert [s.kind for s in c.sent] == ["like_comment", "opener"] and c.state != MANUAL
    assert c.held["template"] == "disclosure"   # the pass went on: the introduction typed and held


def test_unmatch_after_closing_is_held_then_done(tmp_path):
    r = Rig(tmp_path, Talker(), mode=chats.HOLD)
    r.store.save_chat(Chat("Alex", "Alex", T0 - 3 * 86400, state=CLOSED, closed_at=T0 - 2 * 86400,
                           unmatch_at=T0 - 86400, sent=[Sent(T0 - 3 * 86400, "like_comment", "Text 3")]))
    r.d.step()
    assert r.holds == [chats.HOLD_CHAT_UNMATCH] and r.phone.unmatched == []
    r.resume()
    assert r.phone.unmatched == ["chat-comment"] and r.store.chat("Alex").unmatched


def test_failed_calls_retry_then_hand_over(tmp_path):
    t = Talker()
    t.fail = DecideError("timeout")
    r = Rig(tmp_path, t)
    r.like()
    for _ in range(3):
        r.phone.screen = "matches-turns"
        r.t += 601
        r.d.step()
    c = r.store.chat("Alex")
    assert c.state == HANDOVER and chats.NOTICE[HANDOVER] in r.notes
    assert r.phone.ours == []   # her reply to our like comment needs the opener call first: nothing went out


def test_usage_limit_waits_without_input(tmp_path):
    t = Talker()
    t.fail = UsageLimited(T0 + 3600)
    r = Rig(tmp_path, t)
    r.like()
    assert r.d.step() == pytest.approx(3600)
    n = len(r.phone.actions)
    assert r.d.step() == pytest.approx(3600) and len(r.phone.actions) == n


def test_off_and_paused_touch_nothing(tmp_path):
    r = Rig(tmp_path, Talker(), mode=chats.OFF)
    assert r.d.step() == 3600.0 and r.phone.actions == []
    r = Rig(tmp_path, Talker())
    r.paused = True
    assert r.d.step() == 5.0 and r.phone.actions == []


def test_read_ignore(tmp_path):
    (tmp_path / "x").write_text("Alex\n  # note\nSam # her\n\n")
    assert chats.read_ignore(tmp_path / "x") == {"Alex", "Sam"}
    assert chats.read_ignore(tmp_path / "missing") == set()
