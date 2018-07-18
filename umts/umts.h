// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright © 2008-2018 ANSSI. All Rights Reserved.
#ifndef UMTS_H
#define UMTS_H

#define _GNU_SOURCE
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <signal.h>
#include <termio.h>
#include <fcntl.h>
#include <limits.h>
#include <unistd.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <error.h>
#include <errno.h>
#include <syslog.h>

#define SIZE_PIN 8

/************************/
/* Specific error codes */
/************************/

#define EUNSUPDEV 249		/* unsupported device */
#define ESIMPIN 250			/* invalid SIM PIN */
#define ECALLFAILED 251	/* call failed */

/*********************************************************/
/** Gestion des exceptions **/
/*********************************************************/

#define _LOG(prio, fmt, args...) syslog(prio, fmt"\n", ##args)

#define _WARN(prio, fmt, args...) _LOG(prio, "%s(%d): "fmt"\n", \
				__FUNCTION__, __LINE__, ##args)

#define LOG(fmt, args...) _LOG(LOG_INFO, fmt, ##args)

#define DBG(fmt, args...) _LOG(LOG_DEBUG, fmt, ##args)

#ifdef DEBUG
#define DBGV(lvl, fmt, args...) do {\
	if (DEBUG >= (lvl)) \
		DBG(fmt, ##args);\
} while (0)

#else

#define DBGV(fmt, args...)

#endif

#define WARN(fmt, args...) _WARN(LOG_WARNING, fmt, ##args)
#define WARN_ERRNO(fmt, args...) \
	_WARN(LOG_ERR, fmt": %s", ##args, strerror(errno));

#define ERROR(err, fmt, args...) do {\
	_WARN(LOG_ERR, fmt, ##args); \
	exit(err); \
} while (0)

#define ERROR_ERRNO(fmt, args...) do {\
	WARN_ERRNO(fmt, ##args); \
	exit(errno); \
} while (0)

/*********************************************************/
/** Structures de données internes **/
/*********************************************************/
#define MAX_PIN SIZE_PIN + 1
#define MAX_LEN 200
typedef char fixed_pinbuf[MAX_PIN];
typedef char fixed_buf[MAX_LEN];

#define strmatch(str1, str2) (strncmp(str1, str2, strlen(str2)) == 0)

struct cdata {
	fixed_pinbuf pin;
	fixed_buf apn, identity, password;
	fixed_buf operator, ip_address, mask, gateway, dns1, dns2;
};

typedef struct
{
	const char* name;
	const char* device;
	const char* interface;
	int		(*init)(int comd);
	void	(*check_conn_up)(int comd, struct cdata *p_conn_data, char *interface);
	int		(*wait_reg_status)(int comd);
	void	(*set_conn_down)(int comd, char *interface);
	int		(*monitor_connection)(int comd, const char *filename,
					const char *interface __attribute__((unused)),
					const char *ipsec);
} umts_device_t;

extern umts_device_t hso_device;
extern umts_device_t acm_device;
extern umts_device_t huawei_device;

/* 0,1 s Délai d'envoi entre chaque caractère sur le port série */
#define MUDELAY 10000U
/* 1 s Délai d'attente pour chaque interrogation de réponse */
#define UDELAY 1000000U
/* 30 s = TIMEOUT x UDELAY*/
#define TIMEOUT 30U

/*********************************************************/
/** Ouverture/fermeture de fichier                      **/
/*********************************************************/
typedef enum {
  ReadMode = 0,
  WriteMode = 1,
} omode;

FILE *
open_file(const char *path, omode writelock);

int
close_file(const char *path, FILE *fd);

/*********************************************************/
/** Gestion du port série **/
/*********************************************************/

/* variable de mémorisation de la configuration initiale */
struct termios memorized_conf;

int
initiate_serial(const char *device);

void
close_serial(int comd);

int
send_receive(int comd, const char *cmd, fixed_buf answer);

void
get_check_answer(int comd, const char *cmd,
	fixed_buf answer, const char *expected);

int
readcom(int comfd, fixed_buf answer, unsigned int udelay);

void
setcom(int comd);

void
writechar(int comd, char c);

void
writecom(int comd, const char *text);

static inline char *
strip_left(char *buf)
{
	char *ptr = buf;
	while (*ptr == '\r' || *ptr == '\n')
		ptr++; /* Ok, NULL terminated */

	return ptr;
}

static inline void
strip_right(char *buf)
{
	size_t l = strlen(buf);
	if (!l)
		return;
	l--;
	while (l) {
		if (buf[l] != '\r' && buf[l] != '\n')
			break;
		buf[l] = '\0';
		l--;
	}
}

/*********************************************************/
/* Scripts                                               */
/*********************************************************/
void
check_ipaddr(const fixed_buf ip);

void
get_ipaddr(fixed_buf dst, const char *src,
	const char *name __attribute__((unused)), size_t *curoff);

int
fork_exec(char **argv);

/*********************************************************/
/* Configuration                                         */
/*********************************************************/
#define CONFLINK "/etc/admin/conf.d/netconf"

void
check_var(FILE *fd, const char *var);

void
get_var(FILE *fd, const char *var, fixed_buf buf);

void
parse_conf(FILE *fd, struct cdata *p_conn_data);

/*********************************************************/
/* Gestion de tampons                                    */
/*********************************************************/

static inline void
buf_cpy(fixed_buf buf, const char *src)
{
	int ret = snprintf(buf, MAX_LEN, "%s", src);
	if (ret < 0 || ret >= MAX_LEN)
		ERROR(EMSGSIZE, "buf overflow, ret %d", ret);
}

static inline void
pinbuf_cpy(fixed_pinbuf buf, const char *src)
{
	int ret = snprintf(buf, MAX_PIN, "%s", src);
	if (ret < 0 || ret >= MAX_PIN)
		ERROR(EMSGSIZE, "pinbuf overflow, ret %d", ret);
}

static inline int
buf_format(fixed_buf buf, const char *format, int val)
{
	int ret = snprintf(buf, MAX_LEN, format, val);
	if (ret < 0 || ret >= MAX_LEN)
		ERROR(EMSGSIZE, "format overflow, ret %d", ret);

	return(ret);
}

static inline int
buf_format_string(fixed_buf buf, const char *format, const char *val)
{
	int ret = snprintf(buf, MAX_LEN, format, val);
	if (ret < 0 || ret >= MAX_LEN)
		ERROR(EMSGSIZE, "format overflow, ret %d", ret);

	return(ret);
}

static inline int
buf_format_two_strings(fixed_buf buf, const char *format,
				const char *val1, const char *val2)
{
	int ret = snprintf(buf, MAX_LEN, format, val1, val2);
	if (ret < 0 || ret >= MAX_LEN)
		ERROR(EMSGSIZE, "format overflow, ret %d", ret);

	return(ret);
}

/*********************************************************/
/* Device 3G                                             */
/*********************************************************/

int
check_pin_status(int comd, struct cdata *p_conn_data);

#endif /* UMTS_H */
