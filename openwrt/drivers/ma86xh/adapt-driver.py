from pathlib import Path
import sys

source = Path(sys.argv[1])


def replace_once(name, old, new):
    path = source / name
    text = path.read_text()
    if text.count(old) != 1:
        raise SystemExit(f"Expected exactly one diagnostic anchor in {name}: {old[:70]!r}")
    path.write_text(text.replace(old, new))


# These API adaptations are identical to the validated r2 build.
for name, versions in {
    "mac80211.c": ("6, 13, 0", "6, 17, 0"),
    "phy.c": ("6, 13, 0", "6, 18, 0"),
    "phy.h": ("6, 13, 0",),
    "wow.c": ("6, 17, 0",),
}.items():
    path = source / name
    text = path.read_text()
    for version in versions:
        old = f"LINUX_VERSION_CODE >= KERNEL_VERSION({version})"
        assert old in text, (name, version)
        text = text.replace(old, f"KERNEL_VERSION(6, 18, 55) >= KERNEL_VERSION({version})")
        text = text.replace(
            f"LINUX_VERSION_CODE < KERNEL_VERSION({version})",
            f"KERNEL_VERSION(6, 18, 55) < KERNEL_VERSION({version})",
        )
    path.write_text(text)

replace_once(
    "core.h", "struct rtw89_dev;\n",
    "struct rtw89_dev;\n"
    "void rtw89_abtx_log(struct rtw89_dev *rtwdev, const char *stage,\n"
    "\t\t    struct sk_buff *skb, int report_sn, int status, int extra);\n"
    "void rtw89_abtx_note_rx(struct rtw89_dev *rtwdev, unsigned int type,\n"
    "\t\t       unsigned int size, unsigned int offset);\n"
    "void rtw89_abtx_routes(struct rtw89_dev *rtwdev);\n",
)
replace_once(
    "core.c",
    "\trtw89_core_tx_update_desc_info(rtwdev, &tx_req);\n"
    "\trtw89_core_tx_wake(rtwdev, &tx_req);\n",
    "\trtw89_core_tx_update_desc_info(rtwdev, &tx_req);\n"
    '\trtw89_abtx_log(rtwdev, "SUBMIT", skb, tx_req.desc_info.sn,\n'
    "\t\t\t tx_req.desc_info.report, tx_req.desc_info.ch_dma);\n"
    "\trtw89_core_tx_wake(rtwdev, &tx_req);\n",
)
replace_once(
    "usb.c",
    "\t\tskb_pull(skb, txdesc_size);\n\n"
    "\t\tif (rtw89_is_tx_rpt_skb(rtwdev, skb)) {",
    "\t\tskb_pull(skb, txdesc_size);\n"
    '\t\trtw89_abtx_log(rtwdev, "USB_DONE", skb,\n'
    "\t\t\t\t RTW89_TX_SKB_CB(skb)->tx_rpt_sn,\n"
    "\t\t\t\t urb->status, rtw89_is_tx_rpt_skb(rtwdev, skb));\n\n"
    "\t\tif (rtw89_is_tx_rpt_skb(rtwdev, skb)) {",
)
replace_once(
    "mac.h",
    "\tif (rtw89_core_tx_wait_complete(rtwdev, skb_data, tx_status))\n",
    '\trtw89_abtx_log(rtwdev, "TX_STATUS", skb, skb_data->tx_rpt_sn,\n'
    "\t\t\t tx_status, tx_status == RTW89_TX_DONE);\n"
    "\tif (rtw89_core_tx_wait_complete(rtwdev, skb_data, tx_status))\n",
)
replace_once(
    "mac.h",
    "\tidx = skb_data->tx_rpt_sn;\n\n",
    "\tidx = skb_data->tx_rpt_sn;\n"
    '\trtw89_abtx_log(rtwdev, "RPT_QUEUE", skb, idx, 0,\n'
    "\t\t\t skb_data->tx_pkt_cnt_lmt);\n\n",
)
replace_once(
    "mac.c",
    "\t/* claim sw_define is not over size of tx_rpt->skbs[] */\n",
    '\trtw89_abtx_log(rtwdev, "FW_REPORT", NULL, sw_define, tx_status, txcnt);\n'
    "\t/* claim sw_define is not over size of tx_rpt->skbs[] */\n",
)
replace_once(
    "mac.c",
    "\t\tskb_data = RTW89_TX_SKB_CB(skb);\n\n"
    "\t\t/* skip if TX attempt has failed and retry limit has not been\n",
    "\t\tskb_data = RTW89_TX_SKB_CB(skb);\n"
    '\t\trtw89_abtx_log(rtwdev, "FW_MATCH", skb, sw_define, tx_status, txcnt);\n\n'
    "\t\t/* skip if TX attempt has failed and retry limit has not been\n",
)

