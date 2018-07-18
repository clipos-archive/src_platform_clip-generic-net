// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright © 2008-2018 ANSSI. All Rights Reserved.
#ifndef UMTS_HUAWEI_H
#define UMTS_HUAWEI_H

#include "umts.h"
#include <arpa/inet.h>

#define HUAWEI_SCRIPT_UP 	HOOKS_DIR"/umts_huawei_net_up.sh"
#define HUAWEI_SCRIPT_DOWN	HOOKS_DIR"/umts_hso_net_down.sh" /* same as HSO */

extern umts_device_t huawei_device;
#endif /* UMTS_HUAWEI_H */
