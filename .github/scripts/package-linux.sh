#!/usr/bin/env bash
# Builds the Linux release assets from a configured + built CMake tree (preset ubuntu-ci-x86_64)
# and verifies them after packaging:
#   obs-multistream-stats_<v>_ubuntu-26.04_amd64.deb         system install (/usr, OBS 33 obs-modules layout)
#   obs-multistream-stats-<v>-linux-x86_64-manual.tar.xz     per-user install (~/.local/share/obs-studio/plugins)
#   obs-multistream-stats-<v>-linux-x86_64-debug-symbols.tar.xz
#   SHA256SUMS-linux.txt, linux-dependency-audit.txt
# Both packages contain the same stripped .so (checked).
#
# Usage: package-linux.sh <build dir> <output dir>
# Environment: DEB_MAINTAINER ("Name <email>"), PROJECT_URL, SOURCE_DATE_EPOCH (default: last commit time)
set -euo pipefail

build_dir=$(realpath "${1:?build dir}")
out_dir=$(realpath -m "${2:?output dir}")
root=$(realpath "$(dirname "${BASH_SOURCE[0]}")/../..")
name=$(jq -r .name "$root/buildspec.json")
version=$(jq -r .version "$root/buildspec.json")
author=$(jq -r .author "$root/buildspec.json")
email=$(jq -r .email "$root/buildspec.json")
website=$(jq -r .website "$root/buildspec.json")
project_url=${PROJECT_URL:-$website}
maintainer=${DEB_MAINTAINER:-"$author <${email:-maintainer-not-set@invalid}>"}
[[ "$maintainer" == *"<>"* ]] && maintainer="$author <maintainer-not-set@invalid>"
: "${SOURCE_DATE_EPOCH:=$(git -C "$root" log -1 --format=%ct 2>/dev/null || date +%s)}"
export SOURCE_DATE_EPOCH
arch=$(dpkg --print-architecture)
[[ "$arch" == amd64 ]] || { echo "::error::only amd64 is packaged (got $arch)"; exit 1; }
multiarch=$(dpkg-architecture -qDEB_HOST_MULTIARCH)

deb_name="${name}_${version}_ubuntu-26.04_${arch}.deb"
tar_name="${name}-${version}-linux-x86_64-manual.tar.xz"
dbg_name="${name}-${version}-linux-x86_64-debug-symbols.tar.xz"

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
rm -rf "$out_dir"
mkdir -p "$out_dir"

echo "== install components"
DESTDIR="$work/deb" cmake --install "$build_dir" --component system --prefix /usr
cmake --install "$build_dir" --component user --prefix "$work/user"

so_sys="$work/deb/usr/lib/$multiarch/obs-modules/plugins/$name.so"
so_user="$work/user/$name/$name.so"
test -f "$so_sys" || { echo "::error::missing $so_sys"; exit 1; }
test -f "$so_user" || { echo "::error::missing $so_user"; exit 1; }
test -f "$work/deb/usr/share/obs/obs-modules/plugins/$name/manifest.json"
test -f "$work/user/$name/data/manifest.json"

echo "== split debug symbols, strip, use the same binary in both packages"
mkdir -p "$work/debug"
objcopy --only-keep-debug "$so_sys" "$work/debug/$name.so.debug"
strip --strip-unneeded "$so_sys"
objcopy --add-gnu-debuglink="$work/debug/$name.so.debug" "$so_sys"
cp "$so_sys" "$so_user"
chmod 0644 "$so_sys" "$so_user" "$work/debug/$name.so.debug"

echo "== dependency audit"
{
  echo "# $name $version - Linux dependency audit ($(lsb_release -ds 2>/dev/null || echo unknown))"
  echo "## file"; file "$so_sys"
  echo "## readelf -d (NEEDED / RPATH / RUNPATH / SONAME)"; readelf -d "$so_sys" | grep -E 'NEEDED|RPATH|RUNPATH|SONAME' || true
  echo "## highest GLIBC / GLIBCXX symbol versions required"
  objdump -T "$so_sys" | grep -oE 'GLIBC_[0-9.]+' | sort -Vu | tail -1
  objdump -T "$so_sys" | grep -oE 'GLIBCXX_[0-9.]+' | sort -Vu | tail -1
  echo "## Qt symbol version tag required"; objdump -T "$so_sys" | grep -oE 'Qt_[0-9.]+' | sort -Vu | tail -3
  echo "## ldd"; ldd "$so_sys"
} | tee "$out_dir/linux-dependency-audit.txt"
if readelf -d "$so_sys" | grep -qE 'RPATH|RUNPATH'; then echo "::error::unexpected RPATH/RUNPATH"; exit 1; fi
if ldd "$so_sys" | grep -q 'not found'; then echo "::error::unresolved libraries"; exit 1; fi
readelf -d "$so_sys" | grep -q 'libobs.so.33' || { echo "::error::not linked against libobs.so.33"; exit 1; }
glibc_max=$(objdump -T "$so_sys" | grep -oE 'GLIBC_[0-9.]+' | sort -Vu | tail -1 | sed 's/GLIBC_//')

echo "== .deb"
docdir="$work/deb/usr/share/doc/$name"
mkdir -p "$docdir" "$work/deb/DEBIAN"
cat > "$docdir/copyright" <<EOF
Format: https://www.debian.org/doc/packaging-manuals/copyright-format/1.0/
Upstream-Name: $name
Source: ${project_url:-unpublished}

