"""short_context() replaces WhisperEncoder.forward of the pinned transformers: a bump that changes the encoder
breaks the equality with the stock forward here instead of silently training a different model."""

import pytest

torch = pytest.importorskip("torch")  # only with --extra cpu / cuda

from transformers import WhisperConfig  # noqa: E402
from transformers.models.whisper.modeling_whisper import WhisperEncoder  # noqa: E402

from buddy_voice.train import short_context  # noqa: E402


@pytest.fixture
def encoder():
    torch.manual_seed(0)
    cfg = WhisperConfig(d_model=16, encoder_layers=2, encoder_attention_heads=2, encoder_ffn_dim=32, num_mel_bins=80)
    return WhisperEncoder(cfg).eval()  # random weights, no download


def test_full_length_matches_stock_forward(encoder):
    feats = torch.randn(1, 80, 3000)
    with torch.no_grad():
        stock = encoder(feats).last_hidden_state
        short_context(encoder)
        patched = encoder(feats).last_hidden_state
    torch.testing.assert_close(patched, stock)


def test_short_input(encoder):
    short_context(encoder)
    with torch.no_grad():
        out = encoder(torch.randn(2, 80, 200)).last_hidden_state
    assert out.shape == (2, 100, 16)  # conv2 halves the frames
