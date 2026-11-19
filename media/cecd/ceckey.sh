#!/bin/sh
# ceckey.sh — map a CEC UI command to an airbridge.media action.
# usage: ceckey.sh press 0xNN
UBUS=ubus
KEY="$2"

status() { $UBUS call airbridge.media status '{}' 2>/dev/null; }

volume_step() {
	step="$1"
	cur=$(status | grep -o '"volume": *[0-9]*' | head -1 | grep -o '[0-9]*$')
	[ -n "$cur" ] || cur=50
	new=$(awk -v c="$cur" -v s="$step" 'BEGIN { v = c + s; if (v < 0) v = 0; if (v > 100) v = 100; printf "%d", v }')
	$UBUS call airbridge.media volume "{\"value\":$new}" >/dev/null 2>&1
}

case "$KEY" in
	0x00|0x61) # select / pause_play_function: toggle play-pause
		st=$(status | grep -o '"state": *"[a-z]*"' | head -1 | grep -o '[a-z]*"$' | tr -d '"')
		case "$st" in
			playing) $UBUS call airbridge.media pause '{"paused":true}' >/dev/null ;;
			paused)  $UBUS call airbridge.media pause '{"paused":false}' >/dev/null ;;
		esac ;;
	0x0d) $UBUS call airbridge.media stop '{}' >/dev/null ;;           # back
	0x45) $UBUS call airbridge.media stop '{}' >/dev/null ;;            # stop
	0x44) $UBUS call airbridge.media pause '{"paused":false}' >/dev/null ;; # play: resume
	0x46) $UBUS call airbridge.media pause '{"paused":true}'  >/dev/null ;; # pause
	0x03) $UBUS call airbridge.media seek '{"seconds":-10,"relative":true}' >/dev/null ;; # left
	0x04) $UBUS call airbridge.media seek '{"seconds":10,"relative":true}'  >/dev/null ;; # right
	0x01) volume_step 5 ;;                                              # up
	0x02) volume_step -5 ;;                                             # down
	0x41) volume_step 5 ;;                                              # volume_up
	0x42) volume_step -5 ;;                                             # volume_down
	*) : ;;                                                             # ignore
esac
exit 0
