#!/usr/bin/env bash
#
# install.sh -- set up and install satl-source, the Satellite source editor.
#
# Installs GTK 4, GtkSourceView 5, the Papirus icon theme and the build
# tools, points the desktop icon theme at Papirus, builds the editor, and
# installs it into ~/.satl -- your own directory, no root involved.
#
# It then OFFERS to symlink /usr/bin/satl-source at it, so the editor runs
# from anywhere. That is the only step that needs a password, and it is the
# only thing this script puts outside your home directory.
#
# Usage:
#   ./install.sh                    # install into ~/.satl, offer the symlink
#   ./install.sh --prefix DIR       # install into DIR itself, not DIR/bin
#   ./install.sh --symlink          # make the symlink without asking
#   ./install.sh --no-symlink       # never make the symlink
#   ./install.sh --dry-run          # print what would run, change nothing
#   ./install.sh --no-deps          # skip package installation
#   ./install.sh --no-theme         # skip the desktop icon-theme setting
#   ./install.sh --no-install       # build only, do not copy the binary

set -euo pipefail

APP="satl-source"
BUILD_DIR="build"
LINK_PATH="/usr/bin/$APP"

DRY_RUN=0
DO_THEME=1
DO_INSTALL=1
DO_DEPS=1
DO_SYMLINK="ask"   # ask | yes | no
TARGET_DIR=""      # empty means "use the default, ~/.satl"

bold=$'\033[1m'; blue=$'\033[1;34m'; yellow=$'\033[1;33m'
green=$'\033[1;32m'; red=$'\033[1;31m'; off=$'\033[0m'

say()  { printf '%s==>%s %s\n' "$blue" "$off" "$*"; }
warn() { printf '%s warning:%s %s\n' "$yellow" "$off" "$*" >&2; }
die()  { printf '%s error:%s %s\n' "$red" "$off" "$*" >&2; exit 1; }

run() {
    printf '    %s$%s %s\n' "$bold" "$off" "$*"
    if [ "$DRY_RUN" -eq 0 ]; then
        "$@"
    fi
}

while [ $# -gt 0 ]; do
    case "$1" in
        --prefix)      [ $# -ge 2 ] || die "--prefix needs a directory"
                       TARGET_DIR="${2%/}"; shift ;;
        --prefix=*)    TARGET_DIR="${1#*=}"; TARGET_DIR="${TARGET_DIR%/}" ;;
        --symlink)     DO_SYMLINK="yes" ;;
        --no-symlink)  DO_SYMLINK="no" ;;
        --dry-run)     DRY_RUN=1 ;;
        --no-theme)    DO_THEME=0 ;;
        --no-install)  DO_INSTALL=0 ;;
        --no-deps)     DO_DEPS=0 ;;
        -h|--help)     sed -n '3,23p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *)             die "unknown option: $1 (try --help)" ;;
    esac
    shift
done

[ "$DRY_RUN" -eq 1 ] && say "dry run: nothing will actually be changed"

# ---------------------------------------------------------------- privileges
# Under sudo, $HOME is root's. Everything user-owned belongs to whoever
# actually invoked the script.
if [ -n "${SUDO_USER:-}" ]; then
    USER_HOME="$(getent passwd "$SUDO_USER" | cut -d: -f6)"
    [ -n "$USER_HOME" ] || die "cannot determine the home directory of $SUDO_USER"
else
    USER_HOME="$HOME"
fi

[ -n "$TARGET_DIR" ] || TARGET_DIR="$USER_HOME/.satl"

if [ "$(id -u)" -eq 0 ]; then
    SUDO=""
elif command -v sudo >/dev/null 2>&1; then
    SUDO="sudo"
else
    SUDO=""
fi

# ------------------------------------------------------------- dependencies
install_dnf() {
    # RHEL rebuilds (AlmaLinux, Rocky, CentOS Stream) keep the development
    # packages in CRB and Papirus in EPEL; Fedora has everything already.
    if ! grep -qs '^ID=fedora' /etc/os-release; then
        say "enabling CRB and EPEL"
        run $SUDO dnf install -y epel-release || warn "could not install epel-release"
        run $SUDO dnf config-manager --set-enabled crb \
            || run $SUDO dnf config-manager --set-enabled powertools \
            || warn "could not enable the CRB/PowerTools repository"
    fi

    run $SUDO dnf install -y \
        gcc-c++ meson ninja-build pkgconf-pkg-config \
        gtk4-devel gtkmm4.0-devel gtksourceview5-devel \
        papirus-icon-theme
}

install_apt() {
    run $SUDO apt-get update
    run $SUDO apt-get install -y \
        build-essential meson ninja-build pkg-config \
        libgtk-4-dev libgtkmm-4.0-dev libgtksourceview-5-dev \
        papirus-icon-theme
}

install_pacman() {
    run $SUDO pacman -S --needed --noconfirm \
        base-devel meson ninja pkgconf \
        gtk4 gtkmm-4.0 gtksourceview5 \
        papirus-icon-theme
}

install_zypper() {
    run $SUDO zypper --non-interactive install \
        gcc-c++ meson ninja pkg-config \
        gtk4-devel gtkmm4-devel gtksourceview5-devel \
        papirus-icon-theme
}