replace_once(
    "usb.c",
    "\t\t\trtw89_chip_query_rxdesc(rtwdev, &desc_info, pkt_ptr, 0);\n",
    "\t\t\trtw89_chip_query_rxdesc(rtwdev, &desc_info, pkt_ptr, 0);\n"
    "\t\t\trtw89_abtx_note_rx(rtwdev, desc_info.pkt_type,\n"
    "\t\t\t\t\t   desc_info.pkt_size, desc_info.offset);\n",
)
replace_once(
    "fw.c",
    "\tbool dump = true;\n\n"
    "\tif (!test_bit(RTW89_FLAG_RUNNING, rtwdev->flags))\n",
    "\tbool dump = true;\n\n"
    '\trtw89_abtx_log(rtwdev, "C2H_META", NULL, category, class, func);\n'
    "\tif (!test_bit(RTW89_FLAG_RUNNING, rtwdev->flags))\n",
)
replace_once(
    "fw.c",
    "\tSET_CMC_TBL_MGQ_RPT_EN(skb->data, rtwdev->hci.tx_rpt_enabled);\n",
    "\tSET_CMC_TBL_MGQ_RPT_EN(skb->data, rtwdev->hci.tx_rpt_enabled);\n"
    '\trtw89_abtx_log(rtwdev, "CMAC_CFG", NULL, macid,\n'
    "\t\t\t rtwdev->hci.tx_rpt_enabled, chip->h2c_cctl_func_id);\n",
)
replace_once(
    "core.c",
    "\trtwdev->ps_hang_cnt = 0;\n\n\treturn 0;\n",
    "\trtwdev->ps_hang_cnt = 0;\n"
    "\trtw89_abtx_routes(rtwdev);\n\n\treturn 0;\n",
)

