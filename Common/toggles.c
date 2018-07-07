/*
 * Copyright (c) 1993-2009, 2013-2018 Paul Mattes.
 * Copyright (c) 1990, Jeff Sparkes.
 * Copyright (c) 1989, Georgia Tech Research Corporation (GTRC), Atlanta, GA
 *  30332.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the names of Paul Mattes, Jeff Sparkes, GTRC nor the names of
 *       their contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY PAUL MATTES, JEFF SPARKES AND GTRC "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL PAUL MATTES, JEFF SPARKES OR GTRC BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 *	toggles.c
 *		This module handles toggles.
 */

#include "globals.h"
#include "appres.h"
#include "resources.h"

#include "actions.h"
#include "lazya.h"
#include "menubar.h"
#include "popups.h"
#include "toggles.h"
#include "utils.h"

/* Live state of toggles. */
typedef struct _toggle_upcalls {
    struct _toggle_upcalls *next; /* next upcall */
    toggle_upcall_t *upcall;	/* callback */
    unsigned flags;		/* callback flags */
} toggle_upcalls_t;
typedef struct {
    bool changed;		/* has the value changed since init */
    bool supported;		/* is the toggle supported */
    toggle_upcalls_t *upcalls;	/* flags and callbacks */
} toggle_t;
static toggle_t toggle[N_TOGGLES];

/* Extended upcalls. */
typedef struct toggle_extended_upcalls {
    struct toggle_extended_upcalls *next;
    char *name;
    toggle_extended_upcall_t *upcall;
    toggle_extended_done_t *done;
    toggle_extended_canonicalize_t *canonicalize;
    void **address;
    enum resource_type type;
} toggle_extended_upcalls_t;
static toggle_extended_upcalls_t *extended_upcalls;
static toggle_extended_upcalls_t **extended_upcalls_last = &extended_upcalls;
typedef struct toggle_extended_notifies {
    struct toggle_extended_notifies *next;
    toggle_extended_notify_t *notify;
} toggle_extended_notifies_t;
static toggle_extended_notifies_t *extended_notifies;
static toggle_extended_notifies_t **extended_notifies_last = &extended_notifies;

/* Toggle name dictionary. */
toggle_name_t toggle_names[] = {
    { ResMonoCase,        MONOCASE,		false },
    { ResAltCursor,       ALT_CURSOR,		false },
    { ResCursorBlink,     CURSOR_BLINK,		false },
    { ResShowTiming,      SHOW_TIMING,		false },
    { ResCursorPos,       CURSOR_POS,		false },
    { ResTrace,           TRACING,		false },
    { ResDsTrace,         TRACING,		true }, /* compatibility */
    { ResScrollBar,       SCROLL_BAR,		false },
    { ResLineWrap,        LINE_WRAP,		false },
    { ResBlankFill,       BLANK_FILL,		false },
    { ResScreenTrace,     SCREEN_TRACE,		false },
    { ResEventTrace,      TRACING,		true }, /* compatibility */
    { ResMarginedPaste,   MARGINED_PASTE,	false },
    { ResRectangleSelect, RECTANGLE_SELECT,	false },
    { ResCrosshair,	  CROSSHAIR,		false },
    { ResVisibleControl,  VISIBLE_CONTROL,	false },
    { ResAidWait,         AID_WAIT,		false },
    { ResUnderscore,	  UNDERSCORE,		false },
    { ResOverlayPaste,    OVERLAY_PASTE,	false },
    { ResTypeahead,       TYPEAHEAD,		false },
    { ResAplMode,	  APL_MODE,		false },
    { NULL,               0,			false }
};

/*
 * Generic toggle stuff
 */
static void
do_toggle_reason(toggle_index_t ix, enum toggle_type reason)
{
    toggle_t *t = &toggle[ix];
    toggle_upcalls_t *u;

    /*
     * Change the value, call the internal update routine, and reset the
     * menu label(s).
     */
    toggle_toggle(ix);
    for (u = t->upcalls; u != NULL; u = u->next) {
	if (u->upcall != NULL) {
	    u->upcall(ix, reason);
	}
    }
    menubar_retoggle(ix);
}

void
do_toggle(int ix)
{
    do_toggle_reason(ix, TT_INTERACTIVE);
}

void
do_menu_toggle(int ix)
{
    do_toggle_reason(ix, TT_XMENU);
}

