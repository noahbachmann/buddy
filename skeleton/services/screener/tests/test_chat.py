"""The chat state machine (#77): every transition with scripted model answers, no SDK."""

import pytest

from buddy_screener.chat import (CLOSED, CONTACT_CHOICE, DONE, HANDOVER, MANUAL, NEW, SCREENING, THEM, US, Chat,
                                 ChatFlow, ChatLimits, Converse, Message, Verdict, chat_lang, first_lang)
from buddy_screener.library import ChatRules, Contacts, Question

DAY = 86400.0
NUMBER, INSTA = "+41 79 123 45 67", "dev.handle"
TEMPLATES = {
    "disclosure": {"en": "Hi {name}! I'm an AI assistant.", "de": "Hi {name}! Ich bin ein KI-Assistent."},
    "contact_offer": {"en": "{fit_line} Phone or Instagram?", "de": "{fit_line} Telefon oder Instagram?"},
    "contact_number": {"en": "Number: {number}", "de": "Nummer: {number}"},
    "contact_instagram": {"en": "Instagram: {instagram}", "de": "Instagram: {instagram}"},
    "closing": {"en": "Not a fit, all the best.", "de": "Passt nicht, alles Gute."},
    "opt_out": {"en": "No problem!", "de": "Kein Problem!"},
}
RULES = ChatRules("rules", "## About Dev\nx", (Question(1, "Looking for?", True), Question(2, "Pets?", False)),
                  TEMPLATES, frozenset({"https://example.org/track/1"}))


class Driver:
    """Runs the flow like the sim does: her messages in, scripted model answers, our sends onto the screen."""

    def __init__(self, langs=("de",), limits=ChatLimits(), like_comment="Nice view!"):
        self.flow = ChatFlow(RULES, Contacts(NUMBER, INSTA), limits)
        self.now = 1000.0
        self.chat = Chat("k1", "Alex", self.now)
        self.screen: list[Message] = [Message(US, like_comment)] if like_comment else []
        self.flow.open(self.chat, like_comment, list(langs), self.now)
        self.answers: list[Converse] = []
        self.verdicts: list[Verdict] = []
        self.calls: list[tuple[str, str, list]] = []     # (kind, task, transcript)
        self.notices: list[str] = []
        self.unmatched = False

    def her(self, *texts, media=False):
        self.screen += [Message(THEM, t, media) for t in texts]

    def run(self, *answers: Converse, verdicts=()):
        self.answers += answers
        self.verdicts += verdicts
        for _ in range(20):
            act = self.flow.step(self.chat, self.screen, self.now)
            while act.kind in ("converse", "verify"):
                self.calls.append((act.kind, act.task, act.transcript))
                if act.kind == "converse":
                    act = self.flow.conversed(self.chat, self.answers.pop(0), self.now)
                else:
                    act = self.flow.verified(self.chat, self.verdicts.pop(0), self.now)
            if act.kind == "send":
                self.screen.append(Message(US, act.text))
                self.flow.delivered(self.chat, act, self.now)
            elif act.kind == "notify":
                self.notices.append(act.text)
            elif act.kind == "unmatch":
                self.unmatched = True
                self.flow.unmatched(self.chat, self.now)
            else:
                break
        assert not self.answers and not self.verdicts, "unused scripted answers"

    def ours(self):
        return [m.text for m in self.screen if m.side == US]

    def kinds(self):
        return [s.kind for s in self.chat.sent]


def ask(reply="Und, was suchst du?", **kw):
    return Converse(reply=reply, state="screening", lang=kw.pop("lang", "de"), **kw)


def matched(d: Driver):
    """Disclosure, first question, three answered turns with the required answer → verify."""
    d.run(ask())
    for i in range(3):
        d.her(f"answer {i}")
        d.run(ask(answers={1: "something long-term"} if i == 0 else {}))
    d.her("answer 3")


# --- languages -------------------------------------------------------------------------------------------------------

@pytest.mark.parametrize("langs, lang", [
    (["de"], "de"), (["de-ch", "en"], "de"), (["en", "en", "de"], "en"), (["en", "other"], "en"),
    (["other"], "en"), ([], "de"),
])
def test_first_lang(langs, lang):
    assert first_lang(langs) == lang


