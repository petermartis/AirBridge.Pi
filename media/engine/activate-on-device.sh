#!/bin/sh
# Run explicitly after signed installation and successful hardware validation.
# No reboot, partitioning, kernel replacement, or network changes are performed.
set -eu
[ "$(id -u)" = 0 ] || { printf '%s\n' 'Run as root on the validated CM5.' >&2; exit 1; }
[ "$(uname -r)" = 6.12.112 ] || { printf '%s\n' 'Wrong kernel release.' >&2; exit 1; }
apk info -a kernel | grep -Fq 'kernel-6.12.112~294a8151df6d94c4f262106e0654471f-r1' ||
    { printf '%s\n' 'Kernel package ABI mismatch.' >&2; exit 1; }
for package in kmod-airbridge-hdmi-codec kmod-airbridge-hevc airbridge-media-engine airbridge-media; do
    apk info -e "$package" >/dev/null || exit 1
done
test -f /lib/modules/6.12.112/snd-soc-hdmi-codec.ko
test -f /lib/modules/6.12.112/rpi-hevc-dec.ko
test -x /usr/lib/airbridge-media/bin/mpv
test -x /etc/init.d/airbridge-media

entry() {
    path=$1
    module=$2
    if [ -e "$path" ] && [ "$(cat "$path")" != "$module" ]; then
        printf 'Refusing to overwrite modified startup file: %s\n' "$path" >&2
        exit 1
    fi
    printf '%s\n' "$module" > "$path"
}
# VC4 has a soft dependency on this codec that the minimal loader did not
# satisfy. Preload it before the stock drm-vc4 startup entry.
entry /etc/modules.d/04-airbridge-hdmi-codec snd-soc-hdmi-codec
entry /etc/modules.d/62-airbridge-hevc rpi-hevc-dec
uci set airbridge-media.main.enabled=1
uci commit airbridge-media
/etc/init.d/airbridge-media enable
sync
printf '%s\n' 'Media drivers and idle service enabled for boot. Verify after a coordinated reboot.'