/*
 * Called from system initialization code to handle initial toggle settings.
 */
void
initialize_toggles(void)
{
    toggle_index_t ix;

    for (ix = 0; ix < N_TOGGLES; ix++) {
	if (toggled(ix)) {
	    toggle_upcalls_t *u;

	    for (u = toggle[ix].upcalls; u != NULL; u = u->next) {
		if (u->flags & TOGGLE_NEED_INIT) {
		    u->upcall(ix, TT_INITIAL);

		    /* It might have failed. Fix up the menu if it did. */
		    if (!toggled(ix)) {
			menubar_retoggle(ix);
		    }
		}
	    }
	}
    }
}

/*
 * Called from system exit code to handle toggles.
 */
void
toggle_exiting(bool mode _is_unused)
{
    toggle_index_t ix;

    for (ix = 0; ix < N_TOGGLES; ix++) {
	if (toggled(ix)) {
	    toggle_upcalls_t *u;

	    for (u = toggle[ix].upcalls; u != NULL; u = u->next) {
		if (u->flags & TOGGLE_NEED_CLEANUP) {
		    u->upcall(ix, TT_FINAL);
		}
	    }
	}
    }
}

/**
 * Get the current value of an extended toggle.
 * 
 * @param[in] u		Extended upcall struct
 *
 * @returns Current value
 */
static char *
u_value(toggle_extended_upcalls_t *u)
{
    char *value;

    if (u->address != NULL) {
	switch (u->type) {
	case XRM_STRING:
	    value = *(char **)u->address;
	    break;
	case XRM_BOOLEAN:
	    value = *(bool *)u->address? "true": "false";
	    break;
	case XRM_INT:
	    value = lazyaf("%d", *(int *)u->address);
	    break;
	}
    } else {
	value = get_resource(u->name);
    }

    return (*u->canonicalize)(value);
}

/*
 * Toggle/Set action.
 */
static bool
toggle_common(const char *name, bool is_toggle, ia_t ia, unsigned argc,
	const char **argv)
{
    int j;
    int ix;
    toggle_extended_upcalls_t *u = NULL;
    unsigned arg = 0;
    typedef struct {
	toggle_extended_done_t *done;
	bool success;
    } done_success_t;
    done_success_t *dones;
    int n_dones = 0;
    toggle_extended_upcalls_t **done_u;
    int n_done_u = 0;
    bool success = true;
    int d, du;
    toggle_extended_notifies_t *notifies;

    action_debug(name, ia, argc, argv);

    /* Check basic syntax. */
    if (argc < 1) {
	popup_an_error("%s requires at least one argument", name);
	return false;
    }

    dones = (done_success_t *)Malloc(argc * sizeof(done_success_t *));
    done_u = (toggle_extended_upcalls_t **)Malloc(argc
	    * sizeof(toggle_extended_upcalls_t *));

    /* Look up the toggle name. */
    while (arg < argc) {
	for (j = 0; toggle_names[j].name != NULL; j++) {
	    if (!toggle_supported(toggle_names[j].index)) {
		continue;
	    }
	    if (!strcasecmp(argv[arg], toggle_names[j].name)) {
		ix = toggle_names[j].index;
		break;
	    }
	}
	if (toggle_names[j].name == NULL) {
	    for (u = extended_upcalls; u != NULL; u = u->next) {
		if (!strcasecmp(argv[arg], u->name)) {
		    break;
		}
	    }
	}
	if (toggle_names[j].name == NULL && u == NULL) {
	    popup_an_error("%s: Unknown toggle name '%s'", name, argv[arg]);
	    goto failed;
	}

	/* Check for old syntax. */
	if (argc - arg == 1) {
	    if (u != NULL) {
		popup_an_error("%s: '%s' requires a value", name, argv[arg]);
		goto failed;
	    }
	    if (is_toggle || !toggled(ix)) {
		/*
		 * The default for the Toggle() action is to flip the toggle.
		 * The default for the Set() action is to set the toggle.
		 */
		do_toggle_reason(ix, TT_ACTION);
	    }
	    goto done;
	}

	if (u == NULL) {
	    /* Check for explicit Boolean value. */
	    if (!strcasecmp(argv[arg + 1], "set") ||
		    !strcasecmp(argv[arg + 1], "on") ||
		    !strcasecmp(argv[arg + 1], "true") ||
		    !strcasecmp(argv[arg + 1], "1")) {
		if (!toggled(ix)) {
		    do_toggle_reason(ix, TT_ACTION);
		}
	    } else if (!strcasecmp(argv[arg + 1], "clear") ||
		    !strcasecmp(argv[arg + 1], "off") ||
		    !strcasecmp(argv[arg + 1], "false") ||
		    !strcasecmp(argv[arg + 1], "0")) {
		if (toggled(ix)) {
		    do_toggle_reason(ix, TT_ACTION);
		}
	    } else {
		popup_an_error("%s: Unknown keyword '%s' (must be 'Set' "
			"or 'Clear')", name, argv[arg + 1]);
		goto failed;
	    }
	} else {
	    /*
	     * Call an extended toggle, remembering each unique 'done'
	     * function.
	     */
	    if (u->done != NULL) {
		done_u[n_done_u++] = u;
		for (d = 0; d < n_dones; d++) {
		    if (dones[d].done == u->done) {
			break;
		    }
		}
		if (d >= n_dones) {
		    dones[n_dones].done = u->done;
		    dones[n_dones++].success = false;
		}
	    }
	    if (!u->upcall(argv[arg], argv[arg + 1])) {
		goto failed;
	    }
	    if (u->done == NULL) {
		for (notifies = extended_notifies;
		     notifies != NULL;
		     notifies = notifies->next) {
		    char *v = u_value(u);

		    (*notifies->notify)(u->name, v);
		    Free(v);
		}
	    }
	}
	arg += 2;
    }
    goto done;

failed:
    success = false;

done:
    /* Call each of the done functions. */
    for (d = 0; d < n_dones; d++) {
	dones[d].success = (*dones[d].done)(success);
	success &= dones[d].success;
    }

    /* Call each of the notify functions with successful done functions. */
    for (du = 0; du < n_done_u; du++) {
	for (d = 0; d < n_dones; d++) {
	    if (dones[d].done == done_u[du]->done) {
		break;
	    }
	}
	if (d < n_dones && !dones[d].success) {
	    continue;
	}
	for (notifies = extended_notifies;
	     notifies != NULL;
	     notifies = notifies->next) {
	    char *v = u_value(done_u[du]);

	    (*notifies->notify)(done_u[du]->name, v);
	    Free(v);
	}
    }

    Free(dones);
    Free(done_u);
    return success;
}

