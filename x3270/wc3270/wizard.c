/*
 * Copyright (c) 2006-2013, Paul Mattes.
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of Paul Mattes nor his contributors may be used
 *       to endorse or promote products derived from this software without
 *       specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY PAUL MATTES "AS IS" AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL PAUL MATTES BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 *	wizard.c
 *		A Windows console-based 3270 Terminal Emulator
 *		Session creation wizard
 */

#include "globals.h"

#include <signal.h>
#include "appres.h"
#include "3270ds.h"
#include "resources.h"
#include "ctlr.h"

#include "actionsc.h"
#include "ctlrc.h"
#include "hostc.h"
#include "keymapc.h"
#include "kybdc.h"
#include "macrosc.h"
#include "proxy.h"
#include "resources.h"
#include "screenc.h"
#include "tablesc.h"
#include "trace_dsc.h"
#include "utilc.h"
#include "xioc.h"

#include <direct.h>
#include <wincon.h>
#include <shlobj.h>
#include "shlobj_missing.h"

#include "winversc.h"
#include "shortcutc.h"
#include "windirsc.h"
#include "relinkc.h"

#define LEGAL_CNAME	"ABCDEFGHIJKLMNOPQRSTUVWXYZ" \
			"abcedfghijklmnopqrstuvwxyz" \
			"0123456789_- "

#define KEYMAP_SUFFIX	".wc3270km"
#define KS_LEN		strlen(KEYMAP_SUFFIX)

#define KM_3270		".3270"
#define LEN_3270	strlen(KM_3270)

#define KM_NVT		".nvt"
#define LEN_NVT		strlen(KM_NVT)

#define KM_DESC		"!description: "
#define LEN_DESC	strlen(KM_DESC)

#define SESS_SUFFIX	".wc3270"
#define SESS_LEN	strlen(SESS_SUFFIX)

#define CHOICE_NONE	"none"
#define DISPLAY_NONE	"(none)"

enum {
    MN_NONE = 0,
    MN_HOST,		/* host name */
    MN_LU,		/* logical unit */
    MN_PORT,		/* TCP port */
    MN_MODEL,		/* model number */
    MN_OVERSIZE,	/* oversize */
    MN_CHARSET,		/* character set */
    MN_SSL,		/* SSL tunnel */
    MN_VERIFY,		/* verify host certificate */
    MN_PROXY,		/* use proxy host */
    MN_PROXY_SERVER,	/* proxy host name */
    MN_PROXY_PORT,	/* proxy port number */
    MN_3287,		/* printer session */
    MN_3287_MODE,	/* printer mode */
    MN_3287_LU,		/* printer logical unit */
    MN_3287_PRINTER,	/* printer Windows printer */
    MN_3287_CODEPAGE,	/* printer code page */
    MN_KEYMAPS,		/* keymaps */
    MN_EMBED_KEYMAPS,	/* embed keymaps */
    MN_FONT_SIZE,	/* font size */
    MN_BG,		/* background color */
    MN_MENUBAR,		/* menu bar */
    MN_N_OPTS
} menu_option_t;

/* Return value from get_session(). */
typedef enum {
    GS_NEW,		/* file does not exist */
    GS_EDIT,		/* file does exist and is editable, edit it */
    GS_NOEDIT,		/* file does exist and is editable, do not edit it */
    GS_OVERWRITE,	/* file exists but is uneditable, overwrite it */
    GS_ERR = -1,	/* error */
    GS_NOEDIT_LEAVE = -2 /* uneditable and they don't want to overwrite it */
} gs_t;

/* Return value from summarize_and_proceed().  */
typedef enum {
    SRC_ALL,		/* success, in all-users AppData */
    SRC_USER,		/* success, in current user AppData */
    SRC_OTHER,		/* not sure where the file is */
    SRC_ERR = -1	/* error */
} src_t;

#define YN_ERR		(-1)	/* error return from getyn() */
#define YN_RETRY	(-2)	/* user input error from getyn() */

extern char *wversion;

/* Aliases for obsolete character set names. */
struct {
    	char	*alias;
	char	*real;
} charset_alias[] = {
    	{ "japanese-290",  "japanese-kana" },
    	{ "japanese-1027", "japanese-latin" },
	{ NULL, NULL }
};

#define CS_WIDTH	19
#define CP_WIDTH	8
#define WP_WIDTH	6
#define	CS_COLS		2

#define MAX_PRINTERS	256
PRINTER_INFO_1 printer_info[MAX_PRINTERS];
int num_printers = 0;
char default_printer[1024];

static struct {
    	char *name;
	char *description;
} builtin_keymaps[] = {
	{ "rctrl",	"Map PC Right Ctrl key to 3270 'Enter' and PC Enter key to 3270 'Newline'" },
	{ NULL,		NULL }
};

static struct {
    	char *name;
	char *protocol;
	char *port;
} proxies[] = {
    	{ PROXY_HTTP,	"HTTP tunnel (RFC 2817, e.g., squid)",	PORT_HTTP },
	{ PROXY_PASSTHRU,"Sun telnet-passthru",			NULL   },
	{ PROXY_SOCKS4,	"SOCKS version 4",			PORT_SOCKS4 },
	{ PROXY_SOCKS5,	"SOCKS version 5 (RFC 1928)",		PORT_SOCKS5 },
	{ PROXY_TELNET,	"None (just send 'connect host port')",	NULL   },
	{ NULL,		NULL,					NULL   }
};

static int create_session_file(session_t *s, char *path);

static char *mya = NULL;
static char *installdir = NULL;
static char *desktop = NULL;
static char *common_desktop = NULL;
static char *commona = NULL;

static int get_printerlu(session_t *s, int explain);

static char *
get_input(char *buf, int bufsize)
{
    	char *s;
	int sl;

	/* Make sure all of the output gets out. */
	fflush(stdout);

	/* Get the raw input from stdin. */
	if (fgets(buf, bufsize, stdin) == NULL)
	    	return NULL;

	/* Trim leading whitespace. */
	s = buf;
	sl = strlen(buf);
	while (*s && isspace(*s)) {
		s++;
		sl--;
	}
	if (s != buf)
		memmove(buf, s, sl + 1);

	/* Trim trailing whitespace. */
	while (sl && isspace(buf[--sl]))
		buf[sl] = '\0';

	return buf;
}

/*
 * Ask a yes or no question.
 *
 * param[in] defval	Default response (TRUE or FALSE).
 *
 * return TRUE or FALSE	Proper respoonse
 *        YN_ERR	I/O error occurred (usually EOF)
 *        YN_RETRY	User entry error, error message already printed
 */
static int
getyn(int defval)
{
	char yn[STR_SIZE];

	if (get_input(yn, STR_SIZE) == NULL) {
		return YN_ERR;
	}

	if (!yn[0])
		return defval;

	if (!strncasecmp(yn, "yes", strlen(yn)))
		return TRUE;
	if (!strncasecmp(yn, "no", strlen(yn)))
		return FALSE;

	printf("Please answer (y)es or (n)o.\n\n");
	return YN_RETRY;
}

static void
enum_printers(void)
{
	DWORD needed = 0;
	DWORD returned = 0;

	/* Get the default printer name. */
	default_printer[0] = '\0';
	if (GetProfileString("windows", "device", "", default_printer,
		    sizeof(default_printer)) != 0) {
		char *comma;

		if ((comma = strchr(default_printer, ',')) != NULL)
			*comma = '\0';
	}

	/* Get the list of printers. */
	if (EnumPrinters(
		    PRINTER_ENUM_LOCAL | PRINTER_ENUM_CONNECTIONS,
		    NULL,
		    1,
		    (LPBYTE)&printer_info,
		    sizeof(printer_info),
		    &needed,
		    &returned) == 0)
	    return;

	num_printers = returned;
}

/* Get an 'other' printer name. */
static int
get_printer_name(char *defname, char *printername)
{
	for (;;) {
		printf("\nEnter Windows printer name: [%s] ",
			defname[0]? defname: "use system default");
		fflush(stdout);
		if (get_input(printername, STR_SIZE) == NULL)
			return -1;
		if (!printername[0]) {
			if (defname[0])
			    	strcpy(printername, defname);
			break;
		}
		if (!strcmp(printername, "default")) {
		    	printername[0] = '\0';
		}
		if (strchr(printername, '!') ||
		    strchr(printername, ',')) {
			printf("Invalid printer name.\n");
			continue;
		} else {
			break;
		}
	}
	return 0;
}

typedef struct km {
	struct km *next;
    	char name[MAX_PATH];
	char description[STR_SIZE];
	char *def_both;
	char *def_3270;
	char *def_nvt;
} km_t;
km_t *km_first = NULL;
km_t *km_last = NULL;

