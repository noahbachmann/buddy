import pytest

from buddy_screener import shadow
from buddy_screener.decide import Browse, Usage
from buddy_screener.library import Library, LikeComment
from buddy_screener.navigator import Navigator, NeedsYou
from buddy_screener.store import Store
from fake_phone import FakePhone
from fixture_files import PACKAGE

LIB = Library("prefs", (LikeComment("photo_place", "de", "photo of a place", "Wo ist das?"),
                        LikeComment("hot_take", "en", "bold opinion", "Bold.")))


class FakeDecider:
    def __init__(self, browse: Browse):
        self.result, self.seen = browse, None

    def decide(self, p, lib):
        self.seen = p
        return self.result, Usage("browse", "m", 10, 2000, 0, 300, 0.01, 5000, 0.3, 0.1)


def like(on="photo 1", comment="photo_place", missing="", text=None):
    if text is None:   # check_browse always fills it for a comment: at least the template's text
        text = LIB.comment(comment).text if comment != "none" else ""
    return Browse("like", on, comment, missing, ["de", "en", "de"], [], [], "Likes hiking.", comment_text=text)


def nav(phone):
    return Navigator(phone, PACKAGE, pause=lambda s: None, log=lambda s: None)


@pytest.fixture
def store():
    return Store(":memory:")


def test_shadow_label_type_sends_nothing(store, tmp_path):
    phone = FakePhone("feed-01")
    n = nav(phone)
    bot = like()
    bot.soft_plus, bot.uncovered = ["goofy"], ["diet: vegan"]
    r = shadow.run_shadow(n, FakeDecider(bot), LIB, store, tmp_path)
    assert len(r.capture.profile.sections) == 9 and phone.screen == "feed-11"
    lines = shadow.describe(r, LIB)
    assert "LIKE" in lines[0] and any("Wo ist das?" in x for x in lines)
    assert "  soft positives: goofy" in lines and "  preferences silent on: diet: vegan" in lines and not any("soft negatives" in x for x in lines)
    assert lines[1].startswith("  details: ") and len(lines[1].split(" · ")) == len(r.capture.profile.details) > 1

    row = store.shadow(r.row_id)[0]
    assert row.bot["comment"] == "photo_place" and row.bot["chat_lang"] == "de" and row.tokens == 2310
    cap = shadow.load_capture(tmp_path, r.row_id)
    assert cap is not None and cap.profile.name == r.capture.profile.name
    refs = [s.ref for s in cap.profile.sections]

    store.label(r.row_id, shadow.make_label(row, "like", None, None, LIB, refs))    # = the bot's choice
    row = store.shadow(r.row_id)[0]
    assert (row.dev["comment_on"], row.dev["comment"]) == ("photo 1", "photo_place")

    text = shadow.type_label(nav(phone), row, cap, LIB)            # a new process: capture from tmpfs
    assert text == "Wo ist das?" and phone.field == "Wo ist das?" and phone.screen == "sheet"
    assert phone.sent() == []


def test_a_new_shadow_drops_the_old_capture(store, tmp_path):
    a = shadow.run_shadow(nav(FakePhone("feed-01")), FakeDecider(like()), LIB, store, tmp_path)
    b = shadow.run_shadow(nav(FakePhone("feed-01")), FakeDecider(like()), LIB, store, tmp_path)
    assert shadow.load_capture(tmp_path, a.row_id) is None and shadow.load_capture(tmp_path, b.row_id) is not None


def row(store, bot: Browse):
    rid = store.add_shadow("k", 100.0, bot.to_dict() | {"chat_lang": "de"}, Usage("browse", "m"))
    return store.shadow(rid)[0]


def test_label_text_is_what_goes_out(store):
    r = row(store, like(text="Wo ist dieser See?"))
    assert shadow.make_label(r, "like", None, None, LIB)["text"] == "Wo ist dieser See?"      # the bot's adapted
    assert shadow.make_label(r, "like", "prompt 1", "hot_take", LIB)["text"] == "Bold."      # Dev's pick: template
    own = shadow.make_label(r, "like", "prompt 1", "hot_take", LIB, text="Bold  take.")
    assert own["text"] == "Bold take."
    with pytest.raises(shadow.LabelError, match="a number"):
        shadow.make_label(r, "like", "prompt 1", "hot_take", LIB, text="call 0791234567")
    assert shadow.make_label(r, "skip", None, None, LIB)["text"] is None


