#define ETH_LEN	1518
#define ETHER_TYPE	0x0666
#define DEFAULT_IF	"wlp0s20f3"

struct eth_hdr_s {
	uint8_t dst_addr[6];
	uint8_t src_addr[6];
	uint16_t eth_type;
};


struct pulse_hdr_s {
	uint8_t type; // 1: START | 2: HEARTBEAT | 3: TALK
	char hostname[16];
	char talk_msg[32];
};


struct eth_frame_s {
	struct eth_hdr_s ethernet;
	struct pulse_hdr_s pulse;
};


enum pulse_talk_type{
	TYPE_START,
	TYPE_HEARTBEAT,
	TYPE_TALK
};