def test_chat_lang():
    assert [chat_lang(x, "de") for x in ("de", "de-ch", "en", "other", "")] == ["de", "de", "en", "en", "de"]


# --- opening ---------------------------------------------------------------------------------------------------------

def test_disclosure_first_then_a_generated_question():
    d = Driver(langs=["en"])
    d.run(ask("So, what are you looking for?", lang=""))
    assert d.kinds() == ["like_comment", "disclosure", "reply"]
    assert d.ours()[1] == "Hi Alex! I'm an AI assistant."
    assert d.chat.state == SCREENING
    kind, task, transcript = d.calls[0]
    assert (kind, task) == ("converse", "first")
    assert transcript == [(US, "Nice view!"), (US, "Hi Alex! I'm an AI assistant.")]


def test_her_message_before_ours_is_part_of_the_first_call():
    d = Driver(like_comment="")
    d.her("Hey!")
    d.run(ask())
    assert d.calls[0][2] == [(THEM, "Hey!"), (US, "Hi Alex! Ich bin ein KI-Assistent.")]
    assert d.chat.theirs_seen == 1 and d.chat.turns == 0


def test_language_follows_her_messages():
    d = Driver(langs=["de"])
    d.run(ask())
    d.her("Sorry, I only speak English")
    d.run(ask("No problem! What are you looking for?", lang="en"))
    d.run()   # nothing new: nothing happens
    assert d.chat.lang == "en" and len(d.calls) == 2


# --- the match path --------------------------------------------------------------------------------------------------

def test_match_verified_then_contact_number():
    d = Driver()
    matched(d)
    d.run(Converse("Noch eine Frage?", "match", fit_line="Ihr passt gut.", lang="de"), verdicts=[Verdict(True)])
    assert d.ours()[-1] == "Ihr passt gut. Telefon oder Instagram?"
    assert d.chat.state == CONTACT_CHOICE and d.chat.verified_at is not None
    assert [c[0] for c in d.calls][-2:] == ["converse", "verify"]
    d.her("Nummer bitte")
    d.run(Converse("", "screening", contact="phone", lang="de"))
    assert d.ours()[-1] == f"Nummer: {NUMBER}"
    assert d.chat.state == DONE
    n = len(d.calls)
    d.her("Danke!")
    d.run()
    assert d.chat.state == DONE and len(d.calls) == n      # done: Dev's chat now, no more calls


@pytest.mark.parametrize("choice, last", [("instagram", f"Instagram: {INSTA}"), ("both", f"Nummer: {NUMBER}")])
def test_contact_choices(choice, last):
    d = Driver()
    matched(d)
    d.run(Converse("x", "match", fit_line="Passt.", lang="de"), verdicts=[Verdict(True)])
    d.her("…")
    d.run(Converse("", "screening", contact=choice, lang="de"))
    assert d.ours()[-1] == last and d.chat.state == DONE
    assert sum(k.startswith("contact_") and k != "contact_offer" for k in d.kinds()) == 1


def test_unclear_contact_asks_once_then_hands_over():
    d = Driver()
    matched(d)
    d.run(Converse("x", "match", fit_line="Passt.", lang="de"), verdicts=[Verdict(True)])
    d.her("hmm")
    d.run(Converse("Telefon oder Instagram?", "screening", contact="unclear", lang="de"))
    assert d.chat.state == CONTACT_CHOICE and d.ours()[-1] == "Telefon oder Instagram?"
    d.her("weiss nicht")
    d.run(Converse("?", "screening", contact="unclear", lang="de"))
    assert d.chat.state == HANDOVER and d.notices


@pytest.mark.parametrize("contact, state", [("none", "screening"), ("phone", "no_match")])
def test_declined_contact_hands_over(contact, state):
    d = Driver()
    matched(d)
    d.run(Converse("x", "match", fit_line="Passt.", lang="de"), verdicts=[Verdict(True)])
    d.her("lieber nicht")
    d.run(Converse("", state, contact=contact, lang="de"))
    assert d.chat.state == HANDOVER and not any(NUMBER in t for t in d.ours())


def test_verifier_disagrees_keeps_screening_with_the_calls_reply():
    d = Driver()
    matched(d)
    d.run(Converse("Und Haustiere?", "match", fit_line="Passt.", lang="de"), verdicts=[Verdict(False, "too early")])
    assert d.chat.state == SCREENING and d.ours()[-1] == "Und Haustiere?" and d.chat.verified_at is None


