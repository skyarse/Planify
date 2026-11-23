#!/usr/bin/env bash
set -euo pipefail

version_pattern='^v[0-9]+\.[0-9]+\.[0-9]+$'
release_tag=$(git tag --points-at "$GITHUB_SHA" | awk -v re="$version_pattern" '$0 ~ re {print; exit}')
if [[ -n "$release_tag" ]] && gh release view "$release_tag" --repo "$GITHUB_REPOSITORY" >/dev/null 2>&1; then
  exit 0
fi
if [[ -z "$release_tag" ]]; then
  previous_tag=$(git tag --merged "$GITHUB_SHA" --sort=-version:refname | awk -v re="$version_pattern" '$0 ~ re && !found++')
  release_tag=v0.1.0
  if [[ -n "$previous_tag" ]]; then
    IFS=. read -r major minor patch <<< "${previous_tag#v}"
    release_tag="v$major.$minor.$((patch + 1))"
  fi
fi

asset="plan-tasks-${release_tag#v}-x86_64.flatpak"
mv plan-tasks-x86_64.flatpak "$asset"
sha256sum "$asset" > SHA256SUMS
if [[ "$release_tag" == v0.1.0 ]]; then
  cat > release-notes.md <<'EOF'
### Added

- Local projects, sections, tasks and subtasks with list and board views.
- Search, labels, priorities, due dates, pinned tasks and basic recurrence.
- Read-only CalDAV import from Baïkal into a local copy for offline viewing.
- A Linux x86-64 Flatpak using KDE Platform 6.11.

EOF
else
  printf 'See the changes associated with this release.\n\n' > release-notes.md
fi
printf 'Built from commit `%s`.\n\nSHA-256 checksums are in `SHA256SUMS`.\n' "$GITHUB_SHA" >> release-notes.md
gh release create "$release_tag" "$asset" SHA256SUMS --repo "$GITHUB_REPOSITORY" \
  --target "$GITHUB_SHA" --title "Plan Tasks $release_tag" --notes-file release-notes.md