/*
 * Toggle action.
 *  Toggle(toggleName)
 *    flips the value
 *  Toggle(toggleName,value[,toggleName,value...])
 *    sets an explicit value
 * For old-style Boolean toggles, values can be Set/Clear On/Off True/False 1/0
 */
static bool
Toggle_action(ia_t ia, unsigned argc, const char **argv)
{
    return toggle_common("Toggle", true, ia, argc, argv);
}

/*
 * Set action. Near-alias for 'toggle'.
 *  Set(toggleName)
 *     sets the value to 'True'
 *  Set(toggleName,value[,toggleName,value...])
 *    sets an explicit value
 * For old-style Boolean toggles, values can be Set/Clear On/Off True/False 1/0
 */
static bool
Set_action(ia_t ia, unsigned argc, const char **argv)
{
    return toggle_common("Set", false, ia, argc, argv);
}

/**
 * Toggles module registration.
 */
void
toggles_register(void)
{
    static action_table_t toggle_actions[] = {
	{ "Toggle",		Toggle_action,	ACTION_KE },
	{ "Set",		Set_action,	ACTION_KE }
    };

    /* Register the cleanup routine. */
    register_schange(ST_EXITING, toggle_exiting);

    /* Register the actions. */
    register_actions(toggle_actions, array_count(toggle_actions));
}

/**
 * Flip the value of a toggle without notifying anyone.
 *
 * @param ix	Toggle index
 */
void
toggle_toggle(toggle_index_t ix)
{
    set_toggle(ix, !toggled(ix));
}

/**
 * Set the value of a toggle, without notifying anyone.
 *
 * @param ix	Toggle index
 */
void
set_toggle(toggle_index_t ix, bool value)
{
    appres.toggle[ix] = value;
    toggle[ix].changed = true;
}

/**
 * Set the initial value of a toggle, which does not include marking it
 * changed or notifying anyone.
 *
 * @param ix	Toggle index
 */
void
set_toggle_initial(toggle_index_t ix, bool value)
{
    appres.toggle[ix] = value;
}