/* Save a keymap name.  If it is unique, return its node. */
static km_t *
save_keymap_name(char *path, char *keymap_name, char *description)
{
	km_t *km;
    	int sl;
	km_t *kms;
	FILE *f;
	enum { KMF_BOTH, KMF_3270, KMF_NVT } km_mode = KMF_BOTH;
	char **def = NULL;

	km = (km_t *)malloc(sizeof(km_t));
	if (km == NULL) {
	    	fprintf(stderr, "Not enough memory\n");
		return NULL;
	}
	memset(km, '\0', sizeof(km_t));
	strcpy(km->name, keymap_name);
	km->description[0] = '\0';
    	sl = strlen(km->name);

	/* Slice off the '.wc3270km' suffix. */
	if (sl > KS_LEN && !strcasecmp(km->name + sl - KS_LEN, KEYMAP_SUFFIX)) {
		km->name[sl - KS_LEN] = '\0';
		sl -= KS_LEN;
	}

	/* Slice off any '.3270' or '.nvt' before that. */
	if (sl > LEN_3270 && !strcasecmp(km->name + sl - LEN_3270, KM_3270)) {
		km->name[sl - LEN_3270] = '\0';
		sl -= LEN_3270;
		km_mode = KMF_3270;
	} else if (sl > LEN_NVT &&
		    !strcasecmp(km->name + sl - LEN_NVT, KM_NVT)) {
		km->name[sl - LEN_NVT] = '\0';
		sl -= LEN_NVT;
		km_mode = KMF_NVT;
	}

	for (kms = km_first; kms != NULL; kms = kms->next) {
	    	if (!strcasecmp(kms->name, km->name))
		    	break;
	}
	if (kms != NULL) {
	    	free(km);
		km = kms;
	} else {
		km->next = NULL;
		if (km_last != NULL)
			km_last->next = km;
		else
			km_first = km;
		km_last = km;
	}

	/* Check if we've already seen this keymap. */
	switch (km_mode) {
	    case KMF_BOTH:
		def = &km->def_both;
		break;
	    case KMF_3270:
		def = &km->def_3270;
		break;
	    case KMF_NVT:
		def = &km->def_nvt;
		break;
	}
	if (*def != NULL)
	    	return km;

	if (description != NULL) {
	    	strcpy(km->description, description);
		return km;
	}

	/* Dig for a description and save the definition. */
	if (path != NULL) {
		f = fopen(path, "r");
		if (f != NULL) {
			char buf[STR_SIZE];

			while (fgets(buf, STR_SIZE, f) != NULL) {
			    	int any = 0;

				sl = strlen(buf);
				if (sl > 0 && buf[sl - 1] == '\n')
					buf[--sl] = '\0';
				if (!strncasecmp(buf, KM_DESC, LEN_DESC)) {
					strncpy(km->description, buf + LEN_DESC,
						sl - LEN_DESC + 1);
					continue;
				}
				if (buf[0] == '!' || !buf[0])
				    	continue;
				if (*def == NULL)
				    	*def = malloc(strlen(buf) + 2);
				else {
				    	*def = realloc(*def, strlen(*def) + 5 +
						      strlen(buf) + 1);
					any = 1;
				}
				if (*def == NULL) {
				    	fprintf(stderr, "Out of memory\n");
					exit(1);
				}
				if (!any)
				    	strcat(strcpy(*def, " "), buf);
				else
				    	strcat(strcat(*def, "\\n\\\n "), buf);
			}
			fclose(f);
		}
	}

	return km;
}

static void
save_keymaps(void)
{
    	int i;
	char dpath[MAX_PATH];
	char fpath[MAX_PATH];
	HANDLE h;
	WIN32_FIND_DATA find_data;

	for (i = 0; builtin_keymaps[i].name != NULL; i++) {
		(void) save_keymap_name(NULL, builtin_keymaps[i].name,
			builtin_keymaps[i].description);
	}
	sprintf(dpath, "%s*%s", mya, KEYMAP_SUFFIX);
	h = FindFirstFile(dpath, &find_data);
	if (h != INVALID_HANDLE_VALUE) {
		do {
			sprintf(fpath, "%s%s", mya, find_data.cFileName);
			(void) save_keymap_name(fpath, find_data.cFileName,
				NULL);
		} while (FindNextFile(h, &find_data) != 0);
		FindClose(h);
	}
	if (commona != NULL) {
		sprintf(dpath, "%s*%s", commona, KEYMAP_SUFFIX);
		h = FindFirstFile(dpath, &find_data);
		if (h != INVALID_HANDLE_VALUE) {
			do {
				sprintf(fpath, "%s%s", commona,
					find_data.cFileName);
				(void) save_keymap_name(fpath,
					find_data.cFileName, NULL);
			} while (FindNextFile(h, &find_data) != 0);
			FindClose(h);
		}
	}
	sprintf(dpath, "%s*%s", installdir, KEYMAP_SUFFIX);
	h = FindFirstFile(dpath, &find_data);
	if (h != INVALID_HANDLE_VALUE) {
		do {
			sprintf(fpath, "%s%s", installdir,
				find_data.cFileName);
			(void) save_keymap_name(fpath, find_data.cFileName,
				NULL);
		} while (FindNextFile(h, &find_data) != 0);
		FindClose(h);
	}
}

/*
 * Fix up a UNC printer path in an old session file.
 * Returns 1 if the name needed fixing, 0 otherwise.
 */
static int
fixup_printer(session_t *s)
{
	char buf[STR_SIZE];
	int i, j;
	char c;

	if (s->printer[0] == '\\' &&
	    s->printer[1] == '\\' &&
	    s->printer[2] != '\\') {
		/*
		 * The session file was created by an earlier version of the
		 * session wizard, and contains a UNC printer path that has
		 * not had its backslashes expanded.  Expand them.
		 */
		j = 0;
		for (i = 0;
		     i < (STR_SIZE - 1) && (c = s->printer[i]) != '\0';
		     i++) {
			if (c == '\\') {
				if (j < (STR_SIZE - 1)) {
				    	buf[j++] = '\\';
				}
				if (j < (STR_SIZE - 1)) {
				    	buf[j++] = '\\';
				}
			} else {
				if (j < (STR_SIZE - 1)) {
				    	buf[j++] = c;
				}
			}
		}
		buf[j] = '\0';
		strncpy(s->printer, buf, STR_SIZE);

		return 1;
	} else {
	    	return 0;
	}
}

/*
 * Reformat a quoted UNC path for display.
 * Returns 1 if it was reformatted, 0 otherwise.
 */
static int
redisplay_printer(const char *expanded, char *condensed)
{
	int i;
	int j;
	int bsl = 0;
	int reformatted = 0;

	j = 0;
	for (i = 0; i < STR_SIZE; i++) {
		char c = expanded[i];
		if (c == '\0') {
			if (bsl) {
				goto abort;
			}
			condensed[j] = c;
			break;
		}

		if (bsl) {
			if (c == '\\') {
				reformatted = 1;
				bsl = 0;
			} else {
				goto abort;
			}
		} else {
			condensed[j++] = c;
			if (c == '\\') {
				bsl = 1;
			}
		}
	}

	return reformatted;

    abort:
	strcpy(condensed, expanded);
	return 0;
}

/*
 * Clear the screen, print a common banner and a title.
 */
static void
new_screen(session_t *s, char *title)
{
    	system("cls");
	printf(
"wc3270 Session Wizard                                            %s\n",
		wversion);
	if (s->session[0])
	    	printf("\nSession: %s\n", s->session);
	printf("\n%s\n", title);
}

/* Introductory screen. */
static int
intro(session_t *s)
{
	int rc;

	new_screen(s, "\
Overview\n\
\n\
This wizard sets up a new wc3270 session, or allows you to modify an existing\n\
session.\n\
\n\
It creates or edits a session file in your wc3270 Application Data directory\n\
and can create or re-create a shortcut on your desktop.");

	for (;;) {
		printf("\nContinue? (y/n) [y] ");
		fflush(stdout);
		rc = getyn(TRUE);
		if (rc == YN_ERR || rc == FALSE)
			return -1;
		if (rc == TRUE)
			break;
	}
	return 0;
}

/*
 * Search a well-defined series of locations for a session file.
 */
static src_t
find_session_file(const char *session_name, char *path)
{
	/* Try user's AppData. */
	snprintf(path, MAX_PATH, "%s%s%s", mya, session_name, SESS_SUFFIX);
	if (access(path, R_OK) == 0) {
		return SRC_USER;
	}

	/* Not there.  Try common AppData. */
	if (commona != NULL) {
		snprintf(path, MAX_PATH, "%s%s%s", commona, session_name,
			SESS_SUFFIX);
		if (access(path, R_OK) == 0) {
			return SRC_ALL;
		}
	}

	/* Not there.  Try installdir. */
	snprintf(path, MAX_PATH, "%s%s%s", installdir, session_name,
		SESS_SUFFIX);
	if (access(path, R_OK) == 0) {
		return SRC_OTHER;
	}

	/* Not there.  Try cwd. */
	snprintf(path, MAX_PATH, "%s%s", session_name, SESS_SUFFIX);
	if (access(path, R_OK) == 0) {
		return SRC_OTHER;
	}

	/*
	 * Put the new one in the user's AppData.
	 * I don't think this value is actually used.
	 */
	snprintf(path, MAX_PATH, "%s%s%s", mya, session_name, SESS_SUFFIX);
	return SRC_OTHER;
}

/*
 * Session name screen.
 * Parameters:
 *   session_name[in]	If NULL, prompt for one
 *   			If non-NULL and does not end in .wc3270, take this as
 *   			the session name, and fail if it contains invalid
 *   			characters
 *   			If non-NULL and ends in .wc3270, take this as the path
 *   			to the session file
 *   s[out]		Session structure to fill in with name and (if the
 *   			file exists) current contents
 *   path[out]		Pathname of session file
 *   explicit_edit[in]	If TRUE, -e was passed on command line; skip the
 *   			'exists. Edit?' dialog
 *   src[out]		Where the session file was found, if it exists
 *
 * Returns: gs_t
 */
