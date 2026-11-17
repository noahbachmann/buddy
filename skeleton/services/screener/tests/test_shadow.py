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


def like(on="photo 1", comment="photo_place", missing=""):
    return Browse("like", on, comment, missing, ["de", "en", "de"], [], [], "Likes hiking.")


def nav(phone):
    return Navigator(phone, PACKAGE, pause=lambda s: None, log=lambda s: None)


@pytest.fixture
def store():
    return Store(":memory:")


def test_shadow_label_type_sends_nothing(store, tmp_path):
    phone = FakePhone("feed-01")
    n = nav(phone)
    r = shadow.run_shadow(n, FakeDecider(like()), LIB, store, tmp_path)
    assert len(r.capture.profile.sections) == 9 and phone.screen == "feed-11"
    lines = shadow.describe(r, LIB)
    assert "LIKE" in lines[0] and any("Wo ist das?" in x for x in lines)

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


def test_type_needs_a_liked_comment(store):
    r = row(store, like())
    r.dev = shadow.make_label(r, "like", "photo 1", "none", LIB)
    with pytest.raises(shadow.LabelError, match="nothing to type"):
        shadow.type_label(None, r, None, LIB)


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
    assert "section + comment as Dev chose: 1/2 = 50%" in out
    assert "missing: a pet photo" in out


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
