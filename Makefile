# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright © 2008-2018 ANSSI. All Rights Reserved.
INIT_FILES := ipsec netconf netlocal networking
LIB_FILES := common dhcp ip netfilter sp umts wireless wpa
CONF_FILES := 
ETC_FILES := ipsec_default.conf
SBIN_FILES := wpaconfgen.pl wirelessscan.pl umtsconfgen.pl netmonitor.sh checkip.sh list-net-profiles.sh ipsec-updown

INST_INIT := install -D -m 0500
INST_LIB := install -D -m 0500
INST_CONF  := install -D -m 0640
INST_ETC := install -D -m 0500
INST_SBIN := install -D -m 0500

LIBDIR ?= lib

SUBDIRS := umts

all: all_sub

clean: clean_sub

all_sub:
	${foreach dir, ${SUBDIRS}, ${MAKE} -C ${dir} all; }

clean_sub:
	${foreach dir, ${SUBDIRS}, ${MAKE} -C ${dir} clean; }

install: install_sub install_conf install_lib install_init install_etc install_sbin 

install_sub:
	${foreach dir, ${SUBDIRS}, ${MAKE} -C ${dir} install; }

install_conf:
	${foreach file, ${CONF_FILES}, ${INST_CONF} conf/$(file) ${DESTDIR}/etc/conf.d/$(file); }

install_lib:
	${foreach file, ${LIB_FILES}, ${INST_LIB} lib/$(file) ${DESTDIR}/${LIBDIR}/rc/net/$(file); }

install_init:
	${foreach file, ${INIT_FILES}, ${INST_INIT} init/$(file) ${DESTDIR}/etc/init.d/$(file); }

install_etc:
	${foreach file, ${ETC_FILES}, ${INST_ETC} etc/$(file) ${DESTDIR}/etc/$(file); }

install_sbin:
	${foreach file, ${SBIN_FILES}, ${INST_SBIN} sbin/$(file) ${DESTDIR}/sbin/$(file); }

