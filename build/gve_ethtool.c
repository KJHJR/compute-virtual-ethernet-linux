// SPDX-License-Identifier: (GPL-2.0 OR MIT)
/* Google virtual Ethernet (gve) driver
 *
 * Copyright (C) 2015-2021 Google, Inc.
 */

#include "gve_linux_version.h"
#include <linux/rtnetlink.h>
#include "gve.h"
#include "gve_adminq.h"
#include "gve_dqo.h"

static void gve_get_drvinfo(struct net_device *netdev,
			    struct ethtool_drvinfo *info)
{
	struct gve_priv *priv = netdev_priv(netdev);

	strscpy(info->driver, gve_driver_name, sizeof(info->driver));
	strscpy(info->version, gve_version_str, sizeof(info->version));
	strscpy(info->bus_info, pci_name(priv->pdev), sizeof(info->bus_info));
}

static void gve_set_msglevel(struct net_device *netdev, u32 value)
{
	struct gve_priv *priv = netdev_priv(netdev);

	priv->msg_enable = value;
}

static u32 gve_get_msglevel(struct net_device *netdev)
{
	struct gve_priv *priv = netdev_priv(netdev);

	return priv->msg_enable;
}

/* For the following stats column string names, make sure the order
 * matches how it is filled in the code. For xdp_aborted, xdp_drop,
 * xdp_pass, xdp_tx, xdp_redirect, make sure it also matches the order
 * as declared in enum xdp_action inside file uapi/linux/bpf.h .
 */
static const char gve_gstrings_main_stats[][ETH_GSTRING_LEN] = {
	"rx_packets", "rx_hsplit_pkt", "tx_packets", "rx_bytes",
	"tx_bytes", "rx_dropped", "tx_dropped", "tx_timeouts",
	"rx_skb_alloc_fail", "rx_buf_alloc_fail", "rx_desc_err_dropped_pkt",
	"rx_hsplit_unsplit_pkt",
	"interface_up_cnt", "interface_down_cnt", "reset_cnt",
	"page_alloc_fail", "dma_mapping_error", "stats_report_trigger_cnt",
};

static const char gve_gstrings_rx_stats[][ETH_GSTRING_LEN] = {
	"rx_posted_desc[%u]", "rx_completed_desc[%u]", "rx_consumed_desc[%u]",
	"rx_bytes[%u]", "rx_hsplit_bytes[%u]", "rx_cont_packet_cnt[%u]",
	"rx_frag_flip_cnt[%u]", "rx_frag_copy_cnt[%u]", "rx_frag_alloc_cnt[%u]",
	"rx_dropped_pkt[%u]", "rx_copybreak_pkt[%u]", "rx_copied_pkt[%u]",
	"rx_queue_drop_cnt[%u]", "rx_no_buffers_posted[%u]",
	"rx_drops_packet_over_mru[%u]", "rx_drops_invalid_checksum[%u]",
	"rx_xdp_aborted[%u]", "rx_xdp_drop[%u]", "rx_xdp_pass[%u]",
	"rx_xdp_tx[%u]", "rx_xdp_redirect[%u]",
	"rx_xdp_tx_errors[%u]", "rx_xdp_redirect_errors[%u]", "rx_xdp_alloc_fails[%u]",
};

static const char gve_gstrings_tx_stats[][ETH_GSTRING_LEN] = {
	"tx_posted_desc[%u]", "tx_completed_desc[%u]", "tx_consumed_desc[%u]", "tx_bytes[%u]",
	"tx_wake[%u]", "tx_stop[%u]", "tx_event_counter[%u]",
	"tx_dma_mapping_error[%u]", "tx_xsk_wakeup[%u]",
	"tx_xsk_done[%u]", "tx_xsk_sent[%u]", "tx_xdp_xmit[%u]", "tx_xdp_xmit_errors[%u]"
};

static const char gve_gstrings_adminq_stats[][ETH_GSTRING_LEN] = {
	"adminq_prod_cnt", "adminq_cmd_fail", "adminq_timeouts",
	"adminq_describe_device_cnt", "adminq_cfg_device_resources_cnt",
	"adminq_register_page_list_cnt", "adminq_unregister_page_list_cnt",
	"adminq_create_tx_queue_cnt", "adminq_create_rx_queue_cnt",
	"adminq_destroy_tx_queue_cnt", "adminq_destroy_rx_queue_cnt",
	"adminq_dcfg_device_resources_cnt", "adminq_set_driver_parameter_cnt",
	"adminq_report_stats_cnt", "adminq_report_link_speed_cnt", "adminq_get_ptype_map_cnt"
};

static const char gve_gstrings_priv_flags[][ETH_GSTRING_LEN] = {
	"report-stats",
#if (LINUX_VERSION_CODE < KERNEL_VERSION(6,8,0))
	"enable-header-split",  "enable-strict-header-split",
	"enable-max-rx-buffer-size"
#endif /* LINUX_VERSION_CODE < KERNEL_VERSION(6,8,0) */
};

