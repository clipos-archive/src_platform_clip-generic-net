// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright © 2008-2018 ANSSI. All Rights Reserved.
#include "umts.h"

/*********************************************************/
/** Ouverture/fermeture de fichier                      **/
/*********************************************************/
inline FILE *
open_file(const char *path, omode writelock)
{
	int f, operation;
	FILE *filp;
	char *mode;

	if (writelock) {
		f = open(path, O_WRONLY|O_CREAT|O_NOFOLLOW,
			S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH);
		operation = LOCK_EX;
		mode="w";
	} else {
		f = open(path, O_RDONLY|O_NOFOLLOW);
		operation = LOCK_SH;
		mode="r";
	}
	if (f < 0) {
		ERROR_ERRNO("failed to open file %s", path);
		return NULL;
	}

	if (flock(f, operation)) {
		ERROR_ERRNO("failed to lock file %s", path);
		(void)close(f);
		return NULL;
	}

	filp = fdopen(f, mode);
	if (!filp) {
		ERROR_ERRNO("failed to fdopen %s", path);
		(void)close(f);
		return NULL;
	}
	return filp;
}

inline int
close_file(const char *path, FILE *fd)
{
		if (flock(fileno(fd), LOCK_UN)) {
		ERROR_ERRNO("failed to unlock file %s", path);
		return 1;
	}
	if (fclose(fd)) {
		ERROR_ERRNO("failed to close file %s", path);
		return 1;
	}
	return 0;
}

/*********************************************************/
/** Parsing du fichier de configuration **/
/*********************************************************/
/* Vérification de la présence de *var en début de flux
 * La chaîne var ne doit comporter aucun espace
 * La chaîne var doit être plus petite qu'un fixed_buf
 */

inline void
check_var(FILE *fd, const char *var)
{
	fixed_buf buf;
	fixed_buf format;
	int ret;

	if (!var || !*var)
		ERROR(EIO, "empty var name");

	buf_format(format, "%%%ds", MAX_LEN -1);
	ret = fscanf(fd, format, buf);
	if (ret != 1)
		ERROR(EFAULT, "failed to read var %s", var);

	if (strncmp(var, buf, strlen(var)))
		ERROR(EIO, "incorrect var read: %s != %s", var, buf);
 }

/* Extraction de la valeur située après *var et sauvegarde dans *buf
	 On attend une chaîne entre guillemets sans échappement possible
	 d'un caractère guillemet.
	 La chaîne var n'est utilisée que dans les messages d'erreur. */

inline void
get_var(FILE *fd, const char *var, fixed_buf buf)
{
	unsigned int i = 0;
	int c;

	c = fgetc(fd);
	if (c == EOF)
		ERROR(EIO, "%s: unexpected EOF", var);
	if (c != ' ')
		ERROR(EINVAL, "%s: expected whitespace, got 0x%x", var, c);

	c = fgetc(fd);
	if (c == EOF)
		ERROR(EIO, "%s: unexpected EOF", var);
	if (c != '"')
		ERROR(EINVAL, "%s: expected \", got 0x%x", var, c);

	/* on va jusqu'au bout de la ligne */
	do {
		c = fgetc(fd);
		if (c == EOF)
			ERROR(EIO, "%s: unexpected EOF", var);
		buf[i++] = c;
		if (i >= MAX_LEN)
			ERROR(EINVAL, "%s: buffer too short for %.*s",
							var, MAX_LEN, buf);
	} while (c != '"');
	i--; 				/* on se replace sur le guillemet */
	buf[i] = 0;
}