static gs_t
get_session(char *session_name, session_t *s, char *path, int explicit_edit,
	src_t *src)
{
    	FILE *f;
	int rc;
	int editable;

	*src = SRC_OTHER;

	if (session_name != NULL) {
	    	size_t sl = strlen(session_name);
		size_t slen = sizeof(s->session);

		/*
		 * Session file pathname or session name specified on the
		 * command line.
		 */
	    	if (sl > SESS_LEN &&
		    !strcasecmp(session_name + sl - SESS_LEN,
				SESS_SUFFIX)) {
		    	char *bsl;
			char *colon;

		    	/* Ends in .wc3270km. Pathname. */
			path[MAX_PATH - 1] = '\0';
			bsl = strrchr(session_name, '\\');
			colon = strrchr(session_name, ':');
			if (bsl == NULL && colon == NULL) {
			    	/*
				 * No directory or drive prefix -- just a file
				 * name.
				 */
				if (sl - SESS_LEN + 1 < slen) {
				    	slen = sl - SESS_LEN + 1;
				}
			    	strncpy(s->session, session_name, slen);
				s->session[slen - 1] = '\0';

				*src = find_session_file(s->session, path);
			} else {
			    	/*
				 * Full pathname.  Copy what's between the last
				 * [:\] and ".wc3270" as the session name.
				 */
			    	char *start;

				strncpy(path, session_name, MAX_PATH);
				if (bsl != NULL && colon == NULL)
				    	start = bsl + 1;
				else if (bsl == NULL && colon != NULL)
				    	start = colon + 1;
				else if (bsl > colon)
				    	start = bsl + 1;
				else
				    	start = colon + 1;
				if (strlen(start) - SESS_LEN + 1 < slen)
					slen = strlen(start) - SESS_LEN + 1;
				strncpy(s->session, start, slen);
				s->session[slen - 1] = '\0';

				/*
				 * Try to figure out where it is.
				 * This is inherently imperfect.
				 */
				if (!strncmp(path, mya, strlen(mya)) &&
					path[strlen(mya)] == '\\') {
					*src = SRC_USER;
				} else if (commona != NULL &&
					!strncmp(path, commona,
					    strlen(commona)) &&
					path[strlen(commona)] == '\\') {
					*src = SRC_ALL;
				} else {
					*src = SRC_OTHER;
				}
			}

		} else {
			/* Session name, no suffix. */
		    	strncpy(s->session, session_name, slen);
			s->session[slen - 1] = '\0';

			*src = find_session_file(s->session, path);
		}

		/* Validate the session name. */
		if (strspn(s->session, LEGAL_CNAME) != strlen(s->session)) {
			fprintf(stdout, "\
\nIllegal character(s).\n\
Session names can only have letters, numbers, spaces, underscores and dashes.\n");
			return GS_ERR;
		}

	} else {

		/* Get the session name interactively. */
		new_screen(s, "\
Session Name\n\
\n\
This is a unique name for the wc3270 session.  It is the name of the file\n\
containing the session configuration parameters and the name of the desktop\n\
shortcut.");
		for (;;) {
			printf("\nEnter session name: ");
			fflush(stdout);
			if (get_input(s->session, sizeof(s->session)) == NULL) {
				return GS_ERR;
			}
			if (!s->session[0])
				continue;
			if (strspn(s->session, LEGAL_CNAME) !=
				strlen(s->session)) {
				fprintf(stdout, "\
\nIllegal character(s).\n\
Session names can only have letters, numbers, spaces, underscore '_'\n\
and dash '-')\n");
				continue;
			}

			break;
		}
		*src = find_session_file(s->session, path);
	}

	f = fopen(path, "r");
	if (f != NULL) {
	    	editable = read_session(f, s);
		fclose(f);
		if (editable) {
			if (fixup_printer(s)) {
				printf("\n"
"NOTE: This session file contains a UNC printer name that needs to be updated\n"
" to be compatible with the current version of wc3270.  Even if you do not\n"
" need to make any other changes to the session, please select the Edit and\n"
" Update options to have this name automatically corrected.\n");
			}
		}

		if (editable) {
			const char *where;

			if (explicit_edit) {
				return GS_EDIT; /* edit it */
			}
			switch (*src) {
			case SRC_ALL:
				where = " in the all-users AppData "
				    "directory";
				break;
			case SRC_USER:
				where = " in the current user's "
				    "AppData directory";
				break;
			default:
				where = "";
				break;
			}
			for (;;) {
				printf("\nSession '%s' exists%s.\nEdit it? "
					"(y/n) [y] ", s->session, where);
				fflush(stdout);
				rc = getyn(TRUE);
				if (rc == YN_ERR)
					return GS_ERR;
				if (rc == FALSE)
					return GS_NOEDIT; /* do not edit */
				if (rc == TRUE)
					return GS_EDIT; /* edit it */
			}
		} else {
			for (;;) {
				printf("\nSession '%s' already exists but "
					"cannot be edited.  Replace it? "
					"(y/n) [n] ", s->session);
				fflush(stdout);
				rc = getyn(FALSE);
				if (rc == YN_ERR)
					return GS_ERR;
				if (rc == FALSE)
					return GS_NOEDIT_LEAVE; /* don't overwrite */
				if (rc == TRUE)
					return GS_OVERWRITE; /* overwrite */
			}
		}
	} else {
	    	/*
		 * Set the auto-shortcut flag in all new session files,
		 * but not in old ones.  This will prevent unintended
		 * interactions with old shortcuts that don't specify +S, but
		 * will allow new session files to be started with a
		 * double-click.
		 */
	    	s->flags |= WF_AUTO_SHORTCUT;

	    	return GS_NEW; /* create it */
	}
}

static int
get_host(session_t *s)
{
    	char buf[STR_SIZE];
        OSVERSIONINFO info;
	int has_ipv6 = 1;

	/* Win2K and earlier is IPv4-only.  WinXP and later can have IPv6. */
	memset(&info, '\0', sizeof(info));
	info.dwOSVersionInfoSize = sizeof(info);
	if (GetVersionEx(&info) == 0 ||
		info.dwMajorVersion < 5 ||
		(info.dwMajorVersion == 5 && info.dwMinorVersion < 1)) {
	    has_ipv6 = 0;
	}

#define COMMON_HOST_TEXT1 "\
Host Name\n\
\n\
This specifies the IBM host to connect to.  It can be a symbolic name like\n\
'foo.company.com'"

#define COMMON_HOST_TEXT2 "\
an IPv4 address in dotted-decimal notation such as\n\
'1.2.3.4'"

#define IPV6_HOST_TEXT "\
an IPv6 address in colon notation, such as 'fec0:0:0:1::27'"

#define COMMON_HOST_TEXT3 "\
\n\
\n\
To create a session file with no hostname (one that just specifies the model\n\
number, character set, etc.), enter '" CHOICE_NONE "'."

	if (has_ipv6)
	    	new_screen(s, COMMON_HOST_TEXT1 ", " COMMON_HOST_TEXT2 " or "
			IPV6_HOST_TEXT "." COMMON_HOST_TEXT3);
	else
	    	new_screen(s, COMMON_HOST_TEXT1 " or " COMMON_HOST_TEXT2 "."
			COMMON_HOST_TEXT3);

	for (;;) {
		if (s->host[0])
			printf("\nEnter host name or IP address: [%s] ",
				s->host);
		else
			printf("\nEnter host name or IP address: ");
		fflush(stdout);
		if (get_input(buf, sizeof(s->host)) == NULL) {
			return -1;
		}
		if (!strcmp(buf, CHOICE_NONE)) {
		    	strcpy(s->host, buf);
			break;
		}
		if (strchr(buf, ' ') != NULL) {
			printf("\nHost names cannot contain spaces.\n");
			continue;
		}
		if (strchr(buf, '@') != NULL) {
			printf("\nHostnames cannot contain '@' characters.\n");
			continue;
		}
		if (strchr(buf, '[') != NULL) {
			printf("\nHostnames cannot contain '[' characters.\n");
			continue;
		}
		if (strchr(buf, ']') != NULL) {
			printf("\nHostnames cannot contain ']' characters.\n");
			continue;
		}
		if (!buf[0]) {
			if (!s->host[0])
				continue;
		} else
			strcpy(s->host, buf);
		break;
	}
	return 0;
}

static int
get_port(session_t *s)
{
    	char inbuf[STR_SIZE];
	char *ptr;
	unsigned long u;

    	new_screen(s, "\
TCP Port\n\
\n\
This specifies the TCP Port to use to connect to the host.  It is a number from\n\
1 to 65535 or the name 'telnet'.  The default is the 'telnet' port, port 23.");

	for (;;) {
		printf("\nTCP port: [%d] ", (int)s->port);
		if (get_input(inbuf, sizeof(inbuf)) == NULL) {
			return -1;
		}
		if (!inbuf[0])
			break;
		if (!strcasecmp(inbuf, "telnet")) {
		    	s->port = 23;
			break;
		}
		u = strtoul(inbuf, &ptr, 10);
		if (u < 1 || u > 65535 || *ptr != '\0') {
			printf("Invalid port.\n");
		} else {
		    	s->port = (int)u;
			break;
		}
	}
	return 0;
}

static int
get_lu(session_t *s)
{
    	char buf[STR_SIZE];

    	new_screen(s, "\
Logical Unit (LU) Name\n\
\n\
This specifies a particular Logical Unit or Logical Unit group to connect to\n\
on the host.  The default is to allow the host to select the Logical Unit.");

	for (;;) {
		printf("\nEnter Logical Unit (LU) name: [%s] ",
			s->luname[0]? s->luname: CHOICE_NONE);
		fflush(stdout);
		if (get_input(buf, sizeof(buf)) == NULL) {
			return -1;
		}
		if (!buf[0])
		    	break;
		if (!strcmp(buf, CHOICE_NONE)) {
		    	s->luname[0] = '\0';
			break;
		}
		if (strchr(buf, ':') != NULL) {
		    	printf("\nLU name cannot contain ':' characters.\n");
			continue;
		}
		if (strchr(buf, '@') != NULL) {
		    	printf("\nLU name cannot contain '@' characters.\n");
			continue;
		}
		if (strchr(buf, '[') != NULL) {
		    	printf("\nLU name cannot contain '[' characters.\n");
			continue;
		}
		if (strchr(buf, ']') != NULL) {
		    	printf("\nLU name cannot contain ']' characters.\n");
			continue;
		}
		strcpy(s->luname, buf);
		break;
	}
	return 0;
}

