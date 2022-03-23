/* AVB support
 *
 * Copyright © 2022 Wim Taymans
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include <pipewire/pipewire.h>

#include "mrp.h"

#define MRP_JOINTIMER_MS	100
#define MRP_LVTIMER_MS		1000
#define MRP_LVATIMER_MS		10000
#define MRP_PERIODTIMER_MS	1000

#define mrp_emit(s,m,v,...)		spa_hook_list_call(&s->listener_list, struct avbtp_mrp_events, m, v, ##__VA_ARGS__)
#define mrp_emit_tx_event(s,e,st)	mrp_emit(s, tx_event, 0, e, st)

struct attribute {
	struct avbtp_mrp_attribute attr;
	struct spa_list link;
	uint8_t applicant_state;
	uint8_t registrar_state;
	uint16_t pending_indications;
	uint64_t leave_timeout;
	struct spa_callbacks cb;
};

struct mrp {
	struct server *server;
	struct spa_hook server_listener;

	struct spa_hook_list listener_list;

	struct spa_list attributes;

	uint64_t periodic_timeout;
	uint64_t leave_all_timeout;
	uint64_t join_timeout;
};

static void mrp_destroy(void *data)
{
	struct mrp *mrp = data;
	spa_hook_remove(&mrp->server_listener);
	free(mrp);
}

static void global_event(struct mrp *mrp, uint64_t now, uint8_t event)
{
	struct attribute *a;
	spa_list_for_each(a, &mrp->attributes, link)
		avbtp_mrp_update_state((struct avbtp_mrp*)mrp, now, &a->attr, event);
}

static void mrp_periodic(void *data, uint64_t now)
{
	struct mrp *mrp = data;
	bool leave_all = false;
	struct attribute *a;

	if (now > mrp->periodic_timeout) {
		mrp->periodic_timeout = now + MRP_PERIODTIMER_MS * SPA_NSEC_PER_MSEC;

		global_event(mrp, now, AVBTP_MRP_EVENT_PERIODIC);
	}
	if (now > mrp->leave_all_timeout) {
		mrp->leave_all_timeout = now + MRP_LVATIMER_MS * SPA_NSEC_PER_MSEC;

		global_event(mrp, now, AVBTP_MRP_EVENT_RX_LVA);
		leave_all = true;
	}

	if (now > mrp->join_timeout) {
		uint8_t event = leave_all ? AVBTP_MRP_EVENT_TX_LVA : AVBTP_MRP_EVENT_TX;

		mrp->join_timeout = now + MRP_JOINTIMER_MS * SPA_NSEC_PER_MSEC;

		mrp_emit_tx_event(mrp, event, true);

		global_event(mrp, now, event);

		mrp_emit_tx_event(mrp, event, false);
	}

	spa_list_for_each(a, &mrp->attributes, link) {
		if (now > a->leave_timeout) {
			a->leave_timeout = 0;
			avbtp_mrp_update_state((struct avbtp_mrp*)mrp, now, &a->attr, AVBTP_MRP_EVENT_LV_TIMER);
		}
	}


}

static const struct server_events server_events = {
	AVBTP_VERSION_SERVER_EVENTS,
	.destroy = mrp_destroy,
	.periodic = mrp_periodic,
};

int avbtp_mrp_parse_packet(struct avbtp_mrp *mrp, uint64_t now, const void *pkt, int len,
		const struct avbtp_mrp_parse_info *info, void *data)
{
	uint8_t *e = SPA_PTROFF(pkt, len, uint8_t);
	uint8_t *m = SPA_PTROFF(pkt, sizeof(struct avbtp_packet_mrp), uint8_t);

	while (m < e && (m[0] != 0 || m[1] != 0)) {
		const struct avbtp_packet_mrp_hdr *hdr = (const struct avbtp_packet_mrp_hdr*)m;
		uint8_t attr_len = hdr->attribute_length;
		uint8_t attr_type = hdr->attribute_type;
		size_t hdr_size;
		bool has_param;

		if (!info->check_header(data, hdr, &hdr_size, &has_param))
			return -EINVAL;

		m += hdr_size;

		while (m < e && (m[0] != 0 || m[1] != 0)) {
			const struct avbtp_packet_mrp_vector *v =
				(const struct avbtp_packet_mrp_vector*)m;
			uint16_t i, num_values = AVBTP_MRP_VECTOR_GET_NUM_VALUES(v);
			uint8_t event_len = (num_values+2)/3;
			uint8_t param_len = has_param ? (num_values+3)/4 : 0;
			int len = sizeof(*v) + attr_len + event_len + param_len;
			const uint8_t *first = v->first_value;
			uint8_t event[3], param[4] = { 0, };

			if (m + len > e)
				return -EPROTO;

			if (v->lva)
				info->attr_event(data, now, attr_type, AVBTP_MRP_EVENT_RX_LVA);

			for (i = 0; i < num_values; i++) {
				if (i % 3 == 0) {
					uint8_t ep = first[attr_len + i/3];
					event[2] = ep % 6; ep /= 6;
					event[1] = ep % 6; ep /= 6;
					event[0] = ep % 6;
				}
				if (has_param && (i % 4 == 0)) {
					uint8_t ep = first[attr_len + event_len + i/4];
					param[3] = ep % 4; ep /= 4;
					param[2] = ep % 4; ep /= 4;
					param[1] = ep % 4; ep /= 4;
					param[0] = ep % 4;
				}
				info->process(data, now, attr_type, first,
						event[i%3], param[i%4], i);
			}
			m += len;
		}
	}
	return 0;
}

struct avbtp_mrp_attribute *avbtp_mrp_attribute_new(struct avbtp_mrp *m,
		const struct avbtp_mrp_attribute_callbacks *cb, void *data,
		size_t user_size)
{
	struct mrp *mrp = (struct mrp*)m;
	struct attribute *a;

	a = calloc(1, sizeof(*a) + user_size);
	if (a == NULL)
		return NULL;

	a->attr.user_data = SPA_PTROFF(a, sizeof(*a), void);
	a->cb = SPA_CALLBACKS_INIT(cb, data);
	spa_list_append(&mrp->attributes, &a->link);

	return &a->attr;
}

static uint8_t get_tx_event(struct avbtp_mrp *mrp, struct attribute *a,
		int event, bool leave_all)
{
	uint8_t ev = 0;

	switch (a->applicant_state) {
	case AVBTP_MRP_VP:
	case AVBTP_MRP_AA:
	case AVBTP_MRP_AP:
	case AVBTP_MRP_QA:
	case AVBTP_MRP_QP:
		if (leave_all && a->applicant_state == AVBTP_MRP_VP) {
			switch (a->registrar_state) {
			case AVBTP_MRP_IN:
				ev = AVBTP_MRP_ATTRIBUTE_EVENT_IN;
				break;
			default:
				ev = AVBTP_MRP_ATTRIBUTE_EVENT_MT;
				break;
			}
		} else if (leave_all || a->applicant_state != AVBTP_MRP_QP) {
			switch (a->registrar_state) {
			case AVBTP_MRP_IN:
				ev = AVBTP_MRP_ATTRIBUTE_EVENT_JOININ;
				break;
			default:
				ev = AVBTP_MRP_ATTRIBUTE_EVENT_JOINMT;
				break;
			}
		}
		break;
	case AVBTP_MRP_VN:
	case AVBTP_MRP_AN:
		ev = AVBTP_MRP_ATTRIBUTE_EVENT_NEW;
		break;
	case AVBTP_MRP_LA:
		ev = AVBTP_MRP_ATTRIBUTE_EVENT_LV;
		break;
	case AVBTP_MRP_LO:
		switch (a->registrar_state) {
		case AVBTP_MRP_IN:
			ev = AVBTP_MRP_ATTRIBUTE_EVENT_IN;
			break;
		default:
			ev = AVBTP_MRP_ATTRIBUTE_EVENT_MT;
			break;
		}
		break;
	}
	return ev;
}

static int do_tx(struct avbtp_mrp *mrp, struct attribute *a,
		int event, bool leave_all)
{
	uint8_t ev = get_tx_event(mrp, a, event, leave_all);

	spa_callbacks_call(&a->cb, struct avbtp_mrp_attribute_callbacks,
			merge, 0, &a->attr, ev);

	return 0;
}

void avbtp_mrp_update_state(struct avbtp_mrp *mrp, uint64_t now,
		struct avbtp_mrp_attribute *attr, int event)
{
	struct attribute *a = SPA_CONTAINER_OF(attr, struct attribute, attr);
	switch (event) {
	case AVBTP_MRP_EVENT_BEGIN:
		a->registrar_state = AVBTP_MRP_MT;
		break;
	case AVBTP_MRP_EVENT_RX_NEW:
		if (a->registrar_state == AVBTP_MRP_LV)
			a->leave_timeout = 0;
		a->registrar_state = AVBTP_MRP_IN;
		a->pending_indications |= AVBTP_PENDING_JOIN_NEW;
		break;
	case AVBTP_MRP_EVENT_RX_JOININ:
	case AVBTP_MRP_EVENT_RX_JOINMT:
		if (a->registrar_state == AVBTP_MRP_LV)
			a->leave_timeout = 0;
		if (a->registrar_state == AVBTP_MRP_MT) {
			a->pending_indications |= AVBTP_PENDING_JOIN;
		}
		a->registrar_state = AVBTP_MRP_IN;
		break;
	case AVBTP_MRP_EVENT_RX_LV:
	case AVBTP_MRP_EVENT_RX_LVA:
	case AVBTP_MRP_EVENT_TX_LVA:
	case AVBTP_MRP_EVENT_REDECLARE:
		if (a->registrar_state == AVBTP_MRP_IN) {
			a->leave_timeout = now + MRP_LVTIMER_MS * SPA_NSEC_PER_MSEC;
			a->registrar_state = AVBTP_MRP_LV;
		}
		break;
	case AVBTP_MRP_EVENT_LV_TIMER:
	case AVBTP_MRP_EVENT_FLUSH:
		if (a->registrar_state == AVBTP_MRP_LV) {
			a->pending_indications |= AVBTP_PENDING_LEAVE;
		}
		a->registrar_state = AVBTP_MRP_MT;
		break;
	default:
		break;
	}

	switch (event) {
	case AVBTP_MRP_EVENT_BEGIN:
		a->applicant_state = AVBTP_MRP_VO;
		break;
	case AVBTP_MRP_EVENT_NEW:
		a->applicant_state = AVBTP_MRP_VN;
		break;
	case AVBTP_MRP_EVENT_JOIN:
		switch (a->applicant_state) {
		case AVBTP_MRP_VO:
		case AVBTP_MRP_LO:
			a->applicant_state = AVBTP_MRP_VP;
			break;
		case AVBTP_MRP_LA:
			a->applicant_state = AVBTP_MRP_AA;
			break;
		case AVBTP_MRP_AO:
			a->applicant_state = AVBTP_MRP_AP;
			break;
		case AVBTP_MRP_QO:
			a->applicant_state = AVBTP_MRP_QP;
			break;
		}
		break;
	case AVBTP_MRP_EVENT_LV:
		switch (a->applicant_state) {
		case AVBTP_MRP_QP:
			a->applicant_state = AVBTP_MRP_QO;
			break;
		case AVBTP_MRP_AP:
			a->applicant_state = AVBTP_MRP_AO;
			break;
		case AVBTP_MRP_VP:
			a->applicant_state = AVBTP_MRP_VO;
			break;
		case AVBTP_MRP_VN:
		case AVBTP_MRP_AN:
		case AVBTP_MRP_AA:
		case AVBTP_MRP_QA:
			a->applicant_state = AVBTP_MRP_LA;
			break;
		}
		break;
	case AVBTP_MRP_EVENT_RX_JOININ:
		switch (a->applicant_state) {
		case AVBTP_MRP_VO:
			a->applicant_state = AVBTP_MRP_AO;
			break;
		case AVBTP_MRP_VP:
			a->applicant_state = AVBTP_MRP_AP;
			break;
		case AVBTP_MRP_AA:
			a->applicant_state = AVBTP_MRP_QA;
			break;
		case AVBTP_MRP_AO:
			a->applicant_state = AVBTP_MRP_QO;
			break;
		case AVBTP_MRP_AP:
			a->applicant_state = AVBTP_MRP_QP;
			break;
		}
		SPA_FALLTHROUGH;
	case AVBTP_MRP_EVENT_RX_IN:
		switch (a->applicant_state) {
		case AVBTP_MRP_AA:
			a->applicant_state = AVBTP_MRP_QA;
			break;
		}
		SPA_FALLTHROUGH;
	case AVBTP_MRP_EVENT_RX_JOINMT:
	case AVBTP_MRP_EVENT_RX_MT:
		switch (a->applicant_state) {
		case AVBTP_MRP_QA:
			a->applicant_state = AVBTP_MRP_AA;
			break;
		case AVBTP_MRP_QO:
			a->applicant_state = AVBTP_MRP_AO;
			break;
		case AVBTP_MRP_QP:
			a->applicant_state = AVBTP_MRP_AP;
			break;
		case AVBTP_MRP_LO:
			a->applicant_state = AVBTP_MRP_VO;
			break;
		}
		break;
	case AVBTP_MRP_EVENT_RX_LV:
	case AVBTP_MRP_EVENT_RX_LVA:
	case AVBTP_MRP_EVENT_REDECLARE:
		switch (a->applicant_state) {
		case AVBTP_MRP_VO:
		case AVBTP_MRP_AO:
		case AVBTP_MRP_QO:
			a->applicant_state = AVBTP_MRP_LO;
			break;
		case AVBTP_MRP_AN:
			a->applicant_state = AVBTP_MRP_VN;
			break;
		case AVBTP_MRP_AA:
		case AVBTP_MRP_QA:
		case AVBTP_MRP_AP:
		case AVBTP_MRP_QP:
			a->applicant_state = AVBTP_MRP_VP;
			break;
		}
		break;
	case AVBTP_MRP_EVENT_PERIODIC:
		switch (a->applicant_state) {
		case AVBTP_MRP_QA:
			a->applicant_state = AVBTP_MRP_AA;
			break;
		case AVBTP_MRP_QP:
			a->applicant_state = AVBTP_MRP_AP;
			break;
		}
		break;
	case AVBTP_MRP_EVENT_TX:
		switch (a->applicant_state) {
		case AVBTP_MRP_VP:
		case AVBTP_MRP_VN:
		case AVBTP_MRP_AN:
		case AVBTP_MRP_AA:
		case AVBTP_MRP_LA:
		case AVBTP_MRP_AP:
		case AVBTP_MRP_LO:
			do_tx(mrp, a, event, false);
			break;
		}
		switch (a->applicant_state) {
		case AVBTP_MRP_VP:
			a->applicant_state = AVBTP_MRP_AA;
			break;
		case AVBTP_MRP_VN:
			a->applicant_state = AVBTP_MRP_AN;
			break;
		case AVBTP_MRP_AN:
		case AVBTP_MRP_AA:
		case AVBTP_MRP_AP:
			a->applicant_state = AVBTP_MRP_QA;
			break;
		case AVBTP_MRP_LA:
		case AVBTP_MRP_LO:
			a->applicant_state = AVBTP_MRP_VO;
			break;
		}
		break;
	case AVBTP_MRP_EVENT_TX_LVA:
	{
		switch (a->applicant_state) {
		case AVBTP_MRP_VP:
		case AVBTP_MRP_VN:
		case AVBTP_MRP_AN:
		case AVBTP_MRP_AA:
		case AVBTP_MRP_LA:
		case AVBTP_MRP_QA:
		case AVBTP_MRP_AP:
		case AVBTP_MRP_QP:
			do_tx(mrp, a, event, true);
		}
		switch (a->applicant_state) {
		case AVBTP_MRP_VO:
		case AVBTP_MRP_LA:
		case AVBTP_MRP_AO:
		case AVBTP_MRP_QO:
			a->applicant_state = AVBTP_MRP_LO;
			break;
		case AVBTP_MRP_VN:
			a->applicant_state = AVBTP_MRP_AN;
			break;
		case AVBTP_MRP_AN:
		case AVBTP_MRP_AA:
		case AVBTP_MRP_AP:
		case AVBTP_MRP_QP:
			a->applicant_state = AVBTP_MRP_QA;
			break;
		}
		break;
	}
	default:
		break;
	}
}

void avbtp_mrp_rx_event(struct avbtp_mrp *mrp, uint64_t now,
		struct avbtp_mrp_attribute *attr, uint8_t event)
{
	static const int map[] = {
		[AVBTP_MRP_ATTRIBUTE_EVENT_NEW] = AVBTP_MRP_EVENT_RX_NEW,
		[AVBTP_MRP_ATTRIBUTE_EVENT_JOININ] = AVBTP_MRP_EVENT_RX_JOININ,
		[AVBTP_MRP_ATTRIBUTE_EVENT_IN] = AVBTP_MRP_EVENT_RX_IN,
		[AVBTP_MRP_ATTRIBUTE_EVENT_JOINMT] = AVBTP_MRP_EVENT_RX_JOINMT,
		[AVBTP_MRP_ATTRIBUTE_EVENT_MT] = AVBTP_MRP_EVENT_RX_MT,
		[AVBTP_MRP_ATTRIBUTE_EVENT_LV] = AVBTP_MRP_EVENT_RX_LV,
	};
	avbtp_mrp_update_state(mrp, now, attr, map[event]);
}

void avbtp_mrp_mad_begin(struct avbtp_mrp *mrp, uint64_t now, struct avbtp_mrp_attribute *attr)
{
	struct attribute *a = SPA_CONTAINER_OF(attr, struct attribute, attr);
	a->leave_timeout = 0;
	avbtp_mrp_update_state(mrp, now, attr, AVBTP_MRP_EVENT_BEGIN);
}

void avbtp_mrp_mad_join(struct avbtp_mrp *mrp, uint64_t now, struct avbtp_mrp_attribute *attr, bool is_new)
{
	if (is_new)
		avbtp_mrp_update_state(mrp, now, attr, AVBTP_MRP_EVENT_NEW);
	else
		avbtp_mrp_update_state(mrp, now, attr, AVBTP_MRP_EVENT_JOIN);
}

void avbtp_mrp_mad_leave(struct avbtp_mrp *mrp, uint64_t now, struct avbtp_mrp_attribute *attr)
{
	avbtp_mrp_update_state(mrp, now, attr, AVBTP_MRP_EVENT_LV);
}

void avbtp_mrp_destroy(struct avbtp_mrp *mrp)
{
	mrp_destroy(mrp);
}

struct avbtp_mrp *avbtp_mrp_new(struct server *server)
{
	struct mrp *mrp;

	mrp = calloc(1, sizeof(*mrp));
	if (mrp == NULL)
		return NULL;

	mrp->server = server;
	spa_list_init(&mrp->attributes);
	spa_hook_list_init(&mrp->listener_list);

	avdecc_server_add_listener(server, &mrp->server_listener, &server_events, mrp);

	return (struct avbtp_mrp*)mrp;
}

void avbtp_mrp_add_listener(struct avbtp_mrp *m, struct spa_hook *listener,
		const struct avbtp_mrp_events *events, void *data)
{
	struct mrp *mrp = (struct mrp*)m;
	spa_hook_list_append(&mrp->listener_list, listener, events, data);
}