@pytest.mark.parametrize("setup, why", [
    ("unanswered", "required"), ("flag", "dealbreaker"), ("early", "exchanges"),
])
def test_code_refuses_a_match_without_asking_the_verifier(setup, why):
    d = Driver()
    d.run(ask())
    turns = 1 if setup == "early" else 3
    for i in range(turns):
        d.her(f"a{i}")
        last = i == turns - 1
        if last:
            d.run(Converse("Und sonst?", "match", fit_line="Passt.", lang="de",
                           answers={} if setup == "unanswered" else {1: "long-term"},
                           flags=["smokes"] if setup == "flag" else []))
        else:
            d.run(ask())
    assert "verify" not in [c[0] for c in d.calls]
    assert d.chat.state == SCREENING and d.ours()[-1] == "Und sonst?"
    assert any(why in line for line in d.chat.log)


def test_answers_accumulate_and_null_never_erases():
    d = Driver()
    d.run(ask(answers={1: "long-term"}))
    d.her("x")
    d.run(ask(answers={1: None, 2: "a cat"}))
    assert d.chat.answers == {1: "long-term", 2: "a cat"}


def test_bad_fit_line_hands_over():
    d = Driver()
    matched(d)
    d.run(Converse("x", "match", fit_line="Call him: 079 123 45 67", lang="de"), verdicts=[Verdict(True)])
    assert d.chat.state == HANDOVER and "contact_offer" not in d.kinds()


# --- endings ---------------------------------------------------------------------------------------------------------

def test_no_match_sends_closing_then_unmatches_after_24h():
    d = Driver()
    d.run(ask())
    d.her("Ich rauche viel")
    d.run(Converse("", "no_match", flags=["smokes"], lang="de"))
    assert d.ours()[-1] == "Passt nicht, alles Gute." and d.chat.state == CLOSED
    d.now += 23 * 3600
    d.run()
    assert not d.unmatched
    d.now += 2 * 3600
    d.run()
    assert d.unmatched and d.chat.unmatched
    d.now += DAY
    d.run()
    assert d.kinds().count("closing") == 1


def test_opt_out_wins_over_everything():
    d = Driver()
    d.run(ask())
    d.her("Bitte keine KI, lass mich in Ruhe")
    d.run(Converse("Schade!", "handover", opt_out=True, lang="de"))
    assert d.ours()[-1] == "Kein Problem!" and d.chat.state == CLOSED and d.chat.reason == "opt_out"


def test_model_handover_sends_nothing():
    d = Driver()
    d.run(ask())
    d.her("Can I talk to the real person?")
    n = len(d.ours())
    d.run(Converse("Sure", "handover", lang="en"))
    assert d.chat.state == HANDOVER and len(d.ours()) == n and d.notices


def test_media_hands_over_without_a_call():
    d = Driver()
    d.run(ask())
    d.her("", media=True)
    d.run()
    assert d.chat.state == HANDOVER and len(d.calls) == 1


def test_dev_typed_by_hand_is_manual_for_good():
    d = Driver()
    d.run(ask())
    d.screen.append(Message(US, "Hey, it's me, the real one"))
    d.her("Oh hi!")
    d.run()
    assert d.chat.state == MANUAL and len(d.calls) == 1
    d.now += 10 * DAY
    d.run()
    assert len(d.calls) == 1 and len(d.notices) == 1


def test_manual_cancels_a_pending_unmatch():
    d = Driver()
    d.run(ask())
    d.her("x")
    d.run(Converse("", "no_match", lang="de"))
    d.screen.append(Message(US, "Wait, sorry, that was the bot"))
    d.now += 2 * DAY
    d.run()
    assert d.chat.state == MANUAL and not d.unmatched


def test_like_comment_and_whitespace_differences_are_ours():
    d = Driver()
    d.run(ask("Und,   was\nsuchst du?"))
    d.screen[-1] = Message(US, "Und, was suchst du?")
    d.her("x")
    d.run(ask())
    assert d.chat.state == SCREENING


# --- generated text checks -------------------------------------------------------------------------------------------