static int
get_model(session_t *s)
{
	int i;
    	char inbuf[STR_SIZE];
	char *ptr;
	unsigned long u;
	int max_model = is_nt? 5: 4;

	new_screen(s, "\
Model Number\n\
\n\
This specifies the dimensions of the screen.");

	printf("\n");
	for (i = 2; i <= max_model; i++) {
		if (wrows[i]) {
			printf(" Model %d has %2d rows and %3d columns.\n",
			    i, wrows[i], wcols[i]);
		}
	}
	for (;;) {
		printf("\nEnter model number: (2, 3%s) [%d] ",
			is_nt? ", 4 or 5": " or 4", (int)s->model);
		fflush(stdout);
		if (get_input(inbuf, sizeof(inbuf)) == NULL) {
			return -1;
		}
		if (!inbuf[0]) {
			break;
		}
		u = strtoul(inbuf, &ptr, 10);
		if (u < 2 || u > max_model || *ptr != '\0') {
			printf("Invalid model number.\n");
			continue;
		}
		if (s->model != (int)u) {
			s->model = (int)u;
			s->ov_rows = 0;
			s->ov_cols = 0;
		}
		break;
	}
	return 0;
}

static int
get_oversize(session_t *s)
{
    	char inbuf[STR_SIZE];
	unsigned r, c;
	char xc;

	new_screen(s, "\
Oversize\n\
\n\
This specifies 'oversize' dimensions for the screen, beyond the number of\n\
rows and columns specified by the model number.  Some hosts are able to use\n\
this additional screen area; some are not.  Enter '"CHOICE_NONE"' to specify no\n\
oversize.");

	printf("\n\
The oversize must be larger than the default for a model %d (%u rows x %u\n\
columns).\n",
		(int)s->model,
		wrows[s->model],
		wcols[s->model]);

	for (;;) {
	    	printf("\nEnter oversize dimensions (rows x columns) ");
		if (s->ov_rows || s->ov_cols)
		    	printf("[%u x %u]: ", s->ov_rows, s->ov_cols);
		else
		    	printf("["CHOICE_NONE"]: ");
		fflush(stdout);
		if (get_input(inbuf, sizeof(inbuf)) == NULL) {
			return -1;
		}
		if (!inbuf[0]) {
			break;
		}
		if (!strcasecmp(inbuf, CHOICE_NONE)) {
		    	s->ov_rows = 0;
		    	s->ov_cols = 0;
			break;
		}
		if (sscanf(inbuf, "%u x %u%c", &r, &c, &xc) != 2) {
			printf("Please enter oversize in the form "
				"'rows x cols'.\n");
			continue;
		}
		if (r < wrows[s->model] ||
		    c < wcols[s->model]) {
			printf("Oversize must be larger than the default for "
				"a model %d (%u x %u).\n",
				(int)s->model,
				wrows[s->model],
				wcols[s->model]);
			continue;
		}
		if (r > 255 || c > 255) {
		    	printf("Rows and columns must be 255 or less.\n");
			continue;
		}
		if (r * c > 0x4000) {
		    	printf("The total screen area (rows multiplied by "
				"columns) must be less than 16384.\n");
			continue;
		}
		s->ov_rows = (unsigned char)r;
		s->ov_cols = (unsigned char)c;
		break;
	}
	return 0;
}

static int
get_charset(session_t *s)
{
    	char buf[STR_SIZE];
    	int i, k;
	char *ptr;
	unsigned long u;

	new_screen(s, "\
Character Set\n\
\n\
This specifies the EBCDIC character set (code page) used by the host.");

	printf("\
\nAvailable character sets:\n\n\
  #  Name                Host CP      #  Name                Host CP\n\
 --- ------------------- --------    --- ------------------- --------\n");
	k = 0;
	for (i = 0; charsets[i].name != NULL; i++) {
	    	int j;
		char *n, *h;


	    	if (i) {
			if (!(i % CS_COLS))
				printf("\n");
			else
				printf("   ");
		}
		if (!(i % 2))
		    	j = k;
		else {
		    	j += num_charsets / 2;
			k++;
		}
		if (is_nt || !charsets[j].is_dbcs) {
		    	n = charsets[j].name;
		    	h = charsets[j].hostcp;
		} else {
		    	n = "";
		    	h = "";
		}
		printf(" %2d. %-*s %-*s",
			j + 1,
			CS_WIDTH, n,
			CP_WIDTH, h);
	}
	printf("\n");
	for (;;) {
		printf("\nCharacter set: [%s] ", s->charset);
		if (get_input(buf, sizeof(buf)) == NULL) {
			return -1;
		}
		if (!buf[0])
			break;
		/* Check for numeric value. */
		u = strtoul(buf, &ptr, 10);
		if (u > 0 && u <= i && *ptr == '\0' &&
			    (is_nt || !charsets[u - 1].is_dbcs)) {
			strcpy(s->charset, charsets[u - 1].name);
			s->is_dbcs = charsets[u - 1].is_dbcs;
			break;
		}
		/* Check for alias. */
		for (i = 0; charset_alias[i].alias != NULL; i++) {
		    	if (!strcmp(buf, charset_alias[i].alias)) {
			    	strcpy(buf, charset_alias[i].real);
				break;
			}
		}
		/* Check for name match. */
		for (i = 0; charsets[i].name != NULL; i++) {
			if (!strcmp(buf, charsets[i].name) &&
				    (is_nt || !charsets[i].is_dbcs)) {
				strcpy(s->charset, charsets[i].name);
				s->is_dbcs = charsets[i].is_dbcs;
				break;
			}
		}
		if (charsets[i].name != NULL)
			break;
		printf("Invalid character set name.\n");
	}
	return 0;
}

#if defined(HAVE_LIBSSL) /*[*/
static int
get_ssl(session_t *s)
{
    	new_screen(s, "\
SSL Tunnel\n\
\n\
This option causes wc3270 to first create a tunnel to the host using the\n\
Secure Sockets Layer (SSL), then to run the TN3270 session inside the tunnel.");

	do {
		printf("\nUse an SSL tunnel? (y/n) [%s] ",
			s->ssl? "y" : "n");
		fflush(stdout);
		s->ssl = getyn(s->ssl);
		if (s->ssl == YN_ERR)
			return -1;
	} while (s->ssl < 0);
	return 0;
}

static int
get_verify(session_t *s)
{
	int rc;

    	new_screen(s, "\
Verify Host Certificates\n\
\n\
This option causes wc3270 to verify the certificates presented by the host\n\
if an SSL tunnel is used, or if the TELNET TLS option is negotiated.  If the\n\
certificates are not valid, the connection will be aborted.");

	do {
		printf("\nVerify host certificates? (y/n) [%s] ",
			(s->flags & WF_VERIFY_HOST_CERTS)? "y" : "n");
		fflush(stdout);
		rc = getyn((s->flags & WF_VERIFY_HOST_CERTS) != 0);
		switch (rc) {
		case YN_ERR:
			return -1;
		case TRUE:
			s->flags |= WF_VERIFY_HOST_CERTS;
			break;
		case FALSE:
			s->flags &= ~WF_VERIFY_HOST_CERTS;
			break;
		}
	} while (rc < 0);
	return 0;
}
#endif /*]*/

static int
get_proxy_server(session_t *s)
{
    	char hbuf[STR_SIZE];

	/* Get the hostname. */
	for (;;) {
	    	if (s->proxy_host[0]) {
			printf("\nProxy server name: [%s] ", s->proxy_host);
		} else {
			printf("\nProxy server name: ");
		}
		if (get_input(hbuf, STR_SIZE) == NULL)
		    	return -1;
		if (!hbuf[0]) {
		    	if (s->proxy_host[0])
			    	break;
			else
				continue;
		}
		if (strchr(hbuf, '[') != NULL ||
		    strchr(hbuf, ']') != NULL) {
		    	printf("Server name cannot include '[' or ']'\n");
			continue;
		}
		strcpy(s->proxy_host, hbuf);
		break;
	}
    	return 0;
}

static int
get_proxy_server_port(session_t *s)
{
    	char pbuf[STR_SIZE];
	int i;

	for (i = 0; proxies[i].name != NULL; i++) {
	    	if (!strcmp(s->proxy_type, proxies[i].name))
		    	break;
	}
	if (proxies[i].name == NULL) {
	    	printf("Internal error\n");
		return -1;
	}

	for (;;) {
	    	unsigned long l;
		char *ptr;

		if (s->proxy_port[0])
			printf("\nProxy server TCP port: [%s] ", s->proxy_port);
		else if (proxies[i].port != NULL)
			printf("\nProxy server TCP port: [%s] ",
				proxies[i].port);
		else
			printf("\nProxy server TCP port: ");
		if (get_input(pbuf, STR_SIZE) == NULL)
		    	return -1;
		if (!strcmp(pbuf, "default") && proxies[i].port != NULL) {
		    	strcpy(s->proxy_port, proxies[i].port);
			break;
		}
		if (!pbuf[0]) {
		    	if (s->proxy_port[0])
			    	break;
			else if (proxies[i].port != NULL) {
			    	strcpy(s->proxy_port, proxies[i].port);
				break;
			} else
				continue;
		}
		l = strtoul(pbuf, &ptr, 10);
		if (l == 0 || *ptr != '\0' || (l & ~0xffffL))
		    	printf("Invalid port\n");
		else {
			strcpy(s->proxy_port, pbuf);
		    	break;
		}
	}
    	return 0;
}