/* Parsing du fichier de configuration et sauvegarde dans la strucuture
	 pointée par p_conn_data.
*/
void
parse_conf(FILE *fd, struct cdata *p_conn_data)
{
	fixed_buf buf, format;
	unsigned int i, j;
	int ret;
	unsigned long val;
	char *eptr = NULL;

	/* Le fichier de configuration est parsé de façon très stricte */
	/* pin: ABCD */
	check_var(fd, "pin:");

	buf_format(format, "%%%ds", MAX_LEN - 1);

	ret = fscanf(fd, format, buf);
	if (ret != 1)
		ERROR(EFAULT, "read pin ret: %d", ret);

	/* conversion numérique du pin */
	val = strtoul(buf, &eptr, 10);
	if (val == ULONG_MAX && errno == ERANGE)
		ERROR(EINVAL, "pin value too big: %s", buf);
	if (eptr && *eptr)
		ERROR(EINVAL, "trailing chars after pin: %s", eptr);

	/* modulo 10 à la puissance le nombre de chiffres */
	for (i = 1, j = 0; j < SIZE_PIN; j++)
		i *= 10; /* exp10(SIZE_PIN) */
	if (val >= i)
		ERROR(EINVAL, "invalid pin: %s", buf);

	pinbuf_cpy(p_conn_data->pin, buf);
	DBGV(2, "pin: %s", p_conn_data->pin);


	/* apn: "toto foo" */
	check_var(fd, "apn:");
	get_var(fd, "apn", p_conn_data->apn);
	DBGV(2, "apn: %s", p_conn_data->apn);

	/* identity: "toto foo" */
	check_var(fd, "identity:");
	get_var(fd, "identity", p_conn_data->identity);
	DBGV(2, "identity: %s", p_conn_data->identity);

	/* password: "toto foo" */
	check_var(fd, "password:");
	get_var(fd, "password", p_conn_data->password);
	DBGV(2,  "password: %s", p_conn_data->password);
}

/*********************************************************/
/** Parsing des données issues de OWANDATA **/
/*********************************************************/
void
check_ipaddr(const fixed_buf ip)
{
	unsigned int i, A[4];

	int ret = sscanf(ip, "%u.%u.%u.%u", A, A + 1, A + 2, A + 3);
	if (ret != 4)
		ERROR(EINVAL, "Failed to read members of "
				"IP addr %s: %d found", ip, ret);

	for (i = 0; i < 4; i++) {
		if (A[i] > 255)
			ERROR(EINVAL, "Invalid member %u of "
					"IP addr %s", i, ip);
	}
}

void
get_ipaddr(fixed_buf dst, const char *src,
		const char *name __attribute__((unused)), size_t *curoff)
{
	size_t off, len;

	buf_cpy(dst, src + *curoff);
	len = strlen(dst);

	for (off = 0; off < len; off++) {
		if (dst[off] == ',') {
			dst[off] = '\0';
			break;
		}
	}
	check_ipaddr(dst);
	DBGV(2, "%s : %s", name, dst);
	*curoff += off + 1;
	if (src[*curoff] != ' ')
		ERROR(EINVAL, "expected whitespace: %s", src + *curoff);

	*curoff += 1;
}

/*********************************************************/
/** Gestion du port série **/
/*********************************************************/
/* Configuration de la communication série */
void
setcom(int comd)
{
	struct termios stbuf;

  if (tcgetattr(comd, &memorized_conf) == -1)
		ERROR_ERRNO("tcgetattr");
	if (tcgetattr(comd, &stbuf) == -1 )
		ERROR_ERRNO("tcgetattr");

	stbuf.c_iflag &= ~(IGNCR | ICRNL | IUCLC | INPCK
					| IXON | IXANY | IGNPAR );
	stbuf.c_oflag &= ~(OPOST | OLCUC | OCRNL | ONLCR | ONLRET);
	stbuf.c_lflag &= ~(ICANON | XCASE | ECHO | ECHOE | ECHONL);
	stbuf.c_lflag &= ~(ECHO | ECHOE);
	stbuf.c_cc[VMIN] = 1;
	stbuf.c_cc[VTIME] = 0;
	stbuf.c_cc[VEOF] = 1;
	stbuf.c_cflag &= ~(CBAUD | CSIZE | CSTOPB | CLOCAL | PARENB);
	/*
	 * CLOCAL ignore momdem control lines ?
	 * stbuf.c_cflag |= (B0 | CS8 | CREAD	);
	 * if (tcsetattr(comd, TCSANOW, &stbuf) < 0) Error(errno, "tcsetattr");
	 * usleep(UDELAY);
	 */
	stbuf.c_cflag &= ~(CBAUD | CSIZE | CSTOPB | CLOCAL | PARENB);
	/* CLOCAL ignore momdem control lines ? */
	stbuf.c_cflag |= (B115200 | CS8 | CREAD	);

	if (tcsetattr(comd, TCSAFLUSH, &stbuf) < 0)
		ERROR_ERRNO( "tcsetattr");
}

