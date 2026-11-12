#!/usr/bin/env bash
# Builds the macOS release assets from a built CMake/Xcode tree (preset macos-ci) and verifies them
# after packaging:
#   obs-multistream-stats-<v>-macos-arm64.dmg          "Install Multi-Stream Stats.pkg" + Read Me + LICENSE
#   obs-multistream-stats-<v>-macos-arm64-manual.zip   obs-multistream-stats.plugin + INSTALL.txt + LICENSE.txt
#   obs-multistream-stats-<v>-macos-arm64-dSYMs.zip    debug symbols (not for users)
#   obs-multistream-stats-<v>-macos-arm64.pkg          the same installer, for CI tests (not a release asset)
#   SHA256SUMS-macos.txt, macos-dependency-audit.txt, signing-status.txt
#
# Signing is optional and only happens when real credentials are present:
#   CODESIGN_IDENT            "Developer ID Application: ..."   (bundle, DMG)
#   CODESIGN_IDENT_INSTALLER  "Developer ID Installer: ..."     (PKG)
#   NOTARY_KEYCHAIN_PROFILE   notarytool keychain profile (set up by the release workflow)
# Without them the bundle keeps Xcode's ad-hoc signature and the PKG/DMG are unsigned; the
# read-me files then explain the Gatekeeper steps. Nothing claims a signature that is not there.
#
# Usage: package-macos.sh <build dir> <output dir> [config]
set -euo pipefail

build_dir=$(cd "${1:?build dir}" && pwd)
mkdir -p "${2:?output dir}"
out_dir=$(cd "$2" && pwd)
config=${3:-RelWithDebInfo}
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
name=$(jq -r .name "$root/buildspec.json")
version=$(jq -r .version "$root/buildspec.json")
bundle_id=$(jq -r .platformConfig.macos.bundleId "$root/buildspec.json")
website=$(jq -r .website "$root/buildspec.json")
project_url=${PROJECT_URL:-$website}
project_text=${project_url:-the project page you downloaded this from}
min_macos=13.0

bundle="$build_dir/$config/$name.plugin"
dsym="$build_dir/$config/$name.plugin.dSYM"
exe="$bundle/Contents/MacOS/$name"
plist="$bundle/Contents/Info.plist"
[[ -d "$bundle" ]] || { echo "::error::missing $bundle"; exit 1; }