#define GVE_MAIN_STATS_LEN  ARRAY_SIZE(gve_gstrings_main_stats)
#define GVE_ADMINQ_STATS_LEN  ARRAY_SIZE(gve_gstrings_adminq_stats)
#define NUM_GVE_TX_CNTS	ARRAY_SIZE(gve_gstrings_tx_stats)
#define NUM_GVE_RX_CNTS	ARRAY_SIZE(gve_gstrings_rx_stats)
#define GVE_PRIV_FLAGS_STR_LEN ARRAY_SIZE(gve_gstrings_priv_flags)

static void gve_get_strings(struct net_device *netdev, u32 stringset, u8 *data)
{
	struct gve_priv *priv = netdev_priv(netdev);
	char *s = (char *)data;
	int num_tx_queues;
	int i, j;

	num_tx_queues = gve_num_tx_queues(priv);
	switch (stringset) {
	case ETH_SS_STATS:
		memcpy(s, *gve_gstrings_main_stats,
		       sizeof(gve_gstrings_main_stats));
		s += sizeof(gve_gstrings_main_stats);

		for (i = 0; i < priv->rx_cfg.num_queues; i++) {
			for (j = 0; j < NUM_GVE_RX_CNTS; j++) {
				snprintf(s, ETH_GSTRING_LEN,
					 gve_gstrings_rx_stats[j], i);
				s += ETH_GSTRING_LEN;
			}
		}

		for (i = 0; i < num_tx_queues; i++) {
			for (j = 0; j < NUM_GVE_TX_CNTS; j++) {
				snprintf(s, ETH_GSTRING_LEN,
					 gve_gstrings_tx_stats[j], i);
				s += ETH_GSTRING_LEN;
			}
		}

		memcpy(s, *gve_gstrings_adminq_stats,
		       sizeof(gve_gstrings_adminq_stats));
		s += sizeof(gve_gstrings_adminq_stats);
		break;

	case ETH_SS_PRIV_FLAGS:
		memcpy(s, *gve_gstrings_priv_flags,
		       sizeof(gve_gstrings_priv_flags));
		s += sizeof(gve_gstrings_priv_flags);
		break;

	default:
		break;
	}
}

static int gve_get_sset_count(struct net_device *netdev, int sset)
{
	struct gve_priv *priv = netdev_priv(netdev);
	int num_tx_queues;

	num_tx_queues = gve_num_tx_queues(priv);
	switch (sset) {
	case ETH_SS_STATS:
		return GVE_MAIN_STATS_LEN + GVE_ADMINQ_STATS_LEN +
		       (priv->rx_cfg.num_queues * NUM_GVE_RX_CNTS) +
		       (num_tx_queues * NUM_GVE_TX_CNTS);
	case ETH_SS_PRIV_FLAGS:
		return GVE_PRIV_FLAGS_STR_LEN;
	default:
		return -EOPNOTSUPP;
	}
}

static void
gve_get_ethtool_stats(struct net_device *netdev,
		      struct ethtool_stats *stats, u64 *data)
{
	u64 tmp_rx_pkts, tmp_rx_hsplit_pkt, tmp_rx_bytes, tmp_rx_hsplit_bytes,
		tmp_rx_skb_alloc_fail, tmp_rx_buf_alloc_fail,
		tmp_rx_desc_err_dropped_pkt, tmp_rx_hsplit_unsplit_pkt,
		tmp_tx_pkts, tmp_tx_bytes;
	u64 rx_buf_alloc_fail, rx_desc_err_dropped_pkt, rx_hsplit_unsplit_pkt,
		rx_pkts, rx_hsplit_pkt, rx_skb_alloc_fail, rx_bytes, tx_pkts, tx_bytes,
		tx_dropped;
	int stats_idx, base_stats_idx, max_stats_idx;
	struct stats *report_stats;
	int *rx_qid_to_stats_idx;
	int *tx_qid_to_stats_idx;
	struct gve_priv *priv;
	bool skip_nic_stats;
	unsigned int start;
	int num_tx_queues;
	int ring;
	int i, j;

	ASSERT_RTNL();
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4,11,0))
	memset(data, 0, stats->n_stats * sizeof(*data));