/* Initialisation de la communication série */
int
initiate_serial(const char *device)
{
	int comd;

	comd = open(device, O_RDWR|O_EXCL|O_NONBLOCK|O_NOCTTY);
	if (comd < 0)
		ERROR_ERRNO("open device %s", device);

	setcom(comd);

	if (tcflush(comd, TCIOFLUSH) == -1)
		ERROR(errno, "tcflush");

	return comd;
}

/* Fermeture de la communication série et restauration
 * de la configuration initiale */
void
close_serial(int comd)
{
	if (comd >= 0) {
		/*
		 * if (tcflush(comd, TCIOFLUSH) == -1)
		 * 	Error(errno, "tcflush");*/
		/* if (tcsetattr(comd, TCSANOW, &memorized_conf) < 0)
		 * 	Error(errno, "tcsetattr");*/
		close(comd);
	}
}

/*********************************************************/
/** Fonctions d'écriture/lecture sur le port série **/
/*********************************************************/

inline void
writechar(int comd, char c)
{
	ssize_t wret;

retry:
	wret = write(comd, &c, 1);
	if (wret < 0) {
		/* EAGAIN ? */
		if (errno == EINTR)
			goto retry;
		ERROR_ERRNO("write char %c", c);
	}
	if (wret != 1)
 		ERROR(EFAULT, "write char %c returned %zu", c, wret);

	DBGV(3, "write -> %c", c);
}

/* Write a null-terminated string to communication device */
void
writecom(int comd, const char *text)
{
	size_t off, len;
	char c;

	/*
	 * if(tcflush(comd, TCIOFLUSH) == -1)
	 * 	Error(errno, "tcflush");
	 */
	DBGV(2, "-> %s",text);

	len = strlen(text);
	for (off = 0; off < len; off++) {
		c = text[off];
		writechar(comd, c);
		usleep(MUDELAY);
	}
	c = '\015';
	writechar(comd, c);
}

/* Gets a blob from comm. device.
 * Return EOF if none avail. */
int
readcom(int comfd, fixed_buf answer, unsigned int udelay)
{
	fd_set rfds;
	ssize_t rret;
	size_t off = 0;
	const size_t len = sizeof(fixed_buf) - 1;
	int num;
	struct timeval timeout;
	char c;
	char *ptr;
	fixed_buf buf;

	buf[0] = '\0';

	while (off < len) {
		usleep(MUDELAY);
		timeout.tv_sec = 0L;
		timeout.tv_usec = udelay;
		FD_ZERO(&rfds);
		FD_SET(comfd, &rfds);
		num = select(comfd + 1, &rfds, NULL, NULL, &timeout);
		if (num < 0) {
			WARN_ERRNO("select failed on serial device");
			return -1;
		}
		if (!num) {
			DBG("select() timeout on serial link");
			goto out;
		}

		if ((num != 1)) {
			WARN("unexpected select retval : %d", num);
			return -1;
		}

		rret = read(comfd, &c, 1);
		if (rret < 0) {
			if (errno == EAGAIN) /* EOF */
				break;
			if (errno == EINTR)
				continue;
			WARN_ERRNO("read failed on serial device");
			return -1;
		}
		if (!rret) /* EOF */
			break;

		DBGV(3, "read -> %c", c);
		buf[off] = c;
		if (c == '\n') /* EOL */
			break;
		off++;
	}

out:
	if (off == len - 1) {
		WARN("read overflow on serial device");
		return -1;
	}
	off++;
	buf[off] = 0;
	strip_right(buf);
	ptr = strip_left(buf);
	buf_cpy(answer, ptr);
	DBGV(2, "<- %s", answer);
	return (off - 1);
}

