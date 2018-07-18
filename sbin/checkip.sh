#!/bin/sh
# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright © 2008-2018 ANSSI. All Rights Reserved.
#
# checkip.sh - Check if an IP address respond to solicitations and show
#              the related MAC addresses with their manufacturers.
#              Two types of checks are implemented :
#               - check for a duplicate IP on the LAN 
#               - check that the default route is accessible
# Authors: Mickaël Salaün <clipos@ssi.gouv.fr>
#          Vincent Strubel <clipos@ssi.gouv.fr>
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License version
# 2 as published by the Free Software Foundation.

NET_ERROR="/usr/local/var/net_error"

. /lib/clip/net.sub

if [[ $# -ne 3 ]]; then
	echo "usage: $0 <interface> <IP> <action>" >&2
	exit 1
fi

INT="$1"
IP="$2"
ACTION="$3"

OPT=""
[[ "${INT}" == "${INT/%:*/}" ]] && OPT="-0"

findmac() {
	local mac="$(echo "$1" | head -c 8 | tr -- ':a-f' '-A-F')"
	grep "^${mac}" -- "/usr/share/misc/oui.txt" | sed -nr 's/[-0-9A-F]{8}\s*\(hex\)\s*(.*)\s*$/\1/p' | head -n 1
}

# RFC 5227
do_checkip() {
	local ret=0
	for i in {0..2}; do
		MACS="$(arping2 ${OPT} -r -c 1 -I "${INT}" -- "${IP}" 2>/dev/null | sed -nr 's/^([0-9a-f:]{17})$/\1/p')"
		if [[ -n "${MACS}" ]]; then
			for mac in ${MACS}; do
				MANUF="$(findmac "$mac")"
				[[ -n "${MANUF}" ]] && MANUF=" (${MANUF})"
				echo "${mac}${MANUF}"
				let ret++
			done
			break
		fi
	done
	return $ret
}

log() {
	logger -p daemon.info "checkip: ${1}"
}

error() {
	logger -p daemon.warning "checkip: ${1}"
	exit 1
}

errormsg_add() {
	echo "$1" >>"${NET_ERROR}"
}

net_ifwaitup "${INT}" || exit 1

RET="$(do_checkip)"
case "${ACTION}" in
	route)
		if [[ -z "${RET}" ]]; then
			errormsg_add "impossible d'atteindre la passerelle par defaut ${IP}"
			error "Unable to join the default route ${IP}"
		else
			log "Default route ${IP} can be joined through network card ${RET}"
		fi
		;;
	dupip)
		if [[ -n "${RET}" ]]; then
			errormsg_add "l'adresse IP ${IP} est déjà en cours d'utilisation par une autre machine avec l'adresse MAC ${RET}"
			error "Address ${addr} already used by the network card ${RET}"
		fi
		;;
esac
exit 0
		