#endif /* (LINUX_VERSION_CODE < KERNEL_VERSION(4,11,0)) */

	priv = netdev_priv(netdev);
	num_tx_queues = gve_num_tx_queues(priv);
	report_stats = priv->stats_report->stats;
	rx_qid_to_stats_idx = kmalloc_array(priv->rx_cfg.num_queues,
					    sizeof(int), GFP_KERNEL);
	if (!rx_qid_to_stats_idx)
		return;
	tx_qid_to_stats_idx = kmalloc_array(num_tx_queues,
					    sizeof(int), GFP_KERNEL);
	if (!tx_qid_to_stats_idx) {
		kfree(rx_qid_to_stats_idx);
		return;
	}
	for (rx_pkts = 0, rx_bytes = 0, rx_hsplit_pkt = 0,
	     rx_skb_alloc_fail = 0, rx_buf_alloc_fail = 0,
	     rx_desc_err_dropped_pkt = 0, rx_hsplit_unsplit_pkt = 0,
	     ring = 0;
	     ring < priv->rx_cfg.num_queues; ring++) {
		if (priv->rx) {
			do {
				struct gve_rx_ring *rx = &priv->rx[ring];

				start =
				  u64_stats_fetch_begin(&priv->rx[ring].statss);
				tmp_rx_pkts = rx->rpackets;
				tmp_rx_hsplit_pkt = rx->rx_hsplit_pkt;
				tmp_rx_bytes = rx->rbytes;
				tmp_rx_skb_alloc_fail = rx->rx_skb_alloc_fail;
				tmp_rx_buf_alloc_fail = rx->rx_buf_alloc_fail;
				tmp_rx_desc_err_dropped_pkt =
					rx->rx_desc_err_dropped_pkt;
				tmp_rx_hsplit_unsplit_pkt =
					rx->rx_hsplit_unsplit_pkt;
			} while (u64_stats_fetch_retry(&priv->rx[ring].statss,
						       start));
			rx_pkts += tmp_rx_pkts;
			rx_hsplit_pkt += tmp_rx_hsplit_pkt;
			rx_bytes += tmp_rx_bytes;
			rx_skb_alloc_fail += tmp_rx_skb_alloc_fail;
			rx_buf_alloc_fail += tmp_rx_buf_alloc_fail;
			rx_desc_err_dropped_pkt += tmp_rx_desc_err_dropped_pkt;
			rx_hsplit_unsplit_pkt += tmp_rx_hsplit_unsplit_pkt;
		}
	}
	for (tx_pkts = 0, tx_bytes = 0, tx_dropped = 0, ring = 0;
	     ring < num_tx_queues; ring++) {
		if (priv->tx) {
			do {
				start =
				  u64_stats_fetch_begin(&priv->tx[ring].statss);
				tmp_tx_pkts = priv->tx[ring].pkt_done;
				tmp_tx_bytes = priv->tx[ring].bytes_done;
			} while (u64_stats_fetch_retry(&priv->tx[ring].statss,
						       start));
			tx_pkts += tmp_tx_pkts;
			tx_bytes += tmp_tx_bytes;
			tx_dropped += priv->tx[ring].dropped_pkt;
		}
	}

	i = 0;
	data[i++] = rx_pkts;
	data[i++] = rx_hsplit_pkt;
	data[i++] = tx_pkts;
	data[i++] = rx_bytes;
	data[i++] = tx_bytes;
	/* total rx dropped packets */
	data[i++] = rx_skb_alloc_fail + rx_buf_alloc_fail +
		    rx_desc_err_dropped_pkt;
	data[i++] = tx_dropped;
	data[i++] = priv->tx_timeo_cnt;
	data[i++] = rx_skb_alloc_fail;
	data[i++] = rx_buf_alloc_fail;
	data[i++] = rx_desc_err_dropped_pkt;
	data[i++] = rx_hsplit_unsplit_pkt;
	data[i++] = priv->interface_up_cnt;
	data[i++] = priv->interface_down_cnt;
	data[i++] = priv->reset_cnt;
	data[i++] = priv->page_alloc_fail;
	data[i++] = priv->dma_mapping_error;
	data[i++] = priv->stats_report_trigger_cnt;
	i = GVE_MAIN_STATS_LEN;

	/* For rx cross-reporting stats, start from nic rx stats in report */
	base_stats_idx = GVE_TX_STATS_REPORT_NUM * num_tx_queues +
		GVE_RX_STATS_REPORT_NUM * priv->rx_cfg.num_queues;
	max_stats_idx = NIC_RX_STATS_REPORT_NUM * priv->rx_cfg.num_queues +
		base_stats_idx;
	/* Preprocess the stats report for rx, map queue id to start index */
	skip_nic_stats = false;
	for (stats_idx = base_stats_idx; stats_idx < max_stats_idx;
		stats_idx += NIC_RX_STATS_REPORT_NUM) {
		u32 stat_name = be32_to_cpu(report_stats[stats_idx].stat_name);
		u32 queue_id = be32_to_cpu(report_stats[stats_idx].queue_id);

		if (stat_name == 0) {
			/* no stats written by NIC yet */
			skip_nic_stats = true;
			break;
		}
		rx_qid_to_stats_idx[queue_id] = stats_idx;
	}
	/* walk RX rings */
	if (priv->rx) {
		for (ring = 0; ring < priv->rx_cfg.num_queues; ring++) {
			struct gve_rx_ring *rx = &priv->rx[ring];

			data[i++] = rx->fill_cnt;
			data[i++] = rx->cnt;
			data[i++] = rx->fill_cnt - rx->cnt;
			do {
				start =
				  u64_stats_fetch_begin(&priv->rx[ring].statss);
				tmp_rx_bytes = rx->rbytes;
				tmp_rx_hsplit_bytes = rx->rx_hsplit_bytes;
				tmp_rx_skb_alloc_fail = rx->rx_skb_alloc_fail;
				tmp_rx_buf_alloc_fail = rx->rx_buf_alloc_fail;
				tmp_rx_desc_err_dropped_pkt =
					rx->rx_desc_err_dropped_pkt;
			} while (u64_stats_fetch_retry(&priv->rx[ring].statss,
						       start));
			data[i++] = tmp_rx_bytes;
			data[i++] = tmp_rx_hsplit_bytes;
			data[i++] = rx->rx_cont_packet_cnt;
			data[i++] = rx->rx_frag_flip_cnt;
			data[i++] = rx->rx_frag_copy_cnt;
			data[i++] = rx->rx_frag_alloc_cnt;
			/* rx dropped packets */
			data[i++] = tmp_rx_skb_alloc_fail +
				tmp_rx_buf_alloc_fail +
				tmp_rx_desc_err_dropped_pkt;
			data[i++] = rx->rx_copybreak_pkt;
			data[i++] = rx->rx_copied_pkt;
			/* stats from NIC */
			if (skip_nic_stats) {
				/* skip NIC rx stats */
				i += NIC_RX_STATS_REPORT_NUM;
			} else {
				stats_idx = rx_qid_to_stats_idx[ring];
				for (j = 0; j < NIC_RX_STATS_REPORT_NUM; j++) {
					u64 value =
						be64_to_cpu(report_stats[stats_idx + j].value);

					data[i++] = value;
				}
			}
			/* XDP rx counters */
			do {
				start =	u64_stats_fetch_begin(&priv->rx[ring].statss);
				for (j = 0; j < GVE_XDP_ACTIONS; j++)
					data[i + j] = rx->xdp_actions[j];
				data[i + j++] = rx->xdp_tx_errors;
				data[i + j++] = rx->xdp_redirect_errors;
				data[i + j++] = rx->xdp_alloc_fails;
			} while (u64_stats_fetch_retry(&priv->rx[ring].statss,
						       start));
			i += GVE_XDP_ACTIONS + 3; /* XDP rx counters */
		}
	} else {
		i += priv->rx_cfg.num_queues * NUM_GVE_RX_CNTS;
	}

	/* For tx cross-reporting stats, start from nic tx stats in report */
	base_stats_idx = max_stats_idx;
	max_stats_idx = NIC_TX_STATS_REPORT_NUM * num_tx_queues +
		max_stats_idx;
	/* Preprocess the stats report for tx, map queue id to start index */
	skip_nic_stats = false;
	for (stats_idx = base_stats_idx; stats_idx < max_stats_idx;
		stats_idx += NIC_TX_STATS_REPORT_NUM) {
		u32 stat_name = be32_to_cpu(report_stats[stats_idx].stat_name);
		u32 queue_id = be32_to_cpu(report_stats[stats_idx].queue_id);

		if (stat_name == 0) {
			/* no stats written by NIC yet */
			skip_nic_stats = true;
			break;
		}
		tx_qid_to_stats_idx[queue_id] = stats_idx;
	}
	/* walk TX rings */
	if (priv->tx) {
		for (ring = 0; ring < num_tx_queues; ring++) {
			struct gve_tx_ring *tx = &priv->tx[ring];

			if (gve_is_gqi(priv)) {
				data[i++] = tx->req;
				data[i++] = tx->done;
				data[i++] = tx->req - tx->done;
			} else {
				/* DQO doesn't currently support
				 * posted/completed descriptor counts;
				 */
				data[i++] = 0;
				data[i++] = 0;
				data[i++] = tx->dqo_tx.tail - tx->dqo_tx.head;
			}
			do {
				start =
				  u64_stats_fetch_begin(&priv->tx[ring].statss);
				tmp_tx_bytes = tx->bytes_done;
			} while (u64_stats_fetch_retry(&priv->tx[ring].statss,
						       start));
			data[i++] = tmp_tx_bytes;
			data[i++] = tx->wake_queue;
			data[i++] = tx->stop_queue;
			data[i++] = gve_tx_load_event_counter(priv, tx);
			data[i++] = tx->dma_mapping_error;
			/* stats from NIC */
			if (skip_nic_stats) {
				/* skip NIC tx stats */
				i += NIC_TX_STATS_REPORT_NUM;
			} else {
				stats_idx = tx_qid_to_stats_idx[ring];
				for (j = 0; j < NIC_TX_STATS_REPORT_NUM; j++) {
					u64 value =
						be64_to_cpu(report_stats[stats_idx + j].value);
					data[i++] = value;
				}
			}
			/* XDP xsk counters */
			data[i++] = tx->xdp_xsk_wakeup;
			data[i++] = tx->xdp_xsk_done;
			do {
				start = u64_stats_fetch_begin(&priv->tx[ring].statss);
				data[i] = tx->xdp_xsk_sent;
				data[i + 1] = tx->xdp_xmit;
				data[i + 2] = tx->xdp_xmit_errors;
			} while (u64_stats_fetch_retry(&priv->tx[ring].statss,
						       start));
			i += 3; /* XDP tx counters */
		}
	} else {
		i += num_tx_queues * NUM_GVE_TX_CNTS;
	}

	kfree(rx_qid_to_stats_idx);
	kfree(tx_qid_to_stats_idx);
	/* AQ Stats */
	data[i++] = priv->adminq_prod_cnt;
	data[i++] = priv->adminq_cmd_fail;
	data[i++] = priv->adminq_timeouts;
	data[i++] = priv->adminq_describe_device_cnt;
	data[i++] = priv->adminq_cfg_device_resources_cnt;
	data[i++] = priv->adminq_register_page_list_cnt;
	data[i++] = priv->adminq_unregister_page_list_cnt;
	data[i++] = priv->adminq_create_tx_queue_cnt;
	data[i++] = priv->adminq_create_rx_queue_cnt;
	data[i++] = priv->adminq_destroy_tx_queue_cnt;
	data[i++] = priv->adminq_destroy_rx_queue_cnt;
	data[i++] = priv->adminq_dcfg_device_resources_cnt;
	data[i++] = priv->adminq_set_driver_parameter_cnt;
	data[i++] = priv->adminq_report_stats_cnt;
	data[i++] = priv->adminq_report_link_speed_cnt;
	data[i++] = priv->adminq_get_ptype_map_cnt;
}

