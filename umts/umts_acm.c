// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright Â© 2008-2018 ANSSI. All Rights Reserved.
#include "umts_acm.h"

/* umts_config module for 3G modules based on ACM/NCM modules */

/**********************/
/* Internal functions */
/**********************/

static int
acm_wait_emrdy(int comd)
{
	unsigned int i;
	fixed_buf tmp;
	int ret;

	for (i = 0; i < 5; i++) {
		ret = readcom(comd, tmp, UDELAY);
		if (ret < 0)
			return ret;
		if (strlen(tmp))
			goto answer;
		}

answer:
	if (!strmatch(tmp, "*EMRDY: 1")) {
		DBG("expected EMRDY: 1, got %s", tmp);
		return -1;
	}
	return 0;
}

static int
acm_wait_enap(int comd)
{
	fixed_buf tmp;
	int status;
	const char *str;
	unsigned int i;
	for (i = 0; i < 6; i++) {
		/* AT*ENAP? <status>
		 *  0. Not connected
		 *  1. Connected
		 *  2. Connection setup in progress
		 */

		if (send_receive(comd, "AT*ENAP?", tmp))
			continue;

		if (!strmatch(tmp, "*ENAP:")) {
			LOG("Unexpected connection status: %s", tmp);
			continue;
		}

		if (sscanf(tmp, "*ENAP:%d,\"\"", &status) != 1) {
			LOG("Unreadable connection status: %s", tmp);
			continue;
		}
		switch (status) {
			case 0:
				str = "Disconnected";
				break;
			case 1:
				str = "Connected";
				break;
			case 2:
				str = "In setup";
				break;
			default:
				str = "Unknown";
				break;
		}
		LOG("New UMTS connection status: %s (%d)", str, status);
		return status;
	}

	ERROR(EADDRNOTAVAIL, "Timed out waiting for connection status update");
}

static void
acm_configure_net_down(char *interface)
{
	char *argv[] = {
		ACM_SCRIPT_DOWN,
		interface,
		NULL };

	LOG("Bringing down network on %s", interface);

	if (fork_exec(argv))
		ERROR(EFAULT, "Failed to run net_down script");
}

static void
acm_configure_net_up(char *interface)
{
	char *argv[] = {
		ACM_SCRIPT_UP,
		interface,
		NULL };

	LOG("Bringing up network: %s", interface);

	if (fork_exec(argv))
		ERROR(EFAULT, "Failed to run net_up script");
}

static void
acm_set_up_conn(int comd, struct cdata *p_conn_data, char *interface)
{
	fixed_buf cmd, answer;
	unsigned int count = 0;

	LOG("Registering with APN");
	buf_format_string(cmd, "AT+CGDCONT=1,\"IP\",\"%s\"", p_conn_data->apn);
	/*
	 * <cid> PDP context ID, minimum value is 1,
	 * 	maximum value depends on device and can be
	 * 	found with the =? command.
	 * <pdptype> String parameter identifying the
	 * 	protocol type :
	 * 	IP Internet Protocol
	 * 	IPV6 Internet Protocol, version 6
	 * 	PPP	Point to Point Protocol
	 * <apn> String that identifies the Access Point Name
	 * 	in the packet data network.
	 */
	get_check_answer(comd, cmd, answer, "OK");
	DBGV(2, "got CGDCONT answer");

	buf_format_two_strings(cmd, "AT*EIAAUW=1,1,\"%s\",\"%s\",1,0",
			p_conn_data->password, p_conn_data->identity);
	/*
	 * AT*EIAAUW: Ericsson Internet Account: write authentication parameters
	 * <index>
	 * <bearer_type>
	 *  1. PS
	 *  2. CS
	 *  3. Bluetooth
	 *  4. External
	 * <userid>
	 * <password>
	 * <auth_prot>
	 *  0. None
	 *  1. PAP
	 *  2. CHAP
	 *  3. MS-CHAP
	 *  4. MS-CHAPv2
	 * <ask4pwd>
	 *  0. No
	 *  1. Yes
	 */
	if (send_receive(comd, cmd, answer))
		ERROR(EFAULT, "Failed to get AT*EIAAUW answer");
	if (strmatch(answer, "OK")) {
		DBGV(2, "got EIAAUW answer");
	} else {
		ERROR(EPROTO, "Unexpected EIAAUW answer: %s", answer);
	}
	send_receive(comd, "AT*EIAAUR=1,1", answer);

	/*
	 * AT*ENAP: Undocumented Ericsson USB Ethernet Interface Control
	 * <state>
	 *  0. Disconnect
	 *  1. Connect
	 * <index> PDP context / Internet account index
	 */

	get_check_answer(comd, "AT*ENAP=1,1", answer,"OK");
	count = 0;
	for (;;) {
		int ret;
		usleep(UDELAY);

		/* Now wait for the connection */
		ret = acm_wait_enap(comd);
		if (ret == 1)
			break;

		if (count++ >= 5)
			ERROR(EADDRNOTAVAIL, "Timed out waiting "
					"for expected connection status");
	}

	acm_configure_net_up(interface);
}

