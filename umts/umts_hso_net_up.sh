#!/bin/sh
# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright © 2008-2018 ANSSI. All Rights Reserved.

IFACE="${1}"
ADDR="${2}"
GW="${3}"
DNS1="${4}"
DNS2="${5}"

IP="/sbin/ip"

cleanup() {
	/bin/rm -f "/var/run/${IFACE}_umts"
	/bin/rm -f "/var/run/route_umts"
	${IP} route del default
	${IP} link set down dev "${IFACE}"
	${IP} addr flush dev "${IFACE}"
}

error() {
	cleanup
	echo "umts_net_up.sh: ${1}" >&2
	logger -p local0.err -t "[UMTS NET UP]" "${1}"
	exit 1
}

[[ -n "${1}" ]] || error "missing iface"
[[ -n "${2}" ]] || error "missing addr"

source /etc/conf.d/net || error "failed to source /etc/conf.d/net"

DNSCONF=""

if [[ -n "${DHCP_RESOLV_CONF}" ]]; then
	[[ -n "${DNS1}" && "${DNS1}" != "0.0.0.0" ]] && DNSCONF="nameserver ${DNS1}"
	[[ -n "${DNS2}" && "${DNS2}" != "0.0.0.0" ]] && DNSCONF="${DNSCONF}\nnameserver ${DNS2}"

	[[ -n "${DNSCONF}" ]] && printf "${DNSCONF}" > "${DHCP_RESOLV_CONF}" \
		|| error "failed to write DNS conf to ${DHCP_RESOLV_CONF}"
	chown 4000:4000 "${DHCP_RESOLV_CONF}" || error "Failed to chown ${DHCP_RESOLV_CONF}"
fi

${IP} link set up dev "${IFACE}" || error "link set up failed"
${IP} addr add dev "${IFACE}" "${ADDR}" broadcast \
	"${ADDR}" label "${IFACE}:core" || error "addr failed"

${IP} route add dev "${IFACE}" || error "route failed"

echo "${ADDR}" > "/var/run/${IFACE}_umts" \
	|| error "failed to write to /var/run/${IFACE}_umts"

echo "${GW}" > "/var/run/route_umts" || error "failed to create /var/run/route_umts"

exit 0