def test_type_and_send_the_labelled_text(store, tmp_path):
    phone = FakePhone("feed-01")
    n = nav(phone)
    r = shadow.run_shadow(n, FakeDecider(like(text="Wo ist dieser See?")), LIB, store, tmp_path)
    store.label(r.row_id, shadow.make_label(store.shadow(r.row_id)[0], "like", None, None, LIB))
    row_ = store.shadow(r.row_id)[0]
    assert shadow.type_label(n, row_, r.capture, LIB) == "Wo ist dieser See?"
    shadow.act_label(n, row_, r.capture, LIB, "send")
    assert phone.sent()


def test_label_rules(store):
    skipped = row(store, Browse("skip", "none", "none", "", [], [], [], "no"))
    with pytest.raises(shadow.LabelError, match="name the section"):
        shadow.make_label(skipped, "like", None, None, LIB)
    with pytest.raises(shadow.LabelError, match="templates.md"):
        shadow.make_label(skipped, "like", "prompt 1", "not_there", LIB)
    with pytest.raises(shadow.LabelError, match="isn't a section"):
        shadow.make_label(skipped, "like", "prompt 7", "none", LIB, ["photo 1", "prompt 1"])
    lab = shadow.make_label(skipped, "like", "Prompt1", "hot_take", LIB, ["photo 1", "prompt 1"])
    assert (lab["comment_on"], lab["comment"]) == ("prompt 1", "hot_take")
    lab = shadow.make_label(row(store, like()), "skip", None, None, LIB, dealbreaker=True)
    assert (lab["decision"], lab["comment_on"], lab["dealbreaker"]) == ("skip", "none", True)


def test_type_needs_a_like(store):
    r = row(store, like())
    r.dev = shadow.make_label(r, "skip", None, None, LIB)
    with pytest.raises(shadow.LabelError, match="nothing to type"):
        shadow.type_label(None, r, None, LIB)


def test_a_like_without_a_comment_opens_the_panel_and_sends_it_empty(store, tmp_path):
    phone = FakePhone("feed-01")
    phone.upsell = False
    n = nav(phone)
    r = shadow.run_shadow(n, FakeDecider(like(comment="none", missing="x")), LIB, store, tmp_path)
    store.label(r.row_id, shadow.make_label(store.shadow(r.row_id)[0], "like", None, None, LIB))
    row_ = store.shadow(r.row_id)[0]
    assert row_.dev["comment"] == "none" and shadow.type_label(n, row_, r.capture, LIB) == ""
    assert phone.screen == "sheet" and not any(a[0] == "type" for a in phone.actions)
    _, nxt = shadow.act_label(n, row_, r.capture, LIB, "send")
    assert nxt == "Kim" and [a[2] for a in phone.sent()] == ["Send like"]


def test_report_numbers(store):
    def add(bot, dev_args, **flags):
        r = row(store, bot)
        store.label(r.id, shadow.make_label(r, *dev_args, LIB, **flags))

    add(like(), ("like", None, None))                                   # agree, same comment
    add(like(), ("like", "prompt 1", "hot_take"))                       # agree on like, other comment
    add(like("photo 1", "none", "a pet photo"), ("skip", None, None), dealbreaker=True)   # bot liked a dealbreaker
    add(Browse("skip", "none", "none", "", [], [], [], "x"), ("skip", None, None))
    out = "\n".join(shadow.report(store.shadow(), store.usage_rows()))
    assert "agreement like/skip: 3/4 = 75% [NOT YET" in out
    assert "dealbreaker: 1 [NOT YET]" in out
    assert "Dev sent the bot's comment: 1/2 = 50%" in out
    assert "missing: a pet photo" in out and "preferences silent on" not in out


