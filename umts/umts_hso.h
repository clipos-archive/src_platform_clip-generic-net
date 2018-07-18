// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright © 2008-2018 ANSSI. All Rights Reserved.
#ifndef UMTS_HSO_H
#define UMTS_HSO_H

#include "umts.h"

#define HSO_SCRIPT_UP 	HOOKS_DIR"/umts_hso_net_up.sh"
#define HSO_SCRIPT_DOWN	HOOKS_DIR"/umts_hso_net_down.sh"

static const char HSO_DEVICE[]="/dev/ttyHS1";

extern umts_device_t hso_device;
#endif /* UMTS_HSO_H */
