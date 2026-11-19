/* airbridge-cecd — HDMI-CEC remote passthrough for the AirBridge media controller.
 *
 * Claims logical address 4 (Playback 1, OSD name "AirBridge") on the HDMI0
 * CEC adapter, receives USER_CONTROL_PRESSED messages forwarded by the TV
 * from its remote, and maps them to airbridge.media ubus calls via the
 * key helper script. One process, no libraries: the only exec is
 * /bin/sh <key-helper> <ui-cmd>, so failure of one key never kills the loop.
 *
 * Kernel API: linux/cec.h (6.12): open() filehandle owns the LAs configured
 * by CEC_ADAP_S_LOG_ADDRS; messages addressed to those LAs arrive via
 * CEC_RECEIVE. The vc4 driver sets the physical address from the TV EDID,
 * so we only wait for it to appear (never set it).
 */
#include <errno.h>
#include <fcntl.h>
#include <linux/cec.h>
#include <linux/cec-funcs.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <signal.h>
#include <unistd.h>

static int g_fd = -1;

static void on_term(int sig)
{
	struct cec_log_addrs la;

	(void)sig;
	if (g_fd >= 0) { /* release our LAs so the next start is not -EBUSY */
		memset(&la, 0, sizeof(la));
		ioctl(g_fd, CEC_ADAP_S_LOG_ADDRS, &la);
		close(g_fd);
	}
	_exit(0);
}

#define CEC_DEV "/dev/cec0"
#define KEY_HELPER "/usr/lib/airbridge-cecd/ceckey.sh"

static int claim_log_addrs(int fd, __u8 *la_out, __u16 pa)
{
	struct cec_log_addrs la;

	memset(&la, 0, sizeof(la));
	la.cec_version = CEC_OP_CEC_VERSION_1_4;
	la.num_log_addrs = 1;
	la.vendor_id = CEC_VENDOR_ID_NONE;
	snprintf(la.osd_name, sizeof(la.osd_name), "AirBridge");
	la.primary_device_type[0] = CEC_OP_PRIM_DEVTYPE_PLAYBACK;
	la.log_addr_type[0] = CEC_LOG_ADDR_TYPE_PLAYBACK;
	la.log_addr[0] = CEC_LOG_ADDR_INVALID; /* dynamic assignment */
	if (ioctl(fd, CEC_ADAP_S_LOG_ADDRS, &la) < 0) {
		fprintf(stderr, "cecd: S_LOG_ADDRS: %s\n", strerror(errno));
		return -1;
	}
	fprintf(stderr, "cecd: claimed LA %d (mask 0x%x)\n",
		la.log_addr[0], la.log_addr_mask);
	*la_out = la.log_addr[0];
	/* Announce ourselves as the active source: LG webOS routes the TV
	 * remote's keys to the CEC device it believes is active. */
	{
		struct cec_msg msg;

		cec_msg_init(&msg, la.log_addr[0], CEC_LOG_ADDR_BROADCAST);
		msg.msg[1] = CEC_MSG_ACTIVE_SOURCE;
		msg.msg[2] = (pa >> 8) & 0xff;
		msg.msg[3] = pa & 0xff;
		msg.len = 4;
		if (ioctl(fd, CEC_TRANSMIT, &msg) < 0)
			fprintf(stderr, "cecd: Active Source: %s\n",
				strerror(errno));
	}
	/* The framework queues received messages ONLY to follower
	 * filehandles — claiming LAs is not enough (cec-adap.c
	 * cec_receive_notify -> cec_queue_msg_fh(adap->cec_follower)). */
	{
		__u32 mode = CEC_MODE_INITIATOR | CEC_MODE_EXCL_FOLLOWER;

		if (ioctl(fd, CEC_S_MODE, &mode) < 0) {
			fprintf(stderr, "cecd: S_MODE: %s\n",
				strerror(errno));
			return -1;
		}
		fprintf(stderr, "cecd: mode set (initiator+excl follower)\n");
	}
	return 0;
}

