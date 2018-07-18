#!/bin/sh
# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright © 2008-2018 ANSSI. All Rights Reserved.

IFACE="${1}"

cleanup() {
	dhcpcd --release "${IFACE}"
	/bin/rm -f "/var/run/${IFACE}_umts"
	/bin/rm -f "/var/run/route_umts"
	ip route del default
	ip link set "${IFACE}" down
	ip addr flush dev "${IFACE}"
}

warn() {
	echo "umts_acm_net_up.sh: ${1}" >&2
	logger -p local0.err -t "[UMTS NET UP]" "${1}"
}
error() {
	cleanup
	echo "umts_acm_net_up.sh: ${1}" >&2
	logger -p local0.err -t "[UMTS NET UP]" "${1}"
	exit 1
}

[ -n "${1}" ] || error "missing iface"

source /etc/conf.d/net || error "failed to source /etc/conf.d/net"

DNSCONF=""

ip link set ${IFACE} down
ip link set ${IFACE} up

output="$(mktemp /tmp/dhcp.XXXXXXXX)"
if [ $? -ne 0 ]; then
	warn "failed to create temporary dhcp file for $IFACE"
	return 1
fi

/sbin/dhcpcd -p -L "${IFACE}" 2>"${output}"
if [ $? -ne 0 ]; then
	warn "dhcpcd failed on ${IFACE}, client output is as follows:"
	cat "${output}" >&2
	rm -f "${output}"
	error "le client DHCP a retourné une erreur"
	return 1
fi
rm -f "${output}"

addr="$(ip addr show "${IFACE}" | awk '$1 == "inet" { print $2 }' | head -n 1)"
if [ -z "${addr}" ]; then
	warn "could not read dhcp address on ${IFACE}, removing it"
	error "impossible de lire l'adresse DHCP"
	exit 1
fi

ip="${addr%/*}"
warn "${IFACE} got dhcp address ${addr}"
echo "${addr}" > "/var/run/${IFACE}_umts"

gw="$(ip route show default | awk '$1 == "default" { print $3 }')"
if [ -z "${gw}" ]; then
	warn "failed to get a default route"
	error "pas de route par defaut"
fi
warn "${IFACE} got default route ${gw}"
echo "${gw}" > "/var/run/route_umts"

exit 0