static int
get_proxy(session_t *s)
{
    	int i, j;
	char tbuf[STR_SIZE];
	char old_proxy[STR_SIZE];

    	new_screen(s, "\
Proxy\n\
\n\
If you do not have a direct connection to your host, this option allows\n\
wc3270 to use a proxy server to make the connection.");

	printf("\nProxy types available:\n");
	printf(" 1. none      Direct connection to host\n");
	for (i = 0; proxies[i].name != NULL; i++) {
	    	printf(" %d. %-8s  %s\n",
			i + 2,
			proxies[i].name,
			proxies[i].protocol);
	}

	strcpy(old_proxy, s->proxy_type);

	/* Get the proxy type. */
	for (;;) {
	    	int n;

	    	printf("\nProxy type: [%s] ",
			s->proxy_type[0]? s->proxy_type: CHOICE_NONE );
		if (get_input(tbuf, STR_SIZE) == NULL)
		    	return -1;
		if (!tbuf[0])
		    	return 0;
		if (!strcasecmp(tbuf, CHOICE_NONE)) {
			s->proxy_type[0] = '\0';
			s->proxy_host[0] = '\0';
			s->proxy_port[0] = '\0';
		    	return 0;
		}
		for (j = 0; proxies[j].name != NULL; j++) {
		    	if (!strcasecmp(tbuf, proxies[j].name))
			    	break;
		}
		if (proxies[j].name != NULL) {
		    	strcpy(s->proxy_type, tbuf);
		    	break;
		}
		n = atoi(tbuf);
		if (n > 0 && n <= i+1) {
		    	if (n == 1) {
				s->proxy_type[0] = '\0';
				s->proxy_host[0] = '\0';
				s->proxy_port[0] = '\0';
				return 0;
			} else {
				j = n - 2;
				strcpy(s->proxy_type, proxies[j].name);
				break;
			}
		}
		printf("Invalid proxy type.\n");
	}

	/* If the type changed, the rest of the information is invalid. */
	if (strcmp(old_proxy, s->proxy_type)) {
	    	s->proxy_host[0] = '\0';
		s->proxy_port[0] = '\0';

	    	if (get_proxy_server(s) < 0) {
			return -1;
		}

		if (proxies[j].port != NULL)
		    	strcpy(s->proxy_port, proxies[j].port);
		else if (get_proxy_server_port(s) < 0) {
			return -1;
		}
	}

	return 0;
}

static int
get_wpr3287(session_t *s)
{
    	new_screen(s, "\
wpr3287 Session\n\
\n\
This option allows wc3270 to automatically start a wpr3287 printer session\n\
when it connects to the host, allowing the host to direct print jobs to a\n\
Windows printer.");

	do {
		printf("\nAutomatically start a wpr3287 printer session? (y/n) [n] ");
		fflush(stdout);
		s->wpr3287 = getyn(s->wpr3287);
		if (s->wpr3287 == YN_ERR)
		    	return -1;
	} while (s->wpr3287 < 0);
	if (s->wpr3287 == 0)
	    	strcpy(s->printerlu, ".");
	return 0;
}

static int
get_printer_mode(session_t *s)
{
	int rc;

	new_screen(s, "\
wpr3287 Session -- Printer Mode\n\
\n\
The wpr3287 printer session can be configured in one of two ways.  The first\n\
method automatically associates the printer session with the current login\n\
session.  The second method specifies a particular Logical Unit (LU) to use\n\
for the printer session.");

	do {
		printf("\nAssociate the printer session with the current login session (y/n) [%s]: ",
			strcmp(s->printerlu, ".")? "n": "y");
		fflush(stdout);
		rc = getyn(!strcmp(s->printerlu, "."));
		switch (rc) {
		case YN_ERR:
		    	return -1;
		case FALSE:
			if (!strcmp(s->printerlu, "."))
				s->printerlu[0] = '\0';
			break;
		case TRUE:
			strcpy(s->printerlu, ".");
			break;
		}
	} while (rc < 0);


	if (strcmp(s->printerlu, ".") && get_printerlu(s, 0) < 0)
	    	return -1;
	return 0;
}

static int
get_printerlu(session_t *s, int explain)
{
	if (explain) {
	    new_screen(s, "\
wpr3287 Session -- Printer Logical Unit (LU) Name\n\
\n\
If the wpr3287 printer session is associated with a particular Logical Unit,\n\
then that Logical Unit must be configured explicitly.");
	}

	for (;;) {
		char tbuf[STR_SIZE];

	    	if (s->printerlu[0])
			printf("\nEnter printer Logical Unit (LU) name: [%s] ",
				s->printerlu);
		else
			printf("\nEnter printer Logical Unit (LU) name: ");
		fflush(stdout);
		if (get_input(tbuf, STR_SIZE) == NULL)
			return -1;
		if (!tbuf[0]) {
		    	if (s->printerlu[0])
			    	break;
			else
			    	continue;
		} else {
		    	strcpy(s->printerlu, tbuf);
			break;
		}
	}

	return 0;
}

static int
get_printer(session_t *s)
{
	char tbuf[STR_SIZE];
    	int i;
	char *ptr;
	unsigned long u;
	char cbuf[STR_SIZE];
	int matching_printer = -1;

	new_screen(s, "\
wpr3287 Session -- Windows Printer Name\n\
\n\
The wpr3287 session can use the Windows default printer as its real printer,\n\
or you can specify a particular Windows printer.  You can specify a local\n\
printer, or specify a remote printer with a UNC path, e.g.,\n\
'\\\\server\\printer22'.  You can specify the Windows default printer with\n\
the name 'default'.");

	(void) redisplay_printer(s->printer, cbuf);

	enum_printers();
	if (num_printers) {
		printf("\nWindows printers (system default is '*'):\n");
		for (i = 0; i < num_printers; i++) {
			printf(" %2d. %c %s\n", i + 1,
				strcasecmp(default_printer,
				    printer_info[i].pName)? ' ': '*',
				printer_info[i].pName);
			if (!strcasecmp(cbuf, printer_info[i].pName)) {
				matching_printer = i;
			}
		}
		printf(" %2d.   Other\n", num_printers + 1);
		if (cbuf[0] && matching_printer < 0) {
			matching_printer = num_printers;
		}
		for (;;) {
			if (s->printer[0])
				printf("\nEnter Windows printer (1-%d): [%d] ",
				    num_printers + 1, matching_printer + 1);
			else
				printf("\nEnter Windows printer (1-%d): [use system default] ",
					num_printers + 1);
			fflush(stdout);
			if (get_input(tbuf, STR_SIZE) == NULL)
				return -1;
			if (!tbuf[0]) {
				if (!s->printer[0] ||
					matching_printer < num_printers) {
					break;
				}
				/*
				 * An interesting hack. If they entered
				 * nothing, and the default is 'other',
				 * pretend they typed in the number for
				 * 'other'.
				 */
				snprintf(tbuf, sizeof(tbuf), "%d",
					matching_printer + 1);
			}
			if (!strcmp(tbuf, "default")) {
			    	s->printer[0] = '\0';
				break;
			}
			u = strtoul(tbuf, &ptr, 10);
			if (*ptr != '\0' || u == 0 ||
				    u > num_printers + 1)
				continue;
			if (u == num_printers + 1) {
				if (get_printer_name(cbuf, tbuf) < 0)
					return -1;
				strcpy(s->printer, tbuf);
				break;
			}
			strcpy(s->printer, printer_info[u - 1].pName);
			break;
		}
	} else {
		if (get_printer_name(cbuf, tbuf) < 0)
			return -1;
		strcpy(s->printer, tbuf);
	}

	/*
	 * If the resulting printer name is a UNC path, double the
	 * backslashes.
	 */
	(void) fixup_printer(s);
	return 0;
}

static int
get_printercp(session_t *s)
{
    	char buf[STR_SIZE];

	new_screen(s, "\
wpr3287 Session -- Printer Code Page\n\
\n\
By default, wpr3287 uses the system's default ANSI code page.  You can\n\
override that code page here, or specify 'default' to use the system ANSI code\n\
page.");

	for (;;) {
	    	int cp;

		printf("\nPrinter code page [%s]: ",
			s->printercp[0]? s->printercp: "default");
		fflush(stdout);
		if (get_input(buf, STR_SIZE) == NULL)
			return -1;
		if (!buf[0])
		    	break;
		if (!strcmp(buf, "default")) {
		    	s->printercp[0] = '\0';
			break;
		}
		cp = atoi(buf);
		if (cp <= 0) {
		    	printf("Invald code page\n");
		} else {
		    	strcpy(s->printercp, buf);
			break;
		}
	}

	return 0;
}

static int
get_keymaps(session_t *s)
{
	km_t *km;

	new_screen(s, "\
Keymaps\n\
\n\
A keymap is a mapping from the PC keyboard to the virtual 3270 keyboard.\n\
You can override the default keymap and specify one or more built-in or \n\
user-defined keymaps, separated by commas.");

	printf("\n");

	for (km = km_first; km != NULL; km = km->next) {
		printf(" %s\n", km->name);
		if (km->description[0])
			printf("  %s", km->description);
		printf("\n");
	}

	for (;;) {
	    	char inbuf[STR_SIZE];
	    	char tknbuf[STR_SIZE];
		char *t;
		char *buf;
		int wrong = FALSE;

	    	printf("\nEnter keymap name(s) [%s]: ",
			s->keymaps[0]? s->keymaps: CHOICE_NONE);
		fflush(stdout);
		if (get_input(inbuf, sizeof(inbuf)) == NULL)
			return -1;
		if (!inbuf[0])
		    	break;
		if (!strcmp(inbuf, CHOICE_NONE)) {
		    	s->keymaps[0] = '\0';
			break;
		}
		strcpy(tknbuf, inbuf);
		wrong = FALSE;
		buf = tknbuf;
		while (!wrong && (t = strtok(buf, ",")) != NULL) {
		    	buf = NULL;
			for (km = km_first; km != NULL; km = km->next) {
				if (!strcasecmp(t, km->name))
					break;
			}
			if (km == NULL) {
			    	printf("\nInvalid keymap name '%s'\n", t);
				wrong = TRUE;
				break;
			}
		}
		if (!wrong) {
			strcpy(s->keymaps, inbuf);
			break;
		}
	}
	return 0;
}