static void gve_get_channels(struct net_device *netdev,
			     struct ethtool_channels *cmd)
{
	struct gve_priv *priv = netdev_priv(netdev);

	cmd->max_rx = priv->rx_cfg.max_queues;
	cmd->max_tx = priv->tx_cfg.max_queues;
	cmd->max_other = 0;
	cmd->max_combined = 0;
	cmd->rx_count = priv->rx_cfg.num_queues;
	cmd->tx_count = priv->tx_cfg.num_queues;
	cmd->other_count = 0;
	cmd->combined_count = 0;
}

static int gve_set_channels(struct net_device *netdev,
			    struct ethtool_channels *cmd)
{
	struct gve_priv *priv = netdev_priv(netdev);
	struct gve_queue_config new_tx_cfg = priv->tx_cfg;
	struct gve_queue_config new_rx_cfg = priv->rx_cfg;
	struct ethtool_channels old_settings;
	int new_tx = cmd->tx_count;
	int new_rx = cmd->rx_count;

	gve_get_channels(netdev, &old_settings);

	/* Changing combined is not allowed */
	if (cmd->combined_count != old_settings.combined_count)
		return -EINVAL;

	if (!new_rx || !new_tx)
		return -EINVAL;

	if (priv->num_xdp_queues &&
	    (new_tx != new_rx || (2 * new_tx > priv->tx_cfg.max_queues))) {
		dev_err(&priv->pdev->dev, "XDP load failed: The number of configured RX queues should be equal to the number of configured TX queues and the number of configured RX/TX queues should be less than or equal to half the maximum number of RX/TX queues");
		return -EINVAL;
	}