/**********************/
/* External functions */
/**********************/

static int acm_init(int comd)
{
	return acm_wait_emrdy(comd);
}

/* Vérifie si la liaison est établie, l'établit au besoin */
static void
acm_check_conn_up(int comd, struct cdata *p_conn_data, char *interface)
{
	fixed_buf answer;
	unsigned int status;

	if (send_receive(comd, "AT*ENAP?", answer))
		ERROR(EFAULT, "AT*ENAP error");

	if(!strmatch(answer, "*ENAP:"))
		ERROR(EFAULT, "AT*ENAP error");

	sscanf(answer, "*ENAP:%u,\"\"", &status);
	switch(status)
	{
		case 0: /* not connected */
			acm_set_up_conn(comd, p_conn_data, interface);
			break;
		case 1: /* connected */
			/* reactivate interface with params */
			acm_configure_net_up(interface);
			break;
		case 2: /* connection setup in progress */
			usleep(UDELAY);
			acm_configure_net_up(interface);
			break;
	}
}

static int
acm_wait_reg_status(int comd)
{
	fixed_buf answer;
	unsigned int count;

	/* Mise de la carte en mode de sélection automatique */
	if (send_receive(comd, "AT+CFUN=1", answer))
		return -1;

	/* Désactivation de la gestion automatique */
	get_check_answer(comd, "AT+CREG=0", answer, "OK");
	/*
	 * AT+CREG=<n>
	 *  <n>
	 *    0. Disable unsolicited status callback.
	 *    1. Enable unsolicited status callback, +CREG: <stat>
	 *    2. Enable unsolicited status callback,
	 * +CREG: <stat>,[,<lac>,<ci>]
	 *  <stat>
	 *    0. Not registered, not searching
	 *    1. Registered, home network
	 *    2. Not registered, searching
	 *    3. Registration denied
	 *    4. Unknown
	 *    5. Registered, roaming
	 */
	for (count = 0; count < 10; count++) {
		if (send_receive(comd, "AT+CREG?", answer))
			return -1;
		/* enregistré sur le réseau natif */
		if (strmatch(answer, "+CREG: 0,1")) {
			LOG("Registered with network, native");
			return 0;
		}
		/* enregistré en roaming */
		if (strmatch(answer, "+CREG: 0,5")) {
			LOG("Registered with network, roaming");
			return 0;
		}
		/* enregistrement interdit */
		if (strmatch(answer, "+CREG: 0,3")) {
			WARN("CREG permission denied");
			return -1;
		}
		/* non enregistré, inactif */
		/* on vient de rentrer le PIN, on double le temps d'attente */
		if (strmatch(answer, "+CREG: 0,0")) {
			DBG("Not registered with network yet");
			usleep(UDELAY);
			/* unknown, semble se produire une fois lorsqu'on monte l'interface pour la
			 * première fois */
			} else if (strmatch(answer, "+CREG: 0,4")) {
			DBG("Unknown CREG answer, wait a little and retry");
			usleep(UDELAY);
		/* si on n'est pas en recherche */
		} else if (!strmatch(answer, "+CREG: 0,2")) {
			ERROR(EPROTO, "CREG unexpected answer %s", answer);
		}
		usleep(UDELAY);
	}
	WARN("CREG timeout");
	return -1;
}

static void
acm_set_conn_down(int comd, char *interface)
{
	fixed_buf answer;
	unsigned int count = 0;

	while (acm_wait_enap(comd)) {
		send_receive(comd, "AT*ENAP=0", answer);
		if (!(strmatch(answer,"OK")) && !strmatch(answer,"ERROR"))
			ERROR(EFAULT, "AT*ENAP error");

		if (count++ >= 5)
			ERROR(EADDRNOTAVAIL, "Timed out waiting "
					"for expected connection status");
	}
	get_check_answer(comd, "AT+CFUN=4", answer, "OK");
	acm_configure_net_down(interface);
}

