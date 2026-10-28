#!/usr/bin/env bash
# Move litesvm-cxx onto a new litesvm release
#
#   scripts/bump.sh 0.18.2
#
# litesvm declares its Agave deps with caret ranges, so ensure we use a proper agave version in the
# case a new minor is released
set -euo pipefail

ver="${1:?usage: bump.sh <litesvm version>}"
root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root"

lock_url="https://raw.githubusercontent.com/LiteSVM/litesvm/v${ver}/Cargo.lock"

curl -fsSL "$lock_url" -o Cargo.lock.upstream ||
    { echo "bump: no Cargo.lock at LiteSVM tag v${ver}" >&2; exit 1; }
mv Cargo.lock.upstream Cargo.lock

sed -E -i.bak "s/^(litesvm = \{ version = \")=[^\"]+/\1=${ver}/" Cargo.toml
sed -E -i.bak "s/^(#define LITESVM_UPSTREAM_VERSION )\"[^\"]+\"/\1\"${ver}\"/" \
    include/litesvm.h
rm -f Cargo.toml.bak include/litesvm.h.bak

cargo metadata --format-version 1 >/dev/null

msrv="$(cargo metadata --format-version 1 |
    python3 -c 'import json,sys
pk=[p for p in json.load(sys.stdin)["packages"] if p["name"]=="litesvm"]
print(pk[0].get("rust_version") or "")')"
echo "bump: litesvm ${ver} (declares rust-version ${msrv:-unset})"
echo "bump: build with 'make lib test'; if Agave crates need a newer rustc,"
echo "      raise rust-toolchain.toml and rust-version in Cargo.toml together"