	if (!netif_carrier_ok(netdev)) {
		priv->tx_cfg.num_queues = new_tx;
		priv->rx_cfg.num_queues = new_rx;
		return 0;
	}

	new_tx_cfg.num_queues = new_tx;
	new_rx_cfg.num_queues = new_rx;

	return gve_adjust_queues(priv, new_rx_cfg, new_tx_cfg);
}

static void gve_get_ringparam(struct net_device *netdev,
			      struct ethtool_ringparam *cmd
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,17,0) || RHEL_RELEASE_CODE >= RHEL_RELEASE_VERSION(8, 7)
			      ,
			      struct kernel_ethtool_ringparam *kernel_cmd,
			      struct netlink_ext_ack *extack
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(5,17,0) || RHEL_RELEASE_CODE >= RHEL_RELEASE_VERSION(8, 7) */
			      )
{
	struct gve_priv *priv = netdev_priv(netdev);

	cmd->rx_max_pending = priv->max_rx_desc_cnt;
	cmd->tx_max_pending = priv->max_tx_desc_cnt;
	cmd->rx_pending = priv->rx_desc_cnt;
	cmd->tx_pending = priv->tx_desc_cnt;

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(6,8,0))
	if (!gve_header_split_supported(priv))
		kernel_cmd->tcp_data_split = ETHTOOL_TCP_DATA_SPLIT_UNKNOWN;
	else if (priv->header_split_enabled)
		kernel_cmd->tcp_data_split = ETHTOOL_TCP_DATA_SPLIT_ENABLED;
	else
		kernel_cmd->tcp_data_split = ETHTOOL_TCP_DATA_SPLIT_DISABLED;
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(6,8,0) */
}

static int gve_adjust_ring_sizes(struct gve_priv *priv,
				 u16 new_tx_desc_cnt,
				 u16 new_rx_desc_cnt)
{
	struct gve_tx_alloc_rings_cfg tx_alloc_cfg = {0};
	struct gve_rx_alloc_rings_cfg rx_alloc_cfg = {0};
	struct gve_qpls_alloc_cfg qpls_alloc_cfg = {0};
	int err;

	/* get current queue configuration */
	gve_get_curr_alloc_cfgs(priv, &qpls_alloc_cfg,
				&tx_alloc_cfg, &rx_alloc_cfg);

	/* copy over the new ring_size from ethtool */
	tx_alloc_cfg.ring_size = new_tx_desc_cnt;
	rx_alloc_cfg.ring_size = new_rx_desc_cnt;

	if (netif_running(priv->dev)) {
		err = gve_adjust_config(priv, &qpls_alloc_cfg,
					&tx_alloc_cfg, &rx_alloc_cfg);
		if (err)
			return err;
	}

	/* Set new ring_size for the next up */
	priv->tx_desc_cnt = new_tx_desc_cnt;
	priv->rx_desc_cnt = new_rx_desc_cnt;

	return 0;
}

