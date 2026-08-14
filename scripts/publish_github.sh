#!/usr/bin/env bash

set -Eeuo pipefail

readonly DEFAULT_REMOTE_URL="https://github.com/DobiShredder/MolShredder.git"
readonly DEFAULT_BRANCH="master"
readonly LOCAL_CODEX_ROOT="${HOME}/codex"
readonly GDRIVE_CODEX_ROOT="${HOME}/gdrive/codex"
readonly GDRIVE_MARKER_FILENAME=".codex-gdrive-mirror-commit"
readonly MAX_GITHUB_BLOB_BYTES=100000000

today="$(date +"%Y%m%d")"
remote_url="${MOLSHREDDER_REMOTE_URL:-$DEFAULT_REMOTE_URL}"
target_branch="${MOLSHREDDER_BRANCH:-$DEFAULT_BRANCH}"
commit_message="$today"
gdrive_mirror="${MOLSHREDDER_GDRIVE_MIRROR:-}"
assume_yes=false
sync_gdrive=true
temporary_root=""

usage() {
  cat <<'EOF'
Usage: ./scripts/publish_github.sh [OPTIONS]

Commit and push MolShredder to the master branch without force-pushing.
Committed files are mirrored to Google Drive by default.

Options:
  -m, --message TEXT   Commit message (default: today's YYYYMMDD date)
  -y, --yes            Skip final confirmation
      --gdrive-sync    Enable Google Drive synchronization (default)
      --no-gdrive-sync Skip Google Drive synchronization
  -h, --help           Show this help

Environment:
  MOLSHREDDER_REMOTE_URL     Override the Git remote URL
  MOLSHREDDER_BRANCH         Override branch; only master is accepted
  MOLSHREDDER_GDRIVE_MIRROR  Override the derived Google Drive destination

Configure authentication outside this script. Never place a token in the URL.
EOF
}

die() {
  printf 'error: %s\n' "$*" >&2
  exit 1
}

cleanup() {
  if [[ -n "$temporary_root" && -d "$temporary_root" ]]; then
    rm -rf -- "$temporary_root"
  fi
}

while (($# > 0)); do
  case "$1" in
    -m | --message)
      (($# >= 2)) || die "$1 requires a value"
      commit_message="$2"
      shift 2
      ;;
    -y | --yes)
      assume_yes=true
      shift
      ;;
    --gdrive-sync)
      sync_gdrive=true
      shift
      ;;
    --no-gdrive-sync)
      sync_gdrive=false
      shift
      ;;
    -h | --help)
      usage
      exit 0
      ;;
    *)
      die "unknown option: $1"
      ;;
  esac
done

[[ -n "$commit_message" ]] || die "commit message must not be empty"
[[ "$target_branch" == "$DEFAULT_BRANCH" ]] || die \
  "only the master branch is supported"
