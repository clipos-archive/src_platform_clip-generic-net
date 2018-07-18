// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright © 2008-2018 ANSSI. All Rights Reserved.
#include "umts_hso.h"
static void
hso_parse_owandata(fixed_buf answer, struct cdata *p_conn_data)
{
	size_t off = sizeof("_OWANDATA: 1, ") - 1;
	/*
	 *	_OWANDATA: <pdp context>, <ip address>, <route?>,
	 *	<nameserver 1>, <nameserver 2>, <unknown>, <unknown>, <speed>
	 */
	if (!strmatch(answer, "_OWANDATA: 1, "))
		ERROR(EPROTO, "expected _OWANDATA: 1, got %s", answer);

	get_ipaddr(p_conn_data->ip_address, answer, "ip_address", &off);
	get_ipaddr(p_conn_data->gateway, answer, "gateway", &off);
	get_ipaddr(p_conn_data->dns1, answer, "dns1", &off);
	get_ipaddr(p_conn_data->dns2, answer, "dns2", &off);
}

static void
hso_configure_net_down(char *interface)
{
	char *argv[] = {
		HSO_SCRIPT_DOWN,
		interface,
		NULL };

	LOG("Bringing down network on %s", interface);

	if (fork_exec(argv))
		ERROR(EFAULT, "Failed to run net_down script");
}

static void
hso_configure_net_up(char *interface, struct cdata *p_conn_data)
{
	char *argv[] = {
		HSO_SCRIPT_UP,
		interface,
		p_conn_data->ip_address,
		p_conn_data->gateway,
		p_conn_data->dns1,
		p_conn_data->dns2,
		NULL };

	LOG("Bringing up network: %s:%s, GW: %s, DNS: %s / %s",
		interface,
		p_conn_data->ip_address,
		p_conn_data->gateway,
		p_conn_data->dns1,
		p_conn_data->dns2);

	if (fork_exec(argv))
		ERROR(EFAULT, "Failed to run net_up script");
}

static int
hso_wait_obls(int comd)
{
	fixed_buf answer;
	unsigned int i = 0;
	char *ptr;

	while (i++ < 20) {
		if (send_receive(comd, "AT_OBLS", answer))
			return -1;
		if (strmatch(answer, "_OBLS: 1,1,1")) {
			LOG("Device initialized");
			return 0;
		}

		ptr = strchr(answer, ':');
		if (!ptr)
			ptr = answer;
		LOG("Waiting for device (status %s)", ptr);
		usleep(2 * UDELAY);
	}

	WARN("timeout waiting for subsystem");
	return -1;
}

/* Attend la connexion au réseau */
static int
hso_wait_reg_status(int comd)
{
	fixed_buf answer;
	unsigned int count;

	if (hso_wait_obls(comd))
		return -1;

	/* Mise de la carte en mode de sélection automatique */
	if (send_receive(comd, "AT+COPS=0", answer))
		return -1;

	/* Sélection du mode préférentiel 3G/2G */
	get_check_answer(comd, "AT_OPSYS=3", answer, "OK");

	/* Désactivation de la gestion automatique */
	get_check_answer(comd, "AT+CREG=0", answer, "OK");
	/*
	 * <n>
	 *
	 * 0. Disable unsolicited status callback.
	 * 1. Enable unsolicited status callback, +CREG: <stat>
	 * 2. Enable unsolicited status callback,
	 * 	+CREG: <stat>,[,<lac>,<ci>[,<AcT>]]
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
		/* si on n'est pas en recherche */
		} else if (!strmatch(answer, "+CREG: 0,2")) {
			ERROR(EPROTO, "CREG unexpected answer %s", answer);
		}
		usleep(UDELAY);
	}
	WARN("CREG timeout");
	return -1;
}