def test_comment_target_is_the_text_that_goes_out(store):
    def sent(bot, *dev_args, **kw):
        r = row(store, bot)
        r.dev = shadow.make_label(r, "like", *dev_args, LIB, **kw)
        return shadow._sent_bots_comment(r)

    adapted = like(text="Wo ist dieser See?")
    assert sent(adapted, None, None)                                   # bare like: the bot's text
    assert sent(adapted, "photo 1", "photo_place", text="Wo ist dieser See?")
    assert not sent(adapted, "photo 1", "photo_place")                 # same id, but the template's text went out
    assert not sent(adapted, "photo 1", "photo_place", text="Wo ist das genau?")   # Dev rewrote it
    assert not sent(adapted, "prompt 1", "hot_take")
    assert sent(like(comment="none", missing="x"), "photo 1", "none")  # neither had a comment
    old = row(store, like())
    old.bot.pop("comment_text")
    old.dev = {"decision": "like", "comment_on": "photo 1", "comment": "photo_place"}   # before #78: by id
    assert shadow._sent_bots_comment(old)


def test_report_counts_preference_gaps(store):
    for gaps in (["smokes now and then"], [], ["vegan", "rock climbing"]):
        bot = like()
        bot.uncovered = gaps
        row(store, bot)
    out = "\n".join(shadow.report(store.shadow(), []))
    assert "preferences silent on (bot): 3 in 2 profiles" in out and "    rock climbing" in out


def test_unjudged_rows_stay_out_of_the_report(store):
    r = row(store, like())
    store.label(r.id, shadow.make_label(r, "like", None, None, LIB))
    r = row(store, like())
    lab = shadow.make_label(r, "unjudged", None, None, LIB, note="couldn't see it")
    assert (lab["decision"], lab["comment_on"], lab["comment"], lab["text"]) == ("unjudged", "none", "none", None)
    store.label(r.id, lab)
    out = "\n".join(shadow.report(store.shadow(), []))
    assert "labelled: 1 (1 profiles), not judged (left out): 1" in out and "agreement like/skip: 1/1" in out


def test_report_since_counts_one_session(store):
    for decision in ("like", "skip", "skip"):
        r = row(store, like())
        store.label(r.id, shadow.make_label(r, decision, None, None, LIB))
    first = store.shadow()[1].id
    rows, _ = shadow.session_rows(store.shadow(), store.usage_rows, first)
    assert [r.id for r in rows] == [first, first + 1]
    assert "agreement like/skip: 0/2" in "\n".join(shadow.report(rows, []))
    rows, _ = shadow.session_rows(store.shadow(), store.usage_rows, first, first)
    assert [r.id for r in rows] == [first]


def test_report_says_whether_text_first_pays(store):
    early = Browse("skip", "none", "none", "", [], [], ["smokes"], "states it", stage="text")
    store.label(row(store, early).id, {"decision": "like", "comment_on": "prompt 1", "comment": "none", "note": "",
                                       "wrong": False, "dealbreaker": False, "sensitive": False})
    row(store, like())
    for task, cost in (("text", 0.004), ("text", 0.004), ("browse", 0.02)):
        store.add_usage(1.0, Usage(task, "m", cost=cost))
    out = "\n".join(shadow.report(store.shadow(), store.usage_rows()))
    assert "skipped by the text check: 1, Dev liked 1 of them" in out
    assert "text first: 1 of 2 profiles skipped before photos, saved ~$0.02, the text checks cost $0.01 → pays" in out


def test_skip_and_send_carry_out_devs_label(store, tmp_path):
    phone = FakePhone("feed-01")
    n = nav(phone)
    r = shadow.run_shadow(n, FakeDecider(like()), LIB, store, tmp_path)
    row = store.shadow(r.row_id)[0]
    with pytest.raises(shadow.LabelError):
        shadow.act_label(n, row, r.capture, LIB, "send")             # not labelled: nothing happens
    store.label(r.row_id, shadow.make_label(row, "like", None, None, LIB))
    row = store.shadow(r.row_id)[0]
    with pytest.raises(shadow.LabelError):
        shadow.act_label(n, row, r.capture, LIB, "skip")             # label says like
    shadow.type_label(n, row, r.capture, LIB)
    acted, nxt = shadow.act_label(n, row, r.capture, LIB, "send")
    assert acted["acted"] == "send" and nxt == "Kim"
    assert [a[2] for a in phone.sent()] == ["Send like", "Send Like anyway"]