static int
acm_monitor_connection(int comd, const char *filename,
			const char *interface __attribute__((unused)),
			const char *ipsec)
{
	FILE *fd;
	fixed_buf operator, answer;
	int level = 5;
	int type;
	int ret;
	char sep;
	char *typestr, profile[256];
	size_t off, len;

	/* Lecture du nom Activation de la gestion automatique */
	get_check_answer(comd, "AT+COPS?", answer, "+COPS: ");
	/* +COPS: 0,0,"Orange F",2
	 * <mode>
	 *
	 *  0. Automatic network selection (<oper> ignored)
	 *  1. Manual network selection, <oper> must be present,
	 *  	<AcT> is optional.
	 *  2. Deregister from network.
	 *  3. Set <format only, no registration/deregistration.
	 *  4. Manual selection with automatic fall back
	 *  	(enters automatic mode if manual selection fails).
	 *
	 * <format>
	 *
	 *  0. Long alphanumeric string
	 *  1. Short alphanumeric string
	 *  2. Numeric ID
	 *
	 * <oper>
	 * String (based on <format>) that identifies the operator.
	 *
	 * <stat>
	 *
	 *  0. Unknown
	 *  1. Available
	 *  2. Current
	 *  3. Forbidden
	 */

	off = sizeof("+COPS: 0,0") - 1;
	if (answer[off] != ',')
		ERROR(EFAULT,
			"operator, expected ',' at %zd in %s", off, answer);
	off++;
	if (answer[off] == '"') {
		sep = '"';
		off++;
	} else {
		sep = ',';
	}
	buf_cpy(operator, answer + off);
	DBGV(2, "operator: %s (raw)", operator);

	len = strlen(operator);
	for (off = 0; off < len; off++) {
		if (operator[off] == sep) {
			operator[off] = '\0';
			break;
		}
	}
	DBGV(2, "operator: %s", operator);

	/* Lecture de la qualité du signal
	 *
	 * Command: AT+CIND
	 * +CIND:	("battchg",(0-5)),
						("signal",(0-5)),
						("batterywarning",(0-1)),
						("chargerconnected",(0-1)),
						("service",(0-1)),
						("sounder",(0-1)),
						("message",(0-1)),
						("call",(0-1)),
						("roam",(0-1)),
						("smsfull",(0-1)),
						("callsetup",(0-3)),
						("callheld",(0-1))
	 */
	get_check_answer(comd, "AT+CIND?", answer, "+CIND: ");
	off = sizeof("+CIND: 0,") - 1;
	ret = sscanf(answer + off,"%u,", &level);
	if (ret < 1) {
		ERROR_ERRNO("sscanf failed for AT+CIND");
		return(1);
	}

	DBGV(2, "level: %d", level);

	char *types2G[] = {
		"GSM",
		"GPRS",
		"EDGE"
	};
	char *types3G[] = {
		"2G ?",
		"UMTS",
		"HSDPA"
	};

	/* AT*ERINFO?: <gsm_rinfo> <umts_info>
	 * <gsm_rinfo>
	 * 0. No GPRS or EGPRS available
	 * 1. GPRS service is available
	 * 2. EGPRS service is available
	 * <umts_info>
	 * 0. No UMTS or HSDPA service available
	 * 1. UMTS service available
	 * 2. HSDPA service available
	 */
	get_check_answer(comd, "AT*ERINFO?", answer, "*ERINFO: ");
	off = sizeof("*ERINFO: 0,") - 1;
	ret = sscanf(answer + off,"%u",&type);
	if (ret < 1) {
		ERROR_ERRNO("2G sscanf failed for AT*ERINFO");
		return(1);
	}

	if ((type <= 0) || ((unsigned)type >= sizeof(types2G)/sizeof(char *))) {
		off = sizeof("*ERINFO: 0,0,") - 1;
		ret = sscanf(answer + off, "%u", &type);
		if (ret < 1) {
			ERROR_ERRNO("3G sscanf failed for AT*ERINFO");
			return(1);
		}

		if ((type < 0) || ((unsigned)type >= sizeof(types3G)/sizeof(char *)))
			typestr= types2G[0];
		else
			typestr = types3G[type];
	} else
		typestr = types2G[type];

	DBGV(2, "net: %s", typestr);
	fd = open_file(filename, WriteMode);
	if (!fd) {
		ERROR_ERRNO("can't open report file %s", filename);
		return(1);
	}
	len = readlink(CONFLINK, profile, sizeof(profile) - 1);
	if(len > 0) {
		profile[len] = '\0';
		fprintf(fd, "profile: %s\n", basename(profile));
	} else {
		fprintf(fd, "profile: \n");
	}
	fprintf(fd, "ipsec: %s\n", ipsec);
	fprintf(fd, "type: umts\n");
	fprintf(fd, "level: %d\n", level);
	fprintf(fd, "%s (%s)\n", operator, typestr);
	return close_file(filename, fd);
}

umts_device_t acm_device =
{
	.name = "ACM",
	.device = "/dev/ttyACM1",
	.interface = "wwan0",
	.init = acm_init,
	.check_conn_up = acm_check_conn_up,
	.wait_reg_status = acm_wait_reg_status,
	.set_conn_down = acm_set_conn_down,
	.monitor_connection = acm_monitor_connection,
};