static int
get_embed(session_t *s)
{
	int rc;

	new_screen(s, "\
Embed Keymaps\n\
\n\
If selected, this option causes any selected keymaps to be copied into the\n\
session file, instead of being found at runtime.");

	do {
		printf("\nEmbed keymaps? (y/n) [%s] ",
			(s->flags & WF_EMBED_KEYMAPS)? "y": "n");
		fflush(stdout);
		rc = getyn((s->flags & WF_EMBED_KEYMAPS) != 0);
		switch (rc) {
		case YN_ERR:
			return -1;
		case TRUE:
		    	s->flags |= WF_EMBED_KEYMAPS;
			break;
		case FALSE:
		    	s->flags &= ~WF_EMBED_KEYMAPS;
			break;
		}
	} while (rc < 0);
	return 0;
}

static int
get_fontsize(session_t *s)
{
	new_screen(s, "\
Font Size\n\
\n\
Allows the font size (character height in pixels) to be specified for the\n\
wc3270 window.  The size must be between 5 and 72.  The default is 12.");

	for (;;) {
	    	char inbuf[STR_SIZE];
		unsigned long u;
		char *ptr;

		printf("\nFont size (5 to 72) [%u]: ",
			s->point_size? s->point_size: 12);
		fflush(stdout);
		if (get_input(inbuf, sizeof(inbuf)) == NULL)
			return -1;
		if (!inbuf[0])
		    	break;
		if (!strcasecmp(inbuf, CHOICE_NONE)) {
		    	s->point_size = 0;
			break;
		}
		u = strtoul(inbuf, &ptr, 10);
		if (*ptr != '\0' || u == 0 || u < 5 || u > 72)
			continue;
		s->point_size = (unsigned char)u;
		break;
	}
	return 0;
}

static int
get_background(session_t *s)
{
	new_screen(s, "\
Background Color\n\
\n\
This option selects whether the screen background is black (the default) or\n\
white.");

	for (;;) {
	    	char inbuf[STR_SIZE];

		printf("\nBackground color? (black/white) [%s] ",
			(s->flags & WF_WHITE_BG)? "white": "black");
		fflush(stdout);
		if (get_input(inbuf, sizeof(inbuf)) == NULL)
			return -1;
		if (!inbuf[0])
		    	break;
		if (!strcasecmp(inbuf, "black")) {
		    	s->flags &= ~WF_WHITE_BG;
			break;
		} else if (!strcasecmp(inbuf, "white")) {
		    	s->flags |= WF_WHITE_BG;
			break;
		}
	}
	return 0;
}

static int
get_menubar(session_t *s)
{
	int rc;

	new_screen(s, "\
Menu Bar\n\
\n\
This option selects whether the menu bar is displayed on the screen.");

	do  {
		printf("\nDisplay menu bar? (y/n) [%s] ",
			(s->flags & WF_NO_MENUBAR)? "n": "y");
		fflush(stdout);
		rc = getyn(!(s->flags & WF_NO_MENUBAR));
		switch (rc) {
		case YN_ERR:
			return -1;
		case FALSE:
		    	s->flags |= WF_NO_MENUBAR;
			break;
		case TRUE:
		    	s->flags &= ~WF_NO_MENUBAR;
			break;
		}
	} while (rc < 0);
	return 0;
}

typedef enum {
	SP_REPLACE,	/* replace uneditable file */
	SP_CREATE,	/* create new file */
	SP_UPDATE,	/* update editable file */
	N_SP
} sp_t;

static char *how_name[N_SP] = {
	"Replace",
	"Create",
	"Update"
};

static src_t
summarize_and_proceed(session_t *s, sp_t how, char *path, char *session_name)
{
    	int rc;
	char choicebuf[32];

	for (;;) {
	    	int done = 0;
		char *cp = "?";
		int i;

		for (i = 0; charsets[i].name != NULL; i++)
			if (!strcmp(charsets[i].name, s->charset)) {
			    	cp = charsets[i].hostcp;
				break;
			}

		new_screen(s, "");

		printf("%3d. Host ................... : %s\n", MN_HOST,
			strcmp(s->host, CHOICE_NONE)? s->host: DISPLAY_NONE);
		printf("%3d. Logical Unit Name ...... : %s\n", MN_LU,
			s->luname[0]? s->luname: DISPLAY_NONE);
		printf("%3d. TCP Port ............... : %d\n", MN_PORT,
			(int)s->port);
		printf("%3d. Model Number ........... : %d "
			"(%d rows x %d columns)\n", MN_MODEL,
			(int)s->model, wrows[s->model], wcols[s->model]);
		if (is_nt) {
			printf("%3d.  Oversize .............. : ", MN_OVERSIZE);
			if (s->ov_rows || s->ov_cols)
				printf("%u rows x %u columns\n",
					s->ov_rows, s->ov_cols);
			else
				printf(DISPLAY_NONE"\n");
		}
		printf("%3d. Character Set .......... : %s (CP %s)\n",
			MN_CHARSET, s->charset, cp);
#if defined(HAVE_LIBSSL) /*[*/
		printf("%3d. SSL Tunnel ............. : %s\n", MN_SSL,
			s->ssl? "Yes": "No");
		printf("%3d. Verify host certificates : %s\n", MN_VERIFY,
			(s->flags & WF_VERIFY_HOST_CERTS)? "Yes": "No");
#endif /*]*/
		printf("%3d. Proxy .................. : %s\n", MN_PROXY,
			s->proxy_type[0]? s->proxy_type: DISPLAY_NONE);
		if (s->proxy_type[0]) {
			printf("%3d.  Proxy Server .......... : %s\n",
				MN_PROXY_SERVER, s->proxy_host);
			if (s->proxy_port[0])
				printf("%3d.  Proxy Server TCP Port . : %s\n",
					MN_PROXY_PORT, s->proxy_port);
		}
		printf("%3d. wpr3287 Printer Session  : %s\n", MN_3287,
			s->wpr3287? "Yes": "No");
		if (s->wpr3287) {
			char pbuf[STR_SIZE];

			printf("%3d.  wpr3287 Mode .......... : ",
				MN_3287_MODE);
			if (!strcmp(s->printerlu, "."))
				printf("Associate\n");
			else {
				printf("LU\n");
				printf("%3d.  wpr3287 LU ............ : %s\n",
					MN_3287_LU, s->printerlu);
			}
			(void) redisplay_printer(s->printer, pbuf);
			printf("%3d.  wpr3287 Windows printer : %s\n",
				MN_3287_PRINTER,
				s->printer[0]? pbuf: "(system default)");
			printf("%3d.  wpr3287 Code Page ..... : ",
				MN_3287_CODEPAGE);
			if (s->printercp[0])
			    	printf("%s\n", s->printercp);
			else
			    	printf("(system ANSI default of %d)\n",
					GetACP());
		}
		printf("%3d. Keymaps ................ : %s\n", MN_KEYMAPS,
			s->keymaps[0]? s->keymaps: DISPLAY_NONE);
		if (s->keymaps[0])
			printf("%3d.  Embed Keymaps ......... : %s\n",
				MN_EMBED_KEYMAPS,
				(s->flags & WF_EMBED_KEYMAPS)? "Yes": "No");
		if (is_nt)
			printf("%3d. Font Size .............. : %u\n",
				MN_FONT_SIZE,
				s->point_size? s->point_size: 12);
		printf("%3d. Background Color ....... : %s\n", MN_BG,
			(s->flags & WF_WHITE_BG)? "white": "black");
		printf("%3d. Menu Bar ............... : %s\n", MN_MENUBAR,
			(s->flags & WF_NO_MENUBAR)? "No": "Yes");

		for (;;) {
		    	int invalid = 0;
			int was_wpr3287 = 0;

			printf("\nEnter item number to change: [none] ");
			fflush(stdout);
			if (get_input(choicebuf, sizeof(choicebuf)) == NULL)
				return SRC_ERR;
			if (!choicebuf[0]) {
				done = 1;
				break;
			}
			switch (atoi(choicebuf)) {
			case MN_HOST:
				if (get_host(s) < 0)
					return SRC_ERR;
				break;
			case MN_LU:
				if (get_lu(s) < 0)
					return SRC_ERR;
				break;
			case MN_PORT:
				if (get_port(s) < 0)
					return SRC_ERR;
				break;
			case MN_MODEL:
				if (get_model(s) < 0)
					return SRC_ERR;
				break;
			case MN_OVERSIZE:
				if (is_nt) {
					if (get_oversize(s) < 0)
						return SRC_ERR;
				} else {
					printf("Invalid entry.\n");
					invalid = 1;
				}
				break;
			case MN_CHARSET:
				if (get_charset(s) < 0)
					return SRC_ERR;
				break;
#if defined(HAVE_LIBSSL) /*[*/
			case MN_SSL:
				if (get_ssl(s) < 0)
					return SRC_ERR;
				break;
			case MN_VERIFY:
				if (get_verify(s) < 0)
					return SRC_ERR;
				break;
#endif /*]*/
			case MN_PROXY:
				if (get_proxy(s) < 0)
					return SRC_ERR;
				break;
			case MN_PROXY_SERVER:
				if (s->proxy_type[0]) {
					if (get_proxy_server(s) < 0)
						return SRC_ERR;
				} else {
					printf("Invalid entry.\n");
					invalid = 1;
				}
				break;
			case MN_PROXY_PORT:
				if (s->proxy_type[0]) {
					if (get_proxy_server_port(s) < 0)
						return SRC_ERR;
				} else {
					printf("Invalid entry.\n");
					invalid = 1;
				}
				break;
			case MN_3287:
				was_wpr3287 = s->wpr3287;
				if (get_wpr3287(s) < 0)
					return SRC_ERR;
				if (s->wpr3287 && !was_wpr3287) {
					if (get_printer_mode(s) < 0)
						return SRC_ERR;
				}
				break;
			case MN_3287_MODE:
				if (s->wpr3287) {
					if (get_printer_mode(s) < 0)
						return SRC_ERR;
				} else {
					printf("Invalid entry.\n");
					invalid = 1;
				}
				break;
			case MN_3287_LU:
				if (s->wpr3287 && strcmp(s->printerlu, ".")) {
					if (get_printerlu(s, 1) < 0)
						return SRC_ERR;
				} else {
					printf("Invalid entry.\n");
					invalid = 1;
				}
				break;
			case MN_3287_PRINTER:
				if (s->wpr3287) {
					if (get_printer(s) < 0)
						return SRC_ERR;
				} else {
					printf("Invalid entry.\n");
					invalid = 1;
				}
				break;
			case MN_3287_CODEPAGE:
				if (s->wpr3287) {
					if (get_printercp(s) < 0)
						return SRC_ERR;
				} else {
					printf("Invalid entry.\n");
					invalid = 1;
				}
				break;
			case MN_KEYMAPS:
				if (get_keymaps(s) < 0)
					return SRC_ERR;
				break;
			case MN_EMBED_KEYMAPS:
				if (get_embed(s) < 0)
				    	return SRC_ERR;
				break;
			case MN_FONT_SIZE:
				if (is_nt) {
					if (get_fontsize(s) < 0)
						return SRC_ERR;
				} else {
					printf("Invalid entry.\n");
					invalid = 1;
				}
				break;
			case MN_BG:
				if (get_background(s) < 0)
				    	return SRC_ERR;
				break;
			case MN_MENUBAR:
				if (get_menubar(s) < 0)
				    	return SRC_ERR;
				break;
			default:
				printf("Invalid entry.\n");
				invalid = 1;
				break;
			}

			if (!invalid)
				break;
		}
		if (done)
			break;
	}

	for (;;) {
		printf("\nSession file is '%s%s'.\n%s it? (y/n) [y] ",
			(how == SP_CREATE)? session_name: path,
			(how == SP_CREATE)? SESS_SUFFIX: "",
			how_name[how]);
		fflush(stdout);
		rc = getyn(TRUE);
		if (rc == YN_ERR || rc == FALSE)
			return SRC_ERR;
		if (rc == TRUE)
			break;
	}

	if (how == SP_CREATE && is_nt) {
		char ac[STR_SIZE];

		/* Ask where they want the file. */
		for (;;) {
		    	printf("\nCreate file for all users or current user? "
				"(all/current) [current] ");
			fflush(stdout);
			if (get_input(ac, STR_SIZE) == NULL) {
				return SRC_ERR;
			}
			if (!ac[0] || !strcasecmp(ac, "current")) {
				return SRC_USER;
			} else if (!strcasecmp(ac, "all")) {
				return SRC_ALL;
			} else {
				printf("Please answer (a)ll or "
					"(c)urrent.\n\n");
			}
		}
	}

	return SRC_USER;
}