base="$name-$version-macos-arm64"
pkg_label="Install Multi-Stream Stats.pkg"
work=$(mktemp -d)
trap 'hdiutil detach "$work/mnt" -quiet 2>/dev/null || true; rm -rf "$work"' EXIT
rm -f "$out_dir"/*
fail() { echo "::error::$*"; exit 1; }

echo "== bundle checks"
plutil -lint "$plist"
pb() { /usr/libexec/PlistBuddy -c "Print :$1" "$plist"; }
[[ "$(pb CFBundleIdentifier)" == "$bundle_id" ]] || fail "CFBundleIdentifier $(pb CFBundleIdentifier) != $bundle_id"
[[ "$(pb CFBundleShortVersionString)" == "$version" ]] || fail "CFBundleShortVersionString != $version"
[[ "$(pb CFBundleExecutable)" == "$name" ]] || fail "CFBundleExecutable != $name"
[[ "$(pb CFBundlePackageType)" == "BNDL" ]] || fail "CFBundlePackageType != BNDL"
[[ "$(pb LSMinimumSystemVersion)" == "$min_macos" ]] || fail "LSMinimumSystemVersion != $min_macos"
for f in manifest.json locale/en-US.ini; do cmp "$root/data/$f" "$bundle/Contents/Resources/$f"; done
archs=$(lipo -archs "$exe")
[[ "$archs" == "arm64" ]] || fail "expected an Apple Silicon (arm64) only binary, got: $archs"
if [[ "$bundle_id" == com.example.* ]]; then echo "::warning::placeholder bundle identifier $bundle_id (set platformConfig.macos.bundleId before a release)"; fi

{
  echo "# $name $version - macOS dependency audit ($(sw_vers -productVersion), Xcode $(xcodebuild -version | head -1))"
  echo "## architectures: $archs"
  for a in arm64; do
    echo "## $a: minimum OS / SDK"; vtool -arch "$a" -show-build "$exe" | grep -E 'minos|sdk|platform' || true
    echo "## $a: linked libraries"; otool -arch "$a" -L "$exe"
    echo "## $a: LC_RPATH"; otool -arch "$a" -l "$exe" | grep -A2 LC_RPATH | grep path || echo "(none)"
  done
  echo "## code signature"; codesign -dv --verbose=2 "$bundle" 2>&1 || true
} | tee "$out_dir/macos-dependency-audit.txt"
for a in arm64; do
  minos=$(vtool -arch "$a" -show-build "$exe" | awk '/minos/ {print $2}')
  [[ "$minos" == "$min_macos" ]] || fail "$a minos $minos != $min_macos"
  # Every non-system dependency must be @rpath-relative (provided by OBS.app), never a build-machine path.
  bad=$(otool -arch "$a" -L "$exe" | tail -n +2 | awk '{print $1}' | grep -vE '^(@rpath/|/usr/lib/|/System/Library/)' || true)
  [[ -z "$bad" ]] || fail "$a: unexpected library paths: $bad"
  otool -arch "$a" -L "$exe" | grep -q '@rpath/libobs.framework/' || fail "$a: not linked against libobs.framework"
  if otool -arch "$a" -l "$exe" | grep -A2 LC_RPATH | grep -E 'path (/Users|/private|/opt|/var)'; then fail "$a: absolute build rpath"; fi
done

echo "== signing (bundle)"
signed=no
if [[ -n "${CODESIGN_IDENT:-}" && "${CODESIGN_IDENT}" != "-" ]]; then
  codesign --force --options runtime --timestamp --sign "$CODESIGN_IDENT" "$bundle"
  signed=yes
else
  # Ad-hoc signature over the whole bundle (code + resources). Apple Silicon requires at least this;
  # it is not a Developer ID signature and is reported as such.
  codesign --force --sign - --timestamp=none "$bundle"
fi
codesign --verify --strict --verbose=2 "$bundle"

echo "== notes for users"
if [[ "$signed" == yes && -n "${NOTARY_KEYCHAIN_PROFILE:-}" && -n "${CODESIGN_IDENT_INSTALLER:-}" ]]; then
  readme_note=""
  install_note=""
else
  readme_note=$'\nIf macOS says the package "cannot be opened" or "Apple could not verify" it:\nthis build is not signed with an Apple Developer ID. To install it anyway, open\nSystem Settings > Privacy & Security, scroll down to Security, click "Open Anyway"\nnext to the message about the package, and confirm.\n'
  install_note=$'   This build is not signed with an Apple Developer ID. If macOS blocks the plugin\n   or OBS does not load it, remove the download quarantine once, in Terminal:\n     xattr -dr com.apple.quarantine ~/Library/Application\\ Support/obs-studio/plugins/obs-multistream-stats.plugin\n'
fi
fill() { # file -> stdout, replacing placeholders
  local text; text=$(<"$1")
  text=${text//\{VERSION\}/$version}; text=${text//\{MIN_MACOS\}/$min_macos}; text=${text//\{PROJECT_URL\}/$project_text}
  printf '%s\n' "$text"
}

echo "== component package + installer"
pkgroot="$work/pkgroot"
mkdir -p "$pkgroot/Library/Application Support/obs-studio/plugins"
ditto "$bundle" "$pkgroot/Library/Application Support/obs-studio/plugins/$name.plugin"
pkgbuild --analyze --root "$pkgroot" "$work/component.plist"
# Install exactly at Library/Application Support/obs-studio/plugins (never relocate to another copy of
# the bundle); BundleOverwriteAction "upgrade" replaces an older bundle and removes files it no longer has.
plutil -replace 0.BundleIsRelocatable -bool NO "$work/component.plist"
plutil -replace 0.BundleOverwriteAction -string upgrade "$work/component.plist"
mkdir -p "$work/pkgs" "$work/resources"
pkgbuild --root "$pkgroot" --component-plist "$work/component.plist" --identifier "$bundle_id" \
  --version "$version" --install-location / "$work/pkgs/$name.pkg"
fill "$root/packaging/macos/welcome.txt" > "$work/resources/welcome.txt"
fill "$root/packaging/macos/conclusion.txt" > "$work/resources/conclusion.txt"
cp "$root/LICENSE" "$work/resources/LICENSE.txt"
sed -e "s|@VERSION@|$version|g" -e "s|@NAME@|$name|g" -e "s|@BUNDLE_ID@|$bundle_id|g" -e "s|@MIN_MACOS@|$min_macos|g" \
  "$root/packaging/macos/distribution.xml.in" > "$work/distribution.xml"
sign_pkg=()
if [[ -n "${CODESIGN_IDENT_INSTALLER:-}" ]]; then sign_pkg=(--sign "$CODESIGN_IDENT_INSTALLER" --timestamp); fi
productbuild --distribution "$work/distribution.xml" --resources "$work/resources" --package-path "$work/pkgs" \
  ${sign_pkg[@]+"${sign_pkg[@]}"} "$out_dir/$base.pkg"
if [[ -n "${NOTARY_KEYCHAIN_PROFILE:-}" && ${#sign_pkg[@]} -gt 0 ]]; then
  xcrun notarytool submit "$out_dir/$base.pkg" --keychain-profile "$NOTARY_KEYCHAIN_PROFILE" --wait
  xcrun stapler staple "$out_dir/$base.pkg"
fi
pkgutil --check-signature "$out_dir/$base.pkg" | tee "$work/pkg-signature.txt" || true

echo "== DMG"
dmg_src="$work/dmg"
mkdir -p "$dmg_src"
cp "$out_dir/$base.pkg" "$dmg_src/$pkg_label"
fill "$root/packaging/macos/ReadMe-macos.txt" | NOTE="$readme_note" awk '{ if ($0 == "{SIGNING_NOTE}") printf "%s", ENVIRON["NOTE"]; else print }' > "$dmg_src/Read Me First.txt"
cp "$root/LICENSE" "$dmg_src/LICENSE.txt"
hdiutil create -volname "Multi-Stream Stats $version" -srcfolder "$dmg_src" -fs HFS+ -format UDZO -ov "$out_dir/$base.dmg"
if [[ "$signed" == yes ]]; then
  codesign --force --timestamp --sign "$CODESIGN_IDENT" "$out_dir/$base.dmg"
  if [[ -n "${NOTARY_KEYCHAIN_PROFILE:-}" ]]; then
    xcrun notarytool submit "$out_dir/$base.dmg" --keychain-profile "$NOTARY_KEYCHAIN_PROFILE" --wait
    xcrun stapler staple "$out_dir/$base.dmg"
  fi
fi
hdiutil verify "$out_dir/$base.dmg"

echo "== manual ZIP + dSYMs"
zip_src="$work/zip"
mkdir -p "$zip_src"
ditto "$bundle" "$zip_src/$name.plugin"
fill "$root/packaging/macos/INSTALL-macos.txt" | NOTE="$install_note" awk '{ if ($0 == "{SIGNING_NOTE}") printf "%s", ENVIRON["NOTE"]; else print }' > "$zip_src/INSTALL.txt"
cp "$root/LICENSE" "$zip_src/LICENSE.txt"
(cd "$zip_src" && ditto -c -k --norsrc --noextattr --noqtn "$zip_src" "$out_dir/$base-manual.zip")
if [[ -d "$dsym" ]]; then
  ditto -c -k --keepParent --norsrc --noextattr "$dsym" "$out_dir/$base-dSYMs.zip"
else
  echo "::warning::no dSYM at $dsym"
fi

echo "== verify after packaging"
v="$work/verify"; mkdir -p "$v"
pkgutil --expand-full "$out_dir/$base.pkg" "$v/pkg"
payload=$(find "$v/pkg" -type d -path "*Payload/Library/Application Support/obs-studio/plugins/$name.plugin" | head -1)
[[ -n "$payload" ]] || fail "PKG payload does not contain Library/Application Support/obs-studio/plugins/$name.plugin"
diff -r "$bundle" "$payload"
grep -q 'enable_currentUserHome="true"' "$v/pkg/Distribution" || fail "PKG is not a current-user install"
grep -q '<key>BundleIsRelocatable</key>' "$work/component.plist"
mkdir -p "$work/mnt"
hdiutil attach -nobrowse -readonly -mountpoint "$work/mnt" "$out_dir/$base.dmg" >/dev/null
cmp "$work/mnt/$pkg_label" "$out_dir/$base.pkg"
test -f "$work/mnt/Read Me First.txt" && test -f "$work/mnt/LICENSE.txt"
[[ $(find "$work/mnt" -mindepth 1 -maxdepth 1 ! -name '.*' | wc -l | tr -d ' ') == 3 ]] || fail "unexpected DMG contents: $(ls -A "$work/mnt")"
hdiutil detach "$work/mnt" -quiet
ditto -x -k "$out_dir/$base-manual.zip" "$v/zip"
diff -r "$bundle" "$v/zip/$name.plugin"
test -f "$v/zip/INSTALL.txt" && test -f "$v/zip/LICENSE.txt"
codesign --verify --strict "$v/zip/$name.plugin"
if find "$v/zip" "$payload" -name '*.dSYM' | grep -q .; then fail "debug symbols inside a user package"; fi

{
  echo "bundle: $( [[ $signed == yes ]] && echo "Developer ID ($CODESIGN_IDENT)" || echo 'ad-hoc (no Developer ID)')"
  echo "pkg:    $( [[ ${#sign_pkg[@]} -gt 0 ]] && echo "signed ($CODESIGN_IDENT_INSTALLER)" || echo 'unsigned')"
  echo "notarized: $( [[ -n "${NOTARY_KEYCHAIN_PROFILE:-}" && $signed == yes ]] && echo yes || echo no)"
} | tee "$out_dir/signing-status.txt"
(cd "$out_dir" && shasum -a 256 "$base.dmg" "$base-manual.zip" $( [[ -f "$base-dSYMs.zip" ]] && echo "$base-dSYMs.zip") "$base.pkg" > SHA256SUMS-macos.txt && shasum -a 256 -c SHA256SUMS-macos.txt)
echo "== done: $out_dir"
