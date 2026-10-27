from buddy_voice import corpus


def test_corpus_is_valid():
    commands = corpus.load()
    sets = {c.set for c in commands}
    assert sets == {"train", "test", "wake"}
    sentences = [c.sentence for c in commands]
    assert len(sentences) == len(set(sentences)), "duplicate sentence in commands.tsv"
    assert all(c.intent for c in commands)


def test_test_sentences_are_not_training_sentences():
    commands = corpus.load()
    train = {c.sentence.lower() for c in commands if c.set == "train"}
    assert not [c.sentence for c in commands if c.set == "test" and c.sentence.lower() in train]


def test_speakers_only_on_commands_that_start_playback():
    commands = corpus.load()
    assert any(c.device for c in commands)
    assert not [c.sentence for c in commands if c.device and c.intent not in ("play", "play_playlist", "play_liked")]
