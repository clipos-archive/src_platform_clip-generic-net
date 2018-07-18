# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright © 2008-2018 ANSSI. All Rights Reserved.
CFLAGS ?= -O2 -pipe
CFLAGS += -Wall -Wextra -Werror \
	-Wstrict-prototypes -Wmissing-prototypes \
	-Wcast-qual -Wcast-align -Wpointer-arith \
	-Wnested-externs

CFLAGS += -DHOOKS_DIR=\"${HOOKS_PATH}\"

LDFLAGS ?= -Wl,-O1
UMTS_CONFIG := umts_config
UMTS_SRC := umts_config.c umts_common.c \
            umts_hso.c umts_acm.c \
            umts_huawei.c

UMTS_OBJ := ${foreach file, ${patsubst %.c,%.o,${UMTS_SRC}},${file}}

SBIN_FILES := ${UMTS_CONFIG}
HOOK_FILES := umts_hso_net_up.sh umts_hso_net_down.sh \
              umts_acm_net_up.sh umts_acm_net_down.sh \
              umts_huawei_net_up.sh

INST_SBIN := install -D -m 0500
INST_HOOK := install -D -m 0500

HOOKS_PATH=/lib/umts/hooks

all: build

build: ${SBIN_FILES}

%.o:	%.c Makefile

${UMTS_CONFIG}: ${UMTS_OBJ} Makefile
	gcc $(CFLAGS) $(LDFLAGS)  -o ${UMTS_CONFIG} ${UMTS_OBJ}

install: install_sbin install_hooks

clean:
	rm -f "${UMTS_CONFIG}" ${UMTS_OBJ}


install_hooks:
	${foreach file, ${HOOK_FILES}, ${INST_HOOK} $(file) ${DESTDIR}${HOOKS_PATH}/$(file); }

install_sbin: ${SBIN_FILES}
	${foreach file, ${SBIN_FILES}, ${INST_SBIN} $(file) ${DESTDIR}/sbin/$(file); }