if [[ "$remote_url" =~ ^https?://[^/]*@ ]]; then
  die "remote URL must not contain embedded credentials"
fi

script_directory="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
repository_root="$(cd -- "$script_directory/.." && pwd -P)"
cd "$repository_root"

[[ "$repository_root" == "$LOCAL_CODEX_ROOT"/*/project ]] || die \
  "public repository root must be ~/codex/<work_home>/project"

if [[ -z "$gdrive_mirror" ]]; then
  work_home="$(dirname -- "$repository_root")"
  work_home_relative="${work_home#"$LOCAL_CODEX_ROOT"/}"
  [[ -n "$work_home_relative" && "$work_home_relative" != "$work_home" ]] || die \
    "cannot derive work_home relative to $LOCAL_CODEX_ROOT"
  [[ -d "${HOME}/gdrive" ]] || die \
    "${HOME}/gdrive is unavailable; refusing to create a non-synced local mirror"
  gdrive_mirror="$GDRIVE_CODEX_ROOT/$work_home_relative/project"
fi

[[ "$gdrive_mirror" == /* && "$gdrive_mirror" != "/" ]] || die \
  "Google Drive mirror must be a safe absolute path"
[[ "$gdrive_mirror" != "$repository_root" ]] || die \
  "Google Drive mirror must differ from the repository"

command -v git >/dev/null 2>&1 || die "git is not installed"
command -v rsync >/dev/null 2>&1 || die "rsync is not installed"
command -v tar >/dev/null 2>&1 || die "tar is not installed"

git_user_name="$(git config --get user.name || true)"
git_user_email="$(git config --get user.email || true)"
[[ -n "$git_user_name" ]] || die "git user.name is unset"
[[ -n "$git_user_email" ]] || die "git user.email is unset"

if command -v gh >/dev/null 2>&1; then
  gh auth status --hostname github.com >/dev/null 2>&1 || die \
    "GitHub CLI is not authenticated"
fi

printf 'Checking remote access...\n'
git ls-remote "$remote_url" HEAD >/dev/null || die \
  "cannot access $remote_url"
remote_heads="$(git ls-remote --heads "$remote_url")"

if [[ ! -d .git ]]; then
  git init -b "$target_branch"
fi

current_branch="$(git symbolic-ref --quiet --short HEAD || true)"
[[ "$current_branch" == "$target_branch" ]] || die \
  "current branch is '$current_branch', expected '$target_branch'"

if git remote get-url origin >/dev/null 2>&1; then
  existing_origin="$(git remote get-url origin)"
  [[ "$existing_origin" == "$remote_url" ]] || die \
    "origin is $existing_origin; refusing to replace it"
else
  git remote add origin "$remote_url"
fi

remote_branch_count=0
while IFS= read -r remote_branch; do
  [[ -n "$remote_branch" ]] || continue
  ((remote_branch_count += 1))
  [[ "$remote_branch" == "$target_branch" ]] || die \
    "unexpected remote branch '$remote_branch'; only master is allowed"
done < <(printf '%s\n' "$remote_heads" | awk -F'refs/heads/' 'NF == 2 {print $2}')

if ! git rev-parse --verify HEAD >/dev/null 2>&1 && ((remote_branch_count == 1)); then
  git fetch --prune origin "$target_branch"
  git reset --mixed "origin/$target_branch"
fi

git add --all

while IFS= read -r -d '' path; do
  [[ -e "$path" ]] || continue
  size="$(wc -c <"$path")"
  ((size < MAX_GITHUB_BLOB_BYTES)) || die \
    "$path is $size bytes; GitHub rejects blobs of 100 MB or more"
  case "$path" in
    .env | */.env | .env.* | */.env.* | *.pem | *.p12 | *.pfx | *.key | \
      id_rsa | */id_rsa | id_ed25519 | */id_ed25519)
      die "sensitive-looking file is staged: $path"
      ;;
  esac
done < <(git diff --cached --name-only --diff-filter=ACM -z)

if ! git diff --cached --quiet; then
  printf '\nStaged change summary:\n'
  git --no-pager diff --cached --stat
  if [[ "$assume_yes" != true ]]; then
    printf '\nCommit and push to %s on master? [y/N] ' "$remote_url"
    read -r response
    [[ "$response" == "y" || "$response" == "Y" ]] || die "cancelled"
  fi
  git commit -m "$commit_message"
elif ! git rev-parse --verify HEAD >/dev/null 2>&1; then
  die "there are no files to commit"
else
  printf 'No staged changes; pushing existing HEAD.\n'
fi

printf 'Pushing without force...\n'
git push --set-upstream origin "$target_branch"

if [[ "$sync_gdrive" != true ]]; then
  printf 'Google Drive synchronization skipped.\n'
  exit 0
fi

head_commit="$(git rev-parse HEAD)"
marker_path="$gdrive_mirror/$GDRIVE_MARKER_FILENAME"
previous_commit=""
if [[ -e "$marker_path" ]]; then
  [[ -f "$marker_path" && ! -L "$marker_path" ]] || die \
    "invalid mirror marker: $marker_path"
  IFS= read -r previous_commit <"$marker_path" || true
  [[ "$previous_commit" =~ ^[0-9a-f]{40}$ ]] || die \
    "mirror marker has an invalid commit"
  git cat-file -e "$previous_commit^{commit}" 2>/dev/null || die \
    "recorded mirror commit is unavailable locally"
  git merge-base --is-ancestor "$previous_commit" "$head_commit" || die \
    "recorded mirror commit is not an ancestor of HEAD"
fi

temporary_root="$(mktemp -d "${TMPDIR:-/tmp}/molshredder-gdrive.XXXXXX")"
trap cleanup EXIT
head_snapshot="$temporary_root/head"
previous_snapshot="$temporary_root/previous"
mkdir -p -- "$head_snapshot" "$gdrive_mirror"
git archive --format=tar "$head_commit" | tar -xf - -C "$head_snapshot"

if [[ -n "$previous_commit" ]]; then
  mkdir -p -- "$previous_snapshot"
  git archive --format=tar "$previous_commit" | tar -xf - -C "$previous_snapshot"
  drift="$(rsync -rcln --delete --itemize-changes \
    --exclude "$GDRIVE_MARKER_FILENAME" "$previous_snapshot/" "$gdrive_mirror/")"
  [[ -z "$drift" ]] || die \
    "Google Drive mirror differs from recorded commit; refusing to overwrite it"
else
  existing="$(find "$gdrive_mirror" -mindepth 1 \
    ! -name "$GDRIVE_MARKER_FILENAME" -print -quit)"
  [[ -z "$existing" ]] || die \
    "initial Google Drive mirror is not empty"
fi

rsync -a --delete --exclude "$GDRIVE_MARKER_FILENAME" \
  "$head_snapshot/" "$gdrive_mirror/"
marker_temporary="$gdrive_mirror/${GDRIVE_MARKER_FILENAME}.tmp.$$"
printf '%s\n' "$head_commit" >"$marker_temporary"
mv -f -- "$marker_temporary" "$marker_path"

printf 'Upload and Google Drive mirror complete.\n'
printf '  repository: %s\n' "$remote_url"
printf '  mirror:     %s\n' "$gdrive_mirror"

cleanup
temporary_root=""
trap - EXIT
