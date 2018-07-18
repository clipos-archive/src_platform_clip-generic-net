// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright Â© 2008-2018 ANSSI. All Rights Reserved.
#include "umts_huawei.h"

/* umts_config module for Huawei / Option 3G modules */

/**********************/
/* Internal functions */
/**********************/

static void
huawei_parse_dhcp(fixed_buf answer, struct cdata *p_conn_data)
{
	uint32_t ip_address, mask, gateway, dhcp, dns1, dns2, unk1, unk2;
	if (sscanf(answer, "^DHCP:%x,%x,%x,%x,%x,%x,%d,%d",
		&ip_address, &mask, &gateway, &dhcp, &dns1, &dns2, &unk1, &unk2) != 8)
		ERROR_ERRNO("Unreadable connection parameters: %s", answer);

	if (!inet_ntop(AF_INET, &ip_address, p_conn_data->ip_address, MAX_LEN))
		ERROR_ERRNO("Error parsing IP address: %x.", ip_address);

	if (!inet_ntop(AF_INET, &mask, p_conn_data->mask, MAX_LEN))
		ERROR_ERRNO("Error parsing subnet mask: %x.", mask);

	if (!inet_ntop(AF_INET, &gateway, p_conn_data->gateway, MAX_LEN))
		ERROR_ERRNO("Error parsing gateway address: %x.", gateway);

	if (!inet_ntop(AF_INET, &dns1, p_conn_data->dns1, MAX_LEN))
		ERROR_ERRNO("Error parsing DNS1: %x.", dns1);

	if (!inet_ntop(AF_INET, &dns2, p_conn_data->dns2, MAX_LEN))
	{
		WARN("Error parsing DNS2: %x.", dns2);
	}

	DBGV(2, "Connection parameters: IP=%s/%s GW=%s DNS1=%s DNS2=%s\n",
		p_conn_data->ip_address,
		p_conn_data->mask,
		p_conn_data->gateway,
		p_conn_data->dns1,
		p_conn_data->dns2);
}

static int
huawei_wait_dhcp(int comd, struct cdata *p_conn_data)
{
	fixed_buf tmp;
	int ip, mask, gw, dhcp, dns1, dns2, unk1, unk2;
	unsigned int i;
	for (i = 0; i < 6; i++) {
		send_receive(comd, "AT^DHCP?", tmp);
		if (strmatch(tmp, "+CME ERROR:")) {
			LOG("No connection yet\n");
			usleep(UDELAY);
			continue;
		}

		if (sscanf(tmp, "^DHCP:%x,%x,%x,%x,%x,%x,%d,%d",
			&ip, &mask, &gw, &dhcp, &dns1, &dns2, &unk1, &unk2) != 8) {
			LOG("Unreadable connection parameters: %s", tmp);
			continue;
		}
    huawei_parse_dhcp(tmp, p_conn_data);
		return 0;
	}
	ERROR(EADDRNOTAVAIL, "Timed out waiting for connection status update");
}

static void
huawei_configure_net_down(char *interface)
{
	char *argv[] = {
		HUAWEI_SCRIPT_DOWN,
		interface,
		NULL };

	LOG("Bringing down network on %s", interface);

	if (fork_exec(argv))
		ERROR(EFAULT, "Failed to run net_down script");
}

static void
huawei_configure_net_up(char *interface, struct cdata *p_conn_data)
{
	char *argv[] = {
		HUAWEI_SCRIPT_UP,
		interface,
		p_conn_data->ip_address,
		p_conn_data->mask,
		p_conn_data->gateway,
		p_conn_data->dns1,
		p_conn_data->dns2,
		NULL };

	LOG("Bringing up network: %s:%s/%s, GW: %s, DNS: %s / %s",
		interface,
		p_conn_data->ip_address,
		p_conn_data->mask,
		p_conn_data->gateway,
		p_conn_data->dns1,
		p_conn_data->dns2);

	if (fork_exec(argv))
		ERROR(EFAULT, "Failed to run net_up script");
}

static void
huawei_set_up_conn(int comd, struct cdata *p_conn_data, char *interface)
{
	fixed_buf cmd, answer;

	LOG("Registering with APN");
	buf_format_string(cmd, "AT^NDISDUP=1,1,\"%s\"", p_conn_data->apn);
	get_check_answer(comd, cmd, answer, "OK");
	DBGV(2, "got NDISDUP answer");

	huawei_wait_dhcp(comd, p_conn_data);
	huawei_configure_net_up(interface, p_conn_data);
}

/**********************/
/* External functions */
/**********************/

static int huawei_init(int comd)
{
	writecom(comd, "ATE");
	return 0;
}
/* Vérifie si la liaison est établie, l'établit au besoin */
static void
huawei_check_conn_up(int comd, struct cdata *p_conn_data, char *interface)
{
	fixed_buf answer;

	if (send_receive(comd, "AT^DHCP?", answer))
		ERROR(EFAULT, "AT^DHCP error");

  DBGV(2, "answer = %s", answer);

	if(strmatch(answer, "+CME ERROR:"))
		/* interface not up, configure it */
		huawei_set_up_conn(comd, p_conn_data, interface);
	else if (strmatch(answer, "^DHCP:"))
	{
		/* reactivate interface with params */
		huawei_parse_dhcp(answer, p_conn_data);
		huawei_configure_net_up(interface, p_conn_data);
	}
	else
		ERROR(EFAULT, "AT^DHCP error");
}

