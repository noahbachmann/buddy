"""ssh/rsync to the Pi (BUDDY_HOST, default "buddy" from ~/.ssh/config, like deploy/*.sh)."""

import os
import shlex
import subprocess

HOST = os.environ.get("BUDDY_HOST", "buddy")

# buddy.env in the environment, like the services see it (values unquoted, systemd style; "\r" from Windows dropped)
LOAD_ENV = (
    'while IFS="=" read -r k v; do [[ "$k" =~ ^BUDDY_[A-Z0-9_]+$ ]] && export "$k=${v%$\'\\r\'}"; '
    "done < ~/buddy/config/buddy.env"
)


def run(command: str, check: bool = True, capture: bool = True) -> str:
    result = subprocess.run(["ssh", HOST, f"bash -c {shlex.quote(command)}"], capture_output=capture, text=True)
    if check and result.returncode != 0:
        raise RuntimeError(f"ssh {HOST}: {command}\n{result.stderr}")
    return result.stdout if capture else ""


def rsync(src: str, dst: str, *extra: str) -> None:
    subprocess.run(["rsync", "-a", *extra, src, dst], check=True)


def brain(texts: list[str]) -> dict[str, tuple[str, str, str]]:
    """text → (intent, query, device) from the deployed buddy-brain (--test, no MQTT)."""
    import json  # noqa: PLC0415

    if not texts:
        return {}
    out = run("~/buddy/bin/buddy-brain --test " + " ".join(shlex.quote(t) for t in texts))
    result = {}
    for line in out.splitlines():
        text, _, event = line.rpartition(" → ")
        if text.startswith('"') and text.endswith('"'):
            j = json.loads(event)
            result[text[1:-1]] = (j["intent"], j.get("query", ""), j.get("device", ""))
    return result
