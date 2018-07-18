#!/bin/sh
# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright © 2008-2018 ANSSI. All Rights Reserved.

VAR_DIR="/usr/local/var"
MENU_DIR="${VAR_DIR}/menu-xdg"
NET_DIR="${MENU_DIR}/net"
NET_LIST="${VAR_DIR}/net_list"
NET_CONFPATH="/etc/admin/netconf.d"
MEDIA_DIR="${MENU_DIR}/media"
SYS_DIR="${MENU_DIR}/sys"
POWER_DIR="${MENU_DIR}/power"

_mkdir_admin() {
	local dir="$1"
	[[ -e "${dir}" ]] && rm -r -- "${dir}"
	mkdir -p -- "${dir}"
	chmod 0775 -- "${dir}"
	chown root:admin -- "${dir}"
}

init_var() {
	_mkdir_admin "${NET_DIR}"
	_mkdir_admin "${MEDIA_DIR}"
	_mkdir_admin "${SYS_DIR}"
	_mkdir_admin "${POWER_DIR}"
	[[ ! -f "${NET_LIST}" ]] && :>"${NET_LIST}"
	chown root:admin -- "${NET_LIST}"
	chmod 0664 -- "${NET_LIST}"
}

netlist_set() {
	local profile
	:>"${NET_LIST}"
	find "${NET_CONFPATH}" -mindepth 1 -maxdepth 1 -type d | sort | while read profile; do
		echo "${profile##*/}" >>"${NET_LIST}"
	done
}

case "$1" in
	init)
		init_var
		netlist_set
		;;
	update)
		netlist_set
		;;
	*)
		echo "usage: $0 <init|update>" >/dev/stderr
		exit 1
		;;
esac
