#include "lock.h"
#include <stdio.h>
#include <unistd.h>

#ifndef AIRBRIDGE_MEDIA_MPV_SCRIPTING
#define AIRBRIDGE_MEDIA_MPV_SCRIPTING 0
#endif
#ifndef AIRBRIDGE_MEDIA_MPV_LUA
#define AIRBRIDGE_MEDIA_MPV_LUA 0
#endif
#if AIRBRIDGE_MEDIA_MPV_LUA && !AIRBRIDGE_MEDIA_MPV_SCRIPTING
#error Lua requires scripting option guards
#endif

int main(void)
{
	umask(077);
	if (media_lock("/var/run/airbridge-media/player.lock")<0) {
		fprintf(stderr,"airbridge-media: player already running or private lock unavailable\n");
		return 1;
	}
	/* Fixed reviewed argv and minimal environment. No user media path, shell,
	 * arbitrary environment, config search, executable hooks or raw commands. */
	char *const argv[] = {
		"/usr/lib/airbridge-media/bin/mpv",
		"--config=no", "--include=/etc/airbridge-media/mpv.conf",
		"--idle=yes", "--terminal=no", "--input-terminal=no",
		"--input-default-bindings=no", "--input-builtin-bindings=no",
		"--input-vo-keyboard=no", "--input-cursor=no",
		"--input-ipc-server=/var/run/airbridge-media/mpv.sock",
#if AIRBRIDGE_MEDIA_MPV_SCRIPTING
		"--load-scripts=no", "--scripts=",
#endif
#if AIRBRIDGE_MEDIA_MPV_LUA
		"--ytdl=no", "--osc=no",
#endif
		"--access-references=no", "--load-unsafe-playlists=no",
		"--autoload-files=no", "--sub-auto=no", "--audio-file-auto=no", "--cover-art-auto=no",
		"--save-position-on-quit=no", "--resume-playback=no",
		"--demuxer-lavf-o=protocol_whitelist=file",
		"--hwdec=auto-safe", "--hwdec-software-fallback=no",
		"--deband=no", "--interpolation=no", "--cache=no", NULL
	};
	char *const env[] = {
		"LD_LIBRARY_PATH=/usr/lib/airbridge-media/lib",
		"LIBGL_DRIVERS_PATH=/usr/lib/airbridge-media/lib/dri",
		"GBM_BACKENDS_PATH=/usr/lib/airbridge-media/lib/gbm",
		"MESA_SHADER_CACHE_DIR=/var/run/airbridge-media/mesa-cache",
		"MESA_SHADER_CACHE_MAX_SIZE=32M",
		"ALSA_CONFIG_PATH=/usr/share/airbridge-media/alsa/alsa.conf",
		"ALSA_CONFIG_DIR=/usr/share/airbridge-media/alsa",
		"ALSA_PLUGIN_DIR=/usr/lib/airbridge-media/lib/alsa-lib",
		"HOME=/var/run/airbridge-media", "PATH=/usr/sbin:/usr/bin:/sbin:/bin",
		"LC_ALL=C", NULL
	};
	execve(argv[0],argv,env);
	perror("airbridge-media: exec private mpv");
	return 1;
}