static int gve_validate_req_ring_size(struct gve_priv *priv, u16 new_tx_desc_cnt,
				      u16 new_rx_desc_cnt)
{
	/* check for valid range */
	if (new_tx_desc_cnt < priv->min_tx_desc_cnt ||
	    new_tx_desc_cnt > priv->max_tx_desc_cnt ||
	    new_rx_desc_cnt < priv->min_rx_desc_cnt ||
	    new_rx_desc_cnt > priv->max_rx_desc_cnt) {
		dev_err(&priv->pdev->dev, "Requested descriptor count out of range\n");
		return -EINVAL;
	}

	if (!is_power_of_2(new_tx_desc_cnt) || !is_power_of_2(new_rx_desc_cnt)) {
		dev_err(&priv->pdev->dev, "Requested descriptor count has to be a power of 2\n");
		return -EINVAL;
	}
	return 0;
}

static int gve_set_ringparam(struct net_device *netdev,
			     struct ethtool_ringparam *cmd
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,17,0) || RHEL_RELEASE_CODE >= RHEL_RELEASE_VERSION(8, 7)
			     ,
			     struct kernel_ethtool_ringparam *kernel_cmd,
			     struct netlink_ext_ack *extack
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(5,17,0) || RHEL_RELEASE_CODE >= RHEL_RELEASE_VERSION(8, 7) */
			     )
{
	struct gve_priv *priv = netdev_priv(netdev);
	u16 new_tx_cnt, new_rx_cnt;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(6,8,0))
	int err;

	err = gve_set_hsplit_config(priv, kernel_cmd->tcp_data_split);
	if (err)
		return err;
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(6,8,0) */

	if (cmd->tx_pending == priv->tx_desc_cnt && cmd->rx_pending == priv->rx_desc_cnt)
		return 0;

	if (!priv->modify_ring_size_enabled) {
		dev_err(&priv->pdev->dev, "Modify ring size is not supported.\n");
		return -EOPNOTSUPP;
	}

	new_tx_cnt = cmd->tx_pending;
	new_rx_cnt = cmd->rx_pending;

	if (gve_validate_req_ring_size(priv, new_tx_cnt, new_rx_cnt))
		return -EINVAL;

	return gve_adjust_ring_sizes(priv, new_tx_cnt, new_rx_cnt);
}

static int gve_user_reset(struct net_device *netdev, u32 *flags)
{
	struct gve_priv *priv = netdev_priv(netdev);

	if (*flags == ETH_RESET_ALL) {
		*flags = 0;
		return gve_reset(priv, true);
	}

	return -EOPNOTSUPP;
}

static int gve_get_tunable(struct net_device *netdev,
			   const struct ethtool_tunable *etuna, void *value)
{
	struct gve_priv *priv = netdev_priv(netdev);

	switch (etuna->id) {
	case ETHTOOL_RX_COPYBREAK:
		*(u32 *)value = priv->rx_copybreak;
		return 0;
	default:
		return -EOPNOTSUPP;
	}
}

static int gve_set_tunable(struct net_device *netdev,
			   const struct ethtool_tunable *etuna,
			   const void *value)
{
	struct gve_priv *priv = netdev_priv(netdev);
	u32 len;

	switch (etuna->id) {
	case ETHTOOL_RX_COPYBREAK:
	{
		u32 max_copybreak = gve_is_gqi(priv) ?
			GVE_DEFAULT_RX_BUFFER_SIZE : priv->data_buffer_size_dqo;

		len = *(u32 *)value;
		if (len > max_copybreak)
			return -EINVAL;
		priv->rx_copybreak = len;
		return 0;
	}
	default:
		return -EOPNOTSUPP;
	}
}

static u32 gve_get_priv_flags(struct net_device *netdev)
{
	struct gve_priv *priv = netdev_priv(netdev);
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(6,8,0))
	u32 ret_flags = 0;

	/* Only 1 flag exists currently: report-stats (BIT(O)), so set that flag. */
	if (priv->ethtool_flags & BIT(0))
		ret_flags |= BIT(0);
	return ret_flags;
#else /* LINUX_VERSION_CODE >= KERNEL_VERSION(6,8,0) */
	return priv->ethtool_flags & GVE_PRIV_FLAGS_MASK;
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(6,8,0) */
}

static int gve_set_priv_flags(struct net_device *netdev, u32 flags)
{
	struct gve_priv *priv = netdev_priv(netdev);
	u64 ori_flags, new_flags;
	int num_tx_queues;
#if (LINUX_VERSION_CODE < KERNEL_VERSION(6,8,0))
	u64 flag_diff;
	int new_packet_buffer_size;
#endif /* LINUX_VERSION_CODE < KERNEL_VERSION(6,8,0) */

	num_tx_queues = gve_num_tx_queues(priv);
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(6,8,0))
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,20,0)
	ori_flags = READ_ONCE(priv->ethtool_flags);
