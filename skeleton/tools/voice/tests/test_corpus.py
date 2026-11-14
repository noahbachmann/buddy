from buddy_voice import corpus


def test_corpus_is_valid():
    commands = corpus.load()
    sets = {c.set for c in commands}
    assert sets == {"train", "test", "wake", "extra"}
    sentences = [c.sentence for c in commands]
    assert len(sentences) == len(set(sentences)), "duplicate sentence in commands.tsv"
    assert all(c.intent for c in commands)


def test_test_sentences_are_not_training_sentences():
    commands = corpus.load()
    train = {c.sentence.lower() for c in commands if c.set == "train"}
    assert not [c.sentence for c in commands if c.set == "test" and c.sentence.lower() in train]


def test_devices_only_on_playback_and_lights():
    """Speakers only on commands that start playback; rooms and lamps on every light command."""
    commands = corpus.load()
    assert any(c.device for c in commands)
    with_device = {"play", "play_playlist", "play_liked", "on", "off", "brightness", "brighter", "darker", "color",
                   "scene", "status"}
    assert not [c.sentence for c in commands if c.device and c.intent not in with_device]


def test_every_test_word_is_trained_5_times():
    """whisper learns a word from Dev's clips only if train has it often enough: eval 2026-10-28 missed exactly the
    words and names that were rare in train (CLAUDE.md → Deferred)."""
    from collections import Counter

    from buddy_voice.score import norm

    commands = corpus.load()
    trained = Counter(w for c in commands if c.set == "train" for w in norm(c.sentence).split())
    tested = {w for c in commands if c.set == "test" for w in norm(c.sentence).split()}
    assert not sorted(f"{w} ({trained[w]}×)" for w in tested if trained[w] < 5)