Files: *
Copyright: $(date -u -d "@$SOURCE_DATE_EPOCH" +%Y) $author
License: GPL-2+
 This program is free software; you can redistribute it and/or modify it under the terms of the
 GNU General Public License as published by the Free Software Foundation; either version 2 of the
 License, or (at your option) any later version.
 .
 On Debian systems, the complete text of the GNU General Public License version 2 can be found in
 /usr/share/common-licenses/GPL-2.
EOF
{
  echo "$name ($version) unstable; urgency=medium"
  echo
  echo "  * Release $version. See the project's CHANGELOG.md."
  echo
  echo " -- $maintainer  $(date -u -R -d "@$SOURCE_DATE_EPOCH")"
} > "$work/changelog"
gzip -9n -c "$work/changelog" > "$docdir/changelog.gz"
chmod 0644 "$docdir/copyright" "$docdir/changelog.gz"

# Library dependencies from the binary itself (libobs comes from obs-studio, which has no shlibs file).
mkdir -p "$work/shlibs/debian"
printf 'Source: %s\n\nPackage: %s\nArchitecture: any\n' "$name" "$name" > "$work/shlibs/debian/control"
shlibs=$(cd "$work/shlibs" && dpkg-shlibdeps -O --ignore-missing-info -e"$so_sys" 2>/dev/null | sed -n 's/^shlibs:Depends=//p')
depends="obs-studio (>= 33.0.0), obs-studio (<< 34~)${shlibs:+, $shlibs}"
installed_size=$(du -sk --exclude=DEBIAN "$work/deb" | cut -f1)
cat > "$work/deb/DEBIAN/control" <<EOF
Package: $name
Version: $version
Architecture: $arch
Maintainer: $maintainer
Installed-Size: $installed_size
Depends: $depends
Section: video
Priority: optional
${project_url:+Homepage: $project_url
}Description: Multi-Stream Statistics dock for OBS Studio 33
 Adds a dock with live per-destination statistics (status, bitrate, network-dropped frames,
 congestion, connection details) for OBS Studio's own stream and for multistream plugins such as
 Aitum Multistream, plus OBS-wide rendering, encoding and system performance.
 .
 Built for OBS Studio 33.x (libobs.so.33) on Ubuntu 26.04.
EOF
find "$work/deb" -type d -exec chmod 0755 {} +
(cd "$work/deb" && find usr -type f -print0 | sort -z | xargs -0 md5sum) > "$work/deb/DEBIAN/md5sums"
chmod 0644 "$work/deb/DEBIAN/control" "$work/deb/DEBIAN/md5sums"
dpkg-deb --root-owner-group -Zxz --build "$work/deb" "$out_dir/$deb_name"
dpkg-deb --info "$out_dir/$deb_name" | tee "$out_dir/deb-info.txt"
dpkg-deb --contents "$out_dir/$deb_name" | tee "$out_dir/deb-contents.txt"
if command -v lintian >/dev/null; then
  lintian --no-tag-display-limit "$out_dir/$deb_name" | tee "$out_dir/lintian.txt" || true
  if grep -q '^E:' "$out_dir/lintian.txt"; then echo "::warning::lintian reported errors (see lintian.txt)"; fi
fi

echo "== manual .tar.xz"
mkdir -p "$work/manual"
cp -r "$work/user/$name" "$work/manual/"
sed -e "s|{VERSION}|$version|g" -e "s|{GLIBC}|${glibc_max:-?}|g" \
    -e "s|{PROJECT_URL}|${project_url:-the project page you downloaded this from}|g" \
    "$root/packaging/linux/INSTALL-linux.txt" > "$work/manual/INSTALL.txt"
cp "$root/LICENSE" "$work/manual/LICENSE"
find "$work/manual" -type d -exec chmod 0755 {} +
find "$work/manual" -type f -exec chmod 0644 {} +
tar --sort=name --owner=0 --group=0 --numeric-owner --mtime="@$SOURCE_DATE_EPOCH" \
    -C "$work/manual" -cJf "$out_dir/$tar_name" "$name" INSTALL.txt LICENSE
tar -tvJf "$out_dir/$tar_name" | tee "$out_dir/tar-contents.txt"

echo "== debug symbols"
tar --sort=name --owner=0 --group=0 --numeric-owner --mtime="@$SOURCE_DATE_EPOCH" \
    -C "$work/debug" -cJf "$out_dir/$dbg_name" "$name.so.debug"

echo "== verify after packaging"
v="$work/verify"; mkdir -p "$v/deb" "$v/tar"
dpkg-deb -x "$out_dir/$deb_name" "$v/deb"
tar -xJf "$out_dir/$tar_name" -C "$v/tar"
cmp "$v/deb/usr/lib/$multiarch/obs-modules/plugins/$name.so" "$v/tar/$name/$name.so"
for f in manifest.json locale/en-US.ini; do
  cmp "$root/data/$f" "$v/deb/usr/share/obs/obs-modules/plugins/$name/$f"
  cmp "$root/data/$f" "$v/tar/$name/data/$f"
done
dpkg-deb --field "$out_dir/$deb_name" Version | grep -qx "$version"
if find "$v" -name '*.debug' | grep -q .; then echo "::error::debug symbols inside a user package"; exit 1; fi

(cd "$out_dir" && sha256sum "$deb_name" "$tar_name" "$dbg_name" > SHA256SUMS-linux.txt && sha256sum -c SHA256SUMS-linux.txt)
echo "== done: $out_dir"