#else /* LINUX_VERSION_CODE < KERNEL_VERSION(3,20,0) */
	ori_flags = ACCESS_ONCE(priv->ethtool_flags);
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(3,20,0) */
	new_flags = ori_flags;

	/* Only one priv flag exists: report-stats (BIT(0))*/
	if (flags & BIT(0))
		new_flags |= BIT(0);
	else
		new_flags &= ~(BIT(0));
#else /* LINUX_VERSION_CODE >= KERNEL_VERSION(6,8,0) */
	/* If turning off header split, strict header split will be turned off too*/
	if (gve_get_enable_header_split(priv) && !(flags & BIT(GVE_PRIV_FLAGS_ENABLE_HEADER_SPLIT))) {
		flags &= ~BIT(GVE_PRIV_FLAGS_ENABLE_HEADER_SPLIT);
		flags &= ~BIT(GVE_PRIV_FLAGS_ENABLE_STRICT_HEADER_SPLIT);
	}

	/* If strict header-split is requested, turn on regular header-split */
	if (flags & BIT(GVE_PRIV_FLAGS_ENABLE_STRICT_HEADER_SPLIT))
		flags |= BIT(GVE_PRIV_FLAGS_ENABLE_HEADER_SPLIT);

	/* Make sure header-split is available */
	if ((flags & BIT(GVE_PRIV_FLAGS_ENABLE_HEADER_SPLIT)) && !(priv->header_buf_size)) {
		dev_err(&priv->pdev->dev, "Header-split not available\n");
		return -EINVAL;
	}

	if ((flags & BIT(GVE_PRIV_FLAGS_ENABLE_MAX_RX_BUFFER_SIZE)) && priv->max_rx_buffer_size <= GVE_DEFAULT_HEADER_BUFFER_SIZE) {
		dev_err(&priv->pdev->dev,
			"Max-rx-buffer-size not available\n");
		return -EINVAL;
	}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,20,0)
	ori_flags = READ_ONCE(priv->ethtool_flags);
#else /* LINUX_VERSION_CODE < KERNEL_VERSION(3,20,0) */
	ori_flags = ACCESS_ONCE(priv->ethtool_flags);
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(3,20,0) */

	new_flags = flags & GVE_PRIV_FLAGS_MASK;

	flag_diff = new_flags ^ ori_flags;

	if ((flag_diff & BIT(GVE_PRIV_FLAGS_ENABLE_HEADER_SPLIT)) || (flag_diff & BIT(GVE_PRIV_FLAGS_ENABLE_MAX_RX_BUFFER_SIZE))) {
		bool enable_hdr_split = new_flags & BIT(GVE_PRIV_FLAGS_ENABLE_HEADER_SPLIT);
		bool enable_max_buffer_size = new_flags & BIT(GVE_PRIV_FLAGS_ENABLE_MAX_RX_BUFFER_SIZE);
		int err;

		if (enable_max_buffer_size)
			new_packet_buffer_size = priv->max_rx_buffer_size;
		else
			new_packet_buffer_size = GVE_DEFAULT_RX_BUFFER_SIZE;

		
		err = gve_set_buffer_size_config(priv, enable_hdr_split,
						 new_packet_buffer_size);
		if (err)
			return err;
	}
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(6,8,0) */
	priv->ethtool_flags = new_flags;
	/* start report-stats timer when user turns report stats on. */
	if (flags & BIT(0)) {
		mod_timer(&priv->stats_report_timer,
			  round_jiffies(jiffies +
					msecs_to_jiffies(priv->stats_report_timer_period)));
	}
	/* Zero off gve stats when report-stats turned off and */
	/* delete report stats timer. */
	if (!(flags & BIT(0)) && (ori_flags & BIT(0))) {
		int tx_stats_num = GVE_TX_STATS_REPORT_NUM *
			num_tx_queues;
		int rx_stats_num = GVE_RX_STATS_REPORT_NUM *
			priv->rx_cfg.num_queues;

		memset(priv->stats_report->stats, 0, (tx_stats_num + rx_stats_num) *
				   sizeof(struct stats));
		del_timer_sync(&priv->stats_report_timer);
	}
#if (LINUX_VERSION_CODE < KERNEL_VERSION(6,8,0))
	priv->header_split_strict = (priv->ethtool_flags & BIT(GVE_PRIV_FLAGS_ENABLE_STRICT_HEADER_SPLIT)) ? true : false;
#endif /* LINUX_VERSION_CODE < KERNEL_VERSION(6,8,0) */
	return 0;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,6,0)
static int gve_get_link_ksettings(struct net_device *netdev,
				  struct ethtool_link_ksettings *cmd)
{
	struct gve_priv *priv = netdev_priv(netdev);
	int err = 0;

	if (priv->link_speed == 0)
		err = gve_adminq_report_link_speed(priv);

	cmd->base.speed = priv->link_speed;