static wchar_t *
reg_font_from_cset(char *cset, int *codepage)
{
    	int i, j;
	wchar_t *cpname = NULL;
	wchar_t data[1024];
	DWORD dlen;
	HKEY key;
	static wchar_t font[1024];
	DWORD type;

	*codepage = 0;

    	/* Search the table for a match. */
	for (i = 0; charsets[i].name != NULL; i++) {
	    	if (!strcmp(cset, charsets[i].name)) {
		    	cpname = charsets[i].codepage;
		    	break;
		}
	}

	/* If no match, use Lucida Console. */
	if (cpname == NULL)
	    	return L"Lucida Console";

	/*
	 * Look in the registry for the console font associated with the
	 * Windows code page.
	 */
	if (RegOpenKeyEx(HKEY_LOCAL_MACHINE,
		    "Software\\Microsoft\\Windows NT\\CurrentVersion\\"
		    "Console\\TrueTypeFont",
		    0,
		    KEY_READ,
		    &key) != ERROR_SUCCESS) {
	    	printf("RegOpenKey failed -- cannot find font\n");
		return L"Lucida Console";
	}
	dlen = sizeof(data);
    	if (RegQueryValueExW(key,
		    cpname,
		    NULL,
		    &type,
		    (LPVOID)data,
		    &dlen) != ERROR_SUCCESS) {
	    	/* No codepage-specific match, try the default. */
	    	dlen = sizeof(data);
	    	if (RegQueryValueExW(key, L"0", NULL, &type, (LPVOID)data,
			    &dlen) != ERROR_SUCCESS) {
			RegCloseKey(key);
			printf("RegQueryValueEx failed -- cannot find font\n");
			return L"Lucida Console";
		}
	}
	RegCloseKey(key);
	if (type == REG_MULTI_SZ) {
		for (i = 0; i < dlen/sizeof(wchar_t); i++) {
			if (data[i] == 0x0000)
				break;
		}
		if (i+1 >= dlen/sizeof(wchar_t) || data[i+1] == 0x0000) {
			printf("Bad registry value -- cannot find font\n");
			return L"Lucida Console";
		}
		i++;
	} else
	    i = 0;
	for (j = 0; i < dlen; i++, j++) {
		if (j == 0 && data[i] == L'*')
		    i++;
	    	if ((font[j] = data[i]) == 0x0000)
		    	break;
	}
	*codepage = _wtoi(cpname);
	return font;
}

static int
session_wizard(char *session_name, int explicit_edit, int installed)
{
    	session_t session;
	gs_t rc;
	src_t src;
	char linkpath[MAX_PATH];
	char exepath[MAX_PATH];
	char args[MAX_PATH];
	HRESULT hres;
	char save_session_name[STR_SIZE];
	FILE *f;
	int shortcut_exists;
	char path[MAX_PATH];
	int extra_height = 1;

	/* Start with nothing. */
	(void) memset(&session, '\0', sizeof(session));

	/* Intro screen. */
	if (session_name == NULL) {
		if (intro(&session) < 0) {
			return -1;
		}
	} else {
		new_screen(&session, "");
	}

	/* Get the session name. */
	rc = get_session(session_name, &session, path, explicit_edit, &src);
	switch (rc) {
	case GS_NOEDIT_LEAVE: /* Uneditable, and they don't want to overwrite it. */
	    	return 0;
	default:
	case GS_ERR: /* EOF */
		return -1;
	case GS_OVERWRITE: /* Overwrite old (uneditable). */
		/* Clean out the session. */
		strcpy(save_session_name, session.session);
		memset(&session, '\0', sizeof(session));
		strcpy(session.session, save_session_name);
		/* fall through... */
	case GS_NEW: /* New. */

		/* Get the host name, which defaults to the session name. */
		if (strchr(session.session, ' ') == NULL)
			strcpy(session.host, session.session);
		if (get_host(&session) < 0)
			return -1;

		/* Default eveything else. */
		session.port = 23;
		session.model = 4;
		strcpy(session.charset, "bracket");
		strcpy(session.printerlu, ".");
		/* fall through... */
	case GS_EDIT: /* Edit existing file. */
		/* See what they want to change. */
		src = summarize_and_proceed(&session,
			    (rc == 3)? SP_REPLACE:
			    ((rc == 0)? SP_CREATE: SP_UPDATE),
			    path, session.session);
		if (src == SRC_ERR) {
			return -1;
		}

		if (src == SRC_ALL) {
			/* All users. */
			snprintf(path, MAX_PATH, "%s%s%s", commona,
				session.session, SESS_SUFFIX);
		} else {
			/* Current user. */
			snprintf(path, MAX_PATH, "%s%s%s", mya,
				session.session, SESS_SUFFIX);
		}

		/* Create the session file. */
		printf("\nWriting session file '%s'... ", path);
		fflush(stdout);
		if (create_session_file(&session, path) < 0)
			return -1;
		printf("done\n");
		fflush(stdout);
		break;
	case GS_NOEDIT: /* Don't edit existing file, but we do have a copy of the
		   session. */
		break;
	}

	/* Ask about the shortcut. */
	if (is_nt)
		sprintf(linkpath, "%s%s.lnk",
			(src == SRC_ALL)? common_desktop: desktop,
			session.session);
	else
		sprintf(linkpath, "%s%s.pif", desktop, session.session);
	f = fopen(linkpath, "r");
	if ((shortcut_exists = (f != NULL)))
		fclose(f);
	for (;;) {
	    	printf("\n%s desktop shortcut (y/n) [%s]: ",
			shortcut_exists? "Replace": "Create",
			installed? "y": "n");
		rc = getyn(installed == TRUE);
		if (rc == YN_ERR)
		    	return -1;
		if (rc == FALSE)
		    	return 0;
		if (rc == TRUE)
		    	break;
	}

	/* Create the desktop shorcut. */
	printf("\n%s desktop shortcut '%s'... ",
		shortcut_exists? "Replacing": "Creating", linkpath);
	fflush(stdout);
	sprintf(exepath, "%swc3270.exe", installdir);
	sprintf(args, "+S \"%s\"", path);
	if (!(session.flags & WF_NO_MENUBAR))
	    	extra_height += 2;
	if (is_nt) {
	    	wchar_t *font;
		int codepage = 0;

		font = reg_font_from_cset(session.charset, &codepage);

		hres = CreateLink(
			exepath,		/* path to executable */
			linkpath,		/* where to put the link */
			"wc3270 session",	/* description */
			args,			/* arguments */
			installdir,		/* working directory */
			(session.ov_rows?	/* console rows */
			    session.ov_rows: wrows[session.model]) +
				extra_height,
			session.ov_cols?	/* console cols */
			    session.ov_cols: wcols[session.model],
			font,			/* font */
			session.point_size,	/* point size */
			codepage);		/* code page */
	} else
		hres = Piffle(
			session.session,	/* window title */
			exepath,		/* path to executable */
			linkpath,		/* where to put the link */
			"wc3270 session",	/* description */
			args,			/* arguments */
			installdir,		/* working directory */
			wrows[session.model] + extra_height,
			wcols[session.model],
						/* console rows, columns */
			"Lucida Console");	/* font */

	if (SUCCEEDED(hres)) {
		printf("done\n");
		fflush(stdout);
		return 0;
	} else {
		printf("Failed\n");
		fflush(stdout);
		return -1;
	}
}