def test_skip_only_the_captured_profile(store, tmp_path):
    phone = FakePhone("feed-01")
    n = nav(phone)
    r = shadow.run_shadow(n, FakeDecider(like()), LIB, store, tmp_path)
    store.label(r.row_id, shadow.make_label(store.shadow(r.row_id)[0], "skip", None, None, LIB))
    row = store.shadow(r.row_id)[0]
    phone.renamed = True                                             # another profile came up meanwhile
    with pytest.raises(NeedsYou):
        shadow.act_label(n, row, r.capture, LIB, "skip")
    assert phone.sent() == []
    phone.renamed = False
    _, nxt = shadow.act_label(n, row, r.capture, LIB, "skip")
    assert nxt == "Kim" and [a[2] for a in phone.sent()] == ["Skip Alex"]


def test_out_of_likes_keeps_the_comment_for_later(store, tmp_path):
    phone = FakePhone("feed-01")
    n = nav(phone)
    r = shadow.run_shadow(n, FakeDecider(like()), LIB, store, tmp_path)
    store.label(r.row_id, shadow.make_label(store.shadow(r.row_id)[0], "like", None, None, LIB))
    row = store.shadow(r.row_id)[0]
    shadow.type_label(n, row, r.capture, LIB)
    phone.out_of_likes = True
    with pytest.raises(NeedsYou, match="out of free likes"):
        shadow.act_label(n, row, r.capture, LIB, "send")
    assert phone.screen == "sheet" and phone.sent() == []           # paywall closed, never a paid option
    phone.out_of_likes = False                                       # the next day
    _, nxt = shadow.act_label(n, row, r.capture, LIB, "send")
    assert nxt == "Kim" and [a[2] for a in phone.sent()] == ["Send like", "Send Like anyway"]
    assert [a[2] for a in phone.actions if a[0] == "tap"].count("Edit comment") == 2   # refocused, read back


def test_out_of_likes_can_move_on_to_the_next_profile(store, tmp_path):
    phone = FakePhone("feed-01")
    n = nav(phone)
    r = shadow.run_shadow(n, FakeDecider(like()), LIB, store, tmp_path)
    store.label(r.row_id, shadow.make_label(store.shadow(r.row_id)[0], "like", None, None, LIB))
    row = store.shadow(r.row_id)[0]
    shadow.type_label(n, row, r.capture, LIB)
    phone.out_of_likes = phone.paywall_moves_on = True   # seen live 2026-11-19
    with pytest.raises(NeedsYou, match="moved on to the next profile: this one is gone") as e:
        shadow.act_label(n, row, r.capture, LIB, "send")
    assert phone.sent() == [] and "Kim" not in str(e.value) and "Alex" not in str(e.value)   # no names (#80)


def test_skip_works_on_an_unjudged_row(store, tmp_path):
    phone = FakePhone("feed-01")
    n = nav(phone)
    r = shadow.run_shadow(n, FakeDecider(like()), LIB, store, tmp_path)
    store.label(r.row_id, shadow.make_label(store.shadow(r.row_id)[0], "unjudged", None, None, LIB))
    row = store.shadow(r.row_id)[0]
    with pytest.raises(shadow.LabelError):
        shadow.act_label(n, row, r.capture, LIB, "send")
    acted, nxt = shadow.act_label(n, row, r.capture, LIB, "skip")
    assert (acted["decision"], acted["acted"], nxt) == ("unjudged", "skip", "Kim")


def test_relabel_keeps_acted(store, tmp_path):
    phone = FakePhone("feed-01")
    n = nav(phone)
    r = shadow.run_shadow(n, FakeDecider(like()), LIB, store, tmp_path)
    store.label(r.row_id, shadow.make_label(store.shadow(r.row_id)[0], "skip", None, None, LIB))
    acted, _ = shadow.act_label(n, store.shadow(r.row_id)[0], r.capture, LIB, "skip")
    store.label(r.row_id, acted)
    lab = shadow.make_label(store.shadow(r.row_id)[0], "skip", None, None, LIB, note="added later")
    assert (lab["acted"], lab["note"]) == ("skip", "added later")
    assert "acted" not in shadow.make_label(row(store, like()), "skip", None, None, LIB)


