// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright Â© 2008-2018 ANSSI. All Rights Reserved.
/*
 *	umts_config - 3G connectivity for CLIP
 *	Copyright (C) 2009-2011 SGDSN/ANSSI
 *	Author: Florent Chabaud <clipos@ssi.gouv.fr>
 *	Modified : Vincent Strubel <clipos@ssi.gouv.fr>
 *
 *	This program was written from scratch but inspired by
 *	comgt version 0.31 - 3G/GPRS datacard management utility
 *
 *	Copyright (C) 2003	Paul Hardwick <paul@peck.org>
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	This program is distributed in the hope that it will be useful,
 *	but WITHOUT ANY WARRANTY; without even the implied warranty of
 *	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	See the
 *	GNU General Public License for more details.
 *
 *	You should have received a copy of the GNU General Public License
 *	along with this program; if not, write to the Free Software
 *	Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA	02111-1307	USA
 */

#include "umts.h"

/*********************************************************/
/** Programme principal **/
/*********************************************************/
int
main(int argc, char *argv[])
{
	const char *filename;
	char *interface;
	const char *cmd;
	struct stat buf;
	struct cdata conn_data;
	int comd;
	FILE *fd;
	umts_device_t *umts_device;
	const char *type;
	
	/* Quatre arguments - cinq pour check */
	if (argc > 6)
		ERROR(E2BIG, "too many arguments (%d)", argc);
	if (argc < 5)
		ERROR(EIO, "too few arguments (%d)", argc);

	filename = argv[1];
	type = argv[2];
	interface = argv[3];
	cmd = argv[4];

	if (strmatch(type, "hso"))
		umts_device = &hso_device;
	else if (strmatch(type, "cdc_ncm"))
		umts_device = &acm_device;
	else if (strmatch(type, "qmi_wwan"))
		umts_device = &huawei_device;
	else
		ERROR(EUNSUPDEV, "unsupported device type: %s", type);

	/* On vérifie que le device de contrôle est présent */
	if (stat(umts_device->device, &buf))
		ERROR_ERRNO("can't stat device %s", umts_device->device);

	if (!S_ISCHR(buf.st_mode))
		ERROR(ENODEV,
			"%s is not the device we're looking for", umts_device->device);

	openlog("umts_config", LOG_PERROR|LOG_PID, LOG_DAEMON);

	DBG("detected interface type: %s", umts_device->name);

	/* Validation des paramètres 2 et 3 */
	if (!(strmatch(cmd, "up")) && !(strmatch(cmd, "down"))
					&& !(strmatch(cmd, "check")))
		ERROR(EINVAL, "unsupported command : %s", cmd);

	if(!(strmatch(interface, umts_device->interface)) && !(strmatch(interface, "eth0")))
		ERROR(EINVAL, "unsupported interface name : %s", interface);

			/* Validation du paramètre 1 */
	if (stat(filename, &buf))
		ERROR_ERRNO("can't stat config file %s", filename);

	if (!S_ISREG(buf.st_mode))
		ERROR(EINVAL, "config file %s is not a regular file", filename);

	if (strmatch(cmd, "check")) {
		DBG("checking interface %s", interface);
		if (argc < 5)
			ERROR(EIO, "too few arguments (%d)", argc);
		comd = initiate_serial(umts_device->device);
		umts_device->monitor_connection(comd, filename, interface, argv[5]);
		close_serial(comd);
	} else if (strmatch(cmd, "up")) {
		LOG("setting interface %s up", interface);
		fd = open_file(filename, ReadMode);
		if (!fd)
			ERROR_ERRNO("can't open config file %s", filename);
	
		parse_conf(fd, &conn_data);
		close_file(filename, fd);

		comd = initiate_serial(umts_device->device);
		if (check_pin_status(comd, &conn_data))
			ERROR(EPROTO, "Error checking PIN");
		umts_device->wait_reg_status(comd);

		umts_device->check_conn_up(comd, &conn_data, interface);
		close_serial(comd);
	} else if (strmatch(cmd, "down")) {
		LOG("setting interface %s down", interface);
		comd = initiate_serial(umts_device->device);
		if (!check_pin_status(comd, &conn_data))
			umts_device->set_conn_down(comd, interface);
		close_serial(comd);
	}
	closelog();
	return EXIT_SUCCESS;
}
