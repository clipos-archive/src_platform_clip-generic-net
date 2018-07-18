#!/bin/sh
# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright © 2008-2018 ANSSI. All Rights Reserved.

IFACE="${1}"
ADDR="${2}"
MASK="${3}"
GW="${4}"
DNS1="${5}"
DNS2="${6}"

cleanup() {
	/bin/rm -f "/var/run/${IFACE}_umts"
	/bin/rm -f "/var/run/route_umts"
	ip route del default
	ip link set down dev "${IFACE}"
	ip addr flush dev "${IFACE}"
}

error() {
	cleanup
	echo "umts_net_up.sh: ${1}" >&2
	logger -p local0.err -t "[UMTS NET UP]" "${1}"
	exit 1
}

[ -n "${1}" ] || error "missing iface"
[ -n "${2}" ] || error "missing addr"

source /etc/conf.d/net || error "failed to source /etc/conf.d/net"

DNSCONF=""

if [ -n "${DHCP_RESOLV_CONF}" ]; then
	[[ -n "${DNS1}" && "${DNS1}" != "0.0.0.0" ]] && DNSCONF="nameserver ${DNS1}"
	[[ -n "${DNS2}" && "${DNS2}" != "0.0.0.0" ]] && DNSCONF="${DNSCONF}\nnameserver ${DNS2}"

	[ -n "${DNSCONF}" ] && printf "${DNSCONF}" > "${DHCP_RESOLV_CONF}" \
		|| error "failed to write DNS conf to ${DHCP_RESOLV_CONF}"
	chown 4000:4000 "${DHCP_RESOLV_CONF}" || error "Failed to chown ${DHCP_RESOLV_CONF}"
fi

ip link set "${IFACE}" down
# ethernet frames sent by the firmware have the wrong mac address so adjust it
ip link set "${IFACE}" addr 00:01:02:03:04:05
ip link set "${IFACE}" up || error "link set up failed"
ip addr add "${ADDR}/${MASK}" dev "${IFACE}" \
	label "${IFACE}:core" || error "addr failed"

ip route add default via "${GW}" dev "${IFACE}" || error "route failed"

echo "${ADDR}" > "/var/run/${IFACE}_umts" \
	|| error "failed to write to /var/run/${IFACE}_umts"

echo "${GW}" > "/var/run/route_umts" || error "failed to create /var/run/route_umts"

exit 0
