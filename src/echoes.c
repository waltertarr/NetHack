/* NetHack 5.0	echoes.c	$NHDT-Date: 0 $  $NHDT-Branch: NetHack-5.0 $ */
/* Copyright (c) 2026 by the Echoes of the Soul project           */
/* NetHack may be freely redistributed.  See license for details. */

/*
 * Echoes of the Soul - optional time-loop mode (vertical slice).
 *
 * When enabled (the environment variable NETHACK_ECHOES is set to a
 * non-zero value), the dungeon is regenerated from a stable "timeline
 * seed" on every new game, and the soul's knowledge -- currently the set
 * of identified object types -- is persisted to a soul file and restored
 * into the next life.
 *
 *      The dungeon resets.  The soul remembers.
 *
 * Classic NetHack is completely untouched unless the player opts in.  This
 * is intentionally minimal: it proves the core loop (die -> same dungeon ->
 * keep your knowledge) before the larger Echoes systems are layered on.
 */

#include "hack.h"

/* Provided by port-specific code; declared locally here exactly as rnd.c
   does, since its extern.h prototype lives under an #ifdef UNIX guard. */
extern unsigned long sys_random_seed(void);

#define ECHOES_SOUL_FILE "echoes.soul"

static boolean echoes_checked = FALSE;
static boolean echoes_enabled = FALSE;
static boolean echoes_seed_known = FALSE;
static unsigned long echoes_timeline_seed = 0UL;

/* Is the current process running in Echoes of the Soul mode? */
boolean
echoes_mode(void)
{
    if (!echoes_checked) {
        const char *ev = getenv("NETHACK_ECHOES");

        echoes_enabled = (ev && *ev && *ev != '0');
        echoes_checked = TRUE;
    }
    return echoes_enabled;
}

/* Write the soul file: the timeline seed followed by every object type the
   hero currently knows.  Safe to call before objects are initialised -- it
   simply records whatever is known so far (typically nothing yet). */
void
echoes_save_soul(void)
{
    FILE *fp;
    int oindx;

    if (!echoes_mode() || !echoes_seed_known)
        return;

    fp = fopen(ECHOES_SOUL_FILE, "w");
    if (!fp)
        return;

    (void) fprintf(fp, "seed %lu\n", echoes_timeline_seed);
    for (oindx = FIRST_OBJECT; oindx < NUM_OBJECTS; oindx++)
        if (objects[oindx].oc_name_known)
            (void) fprintf(fp, "disco %d\n", oindx);

    (void) fclose(fp);
}

/* Called once at startup, just after the RNG is first initialised.  Loads
   the timeline seed from the soul file, or mints a fresh one for a new
   timeline, then pins the gameplay RNG so the dungeon is reproducible. */
void
echoes_init_seed(void)
{
    FILE *fp;
    unsigned long seed = 0UL;

    if (!echoes_mode())
        return;

    fp = fopen(ECHOES_SOUL_FILE, "r");
    if (fp) {
        if (fscanf(fp, "seed %lu", &seed) == 1) {
            echoes_timeline_seed = seed;
            echoes_seed_known = TRUE;
        }
        (void) fclose(fp);
    }

    if (!echoes_seed_known) {
        /* first loop of a new timeline: mint and persist a stable seed */
        echoes_timeline_seed = sys_random_seed();
        echoes_seed_known = TRUE;
        echoes_save_soul();
    }

    echoes_force_seed(echoes_timeline_seed);
}

/* Called at the end of newgame(): restore the soul's remembered object
   identities into the freshly created hero. */
void
echoes_apply_knowledge(void)
{
    FILE *fp;
    char buf[BUFSZ];

    if (!echoes_mode())
        return;

    fp = fopen(ECHOES_SOUL_FILE, "r");
    if (!fp)
        return;

    while (fgets(buf, (int) sizeof buf, fp)) {
        int oindx = 0;

        if (sscanf(buf, "disco %d", &oindx) == 1
            && oindx >= FIRST_OBJECT && oindx < NUM_OBJECTS)
            discover_object(oindx, TRUE, TRUE, FALSE);
    }

    (void) fclose(fp);
}

/*echoes.c*/
