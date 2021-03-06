#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdint.h>
#include <string.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <net/ethernet.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <endian.h>
#define __FAVOR_BSD
#include <netinet/udp.h>


#define PKTGEN_MAGIC   0xbe9be955
#define ETH_DST_MAC    0x020000000002
#define ETH_SRC_MAC    0x020000000001

#define IP4_SRC_IP     "10.0.0.1"
#define IP4_DST_IP     "10.0.0.2"
#define IP4_TTL        0x20
#define IP4_PROTO_UDP  0x11

#define UDP_SRC_PORT   0x09
#define UDP_DST_PORT   0x09

#define PKTDEV_HDR_LEN 12
#define ETH_HDR_LEN    14
#define IP4_HDR_LEN    20

#define PKTDEV_MAGIC   0x3776

/* from netmap pkt-gen.c */
static uint16_t checksum(const void * data, uint16_t len, uint32_t sum)
{
  const uint8_t *addr = data;
  uint32_t i;

  /* Checksum all the pairs of bytes first... */
  for (i = 0; i < (len & ~1U); i += 2) {
    sum += (u_int16_t)ntohs(*((u_int16_t *)(addr + i)));
    if (sum > 0xFFFF)
      sum -= 0xFFFF;
  }

  /*
   * If there's a single byte left over, checksum it, too.
   * Network byte order is big-endian, so the remaining byte is
   * the high byte.
   */
  if (i < len) {
    sum += addr[i] << 8;
    if (sum > 0xFFFF)
      sum -= 0xFFFF;
  }

  return sum;
}

static u_int16_t wrapsum(u_int32_t sum)
{
  sum = ~sum & 0xFFFF;
  return (htons(sum));
}


/* pktdev header */
struct pd_hdr {
	u_int16_t pd_magic;       /* le */
	u_int16_t pd_frame_len;   /* le */
	struct pd_timestamp {
		uint32_t val_low;
		uint16_t val_high;
		uint8_t resv2;
		uint8_t resv:4;
		uint8_t reg:3;
		bool reset:1;
	} __attribute__((packed)) pd_time;  /* le */
} __attribute__((packed));  /* le */

/* pktgen header */
struct pg_hdr {
  u_int32_t pg_magic;
  u_int32_t pg_id;
  u_int64_t pg_time;
} __attribute__((packed));

/* packet */
struct pktgen_pkt {
  struct pd_hdr pd;
  struct ether_header eth;
  struct ip ip;
  struct udphdr udp;
  struct pg_hdr pg;
} __attribute__((packed));



void set_pdhdr(struct pktgen_pkt *pkt, u_int16_t frame_len)
{
  struct pd_hdr *pd;
  pd = &pkt->pd;

  pd->pd_magic = PKTDEV_MAGIC;
  pd->pd_frame_len = frame_len;
  pd->pd_time.reset = 1;
  pd->pd_time.reg = 1;
  pd->pd_time.resv = 0;
  pd->pd_time.resv2 = 0;
  pd->pd_time.val_high = 0;
  pd->pd_time.val_low = 0;

  return;
}

void set_ethhdr(struct pktgen_pkt *pkt)
{
  struct ether_header *eth;
  eth = &pkt->eth;

  eth->ether_dhost[5] = (ETH_DST_MAC      ) & 0xFF;
  eth->ether_dhost[4] = (ETH_DST_MAC >>  8) & 0xFF;
  eth->ether_dhost[3] = (ETH_DST_MAC >> 16) & 0xFF;
  eth->ether_dhost[2] = (ETH_DST_MAC >> 24) & 0xFF;
  eth->ether_dhost[1] = (ETH_DST_MAC >> 32) & 0xFF;
  eth->ether_dhost[0] = (ETH_DST_MAC >> 40) & 0xFF;
  eth->ether_shost[5] = (ETH_SRC_MAC      ) & 0xFF;
  eth->ether_shost[4] = (ETH_SRC_MAC >>  8) & 0xFF;
  eth->ether_shost[3] = (ETH_SRC_MAC >> 16) & 0xFF;
  eth->ether_shost[2] = (ETH_SRC_MAC >> 24) & 0xFF;
  eth->ether_shost[1] = (ETH_SRC_MAC >> 32) & 0xFF;
  eth->ether_shost[0] = (ETH_SRC_MAC >> 40) & 0xFF;
  eth->ether_type = htons(ETHERTYPE_IP);

  return;
}

void set_ip4hdr(struct pktgen_pkt *pkt, u_int16_t frame_len)
{
  struct ip *ip;
  ip = &pkt->ip;

  ip->ip_v = IPVERSION;
  ip->ip_hl = 5;
  ip->ip_tos = 0;
  ip->ip_len = htons(frame_len - ETH_HDR_LEN);
  ip->ip_id = 0;
  ip->ip_off = htons (IP_DF);
  ip->ip_ttl = 0x20;
  ip->ip_p = IPPROTO_UDP;
  inet_pton(AF_INET, IP4_SRC_IP, &ip->ip_src);
  inet_pton(AF_INET, IP4_DST_IP, &ip->ip_dst);
  ip->ip_sum = 0;

  return;
}

