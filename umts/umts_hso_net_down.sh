#!/bin/sh
# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright © 2008-2018 ANSSI. All Rights Reserved.

IFACE="${1}"
IP="/sbin/ip"

error() {
	echo "umts_net_down.sh: ${1}" >&2
	logger -p local0.err -t "[UMTS NET DOWN]" "${1}"
	exit 1
}

[[ -n "${1}" ]] || error "missing iface"

rm -f "/var/run/${IFACE}_umts" || error "failed to remove /var/run/${IFACE}_umts"
rm -f "/var/run/route_umts" || error "failed to remove /var/run/route_umts"

# Might fail - networking down should have deleted the route already
${IP} route del default

${IP} link set down dev "${IFACE}" || error "link set down failed"

# Might fail - networking down should have flushed the addresses already
${IP} addr flush dev "${IFACE}"

exit 0