/* Commande et réponse */
int
send_receive(int comd, const char *cmd, fixed_buf answer)
{
	unsigned int i;
	int ret;
	fixed_buf tmp, cmd_disp;
	char *ptr;

	buf_cpy(cmd_disp, cmd);
	ptr = strchr(cmd_disp, '=');
	if (ptr)
		*ptr = '\0'; /* Let's not show what's after the '='
				in debug logs.  */
	writecom(comd, cmd);

	for (i = 0; i < 5; i++) {
		ret = readcom(comd, tmp, UDELAY);
		if (ret < 0)
			return ret;
		if (strmatch(tmp, cmd))
			goto matched;
		WARN("read while trying to match %s: %s", cmd_disp, tmp);
	}
	WARN("Failed to read echo of command %s", cmd_disp);
	return -1;

matched:
	for (i = 0; i < 5; i++) {
		ret = readcom(comd, tmp, UDELAY);
		if (ret < 0)
			return ret;
		if (strlen(tmp))
			goto answer;
	}
	WARN("Time out reading answer to %s", cmd_disp);
	return -1;

answer:
	buf_cpy(answer, tmp);
	DBG("%s: %s", cmd_disp, answer);

	if (!strmatch(answer, "OK") && !strmatch(answer, "ERROR")) {
		/* Try to glob trailing OK / ERROR */
		for (i = 0; i < 5; i++) {
			ret = readcom(comd, tmp, UDELAY);
			if (ret >= 0 && (strmatch(tmp, "OK")
					 || strmatch(tmp, "ERROR")))
				break;
		}
	}

	return 0;
}

void
get_check_answer(int comd, const char *cmd,
			fixed_buf answer, const char *expected)
{
	if (send_receive(comd, cmd, answer))
		ERROR(EFAULT, "failed to get answer to %s", cmd);

	if (!strmatch(answer, expected))
		ERROR(EPROTO, "%s answer : expected %s, got %s",
						cmd, expected, answer);
}

int
fork_exec(char **argv)
{
	pid_t pid, wret;
	int status;
	char *envp[] = { NULL };

	pid = fork();

	switch (pid) {
		case 0:
			if (execve(argv[0], argv, envp))
				ERROR_ERRNO("execve %s failed", argv[0]);
			exit(EXIT_FAILURE);
		case -1:
			ERROR_ERRNO("fork %s failed", argv[0]);
			return -1;
		default:
			wret = waitpid(pid, &status, 0);
			if (wret < 0) {
				ERROR_ERRNO("waitpid %s failed", argv[0]);
				return -1;
			}
			if (wret != pid) {
				ERROR(EFAULT,
					"waitpid wtf ? %d != %d", wret, pid);
				return -1;
			}
			if (!WIFEXITED(status) || WEXITSTATUS(status))
				return -1;

			return 0;
	}
}

/* Vérifie si le PIN est nécessaire et dans ce cas le soumet à la carte */
int
check_pin_status(int comd, struct cdata *p_conn_data)
{
	fixed_buf answer, cmd;

	get_check_answer(comd, "AT", answer, "OK");

	if (send_receive(comd, "AT+CPIN?", answer))
		return -1;

	/* il est possible que SIM PUK ou SIM PIN2
	 * soient aussi acceptables */
	if (strmatch(answer, "+CPIN: READY")) {
		LOG("PIN already set up");
		return 0;
	}
	if (strmatch(answer, "+CME ERROR: SIM busy")) {
		LOG("SIM busy, retrying");
		usleep(UDELAY);
		if (send_receive(comd, "AT+CPIN?", answer))
			return -1;
	}
	if (strmatch(answer, "+CPIN: SIM PIN")) {
		LOG("Setting up PIN code");
		buf_format_string(cmd, "AT+CPIN=\"%s\"", p_conn_data->pin);
		if (send_receive(comd, cmd, answer))
			return -1;
		/* Detect incorrect password and return specific error
		 * in that case to avoid retries...
		 */
		if (strmatch(answer, "+CME ERROR: incorrect password"))
			ERROR(ESIMPIN, "Incorrect SIM PIN");

		if (!strmatch(answer, "OK"))
			ERROR(EPROTO, "AT+CPIN answer, expected OK, got %s",
					answer);

		if (send_receive(comd, "AT+CPIN?", answer))
			return -1;
		if (strmatch(answer, "+CPIN: READY"))
			return 0;
	}
	WARN("unexpected CPIN? answer : %s", answer);
	return -1;
}

