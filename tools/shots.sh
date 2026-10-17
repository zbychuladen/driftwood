#!/usr/bin/env bash
# Captures every scene in tools/scenes.txt into .shots/<label>/ and rebuilds
# .shots/index.html, a contact sheet with one column per label.
#
#   tools/shots.sh before          # on the old code
#   tools/shots.sh after           # after your change, then open .shots/index.html
#
# Needs a built game (cmake --build build). On a Linux box without a display
# it runs under xvfb-run.
set -euo pipefail
cd "$(dirname "$0")/.."
label="${1:?usage: tools/shots.sh <label> [scene-name...]}"
shift
bin="${DRIFTWOOD_BIN:-build/driftwood}"
[ -x "$bin" ] || { echo "no game binary at $bin, build first" >&2; exit 1; }
run=()
if [ "$(uname)" = Linux ] && [ -z "${DISPLAY:-}" ]; then run=(xvfb-run -a -s "-screen 0 1600x900x24"); fi

mkdir -p ".shots/$label"
while read -r name flags; do
  case "$name" in ''|'#'*) continue ;; esac
  if [ $# -gt 0 ] && [[ " $* " != *" $name "* ]]; then continue; fi
  # shellcheck disable=SC2086
  ${run[@]+"${run[@]}"} "$bin" --screenshot "$PWD/.shots/$label/$name.png" $flags >/dev/null 2>&1 \
    && echo "  $label/$name.png" || echo "  FAILED $name" >&2
done < tools/scenes.txt

# contact sheet: a row per scene, a column per label (oldest first)
labels=$(ls -tr .shots | grep -v '\.html$' || true)
{
  echo '<!doctype html><meta charset="utf-8"><title>Driftwood shots</title>'
  echo '<style>body{font:14px system-ui;margin:16px;background:#f4f2ea;color:#2c3a2e}'
  echo 'table{border-collapse:collapse}td,th{padding:6px;vertical-align:top;text-align:left}'
  echo 'img{width:640px;display:block;border:1px solid #c9c4b4}a{color:inherit}</style>'
  echo '<h1>Driftwood shots</h1><table><tr><th></th>'
  for l in $labels; do echo "<th>$l</th>"; done
  echo '</tr>'
  while read -r name flags; do
    case "$name" in ''|'#'*) continue ;; esac
    echo "<tr><th>$name<br><small><code>$flags</code></small></th>"
    for l in $labels; do
      if [ -f ".shots/$l/$name.png" ]; then echo "<td><a href=\"$l/$name.png\"><img src=\"$l/$name.png\" loading=\"lazy\"></a></td>"
      else echo '<td></td>'; fi
    done
    echo '</tr>'
  done < tools/scenes.txt
  echo '</table>'
} > .shots/index.html
echo "contact sheet: .shots/index.html"
