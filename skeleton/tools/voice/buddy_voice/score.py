"""Text normalisation and word error rate, for comparing what whisper heard with what was said."""

import re

_UNITS = {w: i for i, w in enumerate(
    "zero one two three four five six seven eight nine ten eleven twelve thirteen fourteen fifteen sixteen "
    "seventeen eighteen nineteen".split())}
_TENS = {w: 10 * i for i, w in enumerate("_ _ twenty thirty forty fifty sixty seventy eighty ninety".split()) if i > 1}


def _numbers(words: list[str]) -> list[str]:
    """"fifty" → "50", "twenty five" → "25", "a hundred" → "100". "one"/"two" stay words ("skip this one")."""
    out, i = [], 0
    while i < len(words):
        w = words[i]
        if w in _TENS:
            n = _TENS[w]
            if i + 1 < len(words) and _UNITS.get(words[i + 1], 99) < 10:
                n += _UNITS[words[i + 1]]
                i += 1
            out.append(str(n))
        elif w == "hundred" or (w == "a" and i + 1 < len(words) and words[i + 1] == "hundred"):
            i += w == "a"
            out.append("100")
        elif _UNITS.get(w, 0) >= 10:
            out.append(str(_UNITS[w]))
        else:
            out.append(w)
        i += 1
    return out


def norm(text: str) -> str:
    """Lowercase words and digits only; "50%" == "fifty percent", "Mm-hmm" == "mm hmm"."""
    text = text.lower().replace("%", " percent ").replace("'", "")
    return " ".join(_numbers(re.sub(r"[^a-z0-9]+", " ", text).split()))


def word_errors(said: str, heard: str) -> tuple[int, int]:
    """(edits, words said): Levenshtein distance over normalised words."""
    r, h = norm(said).split(), norm(heard).split()
    row = list(range(len(h) + 1))
    for i in range(1, len(r) + 1):
        diag, row[0] = row[0], i
        for j in range(1, len(h) + 1):
            diag, row[j] = row[j], min(row[j] + 1, row[j - 1] + 1, diag + (r[i - 1] != h[j - 1]))
    return row[len(h)], len(r)


def live(pairs: list[tuple[str, str, str]], intents: dict[str, tuple[str, str, str]]) -> dict:
    """Live score of labelled commands: (clip, heard, said) + brain's (intent, query, device) per text.

    intent  brain does the same with what whisper heard as with what was said, over the commands brain understands
    gaps    what was said but brain doesn't understand: brain's job, not whisper's
    """
    unknown = ("unknown", "", "")
    rows = []
    for clip, heard, said in pairs:
        want = intents.get(said, unknown)
        errs, words = word_errors(said, heard)
        rows.append({"clip": clip, "said": said, "heard": heard, "exact": norm(heard) == norm(said),
                     "errors": errs, "words": words, "understood": want[0] != "unknown",
                     "intent_ok": intents.get(heard, unknown) == want})
    understood = [r for r in rows if r["understood"]]
    return {
        "clips": len(rows),
        "exact": sum(r["exact"] for r in rows),
        "intent": sum(r["intent_ok"] for r in understood),
        "understood": len(understood),
        "wer": sum(r["errors"] for r in rows) / max(1, sum(r["words"] for r in rows)),
        "gaps": sorted({r["said"] for r in rows if not r["understood"]}),
        "rows": rows,
    }
