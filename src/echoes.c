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
 * into the next life.  A loop counter advances on each death.
 *
 *      The dungeon resets.  The soul remembers.
 *
 * Classic NetHack is completely untouched unless the player opts in.
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
static long echoes_loop_count = 0L; /* deaths so far in this timeline */

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

/* Write the soul file: the timeline seed, the loop counter, and (only when
   inside an actual game) every object type the hero currently knows. */
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
    (void) fprintf(fp, "loops %ld\n", echoes_loop_count);
    /* Only record discoveries from within an actual game.  At startup the
       objects[] table is not game-initialised and its name_known flags do
       not reflect anything the soul has genuinely learned. */
    if (program_state.in_moveloop || program_state.gameover) {
        for (oindx = FIRST_OBJECT; oindx < NUM_OBJECTS; oindx++)
            if (objects[oindx].oc_name_known)
                (void) fprintf(fp, "disco %d\n", oindx);
    }

    (void) fclose(fp);
}

/* Called at startup, just after the RNG is first initialised (and made
   idempotent, since that init phase can run more than once).  Loads the
   timeline seed and loop count from the soul file, or mints a fresh
   timeline, then pins the gameplay RNG so the dungeon is reproducible. */
void
echoes_init_seed(void)
{
    static boolean loaded = FALSE;
    FILE *fp;
    char buf[BUFSZ];

    if (!echoes_mode())
        return;

    if (!loaded) {
        loaded = TRUE;

        fp = fopen(ECHOES_SOUL_FILE, "r");
        if (fp) {
            while (fgets(buf, (int) sizeof buf, fp)) {
                unsigned long s;
                long l;

                if (sscanf(buf, "seed %lu", &s) == 1) {
                    echoes_timeline_seed = s;
                    echoes_seed_known = TRUE;
                } else if (sscanf(buf, "loops %ld", &l) == 1) {
                    echoes_loop_count = l;
                }
            }
            (void) fclose(fp);
        }

        if (!echoes_seed_known) {
            /* first loop of a new timeline: mint and persist a stable seed */
            echoes_timeline_seed = sys_random_seed();
            echoes_loop_count = 0L;
            echoes_seed_known = TRUE;
            echoes_save_soul();
        }
    }

    /* Re-pin every time this init phase runs.  It can execute more than once
       during startup, and each run re-randomises rn2 via init_random just
       before calling us; without re-pinning, that randomisation would win
       and the dungeon would not be reproducible. */
    if (echoes_seed_known)
        echoes_force_seed(echoes_timeline_seed);
}

/* Called at the end of newgame(): restore the soul's remembered object
   identities, and announce the loop if this is not the first life. */
void
echoes_apply_knowledge(void)
{
    FILE *fp;
    char buf[BUFSZ];

    if (!echoes_mode())
        return;

    fp = fopen(ECHOES_SOUL_FILE, "r");
    if (fp) {
        while (fgets(buf, (int) sizeof buf, fp)) {
            int oindx = 0;

            if (sscanf(buf, "disco %d", &oindx) == 1
                && oindx >= FIRST_OBJECT && oindx < NUM_OBJECTS)
                discover_object(oindx, TRUE, TRUE, FALSE);
        }
        (void) fclose(fp);
    }

    if (echoes_loop_count > 0L)
        pline("The dungeon resets, but your soul remembers.  (Loop %ld)",
              echoes_loop_count + 1L);
}

/* Called at the start of game-over handling, BEFORE end-of-game disclosure
   inflates what is "known".  Advances the loop counter and persists the
   soul's genuine knowledge for the next life. */
void
echoes_on_death(void)
{
    if (!echoes_mode())
        return;

    echoes_loop_count++;
    echoes_save_soul();
}

/*echoes.c*/