static int wait_phys_addr_ret(int fd, __u16 *pa_out)
{
	__u16 pa = CEC_PHYS_ADDR_INVALID;
	int tries = 0;

	for (;;) {
		if (ioctl(fd, CEC_ADAP_G_PHYS_ADDR, &pa) == 0 &&
		    pa != CEC_PHYS_ADDR_INVALID && pa != 0) {
			fprintf(stderr, "cecd: phys addr %x.%x.%x.%x\n",
				(pa >> 12) & 0xf, (pa >> 8) & 0xf,
				(pa >> 4) & 0xf, pa & 0xf);
			*pa_out = pa;
			return 0;
		}
		if (++tries > 1800) { /* 1 h without a connected TV */
			fprintf(stderr, "cecd: no physical address, exiting\n");
			return -1;
		}
		sleep(2);
	}
}

/* Key map (CEC UI command operands):
 * select/pause_play(0x61)=toggle, back=stop, left/right=seek ∓10,
 * up/down = volume ∓5 handled in the helper. All others ignored. */
static void dispatch(__u8 ui_cmd)
{
	char arg[8];
	pid_t pid = fork();

	snprintf(arg, sizeof(arg), "0x%02x", ui_cmd);
	if (pid == 0) {
		execl("/bin/sh", "sh", KEY_HELPER, "press", arg,
		      (char *)NULL);
		_exit(127);
	}
	if (pid < 0)
		fprintf(stderr, "cecd: fork: %s\n", strerror(errno));
}

int main(void)
{
	__u8 la = CEC_LOG_ADDR_UNREGISTERED;
	__u16 pa = 0;
	int fd = open(CEC_DEV, O_RDWR);

	if (fd < 0) {
		fprintf(stderr, "cecd: open %s: %s\n", CEC_DEV, strerror(errno));
		return 1;
	}
	g_fd = fd;
	signal(SIGTERM, on_term);
	signal(SIGINT, on_term);
	fprintf(stderr, "cecd: %s opened\n", CEC_DEV);
	for (;;) {
		if (wait_phys_addr_ret(fd, &pa) < 0 ||
		    claim_log_addrs(fd, &la, pa) < 0) {
			close(fd);
			sleep(5);
			fd = open(CEC_DEV, O_RDWR);
			if (fd < 0)
				return 1;
			continue;
		}
		break;
	}

	for (;;) {
		struct cec_msg msg;
		int n;

		memset(&msg, 0, sizeof(msg));
		msg.timeout = 1000; /* 1 s poll granularity for clean exit */
		n = ioctl(fd, CEC_RECEIVE, &msg);
		if (n < 0) {
			if (errno == EINTR || errno == ETIMEDOUT)
				continue;
			fprintf(stderr, "cecd: RECEIVE: %s — reinit\n",
				strerror(errno));
			if (wait_phys_addr_ret(fd, &pa) < 0 ||
			    claim_log_addrs(fd, &la, pa) < 0) {
				close(fd);
				fd = open(CEC_DEV, O_RDWR);
				if (fd < 0)
					return 1;
				if (wait_phys_addr_ret(fd, &pa) < 0 ||
				    claim_log_addrs(fd, &la, pa) < 0)
					return 1;
			}
			continue;
		}
		if (msg.len < 2)
			continue;
		if (getenv("CECD_VERBOSE"))
			fprintf(stderr, "cecd: rx from %d to %d op 0x%02x\n",
				msg.msg[0] >> 4, msg.msg[0] & 0xf, msg.msg[1]);
		switch (cec_msg_opcode(&msg)) {
		case CEC_MSG_REQUEST_ACTIVE_SOURCE: /* TV asks who is active */
		{
			struct cec_msg r;

			cec_msg_init(&r, la, CEC_LOG_ADDR_BROADCAST);
			r.msg[1] = CEC_MSG_ACTIVE_SOURCE;
			r.msg[2] = (pa >> 8) & 0xff;
			r.msg[3] = pa & 0xff;
			r.len = 4;
			ioctl(fd, CEC_TRANSMIT, &r);
			break;
		}
		case CEC_MSG_USER_CONTROL_PRESSED:
			if (msg.len < 3)
				break;
			if (cec_msg_destination(&msg) !=
				    CEC_LOG_ADDR_BROADCAST &&
			    cec_msg_destination(&msg) != la)
				break;
			dispatch(msg.msg[2]);
			break;
		default:
			break;
		}
	}
}
