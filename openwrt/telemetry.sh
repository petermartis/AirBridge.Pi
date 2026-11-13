#!/bin/sh
# No wireless keys are queried or written. Output is tab-separated display data.
set -f
PATH=/usr/sbin:/usr/bin:/sbin:/bin
export PATH

collect() {
    devices=$(iw dev 2>/dev/null)
    ap=$(printf '%s\n' "$devices" | awk '/Interface / {dev=$2} /type AP$/ {print dev; exit}')
    info=""
    stations=""
    if [ -n "$ap" ]; then
        info=$(iw dev "$ap" info 2>/dev/null)
        stations=$(iw dev "$ap" station dump 2>/dev/null)
    fi
    ssid=$(printf '%s\n' "$info" | sed -n 's/^[[:space:]]*ssid //p')
    [ -n "$ssid" ] || ssid="AP OFFLINE"
    # Strip control characters, including tabs/newlines, from display values.
    ssid=$(printf '%s' "$ssid" | tr -d '\000-\037\177')
    lan=$(ubus -t 2 call network.interface.lan status 2>/dev/null)
    ip=$(printf '%s\n' "$lan" | jsonfilter -e '@["ipv4-address"][0].address' 2>/dev/null)
    [ -n "$ip" ] || ip="NO ADDRESS"
    printf 'SSID\t%s\nIP\t%s\n' "$ssid" "$ip"
    eth="DOWN"
    [ "$(cat /sys/class/net/eth0/carrier 2>/dev/null)" = 1 ] && eth="UP"
    printf 'ETH\t%s\n' "$eth"
    if [ -n "$ap" ]; then
        printf 'AP\tUP\n'
    else
        printf 'AP\tDOWN\n'
    fi
    mode="UNKNOWN"
    if [ -n "$ap" ] && [ -d "/sys/class/net/$ap/brport" ]; then mode="BRIDGED"; fi
    printf 'MODE\t%s\n' "$mode"
    printf '%s\n' "$info" | awk '/channel / {
        width="?"; for(i=1;i<=NF;i++) if($i=="width:") width=$(i+1)
        printf "RADIO\tCH %s / %s MHZ / %s MHZ\n",$2,$3,width
    }' | tr -d '()'
    count=$(printf '%s\n' "$stations" | awk '/^Station / {n++} END {print n+0}')
    printf 'COUNT\t%s\n' "$count"
    printf '%s\n' "$stations" | awk '
        /^Station / {mac=$2}
        /^[[:space:]]*signal:/ {printf "CLIENT\t%s  %s DBM\n",mac,$2; mac=""; n++}
    '
}

case "${1:---once}" in
    --once) collect ;;
    --loop)
        umask 077
        dir=/var/run/airbridge-lcd
        mkdir -p "$dir" || exit 1
        trap 'rm -f "$dir/status.tmp"; exit 0' INT TERM
        while :; do
            collect > "$dir/status.tmp" && mv "$dir/status.tmp" "$dir/status"
            sleep 3
        done
        ;;
    *) printf 'Usage: %s [--once|--loop]\n' "$0" >&2; exit 2 ;;
esac