void set_udphdr(struct pktgen_pkt *pkt, u_int16_t frame_len)
{
  struct udphdr *udp;
  udp = &pkt->udp;

  udp->uh_sport = htons(UDP_SRC_PORT);
  udp->uh_dport = htons(UDP_DST_PORT);
  udp->uh_ulen = htons(frame_len - ETH_HDR_LEN - IP4_HDR_LEN);
  udp->uh_sum = 0;

  return;
}

void set_pghdr(struct pktgen_pkt *pkt)
{
  struct pg_hdr *pg;
  pg = &pkt->pg;

  pg->pg_magic = htonl(PKTGEN_MAGIC);
  pg->pg_id = 0;
  pg->pg_time = 0;

  return;
};

//#define step 0xEE6B280
unsigned short id = 0;
unsigned long long ts = 0;
static inline void build_pack(char *pack, struct pktgen_pkt *pkt,
    unsigned int npkt, int pktlen, unsigned int step)
{
	struct ip *ip;
  int i, offset;

  ip = (struct ip *)&pkt->ip;

  offset = 0;
  for (i = 0; i < npkt; i++) {
    ip->ip_id = htons(id);
    pkt->pd.pd_time.val_low = ts & 0xFFFFFFFF;
    pkt->pd.pd_time.val_high = (ts >> 32) & 0xFFFF;
    pkt->pg.pg_id = htonl((u_int32_t)id++);
    ip->ip_sum = wrapsum(checksum(ip, sizeof(*ip), 0));
    //pkt->ip.ip_sum = 0;
    memcpy(pack + offset, pkt, sizeof(struct pktgen_pkt));
    pkt->pd.pd_time.reset = 0;
    offset += pktlen;
    ts += step;
  }
}

//#define mbps 1000
//#define step (int)(84 * (1000 / (float)mbps))
// ./pktgen_stdout -s <frame_len> -n <npkt> -m <nloop>
// ex(595 * 25010 = 14.88Mpps): ./pktgen_stdout -s 60 -n 595 -m 25010
int main(int argc, char **argv)
{
  char *pack = NULL;
  const char *ptr = NULL;
  struct pktgen_pkt *pkt = NULL;
  int ret = 0, i, pktlen, packlen, cnt, nleft;
  unsigned int step = 84;

  unsigned short frame_len = 60;
  unsigned int npkt = 5;
  unsigned int nloop = 10;
  unsigned int mbps = 1000;

  for (i = 1; i < argc; ++i) {
    if (0 == strcmp(argv[i], "-s")) {
      if (++i == argc) perror("-s");
      frame_len = atoi(argv[i]);
    } else if (0 == strcmp(argv[i], "-n")) {
      if (++i == argc) perror("-n");
      npkt = atoi(argv[i]);
    } else if (0 == strcmp(argv[i], "-m")) {
      if (++i == argc) perror("-m");
      nloop = atoi(argv[i]);
    } else if (0 == strcmp(argv[i], "-t")) {
      if (++i == argc) perror("-t");
      mbps = atoi(argv[i]);
    }
  }

  step = (unsigned int)(84 * (1000 / (float)mbps));
  fprintf(stderr, "step=%d\n", step);

  if (frame_len < 60 || frame_len > 9014) {
    fprintf(stderr, "frame size error: %d\n", frame_len);
    ret = -1;
    goto out;
  }
  if (npkt < 1) {
    fprintf(stderr, "npkt error: %d\n", (int)npkt);
    ret = -1;
    goto out;
  }
  if (nloop < 1) {
    fprintf(stderr, "nloop error: %d\n", nloop);
    ret = -1;
    goto out;
  }

  pkt = malloc(sizeof(struct pktgen_pkt));
  set_pdhdr(pkt, frame_len);
  set_ethhdr(pkt);
  set_ip4hdr(pkt, frame_len);
  set_udphdr(pkt, frame_len);
  set_pghdr(pkt);

  pktlen = PKTDEV_HDR_LEN + frame_len;
  pack = calloc((size_t)(pktlen * npkt), sizeof(char));

  // nloop
  packlen = pktlen * npkt;
  for (i = 0; i < nloop; i++) {
    nleft = packlen;
    ptr = (char *)pack;
    build_pack(pack, pkt, npkt, pktlen, step);
    while (nleft > 0) {
      if ((cnt = write(1, ptr, nleft)) <= 0) {
          // :todo (check errno)
          ;
      }
      nleft -= cnt;
      ptr += cnt;
    }
  }

out:
  if (pack)
    free(pack);

  if (pkt)
    free(pkt);

  return ret;
}

