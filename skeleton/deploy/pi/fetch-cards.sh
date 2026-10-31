#!/usr/bin/env bash
# Download the Riftbound card data (Riftcodex API, no key) into ~/buddy/data/riftbound/cards.json for game (Phase 5,
# D21). Runs ON the Pi; `setup.sh cards` calls it. Not in Git: Riot's card text, and new sets or errata only need a
# re-run. Always fetches fresh; the old file stays until the new one is complete.
#
# Pruned to what the engine reads: one entry per printing (Riftcodex lists 147 printings twice: VEN as an old and a
# new import, OPP regular and Metal under one id; the newest entry wins), no images, flavour text or shop ids.
# Grouping printings into cards happens in game's CardDb (tested), not here.
#
# Usage: fetch-cards.sh
# Env (or buddy.env): BUDDY_GAME_CARDS (default ~/buddy/data/riftbound/cards.json)
set -euo pipefail

if [[ -f ~/buddy/config/buddy.env ]]; then
    while IFS='=' read -r key value; do
        [[ "$key" =~ ^BUDDY_[A-Z0-9_]+$ ]] && export "$key=${value%"${value##*[![:space:]]}"}"
    done < ~/buddy/config/buddy.env
fi
OUT="${BUDDY_GAME_CARDS:-$HOME/buddy/data/riftbound/cards.json}"
API=https://api.riftcodex.com
mkdir -p "$(dirname "$OUT")"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

get() {  # path file
    curl -fsS --retry 3 -o "$tmp/$2" "$API$1"
}

get /sets sets.json
get "/cards?size=100&page=1" page-1.json
pages="$(python3 -c 'import json, sys; print(json.load(open(sys.argv[1]))["pages"])' "$tmp/page-1.json")"
for ((p = 2; p <= pages; p++)); do get "/cards?size=100&page=$p" "page-$p.json"; done

python3 -I - "$tmp" "$OUT.part" <<'PY'
import datetime, glob, json, os, sys

src, out = sys.argv[1], sys.argv[2]
pages = [json.load(open(f)) for f in sorted(glob.glob(os.path.join(src, "page-*.json")))]
items = [c for p in pages for c in p["items"]]
if len(items) != pages[0]["total"]:
    sys.exit(f"got {len(items)} printings, the API says {pages[0]['total']}: run again")

newest = {}
for c in items:
    rid = c["riftbound_id"]
    if rid not in newest or c["metadata"]["updated_on"] > newest[rid]["metadata"]["updated_on"]:
        newest[rid] = c

def printing(c):
    cl, at = c["classification"], c["attributes"]
    return {
        "id": c["riftbound_id"], "name": c["name"], "set": c["set"]["set_id"],
        "type": cl["type"], "supertype": cl["supertype"], "domains": cl["domain"],
        "energy": at["energy"], "might": at["might"], "power": at["power"],
        "tags": c["tags"], "text": c["text"]["plain"] or "",
    }

sets = json.load(open(os.path.join(src, "sets.json")))
sets = sets["items"] if isinstance(sets, dict) else sets
sets = sorted(({"id": s["set_id"], "name": s["name"], "published": s["published_on"][:10]} for s in sets),
              key=lambda s: (s["published"], s["id"]))
fetched = datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
line = lambda o: json.dumps(o, ensure_ascii=False)  # one set / printing per line: greppable
with open(out, "w") as f:
    f.write(f'{{"source": "api.riftcodex.com", "fetched": "{fetched}",\n"sets": [\n')
    f.write(",\n".join(line(s) for s in sets))
    f.write('\n],\n"printings": [\n')
    f.write(",\n".join(line(printing(newest[k])) for k in sorted(newest)))
    f.write("\n]}\n")
print(f"  ✓ {len(newest)} printings ({len(items)} entries), {len(sets)} sets")
PY
mv "$OUT.part" "$OUT"
echo "  ✓ $OUT"