static int
huawei_wait_reg_status(int comd)
{
	fixed_buf answer;
	unsigned int count;

	/* Mise de la carte en mode de sélection automatique */
	if (send_receive(comd, "AT+CFUN=1", answer))
		return -1;

  if (strmatch(answer, "ERROR"))
      ERROR(EADDRNOTAVAIL, "Cant activate radio");

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
huawei_set_conn_down(int comd, char *interface)
{
	fixed_buf answer;

	send_receive(comd, "AT^NDISDUP=1,0", answer);
	if (!strmatch(answer,"OK") &&
      !strmatch(answer,"ERROR") &&
      !strmatch(answer,"+CME ERROR"))
		ERROR(EFAULT, "AT^NDISDUP error");

  /* AT+CFUN=4 is a bad idea, it's not possible to go to 1 later without reseting the modem */
	get_check_answer(comd, "AT+CFUN=7", answer, "OK");
	huawei_configure_net_down(interface);
}

static int
huawei_monitor_connection(int comd, const char *filename,
			const char *interface __attribute__((unused)),
			const char *ipsec)
{
	FILE *fd;
	fixed_buf operator, answer;
	int level = 5;
	int ret;
	char sep;
	char profile[256];
	size_t off, len;
	fixed_buf mode, submode;

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

	/* System information query
	 * Command: AT^SYSINFOEX
	 * ^SYSINFOEX:<srv_status>,<srv_domain>,<roam_status>,<sim_state>,<reserved>,<sysmode>,<sysmode_name>,<submode>,<submode_name>
	 *
	 * <srv>status>: System service state
	 * 	0. no service
	 * 	1. restricted service
	 * 	2. valid service
	 * 	3. restricted regional service
	 * 	4. power-saving and deep sleep state
	 *
	 * 	<srv_domain>: System service domain
	 * 	0. no service
	 * 	1. only CS service
	 * 	2. only PS service
	 * 	3. PS+CS service
	 * 	4. CS and PS not registered, searching
	 *
	 * 	<roam_status>: Roaming status
	 * 	0. non roaming state
	 * 	1. romaing state
	 *
	 * 	<sim_state>: SIM card state
	 * 	0. invalid USIM card state or pin code locked
	 * 	1. valid USIM card state
	 * 	2. USIM is invalid in case of CS
	 * 	3. USIM is invalid in case of PS
	 * 	4. USIM is invalid in case of either CS or PS
	 * 	240. ROMSIM
	 * 	255. USIM card is not existent
	 *
	 * 	<reserved>: reserved. E618 usied it to indicate the simlock state
	 *
	 * 	<sysmode>: System mode
	 * 	0. no service
	 * 	1. GSM
	 * 	2. CDMA
	 * 	3. WCDMA
	 * 	4. TD-SCDMA
	 * 	5. WIMAX
	 * 	6. LTE
	 *
	 * 	<sysmode_name>: Sytem mode as a string
	 *
	 * 	<submode> Sytem sub mode
	 * 	0. no service
	 * 	1. GSM
	 * 	2. GPRS
	 * 	3. EDGE
	 * 	4. undefined
	 * 	5. ...
	 * 	20. undefined
	 * 	21. IS95A
	 * 	22. IS95B
	 * 	23. CDMA2000 1X
	 * 	24. EVDO Rel0
	 * 	25. EVDO RelA
	 * 	26. EVDO RelB
	 * 	27. Hybrid (CDMA 2000 1X)
	 * 	28. Hybrid (EVDO Rel0)
	 * 	29. Hybrid (EVDO RelA)
	 * 	30. Hybrid (EVDO RelB)
	 * 	31. undefined
	 * 	...
	 * 	40. undefined
	 * 	41. WCDMA
	 * 	42. HSDPA
	 * 	43. HSUPA
	 * 	44. HSPA
	 * 	45. HSPA+
	 * 	47. undefined
	 * 	...
	 * 	60. undefined
	 * 	61. TD-SCDMA
	 * 	62. HSDPA
	 * 	63. HSUPA
	 * 	64. HSPA
	 * 	65. HSPA+
	 * 	66. undefined
	 * 	...
	 * 	80. undefined
	 * 	81. 802.16e
	 * 	82. undefined
	 * 	...
	 * 	100. undefined
	 * 	101. LTE
	 * 	102. undefined
	 * 	...
	 * 	140. undefined
	 *
	 * 	<submode_name> System sub mode as a string
	 */

	get_check_answer(comd, "AT^SYSINFOEX", answer, "^SYSINFOEX:");
	ret = sscanf(answer, "^SYSINFOEX:%*d,%*d,%*d,%*d,,%*d,\"%[^\"]\",%*d,\"%[^\"]\"", mode, submode);
	if (ret < 2)
		ERROR_ERRNO("sscanf failed for AT^SYSINFOEX");

	DBGV(2, "net: %s/%s", mode, submode);
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
	fprintf(fd, "%s (%s/%s)\n", operator, mode, submode);
	return close_file(filename, fd);
}

umts_device_t huawei_device =
{
	.name = "Huawei/Option",
	.device = "/dev/ttyUSB0",
	.interface = "wwan0",
	.init = huawei_init,
	.check_conn_up = huawei_check_conn_up,
	.wait_reg_status = huawei_wait_reg_status,
	.set_conn_down = huawei_set_conn_down,
	.monitor_connection = huawei_monitor_connection,
};
