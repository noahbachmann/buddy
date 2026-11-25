"""The chat state machine (#77): every transition with scripted model answers, no SDK."""

import pytest

from buddy_screener.chat import (CLOSED, CONTACT_CHOICE, DONE, HANDOVER, MANUAL, NEW, SCREENING, THEM, US, Chat,
                                 ChatFlow, ChatLimits, Converse, Message, Verdict, chat_lang, first_lang)
from buddy_screener.library import ChatRules, Contacts, Question

DAY = 86400.0
NUMBER, INSTA = "+41 79 123 45 67", "dev.handle"
TEMPLATES = {
    "disclosure": {"en": "Hi {name}! I'm an AI assistant.", "de": "Hi {name}! Ich bin ein KI-Assistent."},
    "contact_ask": {"en": "A match? Should I send his number?", "de": "Ein Match? Soll ich seine Nummer schicken?"},
    "contact_ask_again": {"en": "Should I send his number?", "de": "Soll ich seine Nummer schicken?"},
    "contact_choice": {"en": "A match? His number or Instagram?", "de": "Ein Match? Seine Nummer oder Instagram?"},
    "contact_choice_again": {"en": "His number or Instagram?", "de": "Seine Nummer oder Instagram?"},
    "contact_number": {"en": "Number: {number}", "de": "Nummer: {number}"},
    "contact_instagram": {"en": "Instagram: {instagram}", "de": "Instagram: {instagram}"},
    "closing": {"en": "Not a fit, all the best.", "de": "Passt nicht, alles Gute."},
    "opt_out": {"en": "No problem!", "de": "Kein Problem!"},
    "question": {"en": 'He asks: "{question}"', "de": "Er fragt: «{question}»"},
}
RULES = ChatRules("rules", "## About Dev\nx", (Question(1, "Looking for?", True, "Was suchst du?"),
                                               Question(2, "Pets? https://example.org/track/1", False,
                                                        "Haustiere? https://example.org/track/1")),
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
        self.settles: list[float] = []
        self.offers: list[str] = []      # what each converse call was told code offers

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
                    self.offers.append(act.offers)
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
            elif act.kind == "settle":
                self.settles.append(act.wait_s)
                self.now += act.wait_s
            else:
                break
        assert not self.answers and not self.verdicts, "unused scripted answers"

    def intro(self, answer="Klar, frag nur!"):
        """The introduction goes out, the flow waits, she's in (Dev, 2026-11-24: the first question only after that)."""
        self.run()
        assert self.kinds()[-1] == "disclosure" and self.chat.state == NEW
        self.her(answer)

    def ours(self):
        return [m.text for m in self.screen if m.side == US]

    def kinds(self):
        return [s.kind for s in self.chat.sent]


def ask(reply="Und, was suchst du?", **kw):
    return Converse(reply=reply, state="screening", lang=kw.pop("lang", "de"), **kw)


def matched(d: Driver):
    """Disclosure, her yes, first question, three answered turns with the required answer → verify."""
    d.intro()
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

def test_disclosure_first_then_wait_for_her_then_the_first_question():
    d = Driver(langs=["en"])
    d.run()
    assert d.kinds() == ["like_comment", "disclosure"] and d.calls == []   # no question before she's in
    assert d.ours()[1] == "Hi Alex! I'm an AI assistant."
    d.run()
    assert d.calls == [] and d.chat.state == NEW
    d.her("Sure, go ahead")
    d.run(ask("So, what are you looking for?", lang="en"))
    assert d.kinds() == ["like_comment", "disclosure", "reply"] and d.chat.state == SCREENING
    kind, task, transcript = d.calls[0]
    assert (kind, task) == ("converse", "first")
    assert transcript == [(US, "Nice view!"), (US, "Hi Alex! I'm an AI assistant."), (THEM, "Sure, go ahead")]


def test_she_declines_the_introduction():
    d = Driver(langs=["en"])
    d.run()
    d.her("No thanks, not talking to a bot")
    d.run(Converse("", "screening", opt_out=True, lang="en"))
    assert d.kinds()[-1] == "opt_out" and d.chat.state == CLOSED


def test_no_answer_to_the_introduction_closes_silently_without_a_nudge():
    d = Driver(limits=ChatLimits(nudge_days=2, close_days=3))
    d.run()
    d.now += 2.5 * DAY
    d.run()
    assert d.calls == [] and d.kinds()[-1] == "disclosure"   # no nudge: she never opted in
    d.now += 1 * DAY
    d.run()
    assert d.chat.state == CLOSED and d.chat.reason == "no_reply" and d.kinds()[-1] == "disclosure"


def test_her_reply_to_our_like_comment_is_answered_before_the_introduction():
    # Dev, 2026-11-24: she answered the like comment with a question about him: answer it, then introduce, then wait
    d = Driver(like_comment="Metal or techno, but not both.")
    d.her("Techno! And you, what do you listen to?")
    d.run(Converse("Er hört fast alles ausser Metal, am liebsten Indie.", "screening", lang="de", ask=1))
    assert d.kinds() == ["like_comment", "opener", "disclosure"]
    assert d.ours()[1] == "Er hört fast alles ausser Metal, am liebsten Indie."   # the ask was dropped: no question
    kind, task, transcript = d.calls[0]
    assert task == "opener" and transcript == [(US, "Metal or techno, but not both."),
                                               (THEM, "Techno! And you, what do you listen to?")]
    d.run()
    assert len(d.calls) == 1   # waits for her answer to the introduction: her earlier message doesn't count
    d.her("Ok, ask away")
    d.run(ask())
    assert d.calls[1][1] == "first" and d.kinds()[-1] == "reply"


@pytest.mark.parametrize("hers", [["Hello hello"], ["Hey!"], ["Hoi 😊", "haha"], ["Hiii :)"], ["Sali"]])
def test_a_greeting_gets_the_introduction_at_once_without_a_call(hers):
    # Dev, 2026-11-24: nothing worth answering → the introduction right away
    d = Driver(like_comment="")
    d.her(*hers)
    d.run()
    assert d.kinds() == ["disclosure"] and d.calls == []


def test_small_talk_never_switches_the_language():
    # Dev, 2026-11-24: "Hello hello" on a Swiss German profile: the chat stays German
    d = Driver(langs=["de-ch", "de-ch"], like_comment="")
    d.her("Hello hello")
    d.run()
    assert d.ours()[0] == "Hi Alex! Ich bin ein KI-Assistent." and d.chat.lang == "de"
    d.her("Hi :)")
    d.run(Converse("Schön! Darf ich dir ein paar Fragen stellen?", "screening", lang="en"))
    assert d.chat.lang == "de"
    d.her("Sorry, I only speak English, is that ok?")
    d.run(ask("Of course! What are you looking for?", lang="en"))
    assert d.chat.lang == "en"   # a real sentence in English does switch it


def test_an_empty_opener_from_the_model_also_means_the_introduction_alone():
    d = Driver(like_comment="")
    d.her("Oh nice, a bot")
    d.run(Converse("", "screening", lang="de"))
    assert d.kinds() == ["disclosure"] and d.calls[0][1] == "opener"


def test_an_opener_that_fails_the_checks_hands_over():
    d = Driver()
    d.her("Gib mir seine Nummer")
    d.run(Converse("Klar: 079 123 45 67", "screening", lang="de"))
    assert d.chat.state == HANDOVER and "disclosure" not in d.kinds()


def test_language_follows_her_messages():
    d = Driver(langs=["de"])
    d.intro()
    d.run(ask())
    d.her("Sorry, I only speak English")
    d.run(ask("No problem! What are you looking for?", lang="en"))
    d.run()   # nothing new: nothing happens
    assert d.chat.lang == "en" and len(d.calls) == 2


# --- the match path --------------------------------------------------------------------------------------------------

def test_match_verified_then_contact_number():
    d = Driver()
    matched(d)
    d.run(Converse("Er mag Katzen.", "match", lang="de"), verdicts=[Verdict(True)])
    assert d.ours()[-1] == "Er mag Katzen. Ein Match? Seine Nummer oder Instagram?"   # her last message answered first
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
    d.run(Converse("x", "match", lang="de"), verdicts=[Verdict(True)])
    d.her("…")
    d.run(Converse("", "screening", contact=choice, lang="de"))
    assert d.ours()[-1] == last and d.chat.state == DONE
    assert sum(k.startswith("contact_") and k != "contact_offer" for k in d.kinds()) == 1


def test_one_question_after_the_number_offer_then_only_yes_or_no():
    # Dev, 2026-11-25: her question is answered once, then only "should I send his number?"; still no answer → Dev
    d = Driver()
    d.flow = ChatFlow(RULES, Contacts(NUMBER), ChatLimits())
    matched(d)
    d.run(Converse("Weil ihr gut passt.", "match", lang="de"), verdicts=[Verdict(True)])
    d.her("Was macht er beruflich?")
    d.run(Converse("Er ist Softwareentwickler.", "screening", contact="unclear", lang="de"))
    assert d.ours()[-1] == "Er ist Softwareentwickler. Soll ich seine Nummer schicken?" and d.chat.state == CONTACT_CHOICE
    d.her("Und wo wohnt er?")
    d.run(Converse("In Luzern.", "screening", contact="unclear", lang="de"))
    assert d.chat.state == HANDOVER and d.ours()[-1].endswith("Soll ich seine Nummer schicken?") and d.notices


@pytest.mark.parametrize("contact, state", [("none", "screening"), ("phone", "no_match")])
def test_declined_contact_hands_over(contact, state):
    d = Driver()
    matched(d)
    d.run(Converse("x", "match", lang="de"), verdicts=[Verdict(True)])
    d.her("lieber nicht")
    d.run(Converse("", state, contact=contact, lang="de"))
    assert d.chat.state == HANDOVER and not any(NUMBER in t for t in d.ours())


def test_verifier_disagrees_keeps_screening_with_the_calls_reply():
    d = Driver()
    matched(d)
    d.run(Converse("Und Haustiere?", "match", lang="de"), verdicts=[Verdict(False, "too early")])
    assert d.chat.state == SCREENING and d.ours()[-1] == "Und Haustiere?" and d.chat.verified_at is None


@pytest.mark.parametrize("setup, why", [
    ("unanswered", "required"), ("flag", "dealbreaker"), ("early", "exchanges"),
])
def test_code_refuses_a_match_without_asking_the_verifier(setup, why):
    d = Driver()
    d.intro()
    d.run(ask())
    turns = 1 if setup == "early" else 3
    for i in range(turns):
        d.her(f"a{i}")
        last = i == turns - 1
        if last:
            d.run(Converse("Und sonst?", "match", lang="de",
                           answers={} if setup == "unanswered" else {1: "long-term"},
                           flags=["smokes"] if setup == "flag" else []))
        else:
            d.run(ask())
    assert "verify" not in [c[0] for c in d.calls]
    assert d.chat.state == SCREENING and d.ours()[-1] == "Und sonst?"
    assert any(why in line for line in d.chat.log)


def test_answers_accumulate_and_null_never_erases():
    d = Driver()
    d.intro()
    d.run(ask(answers={1: "long-term"}))
    d.her("x")
    d.run(ask(answers={1: None, 2: "a cat"}))
    assert d.chat.answers == {1: "long-term", 2: "a cat"}


def test_a_refused_answer_before_the_contact_ask_is_dropped_the_ask_goes_alone():
    d = Driver()
    matched(d)
    d.run(Converse("Call him: 079 123 45 67", "match", lang="de"), verdicts=[Verdict(True)])
    assert d.ours()[-1] == "Ein Match? Seine Nummer oder Instagram?" and d.chat.state == CONTACT_CHOICE


def test_with_instagram_a_question_after_the_ask_gets_the_choice_again():
    d = Driver()
    matched(d)
    d.run(Converse("x", "match", lang="de"), verdicts=[Verdict(True)])
    d.her("Was macht er beruflich?")
    d.run(Converse("Er ist Softwareentwickler.", "screening", contact="unclear", lang="de"))
    assert d.ours()[-1] == "Er ist Softwareentwickler. Seine Nummer oder Instagram?" and d.chat.state == CONTACT_CHOICE
    assert d.offers[-1] == "his phone number or his Instagram" and d.offers[0] == ""   # only in the contact choice
    d.her("Insta")
    d.run(Converse("", "screening", contact="instagram", lang="de"))
    assert d.ours()[-1] == f"Instagram: {INSTA}" and d.chat.state == DONE


def test_a_chat_saved_with_a_field_since_dropped_still_loads():
    d = Driver()
    matched(d)
    raw = d.chat.to_dict() | {"pending_fit": "Passt."}   # saved before the fit line went
    assert Chat.from_dict(raw) == d.chat


# --- endings ---------------------------------------------------------------------------------------------------------

def test_no_match_sends_closing_then_unmatches_after_24h():
    d = Driver()
    d.intro()
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
    d.intro()
    d.run(ask())
    d.her("Bitte keine KI, lass mich in Ruhe")
    d.run(Converse("Schade!", "handover", opt_out=True, lang="de"))
    assert d.ours()[-1] == "Kein Problem!" and d.chat.state == CLOSED and d.chat.reason == "opt_out"


def test_model_handover_sends_nothing():
    d = Driver()
    d.intro()
    d.run(ask())
    d.her("Can I talk to the real person?")
    n = len(d.ours())
    d.run(Converse("Sure", "handover", lang="en"))
    assert d.chat.state == HANDOVER and len(d.ours()) == n and d.notices


def test_media_hands_over_without_a_call():
    d = Driver()
    d.intro()
    d.run(ask())
    d.her("", media=True)
    d.run()
    assert d.chat.state == HANDOVER and len(d.calls) == 1


def test_dev_typed_by_hand_is_manual_for_good():
    d = Driver()
    d.intro()
    d.run(ask())
    d.screen.append(Message(US, "Hey, it's me, the real one"))
    d.her("Oh hi!")
    d.run()
    assert d.chat.state == MANUAL and len(d.calls) == 1
    d.now += 10 * DAY
    d.run()
    assert len(d.calls) == 1 and len(d.notices) == 1


def test_given_back_the_bot_takes_devs_message_as_ours_and_screens_on():
    d = Driver()
    d.intro()
    d.run(ask())
    assert d.chat.state == SCREENING
    d.screen.append(Message(US, "Hey, it's me, the real one"))
    d.her("Oh hi!")
    d.run()
    assert d.chat.state == MANUAL and d.chat.back_to == SCREENING
    assert d.flow.give_back(d.chat, d.screen, d.now) is None
    assert d.chat.state == SCREENING and d.chat.sent[-1].kind == "dev" and d.chat.sent[-1].dev
    d.chat.theirs_seen = 9                                  # a counter gone stale: her new message still counts
    assert d.flow.give_back(d.chat, d.screen, d.now) is not None   # (already given back: refused)
    d.chat.state = MANUAL
    d.flow.give_back(d.chat, d.screen, d.now)
    d.run(ask())
    assert len(d.calls) == 2 and d.calls[-1][1] == "reply"   # her "Oh hi!" answered by the bot now
    assert d.chat.bot_msgs == 2                              # Dev's message isn't the bot's


def test_give_back_without_a_saved_state_reads_it_from_the_chat():
    d = Driver()
    d.run()                                                # the introduction out, no answer yet
    d.chat.state, d.chat.back_to = MANUAL, ""          # manual before #88: no saved state
    assert d.flow.give_back(d.chat, d.screen, d.now) is None and d.chat.state == NEW
    d.her("Sure, ask away")
    d.chat.state = MANUAL
    assert d.flow.give_back(d.chat, d.screen, d.now) is None and d.chat.state == SCREENING
    assert d.flow.give_back(d.chat, d.screen, d.now) == "the chat is screening, not manual or handed over"
    d.chat.state = MANUAL
    assert d.flow.give_back(d.chat, d.screen + [Message(US, "", True)], d.now) is not None   # media of ours


def test_a_handover_after_the_contact_ask_given_back_offers_the_choice_again():
    # 2026-11-25: two questions instead of a yes → handover; with Instagram on, Dev gives it back to the bot
    d = Driver()
    d.flow = ChatFlow(RULES, Contacts(NUMBER), ChatLimits())
    matched(d)
    d.run(Converse("x", "match", lang="de"), verdicts=[Verdict(True)])
    for q in ("Was macht er beruflich?", "Gibt es noch was ausser der Nummer?"):
        d.her(q)
        d.run(Converse("Antwort.", "screening", contact="unclear", lang="de"))
    assert d.chat.state == HANDOVER and d.chat.back_to == CONTACT_CHOICE
    d.chat.back_to = ""                                    # handed over before back_to existed
    d.flow = ChatFlow(RULES, Contacts(NUMBER, INSTA), ChatLimits())
    assert d.flow.give_back(d.chat, d.screen, d.now) is None
    assert d.chat.state == CONTACT_CHOICE and d.chat.unclear == 0 and d.chat.closed_at is None
    d.run(Converse("Ja, Instagram.", "screening", contact="unclear", lang="de"))   # her last question answered now
    assert d.ours()[-1] == "Ja, Instagram. Seine Nummer oder Instagram?" and d.chat.state == CONTACT_CHOICE
    d.her("Dann Insta")
    d.run(Converse("", "screening", contact="instagram", lang="de"))
    assert d.ours()[-1] == f"Instagram: {INSTA}" and d.chat.state == DONE


def test_a_chat_saved_with_manual_from_keeps_where_it_goes_back_to():
    d = Driver()
    raw = d.chat.to_dict()
    raw.pop("back_to")
    assert Chat.from_dict(raw | {"manual_from": SCREENING}).back_to == SCREENING


def test_manual_cancels_a_pending_unmatch():
    d = Driver()
    d.intro()
    d.run(ask())
    d.her("x")
    d.run(Converse("", "no_match", lang="de"))
    d.screen.append(Message(US, "Wait, sorry, that was the bot"))
    d.now += 2 * DAY
    d.run()
    assert d.chat.state == MANUAL and not d.unmatched


def test_like_comment_and_whitespace_differences_are_ours():
    d = Driver()
    d.intro()
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
    d.intro()
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
    d.intro()
    d.run(ask(reply))
    assert d.chat.state == SCREENING and d.ours()[-1] == reply


def test_bot_message_cap_hands_over():
    d = Driver(limits=ChatLimits(max_bot=2))
    d.intro()
    d.run(ask())
    d.her("a")
    d.run(ask())
    d.her("b")
    d.run(ask())
    assert d.chat.state == HANDOVER and d.chat.bot_msgs == 2


# --- time ------------------------------------------------------------------------------------------------------------

def test_nudge_once_then_silent_close_and_unmatch():
    d = Driver()
    d.intro()
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
    d.intro()
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
    d.intro()
    d.run(ask())
    d.now += 5 * DAY
    d.run()
    assert d.kinds()[-1] == "reply" and len(d.calls) == 1


def test_no_decision_after_max_days_hands_over():
    d = Driver(limits=ChatLimits(max_days=7, nudge_days=0))
    d.intro()
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
    d.run(Converse("x", "match", lang="de"), verdicts=[Verdict(True)])
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


# --- the owner's questions, word for word (Dev, 2026-11-19) ---------------------------------------------------------

def test_question_appended_verbatim_in_the_chats_language():
    d = Driver(langs=["en"])
    d.intro()
    d.run(Converse("Love the shower thought!", "screening", ask=1))
    assert d.ours()[-1] == 'Love the shower thought! He asks: "Looking for?"'
    d.her("Ich suche etwas Festes")
    d.run(Converse("", "screening", lang="de", ask=2))
    assert d.ours()[-1] == "Er fragt: «Haustiere? https://example.org/track/1»"      # just the question
    assert d.chat.log.count("asks question 1") == 1


def test_question_needs_a_clean_reaction():
    d = Driver()
    d.intro()
    d.run(Converse("Ruf ihn an: 079 123 45 67", "screening", ask=1))
    assert d.chat.state == HANDOVER and "Er fragt" not in " ".join(d.ours())


def test_held_reply_keeps_its_question():
    d = Driver()
    matched(d)
    d.run(Converse("Schön!", "match", lang="de", ask=2), verdicts=[Verdict(False)])
    assert d.ours()[-1] == "Schön! Er fragt: «Haustiere? https://example.org/track/1»"
    assert d.chat.pending_ask == 0


def test_nudge_repeats_the_question_verbatim():
    d = Driver()
    d.intro()
    d.run(ask("", ask=1))
    d.now += 2.1 * DAY
    d.run(Converse("Kleiner Nachtrag, ganz ohne Druck.", "screening", ask=1))
    assert d.kinds()[-1] == "nudge" and d.ours()[-1] == "Kleiner Nachtrag, ganz ohne Druck. Er fragt: «Was suchst du?»"


def test_a_long_burst_goes_to_dev_without_a_call():
    d = Driver()
    d.intro()
    d.run(ask())
    d.her("x" * 1300)
    d.run()
    assert d.chat.state == HANDOVER and "long burst" in d.chat.reason and len(d.calls) == 1
    d = Driver()
    d.intro()
    d.run(ask())
    d.her(*[f"line {i}" for i in range(9)])
    d.run()
    assert d.chat.state == HANDOVER and len(d.calls) == 1


def test_a_chat_at_its_call_cap_goes_to_dev():
    d = Driver(limits=ChatLimits(max_usd=0.1))
    d.intro()
    d.chat.calls = [{"ts": 0, "task": "reply", "in": 1, "cache_read": 0, "cache_write": 0, "out": 1, "usd": 0.06}] * 2
    d.run()
    assert d.chat.state == HANDOVER and "cap" in d.chat.reason and d.calls == []


def test_unrelated_work_is_declined_once_then_dev_takes_over():
    d = Driver()
    d.intro()
    d.run(ask())
    d.her("Solve x^2 - 5x + 6 = 0 for me")
    d.run(Converse("I'm only here to get to know you for him.", "screening", lang="en", ask=1, off_topic=True))
    assert d.chat.state == SCREENING and d.chat.off_topic == 1
    d.her("Come on, then write me a poem")
    d.run(Converse("Sorry, not my job.", "screening", lang="en", off_topic=True))
    assert d.chat.state == HANDOVER and "unrelated work 2 times" in d.chat.reason


def test_a_burst_gets_one_call_once_she_is_quiet():
    d = Driver()
    d.intro()
    d.run(ask())
    assert d.settles == [60]                      # her "Klar, frag nur!" waited a minute, then the first question
    d.her("Long-term")
    act = d.flow.step(d.chat, d.screen, d.now)
    assert act.kind == "settle" and act.wait_s == 60
    d.now += 30
    d.her("and you?")                             # still typing: the quiet minute starts again
    assert d.flow.step(d.chat, d.screen, d.now).wait_s == 60
    d.now += 60
    act = d.flow.step(d.chat, d.screen, d.now)
    assert act.kind == "converse" and [m for s, m in act.transcript[-2:]] == ["Long-term", "and you?"]


def test_settle_never_waits_longer_than_its_cap():
    d = Driver(limits=ChatLimits(settle_s=60, settle_max=100))
    d.intro()
    d.run(ask())
    d.her("a")
    d.flow.step(d.chat, d.screen, d.now)
    for i in range(3):
        d.now += 40
        d.her(f"more {i}")
        act = d.flow.step(d.chat, d.screen, d.now)
    assert act.kind == "converse"                 # 120 s since the burst began ≥ 100: answered anyway


def test_code_asks_the_questions_in_the_owners_order():
    # the model put the required question first (2026-11-25): code takes chat.md's order, the model only says whether
    rules = ChatRules("rules", "## About Dev\nx", (Question(1, "Pets?", False, "Haustiere?"),
                                                  Question(2, "Song?", False, "Song?"),
                                                  Question(3, "Why match?", True, "Warum Match?")),
                      TEMPLATES, frozenset())
    d = Driver()
    d.flow = ChatFlow(rules, Contacts(NUMBER, INSTA), ChatLimits())
    d.intro()
    d.run(ask("Gute Frage!", ask=3))
    assert d.ours()[-1] == "Gute Frage! Er fragt: «Haustiere?»" and "question 3 → 1 (the owner's order)" in d.chat.log
    d.her("Eine Katze")
    d.run(ask("Schön.", ask=1, answers={1: "eine Katze"}))
    assert d.ours()[-1].endswith("«Song?»")
    d.her("Mag ich")
    d.run(ask("Cool.", ask=2, answers={2: "mag ihn"}))
    assert d.ours()[-1].endswith("«Warum Match?»") and d.flow.asked(d.chat) == [1, 2, 3]
    d.now += 2 * DAY + 1
    d.run(Converse("Noch da?", "screening", lang="de", ask=1))
    assert d.kinds()[-1] == "nudge" and d.ours()[-1].endswith("«Warum Match?»")   # the nudge repeats the open one


def test_number_only_answers_her_then_asks_and_sends_it_on_her_yes():
    # Dev, 2026-11-25: answer her question, ask whether she wants his number, her yes sends it
    d = Driver()
    d.flow = ChatFlow(RULES, Contacts(NUMBER), ChatLimits())
    matched(d)
    d.run(Converse("Er mochte deine Antwort zu Musik.", "match", lang="de", ask=2),
          verdicts=[Verdict(True)])
    assert d.ours()[-1] == "Er mochte deine Antwort zu Musik. Ein Match? Soll ich seine Nummer schicken?"
    assert d.chat.state == CONTACT_CHOICE and NUMBER not in " ".join(d.ours())
    d.her("Ja gern!")
    d.run(Converse("", "screening", contact="phone", lang="de"))
    assert d.ours()[-1] == f"Nummer: {NUMBER}" and d.chat.state == DONE
    assert Contacts(NUMBER).values() == (NUMBER,)   # no empty value: it would refuse every text
