#ifndef WG_NETIF_H
#define WG_NETIF_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lwip/netif.h"
#include "wireguard.h"

err_t wg_netif_init(struct netif *netif);
void wg_netif_input(struct netif *netif, const void *data, size_t len);
void wg_netif_set_tunnel(struct netif *netif, WgTunnel *tunnel);

#ifdef __cplusplus
}
#endif

#endif
