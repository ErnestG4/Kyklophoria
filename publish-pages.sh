#!/usr/bin/env bash
# Publish web/ to Codeberg Pages: rebuilds the `pages` branch from the web/
# subtree of HEAD and prints the push. Codeberg serves a repo's `pages`
# branch at https://<user>.codeberg.page/<repo>/ over https — a secure
# context, which Web Serial (desktop Chrome/Edge) requires.
#
# What works on Pages and what does not:
#   Serial   — yes. https is a secure context, so the page talks to the
#              module from any machine, including a phone over OTG.
#   Bridge   — no. Pages is static hosting; nothing there can run
#              `kykdesk --serve`. Developing without the module stays a
#              local job (tools/bridge/bridge.py), until attract mode
#              replays a recorded telemetry log (docs/spec.md §8).
set -euo pipefail
cd "$(git rev-parse --show-toplevel)"
git branch -D pages 2>/dev/null || true
git subtree split --prefix=web -b pages >/dev/null 2>&1
# Stamp the published copy with the commit it came from: the footer's "page"
# field, and link.js loaded as link.js?v=<commit> so a browser holding an old
# parser in its cache cannot pair it with a new page. A page that "never
# started working" was indistinguishable from a page that never arrived
# (Combust, 2026-09-23); now the footer says which one you have.
stamp=$(git rev-parse --short HEAD)
wt=$(mktemp -d)
git worktree add -q "$wt" pages
sed -i "s|<!--page-build--><b>local</b>|<!--page-build--><b>$stamp</b>|; s|<script src=\"link.js\"></script>|<script src=\"link.js?v=$stamp\"></script>|" "$wt/index.html"
grep -q "<b>$stamp</b>" "$wt/index.html" && grep -q "link.js?v=$stamp" "$wt/index.html" || { echo "stamp failed"; git worktree remove --force "$wt"; exit 1; }
git -C "$wt" commit -q -am "Page build $stamp"
git worktree remove "$wt"
echo "pages branch rebuilt at $(git rev-parse --short pages) from $stamp (stamped)"
echo "now: git push -f origin pages    → https://combust.codeberg.page/Kyklophoria/"