/* Embed the selected keymaps in the session file. */
static void
embed_keymaps(session_t *session, FILE *f)
{
    	char keymaps[STR_SIZE];
	char *keymap;
	char *ptr = keymaps;
	km_t *km;
	char *pfx = "! Embedded user-defined keymaps\n";

	strcpy(keymaps, session->keymaps);
	while ((keymap = strtok(ptr, ",")) != NULL) {
	    	ptr = NULL;
		for (km = km_first; km != NULL; km = km->next) {
		    	if (!strcasecmp(keymap, km->name)) {
			    	if (km->def_both) {
				    	fprintf(f,
						"%swc3270.%s.%s:"
						"\\n\\\n%s\n",
						pfx, ResKeymap, keymap,
						km->def_both);
					pfx = "";
				}
			    	if (km->def_3270) {
				    	fprintf(f,
						"%swc3270.%s.%s.3270:"
						"\\n\\\n%s\n",
						pfx, ResKeymap, keymap,
						km->def_3270);
					pfx = "";
				}
			    	if (km->def_nvt) {
				    	fprintf(f,
						"%swc3270.%s.%s.nvt:"
						"\\n\\\n%s\n",
						pfx, ResKeymap, keymap,
						km->def_nvt);
					pfx = "";
				}
			    	break;
			}
		}
	}
}

/* Create the session file. */
static int
create_session_file(session_t *session, char *path)
{
    	FILE *f;
	time_t t;
	int bracket;
	long eot;
	unsigned long csum;
	int i;
	char buf[1024];

	f = fopen(path, "w+");
	if (f == NULL) {
		perror("Cannot create session file");
		return -1;
	}

	fprintf(f, "! wc3270 session '%s'\n", session->session);

	t = time(NULL);
	fprintf(f, "! Created or modified by the wc3270 %s Session Wizard %s",
		wversion, ctime(&t));

	if (strcmp(session->host, CHOICE_NONE)) {
		bracket = (strchr(session->host, ':') != NULL);
		fprintf(f, "wc3270.%s: ", ResHostname);
		if (session->ssl)
			fprintf(f, "L:");
		if (session->luname[0])
			fprintf(f, "%s@", session->luname);
		fprintf(f, "%s%s%s",
			bracket? "[": "",
			session->host,
			bracket? "]": "");
		if (session->port != 23)
			fprintf(f, ":%d", (int)session->port);
		fprintf(f, "\n");
	} else if (session->port != 23)
	    	fprintf(f, "wc3270.%s: %d\n", ResPort, (int)session->port);

	if (session->proxy_type[0])
	    	fprintf(f, "wc3270.%s: %s:%s%s%s%s%s\n",
			ResProxy,
			session->proxy_type,
			strchr(session->proxy_host, ':')? "[": "",
			session->proxy_host,
			strchr(session->proxy_host, ':')? "]": "",
			session->proxy_port[0]? ":": "",
			session->proxy_port);

	fprintf(f, "wc3270.%s: %d\n", ResModel, (int)session->model);
	if (session->ov_rows || session->ov_cols)
	    	fprintf(f, "wc3270.%s: %ux%u\n", ResOversize,
			session->ov_cols, session->ov_rows);
	fprintf(f, "wc3270.%s: %s\n", ResCharset, session->charset);
	if (session->is_dbcs)
	    	fprintf(f, "wc3270.%s: %s\n", ResAsciiBoxDraw, ResTrue);

	if (session->wpr3287) {
	    	fprintf(f, "wc3270.%s: %s\n", ResPrinterLu, session->printerlu);
		if (session->printer[0])
		    	fprintf(f, "wc3270.%s: %s\n", ResPrinterName,
				session->printer);
		if (session->printercp[0])
		    	fprintf(f, "wc3270.%s: %s\n", ResPrinterCodepage,
				session->printercp);
	}

	if (session->keymaps[0]) {
	    	fprintf(f, "wc3270.%s: %s\n", ResKeymap, session->keymaps);
		if (session->flags & WF_EMBED_KEYMAPS)
		    	embed_keymaps(session, f);
	}

	if (session->flags & WF_AUTO_SHORTCUT)
	    	fprintf(f, "wc3270.%s: %s\n", ResAutoShortcut, ResTrue);

	if (session->flags & WF_WHITE_BG)
	    	fprintf(f, "\
! These resources set the background to white\n\
wc3270." ResConsoleColorForHostColor "NeutralBlack: 15\n\
wc3270." ResConsoleColorForHostColor "NeutralWhite: 0\n");

	if (session->flags & WF_VERIFY_HOST_CERTS)
	    	fprintf(f, "wc3270.%s: %s\n", ResVerifyHostCert, ResTrue);

	if (session->flags & WF_NO_MENUBAR)
	    	fprintf(f, "wc3270.%s: %s\n", ResMenuBar, ResFalse);

	/* Emit the warning. */
	fprintf(f, "\
!\n\
! The following block of text is used to read the contents of this file back\n\
! into the Session Wizard.  If any of the text from the top of the file\n\
! through the line below reading \"Additional resource definitions...\" is\n\
! modified, the Session Wizard will not be able to edit this file.\n\
!");

	/* Write out the session structure in hex. */
	for (i = 0; i < sizeof(*session); i++) {
	    	if (!(i % 32))
		    	fprintf(f, "\n!x");
		fprintf(f, "%02x", ((unsigned char *)session)[i]);
	}
	fprintf(f, "\n");

	/* Save where we are in the file. */
	fflush(f);
	eot = ftell(f);

	/* Go back and read what we wrote. */
	rewind(f);
	csum = 0;
	while (fgets(buf, sizeof(buf), f) != NULL) {
		for (i = 0; buf[i]; i++) {
		    	csum += buf[i] & 0xff;
		}
		if (ftell(f) >= eot)
		    	break;
	}
	fflush(f);

	/* Write out the checksum and structure version. */
	fseek(f, 0, SEEK_END);
	fprintf(f, "!c%08lx %d\n", csum, WIZARD_VER);

	fprintf(f, "!\n\
! Note that in this file, backslash ('\\') characters are used to specify\n\
! escape sequences, such as '\\r' for a Carriage Return character or '\\t'\n\
! for a Tab character.  To include literal backslashes in this file, such as\n\
! in Windows pathnames or UNC paths, they must be doubled, for example:\n\
!\n\
!   Desired text            Must be specified this way\n\
!    C:\\xdir\\file            C:\\\\xdir\\\\file\n\
!    \\\\server\\printer        \\\\\\\\server\\\\printer\n\
!\n\
!*Additional resource definitions can go after this line.\n");

	/* Write out the user's previous extra settings. */
	if (user_settings != NULL)
	    	fprintf(f, "%s", user_settings);

	fclose(f);

	return 0;
}

/* Make sure the console window is long enough. */
static int
resize_window(int rows)
{
    	int rv = 0;
	HANDLE h;
    	CONSOLE_SCREEN_BUFFER_INFO info;

	do {
	    	/* Get a handle to the console. */
		h = CreateFile("CONOUT$",
			GENERIC_READ | GENERIC_WRITE, FILE_SHARE_WRITE, NULL,
			OPEN_EXISTING, 0, NULL);
		if (h == NULL) {
		    	rv = -1;
			break;
		}

		/* Get its current geometry. */
		if (GetConsoleScreenBufferInfo(h, &info) == 0) {
		    	rv = -1;
			break;
		}

		/* If the buffer isn't big enough, make it bigger. */
		if (info.dwSize.Y < rows) {
			COORD new_size;

			new_size.X = info.dwSize.X;
			new_size.Y = rows;

			if (SetConsoleScreenBufferSize(h, new_size) == 0) {
				rv = -1;
				break;
			}
		}

		/* If the window isn't big enough, make it bigger. */
		if (info.srWindow.Bottom - info.srWindow.Top < rows) {
		    	SMALL_RECT sr;

			sr.Top = 0;
			sr.Bottom = rows;
			sr.Left = 0;
			sr.Right = info.srWindow.Right - info.srWindow.Left;

		    	if (SetConsoleWindowInfo(h, TRUE, &sr) == 0) {
				rv = -1;
				break;
			}
		}

	} while(0);

	if (h != NULL)
	    	CloseHandle(h);
	return rv;
}

static void
w_usage(void)
{
    	fprintf(stderr, "Usage: wc3270wiz [session-name]\n"
		        "       wc3270wiz [session-file]\n");
	exit(1);
}

int
main(int argc, char *argv[])
{
	int rc;
	char buf[2];
	char *session_name = NULL;
	char *program = argv[0];
	int installed = FALSE;
	int explicit_edit = FALSE;

	/*
	 * Parse command-line arguments.
	 * For now, there is only one -- the optional name of the session.
	 */
	program = argv[0];
	if (argc > 1 && !strcmp(argv[1], "-e")) {
		explicit_edit = TRUE;
		argc--;
		argv++;
	}
	switch (argc) {
	    case 1:
		break;
	    case 2:
		session_name = argv[1];
		break;
	    default:
	    	w_usage();
		break;
	}

	/* Figure out the version. */
	if (get_version_info() < 0)
	    	return 1;

	/* Get some paths from Windows. */
	if (get_dirs(program, "wc3270", &installdir, &desktop, &mya,
		    &common_desktop, &commona, &installed) < 0)
	    	return -1;

	/* Resize the console window. */
	if (is_nt)
		resize_window(44);
	else
	    	system("mode con lines=50");

	signal(SIGINT, SIG_IGN);

	save_keymaps();

	rc = session_wizard(session_name, explicit_edit, installed);

	printf("\nWizard %s.  [Press <Enter>] ",
		    (rc < 0)? "aborted": "complete");
	fflush(stdout);
	(void) fgets(buf, 2, stdin);

	return 0;
}