static int
hso_wait_owancall(int comd)
{
	fixed_buf tmp;
	int status;
	const char *str;
	unsigned int i;
	for (i = 0; i < 6; i++) {
		if (readcom(comd, tmp, 5 * UDELAY) <= 0)
			continue;

		if (!strmatch(tmp, "_OWANCALL: 1,")) {
			LOG("Unexpected connection status: %s", tmp);
			continue;
		}

		if (sscanf(tmp, "_OWANCALL: 1, %d", &status) != 1) {
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
			case 3:
				str = "Call failed";
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
hso_set_up_conn(int comd, struct cdata *p_conn_data, char *interface)
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
	buf_format_two_strings(cmd, "AT$QCPDPP=1,1,\"%s\",\"%s\"",
			p_conn_data->password, p_conn_data->identity);
	/*
	 * <auth_type>
	 * 0. None
	 * 1. PAP
	 * 2. CHAP
	 *
	 * <auth_name> and <auth_pwd> are strings with the
	 * 	authentication information.
	 */
	if (send_receive(comd, cmd, answer))
		ERROR(EFAULT, "Failed to get AT$QCPDPP answer");
	if (strmatch(answer, "OK")) {
		DBGV(2, "got QCPDPP answer");
	} else if (strmatch(answer, "ERROR")) {
		buf_format_two_strings(cmd, "AT_OPDPP=1,1,\"%s\",\"%s\"",
			p_conn_data->password, p_conn_data->identity);
		get_check_answer(comd, cmd, answer, "OK");
	} else {
		ERROR(EPROTO, "Unexpected AT$QCDPP answer: %s", answer);
	}


	get_check_answer(comd, "AT_OWANCALL=1,1,1", answer,"OK");
	count = 0;
	for (;;) {
		int ret;
		usleep(UDELAY);

		/* Now wait for an OWANCALL update */
		ret = hso_wait_owancall(comd);
		if (ret == 1)
			break;

		if (ret == 3)
			ERROR(ECALLFAILED, "Call failed");

		if (count++ >= 2)
			ERROR(EADDRNOTAVAIL, "Timed out waiting "
					"for expected connection status");
	}

	/*
	 * <pdp context> Existing, valid, PDP context that
	 * 	specifies the intended APN to connect to.
	 * <enabled> 1 = Enable connection,
	 * 	0 = Disable connection (disconnect)
	 * <callback enabled> 1 = Asynchronous callback
	 * 	when connection is established, 0 = silent
	 */

	count = 0;
	for (;;) {
		usleep(UDELAY);
		if (send_receive(comd, "AT_OWANDATA=1", answer))
			ERROR(EFAULT, "AT_OWANDATA error");
		if (strmatch(answer, "_OWANDATA: 1, "))
			break;
		if (count++ >= 15)
			ERROR(EADDRNOTAVAIL, "OWANDATA time-out");
	} while (!strmatch(answer, "_OWANDATA: 1, "));

	hso_parse_owandata(answer, p_conn_data);
	hso_configure_net_up(interface, p_conn_data);
}

static int
hso_init(__attribute__((unused)) int comd)
{
	return 0;
}

/* Vérifie si la liaison est établie, l'établit au besoin */
static void
hso_check_conn_up(int comd, struct cdata *p_conn_data, char *interface)
{
	fixed_buf answer;

	if (send_receive(comd, "AT_OWANDATA=1", answer))
		ERROR(EFAULT, "AT_OWANDATA error");

	if (strmatch(answer, "ERROR") || strmatch(answer, "OK")) { /* connexion non établie */
		hso_set_up_conn(comd, p_conn_data, interface);
	} else {
		/* reactivate interface with params */
		hso_parse_owandata(answer, p_conn_data);
		hso_configure_net_up(interface, p_conn_data);
	}
}

static void
hso_set_conn_down(int comd, char *interface)
{
	fixed_buf answer;
	unsigned int count = 0;

	for (;;) {
		get_check_answer(comd, "AT_OWANCALL=1,0", answer,"OK");

		/* Now wait for an OWANCALL update */
		if (!hso_wait_owancall(comd))
			break;
		if (count++ >= 2)
			ERROR(EADDRNOTAVAIL, "Timed out waiting "
					"for expected connection status");
	}
	hso_configure_net_down(interface);
}

/* Monitoring */
static int
hso_monitor_connection(int comd, const char *filename,
			const char *interface __attribute__((unused)),
			const char *ipsec)
{
	FILE *fd;
	fixed_buf operator, answer;
	int level = 5;
	int strength, quality, type;
	int ret;
	char sep;
	char *typestr, profile[256];
	size_t off, len;

	/* Lecture du nom Activation de la gestion automatique */
	get_check_answer(comd, "AT+COPS?", answer, "+COPS: 0,");
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
	 * Command: AT+CSQ
	 *  Response: +CSQ: <rssi>,<ber>
	 *  Description: Returns signal quality.
	 * <rssi> Received Signal Strength Indicator
	 * 0
	 * -113 dBm or less
	 * 1
	 * -111 dBm
	 * 2 to 30
	 * -109 to -53 dBm
	 * 31
	 * -51 dBm or greater
	 * 99
	 * not known or not detectable
	 * <ber> Bit Error Rate, in percent 0..7.
	 *  99 not known or not detectable
	 * A note on the RSSI (received signal strength), dBm is a decibel
	 * (logarithmic) scale with a reference of 1 milliwatt thus 0 dBm
	 * equals a received signal of 1 mW.
	 * Signal strength is usually lower than 1 mW and therefore below 0,
	 * so the larger (closer to 0) the better signal strength.
	 *  You can convert the RSSI to dBM with
	 *  dBm = (rssi \times 2) - 113
	 *
	 * < 10 is unreliable
	 * 15 is good
	 * 25 is excellent
	 *
	 * Codage retenu :
	 * 99 => 0
	 * 31 => 5
	 * 25-30 => 4
	 * 15-24 => 3
	 * 10-14 => 2
	 * 0-9 => 1
	 *
	 * si ber > 0, -1 sur le signal.
	 */
	get_check_answer(comd, "AT+CSQ", answer, "+CSQ: ");
	off = sizeof("+CSQ: ") - 1;
	ret = sscanf(answer + off,"%u,%u", &strength, &quality);
	if (ret < 2) {
		ERROR_ERRNO("sscanf failed for AT+CSQ");
		return(1);
	}

	if ((strength > 99) || (strength < 0))
		WARN("out of bounds strength %d", strength);
	else if (strength == 99)
		level = 0;
	else {
		if (strength > 31)
			WARN("out of bounds strength %d", strength);
		if (strength == 31)
			level = 5;
		else if (strength >= 25)
			level = 4;
		else if (strength >= 15)
			level = 3;
		else if (strength >= 10)
			level = 2;
		else
			level = 1;
		if (quality != 99) {
			if ((quality < 0) || (quality>7))
				WARN("out of bounds quality %d", quality);
			else if (quality > 0)
				level--;
		}
	}
	DBGV(2, "level: %d", level);
	/*
	 * Retro-conception hsoconnect
	 *
	 * WCDMA Cell Type Indicator \u201cAT_OWCTI\u201d
	 * Command   Possible Response(s)
	 * AT_OWCTI?   _OWCTI: <WCDMA Cell Type>
	 *
	 * Description
	 * Query command returns the current cell type, broadcasted by a cell
	 * in SIB 5 if compliant with 3GPP Rel. 6.
	 * Defined values
	 *
	 * <WCDMA Cell Type>:
	 * 0   Non-WCDMA cell type or not registered to cell
	 * 1   WCDMA only
	 * 2   WCDMA + HSDPA
	 * 3   WCDMA + HSUPA
	 * 4   WCDMA + HSDPA + HSUPA
	 *
	 * For the others...
	 * Search the main website www.pharscape.org.
	 *
	 * If you need official documentation you can contact Option.
	 * If you are a developer then they can supply AT command documentation.
	 * Note: commands beginning with "_" are created by Option.
	 * Commands beginning with "+" are defined in 3GPP specifications
	 * freely downloaded from the web.
	 */
	char *types3G[] = {
		"2G ?",
		"UMTS",
		"HSDPA",
		"HSUPA",
		"HS(D+U)PA"
	};

	/*
	 * Cell Type Indicator (GSM/EDGE) AT_OCTI
	 * Command                Possible Response(s)
	 * _OCTI=[mode]       OK
	 * _ OCTI?                 _ OCTI: mode,celltype
	 * _ OCTI=?               _ OCTI: (0-1)
	 *
	 * Description
	 * This command is used to get the Cell Type (GSM/GPRS/EDGE) and to
	 * enable the unsolicited result code _OCTI: celltype that is sent
	 * whenever the cell type changes.
	 *
	 * Defined values
	 * mode
	 * 0   Disable sending of the unsolicited result code
	 * 1   Enable sending of the unsolicited result code
	 *
	 * celltype
	 * 0   Unknown
	 * 1   GSM
	 * 2   GPRS
	 * 3   EDGE
	 */
	char *types2G[] = {
		"3G ?",
		"GSM",
		"GPRS",
		"EDGE"
	};
	/*
	 *
	 * HSDPA Call In Progress AT_OHCIP
	 *
	 * Command             Possible Response(s)
	 * _OHCIP=[mode]   ERROR
	 * _ OHCIP                _ OHCIP: status
	 * _ OHCIP?       _ OHCIP: status
	 * _ OCTI=?       OK
	 *
	 * Description
	 * This command can be used to see whether a HSDPA call is in progress.
	 * status will be set to 1 only when an HSDPA transaction is in
	 * progress (i.e. when an HS-DSCH transport channel is active). It is
	 * not possible to determine whether the cell supports HSDPA until
	 * HSDPA is being used by the datacard because the System Information
	 * messages broadcast by the network don\u2019t report HSDPA status.
	 *
	 * Defined values
	 *
	 * 0   HSDPA call not in progress
	 * 1   HSDPA call in progress
	 */

	get_check_answer(comd, "AT_OWCTI?", answer, "_OWCTI: ");
	off = sizeof("_OWCTI: ") - 1;
	ret = sscanf(answer + off,"%u",&type);
	if (ret < 1) {
		ERROR_ERRNO("sscanf failed for AT+OWCTI");
		return(1);
	}

	if ((type <= 0) || ((unsigned)type >= sizeof(types3G)/sizeof(char *))) {
		get_check_answer(comd, "AT_OCTI?", answer, "_OCTI: ");
		off = sizeof("_OCTI: ") - 1;
		ret = sscanf(answer + off, "%zd,%u", &off /*foo*/ , &type);
		if (ret < 2) {
			ERROR_ERRNO("sscanf failed for AT+OCTI");
			return(1);
		}

		if ((type < 0) || ((unsigned)type
				>= sizeof(types2G)/sizeof(char *)))
			type=0;
		typestr = types2G[type];
	} else
		typestr = types3G[type];
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

umts_device_t hso_device =
{
	.name = "HSO",
	.device = "/dev/ttyHS1",
	.interface = "hso0",
	.init = hso_init,
	.check_conn_up = hso_check_conn_up,
	.wait_reg_status = hso_wait_reg_status,
	.set_conn_down = hso_set_conn_down,
	.monitor_connection = hso_monitor_connection,
};