/**
 * Return current state of a toggle.
 *
 * @param ix	Toggle index
 *
 * @return Toggle state
 */
bool
toggled(toggle_index_t ix)
{
    return appres.toggle[ix];
}

/**
 * Return change status of a toggle.
 *
 * @param ix	Toggle index
 *
 * @return true if changed, false otherwise
 */
bool
toggle_changed(toggle_index_t ix)
{
    return toggle[ix].changed;
}

/**
 * Check for a toggle being supported in this app.
 *
 * @param[in] ix	Toggle index
 *
 * @return true if supported, false otherwise.
 */
bool
toggle_supported(toggle_index_t ix)
{
    return toggle[ix].supported;
}

/**
 * Register a group of toggle callbacks.
 *
 * @param[in] toggles	Array of callbacks to register
 * @param[in] count	Number of elements in toggles[]
 */
void
register_toggles(toggle_register_t toggles[], unsigned count)
{
    unsigned i;

    for (i = 0; i < count; i++) {
	toggle_upcalls_t *u;

	toggle[toggles[i].ix].supported = true;

	u = (toggle_upcalls_t *)Malloc(sizeof(toggle_upcalls_t));
	u->next = toggle[toggles[i].ix].upcalls;
	u->upcall = toggles[i].upcall;
	u->flags = toggles[i].flags;
	toggle[toggles[i].ix].upcalls = u;
    }
}

/**
 * Default canonicalization function. Just a pass-through.
 *
 * @param[in] value	Value to canonicalize
 *
 * @returns Canonicalized value
 */
static char *
default_canonicalize(const char *value)
{
    return value? NewString(value): NULL;
}

/**
 * Register an extended toggle.
 *
 * @param[in] name	Toggle name
 * @param[in] upcall	Value-change upcall
 * @param[in] done	Done upcall
 * @param[in] canonicalize Canonicalization upcall
 * @param[in] address	Address of value in appres
 * @param[in] type	Resource type
 */
void
register_extended_toggle(const char *name, toggle_extended_upcall_t upcall,
	toggle_extended_done_t done,
	toggle_extended_canonicalize_t canonicalize, void **address,
	enum resource_type type)
{
    toggle_extended_upcalls_t *u;
    toggle_extended_notifies_t *notifies;
    char *v;

    /* Register the toggle. */
    u = (toggle_extended_upcalls_t *)Malloc(sizeof(toggle_extended_upcalls_t)
	    + strlen(name) + 1);
    u->next = NULL;
    u->name = (char *)(u + 1);
    strcpy(u->name, name);
    u->upcall = upcall;
    u->done = done;
    u->canonicalize = canonicalize? canonicalize: default_canonicalize;
    u->address = address;
    u->type = type;

    *extended_upcalls_last = u;
    extended_upcalls_last = &u->next;

    /* Notify with the current value. */
    v = u_value(u);
    for (notifies = extended_notifies;
	 notifies != NULL;
	 notifies = notifies->next) {

	(*notifies->notify)(name, v);
    }
    Free(v);
}

/**
 * Register an extended toggle notify upcall -- called when a toggle is
 * changed successfully.
 *
 * @param[in] notify	Notify upcall.
 */
void
register_extended_toggle_notify(toggle_extended_notify_t notify)
{
    toggle_extended_upcalls_t *u;

    /* Register the notify function. */
    toggle_extended_notifies_t *notifies =
	Malloc(sizeof(toggle_extended_notifies_t));

    notifies->next = NULL;
    notifies->notify = notify;

    *extended_notifies_last = notifies;
    extended_notifies_last = &notifies->next;

    /* Call it with everything registered so far. */
    for (u = extended_upcalls; u != NULL; u = u->next) {
	char *v = u_value(u);
	(*notify)(u->name, v);
	Free(v);
    }
}

/**
 * Force notification of a toggle change.
 */
void
force_toggle_notify(const char *name)
{
    toggle_extended_upcalls_t *u;
    toggle_extended_notifies_t *n;
    char *v;

    for (u = extended_upcalls; u != NULL; u = u->next) {
	if (!strcmp(name, u->name)) {
	    break;
	}
    }
    if (u == NULL) {
	return;
    }

    /* Notify with the current value. */
    v = u_value(u);
    for (n = extended_notifies; n != NULL; n = n->next) {
	(*n->notify)(name, v);
    }
    Free(v);
}
