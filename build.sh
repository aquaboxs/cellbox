#!/usr/bin/env bash

set -e # exit if a command fails
set -o pipefail # Will return the exit status of make if it fails

package_windows() { # Script to prepare the windows exe
    mkdir -p dist
    cp i386-softmmu/qemu-system-i386.exe dist/cellbox.exe
    cp i386-softmmu/qemu-system-i386w.exe dist/cellboxw.exe
    python3 ./get_deps.py dist/cellbox.exe dist
    strip dist/cellbox.exe
    strip dist/cellboxw.exe
}

package_macos() {
    #
    # Create bundle
    #
    rm -rf dist

    # Copy in executable
    mkdir -p dist/cellbox.app/Contents/MacOS/
    cp i386-softmmu/qemu-system-i386 dist/cellbox.app/Contents/MacOS/cellbox

    # Copy in in executable dylib dependencies
    mkdir -p dist/cellbox.app/Contents/Frameworks
    dylibbundler -cd -of -b -x dist/cellbox.app/Contents/MacOS/cellbox \
        -d dist/cellbox.app/Contents/Frameworks/ \
        -p '@executable_path/../Frameworks/'

    # Generate icon file
    mkdir -p cellbox.iconset
    for r in 16 32 128 256 512; do cp ui/icons/qemu_${r}x${r}.png cellbox.iconset/icon_${r}x${r}.png; done
    iconutil --convert icns --output dist/cellbox.app/Contents/Resources/cellbox.icns cellbox.iconset

    # Generate Info.plist file
    cat <<EOF > dist/cellbox.app/Contents/Info.plist
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleDevelopmentRegion</key>
  <string>en</string>
  <key>CFBundleExecutable</key>
  <string>cellbox</string>
  <key>CFBundleIconFile</key>
  <string>cellbox.icns</string>
  <key>CFBundleIdentifier</key>
  <string>cellbox.app.0</string>
  <key>CFBundleInfoDictionaryVersion</key>
  <string>6.0</string>
  <key>CFBundleName</key>
  <string>cellbox</string>
  <key>CFBundlePackageType</key>
  <string>APPL</string>
  <key>CFBundleShortVersionString</key>
  <string>1</string>
  <key>CFBundleSignature</key>
  <string>cellbox</string>
  <key>CFBundleVersion</key>
  <string>1</string>
  <key>LSApplicationCategoryType</key>
  <string>public.app-category.games</string>
  <key>LSMinimumSystemVersion</key>
  <string>10.6</string>
  <key>NSPrincipalClass</key>
  <string>NSApplication</string>
  <key>NSHighResolutionCapable</key>
  <true/>
</dict>
</plist>
EOF
}

package_linux() {
    rm -rf dist
    mkdir -p dist
    cp i386-softmmu/qemu-system-i386 dist/cellbox
}

postbuild=''
debug_opts='--enable-debug'
user_opts=''
build_cflags='-O0 -g'
job_count='4'

while [ ! -z ${1} ]
do
    case "${1}" in
    '-j'*)
        job_count="${1:2}"
        shift
        ;;
    '--release')
        build_cflags='-O3'
        debug_opts=''
        shift
        ;;
    *)
        user_opts="${user_opts} ${1}"
        shift
        ;;
    esac
done

readlink=$(command -v readlink)

case "$(uname -s)" in # adjust compilation option based on platform
    Linux)
        echo 'Compiling for Linux…'
        sys_cflags='-march=native -Wno-error=redundant-decls -Wno-error=unused-but-set-variable'
        sys_opts='--enable-kvm --disable-xen --disable-werror'
        postbuild='package_linux'
        ;;
    Darwin)
        echo 'Compiling for MacOS…'
        sys_cflags='-march=native'
        sys_opts='--disable-cocoa'
        # necessary to find libffi, which is required by gobject
        export PKG_CONFIG_PATH="${PKG_CONFIG_PATH}/usr/local/opt/libffi/lib/pkgconfig"
        # macOS needs greadlink for a GNU compatible version of readlink
        if readlink=$(command -v greadlink); then
            echo 'GNU compatible readlink detected'
        else
            echo 'Could not find a GNU compatible readlink. Please install coreutils with homebrew'
            exit -1
        fi
        postbuild='package_macos'
        ;;
    CYGWIN*|MINGW*|MSYS*)
        echo 'Compiling for Windows…'
        sys_cflags='-Wno-error'
        sys_opts='--python=python3 --disable-cocoa --disable-opengl --disable-fortify-source'
        postbuild='package_windows' # set the above function to be called after build
        ;;
    *)
        echo "could not detect OS $(uname -s), aborting" >&2
        exit -1
        ;;
esac

# find absolute path (and resolve symlinks) to build out of tree
configure="$(dirname "$($readlink -f "${0}")")/configure"

set -x # Print commands from now on

"${configure}" \
    --extra-cflags="-DXBOX=1 ${build_cflags} ${sys_cflags} ${CFLAGS}" \
    ${debug_opts} \
    ${sys_opts} \
    --target-list=i386-softmmu \
    --enable-trace-backends="nop" \
    --enable-sdl \
    --disable-curl \
    --disable-vnc \
    --disable-vnc-sasl \
    --disable-docs \
    --disable-tools \
    --disable-guest-agent \
    --disable-tpm \
    --disable-live-block-migration \
    --disable-rdma \
    --disable-replication \
    --disable-capstone \
    --disable-fdt \
    --disable-libiscsi \
    --disable-spice \
    --disable-user \
    --disable-stack-protector \
    --disable-glusterfs \
    --disable-bluez \
    --disable-gtk \
    --disable-curses \
    --disable-gnutls \
    --disable-nettle \
    --disable-gcrypt \
    --disable-crypto-afalg \
    --disable-virglrenderer \
    --disable-vhost-net \
    --disable-vhost-crypto \
    --disable-vhost-vsock \
    --disable-vhost-user \
    --disable-virtfs \
    --disable-libssh2 \
    --disable-snappy \
    --disable-bzip2 \
    --disable-vde \
    --disable-libxml2 \
    --disable-seccomp \
    --disable-numa \
    --disable-lzo \
    --disable-smartcard \
    --disable-usb-redir \
    --disable-bochs \
    --disable-cloop \
    --disable-dmg \
    --disable-vdi \
    --disable-vvfat \
    --disable-qcow1 \
    --disable-qed \
    --disable-parallels \
    --disable-sheepdog \
    --without-default-devices \
    --disable-blobs \
    ${user_opts}

time make -j"${job_count}" subdir-i386-softmmu 2>&1 | tee build.log

${postbuild} # call post build functions