def test_report_lists_soft_flags_per_profile(store):
    bot = like()
    bot.soft_flags, bot.soft_plus = ['late (prompt 1: "always late")'], ['hikes (prompt 2: "hiking")']
    bot.dropped_flags = ["absence: tattoos? none stated"]
    r = row(store, bot)
    store.label(r.id, shadow.make_label(r, "skip", None, None, LIB))
    row(store, like())                                                  # no flags: no line
    out = shadow.report(store.shadow(), [])
    assert "  soft flags per profile: 1" in out
    assert (f'    #{r.id} bot like, Dev skip: - late (prompt 1: "always late"); + hikes (prompt 2: "hiking"); '
            "dropped absence: tattoos? none stated") in out


def test_report_photo_check(store):
    from buddy_screener.decide import physical_skip

    def add(bot, check, *dev, **kw):
        bot.photo_check = check
        r = row(store, bot)
        store.label(r.id, shadow.make_label(r, *dev, LIB, **kw))
        return r.id

    add(physical_skip("photo 2"), "yes", "skip", None, None, dealbreaker=True)   # hit
    add(physical_skip(""), "yes", "like", "photo 1", "none")                     # Dev disagreed
    miss = add(like(), "cant_tell", "skip", None, None, dealbreaker=True, note="overweight")
    add(like(), "no", "like", None, None)
    out = shadow.report(store.shadow(), [("photo", "m", 0, 0, 0, 0, 0.01, 0, None, None)] * 4)
    assert ("  overweight (photo check): yes 2, no 1, can't tell 1 of 4; Dev liked 1 of the yes; Dev's dealbreakers "
            "without a yes: 1 (misses if the note says weight)") in out
    assert f"    #{miss}: overweight" in out and "  photo check: 4 calls, $0.010 per profile" in out


def test_own_comments_label_and_type(store):
    bot = like(comment="own", text="wo isch das, gopfried?")
    r = row(store, bot)
    lab = shadow.make_label(r, "like", None, None, LIB)                 # bare like: the bot's own text
    assert (lab["comment"], lab["text"]) == ("own", "wo isch das, gopfried?")
    lab = shadow.make_label(r, "like", "prompt 1", "own", LIB, text="mutig")
    assert (lab["comment_on"], lab["comment"], lab["text"]) == ("prompt 1", "own", "mutig")
    with pytest.raises(shadow.LabelError, match="needs its text"):
        shadow.make_label(r, "like", "prompt 1", "own", LIB)


def test_report_what_dev_did_with_the_bots_comment(store):
    def add(on, bot_text, *dev, **kw):
        r = row(store, like(on, "own", text=bot_text))
        store.label(r.id, shadow.make_label(r, "like", *dev, LIB, **kw))

    add("photo 1", "wo isch dä see?", None, None)                                            # sent
    add("photo 1", "wo isch dä see?", "photo 1", "own", text="wo isch dä see genau?")       # edited
    add("prompt 1", "bold, i'm in", "prompt 1", "hot_take")                                   # own (Dev's template)
    add("prompt 1", "bold, i'm in", "prompt 2", "hot_take")                                   # other section
    bot = like("prompt 1", "none")
    bot.comment_refused = ["copies hot_take", "invents me too"]
    r = row(store, bot)
    store.label(r.id, shadow.make_label(r, "skip", None, None, LIB))
    out = shadow.report(store.shadow(), [])
    assert "  Dev sent the bot's comment: 1/4 = 25% [NOT YET: ≥ 80%]" in out
    assert "    on a photo: sent 1, edited 1" in out and "    on a prompt: own 1, other section 1" in out
    assert "  comments refused by the checks: 2 (copies 1, invents 1)" in out


def test_an_option_label_is_normalised():
    r = row(store=Store(":memory:"), bot=like())
    lab = shadow.make_label(r, "like", "Option-2", "own", LIB, ["photo 1", "poll 1", "option 2"], text="the hut")
    assert (lab["comment_on"], lab["text"]) == ("option 2", "the hut")


def test_a_capture_without_details_is_never_judged(store, tmp_path):
    # #165: a capture that ended early (no details card) went to the bot anyway
    decider = FakeDecider(like())
    walk = [f"feed-friend-{i}" for i in range(1, 6)]   # a profile's top only: no details card
    with pytest.raises(NeedsYou, match="no details"):
        shadow.run_shadow(nav(FakePhone("feed-friend-1", walk=walk)), decider, LIB, store, tmp_path)
    assert decider.seen is None   # the bot never saw it, no row was stored
