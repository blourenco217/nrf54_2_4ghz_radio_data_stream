#ifndef CONN_TIME_SYNC_H__
#define CONN_TIME_SYNC_H__

#include <zephyr/kernel.h>
#include <esb.h>

#define RADIO_FREQUENCY_MHZ    2400
#define ESB_CHANNEL            10
#define ESB_PIPE               0
#define RADIO_SEND_INTERVAL_MS 1000
#define RADIO_VALUE_MIN        1
#define RADIO_VALUE_MAX        10

struct radio_packet {
	uint8_t sequence;
	uint8_t value;
};

/** @brief Start central demo. */
void central_start(void);

/** @brief Start peripheral demo. */
void peripheral_start(void);

/** @brief Start the high-frequency clock required by the radio. */
int radio_hf_clock_start(void);

/** @brief Initialize the shared ESB link configuration. */
int esb_link_init(enum esb_mode mode, esb_event_handler event_handler);

static inline bool radio_value_is_valid(uint8_t value)
{
	return (value >= RADIO_VALUE_MIN) && (value <= RADIO_VALUE_MAX);
}

static inline void radio_packet_encode(struct esb_payload *payload,
				       const struct radio_packet *packet)
{
	payload->length = sizeof(*packet);
	payload->pipe = ESB_PIPE;
	payload->data[0] = packet->sequence;
	payload->data[1] = packet->value;
}

static inline bool radio_packet_decode(const struct esb_payload *payload,
				       struct radio_packet *packet)
{
	if (payload->length != sizeof(*packet)) {
		return false;
	}

	packet->sequence = payload->data[0];
	packet->value = payload->data[1];
	return radio_value_is_valid(packet->value);
}

#endif /* CONN_TIME_SYNC_H__ */
