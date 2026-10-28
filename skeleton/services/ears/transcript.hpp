#pragma once
// Guards against whisper hallucinations on short commands, without whisper itself (ears_core, tested on the VM).
// Seen in the eval of 2026-10-28: a model run with a short encoder window repeated "Turn it down a little." 40 times.

#include <cstddef>
#include <string>

namespace buddy::ears {

// Upper bound for whisper's output tokens: fast speech is ~5 tokens/s, so 10/s + 10 never cuts a real command but
// stops a repetition loop early (and bounds the decode time). `samples` at 16 kHz.
inline int max_tokens_for(std::size_t samples) {
    return 10 + static_cast<int>(samples / 1600);
}

// "Turn it down a little.Turn it down a little.Turn it do" → "Turn it down a little.", also without punctuation.
// Only when the whole text is one phrase repeated; anything else unchanged.
std::string drop_repeats(const std::string& text);

} // namespace buddy::ears