@pytest.mark.parametrize("reply, why", [
    ("", "empty"),
    ("x" * 401, "chars"),
    ("Schreib ihm: 079 123 45 67", "number"),
    ("Seine Nummer endet auf 1234567", "number"),
    ("Folg ihm auf @dev.handle", "contact value"),
    ("Er ist dev.handle auf Insta", "contact value"),
    ("Schreib @someone", "handle"),
    ("Schau mal: https://evil.example/x", "link"),
    ("Schau mal: www.evil.example", "link"),
])
def test_refused_replies_hand_over(reply, why):
    d = Driver()
    d.run(ask(reply))
    assert d.chat.state == HANDOVER and any(why in line for line in d.chat.log), d.chat.log
    assert d.kinds() == ["like_comment", "disclosure"]


@pytest.mark.parametrize("reply", [
    "Was hältst du von dem Song? https://example.org/track/1",
    "Wie wär's mit 2 Tagen Wandern?",
    "Mail me at…? No, just kidding",
])
def test_allowed_replies(reply):
    d = Driver()
    d.run(ask(reply))
    assert d.chat.state == SCREENING and d.ours()[-1] == reply


def test_bot_message_cap_hands_over():
    d = Driver(limits=ChatLimits(max_bot=2))
    d.run(ask())
    d.her("a")
    d.run(ask())
    d.her("b")
    d.run(ask())
    assert d.chat.state == HANDOVER and d.chat.bot_msgs == 2


# --- time ------------------------------------------------------------------------------------------------------------

def test_nudge_once_then_silent_close_and_unmatch():
    d = Driver()
    d.run(ask())
    d.now += 1.9 * DAY
    d.run()
    assert len(d.calls) == 1
    d.now += 0.2 * DAY
    d.run(Converse("Noch da? 🙂", "screening", lang="de"))
    assert d.calls[-1][1] == "nudge" and d.kinds()[-1] == "nudge" and d.chat.turns == 0
    d.now += 2.9 * DAY
    d.run()
    assert d.chat.state == SCREENING
    d.now += 0.2 * DAY
    d.run()
    assert d.chat.state == CLOSED and d.chat.reason == "no_reply" and d.kinds()[-1] == "nudge"
    d.now += DAY
    d.run()
    assert d.unmatched


def test_a_reply_after_the_nudge_continues():
    d = Driver()
    d.run(ask())
    d.now += 2.1 * DAY
    d.run(Converse("Noch da?", "screening", lang="de"))
    d.her("Ja sorry!")
    d.run(ask())
    d.now += 2.1 * DAY
    d.run()
    assert d.chat.state == SCREENING and d.kinds().count("nudge") == 1


def test_nudge_off():
    d = Driver(limits=ChatLimits(nudge_days=0))
    d.run(ask())
    d.now += 5 * DAY
    d.run()
    assert d.kinds()[-1] == "reply" and len(d.calls) == 1


def test_no_decision_after_max_days_hands_over():
    d = Driver(limits=ChatLimits(max_days=7, nudge_days=0))
    d.run(ask())
    for i in range(6):
        d.now += DAY
        d.her(f"m{i}")
        d.run(ask())
    d.now += DAY
    d.her("late")
    d.run()                       # over the limit: no call, even with a new message
    assert d.chat.state == HANDOVER and len(d.calls) == 7


# --- what the model sees ---------------------------------------------------------------------------------------------

def test_transcript_redacts_sent_contacts_and_media():
    d = Driver()
    matched(d)
    d.run(Converse("x", "match", fit_line="Passt.", lang="de"), verdicts=[Verdict(True)])
    d.her("Insta!")
    d.run(Converse("", "screening", contact="instagram", lang="de"))
    d.her("", media=True)
    t = d.flow.transcript(d.chat, d.screen)
    assert (US, "[contact sent]") in t and (THEM, "[media]") in t
    assert INSTA not in str(t) and all(INSTA not in str(c[2]) and NUMBER not in str(c[2]) for c in d.calls)


def test_round_trip_through_dict():
    d = Driver()
    matched(d)
    c = Chat.from_dict(d.chat.to_dict())
    assert c == d.chat and c.state == SCREENING and c.answers == {1: "something long-term"}


def test_states_start_new():
    assert Driver().chat.state == NEW