	cmd->base.duplex = DUPLEX_FULL;

	return err;
}
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(4,6,0) */

static int gve_get_coalesce(struct net_device *netdev,
			    struct ethtool_coalesce *ec
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,15,0) || (RHEL_RELEASE_CODE >= RHEL_RELEASE_VERSION(8,4) && RHEL_RELEASE_CODE < RHEL_RELEASE_VERSION(9,0)) || RHEL_RELEASE_CODE >= RHEL_RELEASE_VERSION(9,2)
			    ,
			    struct kernel_ethtool_coalesce *kernel_ec,
			    struct netlink_ext_ack *extack
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(5,15,0) || (RHEL_RELEASE_CODE >= RHEL_RELEASE_VERSION(8,4) && RHEL_RELEASE_CODE < RHEL_RELEASE_VERSION(9,0)) || RHEL_RELEASE_CODE >= RHEL_RELEASE_VERSION(9,2) */
			    )
{
	struct gve_priv *priv = netdev_priv(netdev);

	if (gve_is_gqi(priv))
		return -EOPNOTSUPP;
	ec->tx_coalesce_usecs = priv->tx_coalesce_usecs;
	ec->rx_coalesce_usecs = priv->rx_coalesce_usecs;

	return 0;
}

static int gve_set_coalesce(struct net_device *netdev,
			    struct ethtool_coalesce *ec
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,15,0) || (RHEL_RELEASE_CODE >= RHEL_RELEASE_VERSION(8,4) && RHEL_RELEASE_CODE < RHEL_RELEASE_VERSION(9,0)) || RHEL_RELEASE_CODE >= RHEL_RELEASE_VERSION(9,2)
			    ,
			    struct kernel_ethtool_coalesce *kernel_ec,
			    struct netlink_ext_ack *extack
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(5,15,0) || (RHEL_RELEASE_CODE >= RHEL_RELEASE_VERSION(8,4) && RHEL_RELEASE_CODE < RHEL_RELEASE_VERSION(9,0)) || RHEL_RELEASE_CODE >= RHEL_RELEASE_VERSION(9,2) */
			    )
{
	struct gve_priv *priv = netdev_priv(netdev);
	u32 tx_usecs_orig = priv->tx_coalesce_usecs;
	u32 rx_usecs_orig = priv->rx_coalesce_usecs;
	int idx;

	if (gve_is_gqi(priv))
		return -EOPNOTSUPP;

	if (ec->tx_coalesce_usecs > GVE_MAX_ITR_INTERVAL_DQO ||
	    ec->rx_coalesce_usecs > GVE_MAX_ITR_INTERVAL_DQO)
		return -EINVAL;
	priv->tx_coalesce_usecs = ec->tx_coalesce_usecs;
	priv->rx_coalesce_usecs = ec->rx_coalesce_usecs;

	if (tx_usecs_orig != priv->tx_coalesce_usecs) {
		for (idx = 0; idx < priv->tx_cfg.num_queues; idx++) {
			int ntfy_idx = gve_tx_idx_to_ntfy(priv, idx);
			struct gve_notify_block *block = &priv->ntfy_blocks[ntfy_idx];

			gve_set_itr_coalesce_usecs_dqo(priv, block,
						       priv->tx_coalesce_usecs);
		}
	}

	if (rx_usecs_orig != priv->rx_coalesce_usecs) {
		for (idx = 0; idx < priv->rx_cfg.num_queues; idx++) {
			int ntfy_idx = gve_rx_idx_to_ntfy(priv, idx);
			struct gve_notify_block *block = &priv->ntfy_blocks[ntfy_idx];

			gve_set_itr_coalesce_usecs_dqo(priv, block,
						       priv->rx_coalesce_usecs);
		}
	}

	return 0;
}

const struct ethtool_ops gve_ethtool_ops = {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5,7,0)
	.supported_coalesce_params = ETHTOOL_COALESCE_USECS,
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(5,7,0) */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(6,8,0))
	.supported_ring_params = ETHTOOL_RING_USE_TCP_DATA_SPLIT,
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(6,8,0) */
	.get_drvinfo = gve_get_drvinfo,
	.get_strings = gve_get_strings,
	.get_sset_count = gve_get_sset_count,
	.get_ethtool_stats = gve_get_ethtool_stats,
	.set_msglevel = gve_set_msglevel,
	.get_msglevel = gve_get_msglevel,
	.set_channels = gve_set_channels,
	.get_channels = gve_get_channels,
	.get_link = ethtool_op_get_link,
	.get_coalesce = gve_get_coalesce,
	.set_coalesce = gve_set_coalesce,
	.get_ringparam = gve_get_ringparam,
	.set_ringparam = gve_set_ringparam,
	.reset = gve_user_reset,
	.get_tunable = gve_get_tunable,
	.set_tunable = gve_set_tunable,
	.get_priv_flags = gve_get_priv_flags,
	.set_priv_flags = gve_set_priv_flags,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,6,0)
	.get_link_ksettings = gve_get_link_ksettings,
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(4,6,0) */
	.get_ts_info = ethtool_op_get_ts_info,
};