if [ "$DO_DEPS" -eq 0 ]; then
    say "skipping package installation (--no-deps)"
else
    say "installing GTK 4, GtkSourceView 5, Papirus and the build tools"
    if   command -v dnf     >/dev/null 2>&1; then install_dnf
    elif command -v apt-get >/dev/null 2>&1; then install_apt
    elif command -v pacman  >/dev/null 2>&1; then install_pacman
    elif command -v zypper  >/dev/null 2>&1; then install_zypper
    else
        die "no supported package manager found (dnf, apt-get, pacman, zypper).
     Install GTK 4, GtkSourceView 5, gtkmm 4, meson, ninja and
     papirus-icon-theme by hand, then re-run with --no-deps."
    fi
fi

# --------------------------------------------------------------- icon theme
#
# The editor does NOT depend on this: it looks its file icons up through a
# private Papirus theme of its own, so they are right whatever the desktop is
# set to. This only makes the rest of the desktop match.
set_icon_theme() {
    if ! command -v gsettings >/dev/null 2>&1; then
        warn "gsettings not found; leaving the desktop icon theme alone"
        return
    fi

    if ! gsettings writable org.gnome.desktop.interface icon-theme >/dev/null 2>&1; then
        warn "org.gnome.desktop.interface is not available; leaving the icon theme alone"
        return
    fi

    # A per-user dconf setting: under sudo this would land in root's profile.
    if [ -n "${SUDO_USER:-}" ]; then
        warn "running under sudo -- run this yourself to set your own desktop:
           gsettings set org.gnome.desktop.interface icon-theme Papirus"
        return
    fi

    run gsettings set org.gnome.desktop.interface icon-theme Papirus
}

if [ "$DO_THEME" -eq 1 ]; then
    say "setting the desktop icon theme to Papirus"
    set_icon_theme
else
    say "skipping the desktop icon theme (--no-theme)"
fi

# --------------------------------------------------------------------- build
say "building $APP"
cd "$(dirname "$0")"

if [ -d "$BUILD_DIR" ]; then
    run meson setup --reconfigure "$BUILD_DIR" --buildtype=release
else
    run meson setup "$BUILD_DIR" --buildtype=release
fi

run ninja -C "$BUILD_DIR"

# ------------------------------------------------------------------- install
#
# Deliberately NOT `ninja install`: meson always appends bindir to its
# prefix, and --prefix here means "put it exactly there".
offer_symlink() {
    local target="$TARGET_DIR/$APP"

    # Pointless if the binary already lives in /usr/bin.
    if [ "$TARGET_DIR/$APP" = "$LINK_PATH" ]; then
        return
    fi

    if [ "$DO_SYMLINK" = "no" ]; then
        say "skipping the $LINK_PATH symlink (--no-symlink)"
        return
    fi

    if [ "$(readlink -f "$LINK_PATH" 2>/dev/null)" = "$(readlink -f "$target" 2>/dev/null)" ]; then
        say "already linked: $LINK_PATH -> $target"
        return
    fi

    if [ "$DO_SYMLINK" = "ask" ]; then
        if [ ! -t 0 ]; then
            warn "not running in a terminal, so not asking about the symlink.
           $APP is installed, but only at $target.
           To run it from anywhere:
           sudo ln -sfn $target $LINK_PATH"
            return
        fi

        printf '\n%s==>%s Link %s%s%s -> %s so %s runs from anywhere?\n' \
            "$blue" "$off" "$bold" "$LINK_PATH" "$off" "$target" "$APP"
        printf '    This is the only change outside your home directory, and it needs\n'
        printf '    your password. %sRecommended.%s [Y/n] ' "$green" "$off"
        read -r reply || reply=""

        case "$reply" in
            [Nn]*)
                say "left alone. To run the editor: $target"
                printf '    Or add it to your PATH:\n'
                printf '      echo '\''export PATH="%s:$PATH"'\'' >> ~/.bashrc\n' "$TARGET_DIR"
                return
                ;;
        esac
    fi

    run $SUDO ln -sfn "$target" "$LINK_PATH"
}

if [ "$DO_INSTALL" -eq 1 ]; then
    say "installing $APP into $TARGET_DIR (overwriting any existing copy)"

    # Root is only needed if we cannot write where the binary is going. The
    # directory may not exist yet, so test the nearest ancestor that does --
    # the default, ~/.satl, never needs a password.
    probe="$TARGET_DIR"
    while [ ! -e "$probe" ] && [ "$probe" != "/" ] && [ "$probe" != "." ]; do
        probe="$(dirname "$probe")"
    done

    if [ -w "$probe" ]; then
        INSTALL_SUDO=""
    else
        INSTALL_SUDO="$SUDO"
    fi

    # install(1) truncates and rewrites the destination, so an already
    # running copy is replaced cleanly every time.
    run $INSTALL_SUDO install -D -m 0755 "$BUILD_DIR/$APP" "$TARGET_DIR/$APP"

    [ "$DRY_RUN" -eq 0 ] && \
        printf '%s==>%s installed: %s\n' "$green" "$off" "$TARGET_DIR/$APP"

    offer_symlink
else
    say "skipping installation (--no-install); the binary is at $BUILD_DIR/$APP"
fi

printf '%s==>%s done.\n' "$green" "$off"
