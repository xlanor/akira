# WireGuard library
set(WIREGUARD_DIR ${CMAKE_CURRENT_SOURCE_DIR}/library/switch-wireguard)
set(WIREGUARD_LIB ${WIREGUARD_DIR}/libwireguard.a)

add_custom_target(wireguard_build
    COMMAND make -C ${WIREGUARD_DIR}
    WORKING_DIRECTORY ${WIREGUARD_DIR}
    COMMENT "Building libwireguard.a"
    BYPRODUCTS ${WIREGUARD_LIB}
)

###########################################
# lwIP library (for WireGuard relay)
###########################################
set(LWIP_DIR ${CMAKE_CURRENT_SOURCE_DIR}/library/lwip)

set(LWIP_SRCS
    ${LWIP_DIR}/src/core/init.c
    ${LWIP_DIR}/src/core/def.c
    ${LWIP_DIR}/src/core/inet_chksum.c
    ${LWIP_DIR}/src/core/ip.c
    ${LWIP_DIR}/src/core/mem.c
    ${LWIP_DIR}/src/core/memp.c
    ${LWIP_DIR}/src/core/netif.c
    ${LWIP_DIR}/src/core/pbuf.c
    ${LWIP_DIR}/src/core/tcp.c
    ${LWIP_DIR}/src/core/tcp_in.c
    ${LWIP_DIR}/src/core/tcp_out.c
    ${LWIP_DIR}/src/core/timeouts.c
    ${LWIP_DIR}/src/core/udp.c
    ${LWIP_DIR}/src/core/ipv4/ip4.c
    ${LWIP_DIR}/src/core/ipv4/ip4_addr.c
    ${LWIP_DIR}/src/core/ipv4/ip4_frag.c
    ${LWIP_DIR}/src/core/ipv4/icmp.c
)

set(LWIP_RELAY_SRCS
    ${WIREGUARD_DIR}/lwip-relay/src/wg_lwip_relay.cpp
    ${WIREGUARD_DIR}/lwip-relay/src/wg_netif.c
    ${WIREGUARD_DIR}/lwip-relay/src/sys_arch.c
    ${CMAKE_CURRENT_SOURCE_DIR}/source/core/thread_affinity.cpp
)
