#include "core/wg_netif.h"
#include "lwip/pbuf.h"
#include "lwip/ip.h"
#include <string.h>

#define WG_MTU 1420

static err_t wg_netif_output(struct netif *netif, struct pbuf *p, const ip4_addr_t *ipaddr) {
    (void)ipaddr;
    WgTunnel *tunnel = (WgTunnel *)netif->state;
    if (!tunnel) {
        return ERR_IF;
    }

    uint8_t buf[1500];
    u16_t len = pbuf_copy_partial(p, buf, sizeof(buf), 0);
    if (len == 0) {
        return ERR_BUF;
    }

    int ret = wg_send(tunnel, buf, len);
    if (ret < 0) {
        return ERR_IF;
    }

    return ERR_OK;
}

err_t wg_netif_init(struct netif *netif) {
    netif->name[0] = 'w';
    netif->name[1] = 'g';
    netif->output = wg_netif_output;
    netif->mtu = WG_MTU;
    netif->flags = NETIF_FLAG_UP | NETIF_FLAG_LINK_UP;
    return ERR_OK;
}

void wg_netif_input(struct netif *netif, const void *data, size_t len) {
    if (!netif || !data || len == 0) {
        return;
    }

    struct pbuf *p = pbuf_alloc(PBUF_RAW, (u16_t)len, PBUF_RAM);
    if (!p) {
        return;
    }

    memcpy(p->payload, data, len);

    if (netif->input(p, netif) != ERR_OK) {
        pbuf_free(p);
    }
}

void wg_netif_set_tunnel(struct netif *netif, WgTunnel *tunnel) {
    if (netif) {
        netif->state = tunnel;
    }
}