# No payload bytes or keys are inspected. A module parameter gates all output,
# and the shared rate limiter bounds noisy clients to 30 entries per second.
path = source / "core.c"
path.write_text(path.read_text() + r'''

/* AirBridge metadata diagnostics and explicitly enabled routing repair. */
#include <linux/ratelimit.h>

static bool rtw89_abtx_trace;
module_param_named(abtx_trace, rtw89_abtx_trace, bool, 0644);
MODULE_PARM_DESC(abtx_trace, "Log rate-limited management TX status only");

static bool rtw89_abtx_repair_route;
module_param_named(abtx_repair_route, rtw89_abtx_repair_route, bool, 0444);
MODULE_PARM_DESC(abtx_repair_route,
		 "Reapply intended MAC0 special-report route on RTL8852C USB");

static atomic_long_t abtx_rx_counts[16];
static atomic_long_t abtx_stage_counts[7];

static int abtx_counts_get(char *buffer, const struct kernel_param *kp)
{
	int i, used = 0;

	used += scnprintf(buffer + used, PAGE_SIZE - used, "rx_type[0..15]=");
	for (i = 0; i < ARRAY_SIZE(abtx_rx_counts); i++)
		used += scnprintf(buffer + used, PAGE_SIZE - used, "%s%ld",
				  i ? "," : "", atomic_long_read(&abtx_rx_counts[i]));
	used += scnprintf(buffer + used, PAGE_SIZE - used,
			  "\nstage[submit,usb_done,queued,fw_report,fw_match,status,expected_c2h]=");
	for (i = 0; i < ARRAY_SIZE(abtx_stage_counts); i++)
		used += scnprintf(buffer + used, PAGE_SIZE - used, "%s%ld",
				  i ? "," : "", atomic_long_read(&abtx_stage_counts[i]));
	used += scnprintf(buffer + used, PAGE_SIZE - used, "\n");
	return used;
}

static const struct kernel_param_ops abtx_counts_ops = {
	.get = abtx_counts_get,
};
module_param_cb(abtx_counts, &abtx_counts_ops, NULL, 0444);

void rtw89_abtx_log(struct rtw89_dev *rtwdev, const char *stage,
		    struct sk_buff *skb, int report_sn, int status, int extra)
{
	static DEFINE_RATELIMIT_STATE(abtx_limit, HZ, 30);
	struct ieee80211_tx_info *info;
	struct ieee80211_hdr *hdr;
	u16 fc = 0, frame_sn = 0;
	u32 flags = 0;

	if (!READ_ONCE(rtw89_abtx_trace))
		return;
	if (skb) {
		if (skb->len < 24)
			return;
		hdr = (struct ieee80211_hdr *)skb->data;
		if (!ieee80211_is_mgmt(hdr->frame_control))
			return;
		if (!ieee80211_is_auth(hdr->frame_control) &&
		    !ieee80211_is_assoc_resp(hdr->frame_control) &&
		    !ieee80211_is_reassoc_resp(hdr->frame_control) &&
		    !ieee80211_is_deauth(hdr->frame_control) &&
		    !ieee80211_is_disassoc(hdr->frame_control))
			return;
		fc = le16_to_cpu(hdr->frame_control);
		frame_sn = le16_get_bits(hdr->seq_ctrl, IEEE80211_SCTL_SEQ);
		info = IEEE80211_SKB_CB(skb);
		flags = info->flags;
	}
	if (!strcmp(stage, "SUBMIT"))
		atomic_long_inc(&abtx_stage_counts[0]);
	else if (!strcmp(stage, "USB_DONE"))
		atomic_long_inc(&abtx_stage_counts[1]);
	else if (!strcmp(stage, "RPT_QUEUE"))
		atomic_long_inc(&abtx_stage_counts[2]);
	else if (!strcmp(stage, "FW_REPORT"))
		atomic_long_inc(&abtx_stage_counts[3]);
	else if (!strcmp(stage, "FW_MATCH"))
		atomic_long_inc(&abtx_stage_counts[4]);
	else if (!strcmp(stage, "TX_STATUS"))
		atomic_long_inc(&abtx_stage_counts[5]);
	else if (!strcmp(stage, "C2H_META") &&
		 report_sn == RTW89_C2H_CAT_MAC &&
		 status == RTW89_MAC_C2H_CLASS_MISC &&
		 extra == RTW89_MAC_C2H_FUNC_TX_REPORT)
		atomic_long_inc(&abtx_stage_counts[6]);
	if (!__ratelimit(&abtx_limit))
		return;
	rtw89_info(rtwdev,
		   "ABTX %s fc=%04x frame_sn=%u rpt_sn=%d status=%d extra=%d flags=%08x\n",
		   stage, fc, frame_sn, report_sn, status, extra, flags);
}
EXPORT_SYMBOL_GPL(rtw89_abtx_log);

void rtw89_abtx_note_rx(struct rtw89_dev *rtwdev, unsigned int type,
		       unsigned int size, unsigned int offset)
{
	if (!READ_ONCE(rtw89_abtx_trace))
		return;
	if (type < ARRAY_SIZE(abtx_rx_counts))
		atomic_long_inc(&abtx_rx_counts[type]);
	if (type != RTW89_CORE_RX_TYPE_WIFI &&
	    type != RTW89_CORE_RX_TYPE_PPDU_STAT)
		rtw89_abtx_log(rtwdev, "RX_META", NULL, type, size, offset);
}
EXPORT_SYMBOL_GPL(rtw89_abtx_note_rx);

void rtw89_abtx_routes(struct rtw89_dev *rtwdev)
{
	bool repair = rtw89_abtx_repair_route &&
		      rtwdev->chip->chip_id == RTL8852C &&
		      rtwdev->hci.type == RTW89_HCI_TYPE_USB &&
		      rtwdev->hci.tx_rpt_enabled;
	u8 before, after;

	if (!READ_ONCE(rtw89_abtx_trace) && !repair)
		return;
	before = rtw89_read8(rtwdev, R_AX_PTCLRPT_FULL_HDL);
	if (repair &&
	    FIELD_GET(B_AX_SPE_RPT_PATH_MASK, before) != FWD_TO_WLCPU) {
		/* Preserve every other field; execute outside firmware IO packs. */
		rtw89_write8_mask(rtwdev, R_AX_PTCLRPT_FULL_HDL,
				 B_AX_SPE_RPT_PATH_MASK, FWD_TO_WLCPU);
	}
	after = rtw89_read8(rtwdev, R_AX_PTCLRPT_FULL_HDL);
	if (repair) {
		rtw89_info(rtwdev,
			   "ABTX ROUTE_REPAIR before=%02x after=%02x special_path=%u\n",
			   before, after,
			   (unsigned int)FIELD_GET(B_AX_SPE_RPT_PATH_MASK, after));
		if (FIELD_GET(B_AX_SPE_RPT_PATH_MASK, after) != FWD_TO_WLCPU)
			rtw89_warn(rtwdev, "ABTX special-report route repair did not take effect\n");
	}
	rtw89_abtx_log(rtwdev, "ROUTE", NULL,
			 after,
			 rtw89_read32(rtwdev, 0x906c),
			 rtwdev->hci.tx_rpt_enabled);
}
''')
print("Applied validated OpenWrt callbacks and opt-in ABTX trace hooks.")
