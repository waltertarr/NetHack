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

/* Memory-tree upgrades: each gates a category of soul knowledge across loops. */
#define ECHOES_MEM_ITEMS    0x1L /* identified object types persist */
#define ECHOES_MEM_MONSTERS 0x2L /* bestiary (encountered species) persists */
#define ECHOES_MEM_SPELLS   0x4L /* learned spells persist */

static boolean echoes_checked = FALSE;
static boolean echoes_enabled = FALSE;
static boolean echoes_seed_known = FALSE;
static unsigned long echoes_timeline_seed = 0UL;
static long echoes_loop_count = 0L; /* deaths so far in this timeline */
static long echoes_fragments = 0L;  /* memory fragments: the soul's meta-currency */
static long echoes_upgrades = 0L;   /* unlocked memory-tree upgrades (bitmask) */
static boolean echoes_game_ready = FALSE; /* a game is initialised enough that
                                             objects[]/mvitals[]/spl_book[] are real */

/* Is the headless self-test active? (NETHACK_ECHOES_DUMP names a file.)
   In this mode interactive character selection is skipped -- the character
   must be fully specified via NETHACKOPTIONS. */
boolean
echoes_selftest_active(void)
{
    return (boolean) (getenv("NETHACK_ECHOES_DUMP") != 0
                      || getenv("NETHACK_ECHOES_KINJECT") != 0
                      || getenv("NETHACK_ECHOES_KVERIFY") != 0
                      || getenv("NETHACK_ECHOES_REEXEC_TARGET") != 0
                      || getenv("NETHACK_ECHOES_STATUS") != 0
                      || getenv("NETHACK_ECHOES_SUMMARY") != 0);
}

/* The fixed "Soulbound Wanderer" base.  Echoes starts classless -- the player
   does not choose a role -- and the same character every loop keeps the
   timeline coherent (same character => reproducible dungeon).  Tourist is used
   as a deliberately weak, generic traveler base; its starting kit is pared
   back toward a true wanderer in echoes_wanderer_start(). */
#define ECHOES_ROLE  "Tourist"
#define ECHOES_RACE  "human"
#define ECHOES_GEND  "male"
#define ECHOES_ALIGN "neutral"

/* Force the fixed Echoes character before role selection.  No-op outside
   Echoes mode (classic NetHack keeps normal character selection). */
void
echoes_force_character(void)
{
    if (!echoes_mode())
        return;

    flags.initrole = str2role(ECHOES_ROLE);
    flags.initrace = str2race(ECHOES_RACE);
    flags.initgend = str2gend(ECHOES_GEND);
    if (flags.initgend != ROLE_NONE)
        flags.female = flags.initgend;
    flags.initalign = str2align(ECHOES_ALIGN);

    /* In the headless UI tester, skip the multi-page Book intro so scripted
       keystrokes line up predictably with the game's prompts. */
    if (echoes_scripted_active())
        flags.legacy = FALSE;
}

/* The Soulbound Wanderer is classless: it can train toward any discipline.
   Called at the start of each life (after skills are initialised), this lifts
   every skill cap to at least Skilled -- a capable generalist, deliberately
   short of Expert/Master mastery so power grows gradually -- and makes
   otherwise-restricted skills trainable.  No-op outside Echoes mode. */
void
echoes_broaden_skills(void)
{
    int sk;

    if (!echoes_mode())
        return;

    for (sk = P_FIRST_WEAPON; sk < P_NUM_SKILLS; sk++) {
        if (u.weapon_skills[sk].max_skill < P_SKILLED)
            u.weapon_skills[sk].max_skill = P_SKILLED;
        if (u.weapon_skills[sk].skill == P_ISRESTRICTED)
            u.weapon_skills[sk].skill = P_UNSKILLED;
    }
}

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
    (void) fprintf(fp, "fragments %ld\n", echoes_fragments);
    (void) fprintf(fp, "upgrades %ld\n", echoes_upgrades);
    /* Only record knowledge once a game is far enough along that objects[],
       mvitals[] and spl_book[] reflect real state.  At startup they do not.
       Each knowledge category is gated by its memory-tree upgrade. */
    if (echoes_game_ready || program_state.in_moveloop || program_state.gameover) {
        int mndx, si;

        /* identified object types */
        if (echoes_upgrades & ECHOES_MEM_ITEMS)
            for (oindx = FIRST_OBJECT; oindx < NUM_OBJECTS; oindx++)
                if (objects[oindx].oc_name_known)
                    (void) fprintf(fp, "disco %d\n", oindx);
        /* bestiary: monster species the soul has encountered */
        if (echoes_upgrades & ECHOES_MEM_MONSTERS)
            for (mndx = LOW_PM; mndx < NUMMONS; mndx++)
                if (svm.mvitals[mndx].mvflags & G_KNOWN)
                    (void) fprintf(fp, "mvital %d\n", mndx);
        /* learned spells */
        if (echoes_upgrades & ECHOES_MEM_SPELLS)
            for (si = 0; si < MAXSPELL && spellid(si) != NO_SPELL; si++)
                (void) fprintf(fp, "spell %d\n", (int) spellid(si));
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
                } else if (sscanf(buf, "fragments %ld", &l) == 1) {
                    echoes_fragments = l;
                } else if (sscanf(buf, "upgrades %ld", &l) == 1) {
                    echoes_upgrades = l;
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

/* Offer the player each affordable, not-yet-owned memory upgrade (cheapest
   first).  The soul chooses what it learns to remember; the spend is persisted
   by the next echoes_on_death().  (A browsable memory-tree menu can replace
   these prompts later.) */
static void
echoes_offer_upgrades(void)
{
    static const struct echoes_upgrade {
        long bit;
        long cost;
        const char *what;
    } tree[] = {
        { ECHOES_MEM_ITEMS, 3L, "items" },
        { ECHOES_MEM_MONSTERS, 5L, "monsters" },
        { ECHOES_MEM_SPELLS, 8L, "spells" },
    };
    int i;

    for (i = 0; i < SIZE(tree); i++) {
        char qbuf[QBUFSZ];

        if ((echoes_upgrades & tree[i].bit) != 0
            || echoes_fragments < tree[i].cost)
            continue;
        Sprintf(qbuf,
                "Spend %ld memory fragment%s to remember %s across loops?",
                tree[i].cost, plur(tree[i].cost), tree[i].what);
        if (y_n(qbuf) == 'y') {
            echoes_fragments -= tree[i].cost;
            echoes_upgrades |= tree[i].bit;
            pline("Your soul learns to remember %s.", tree[i].what);
        }
    }
}

/* Called at the end of newgame(): spend fragments on memory upgrades, restore
   the soul's remembered knowledge (gated by those upgrades), and announce the
   loop if this is not the first life. */
void
echoes_apply_knowledge(void)
{
    FILE *fp;
    char buf[BUFSZ];

    if (!echoes_mode())
        return;

    echoes_game_ready = TRUE; /* objects[]/mvitals[]/spl_book[] are valid now */

    /* Spend fragments on memory upgrades before restoring: a test override
       (NETHACK_ECHOES_BUY = bitmask) or, in real play, automatic purchase. */
    {
        const char *buy = getenv("NETHACK_ECHOES_BUY");

        if (buy != 0 && *buy != '\0')
            echoes_upgrades |= atol(buy);
        else if (!echoes_selftest_active())
            echoes_offer_upgrades();
    }

    fp = fopen(ECHOES_SOUL_FILE, "r");
    if (fp) {
        while (fgets(buf, (int) sizeof buf, fp)) {
            int oindx = 0, mndx = 0, sid = 0;

            if (sscanf(buf, "disco %d", &oindx) == 1) {
                if (oindx >= FIRST_OBJECT && oindx < NUM_OBJECTS)
                    discover_object(oindx, TRUE, TRUE, FALSE);
            } else if (sscanf(buf, "mvital %d", &mndx) == 1) {
                if (mndx >= LOW_PM && mndx < NUMMONS)
                    svm.mvitals[mndx].mvflags |= G_KNOWN;
            } else if (sscanf(buf, "spell %d", &sid) == 1) {
                if (sid >= FIRST_SPELL && sid <= LAST_SPELL)
                    (void) force_learn_spell((short) sid);
            }
        }
        (void) fclose(fp);
    }

    if (echoes_loop_count > 0L && !echoes_selftest_active())
        pline("The dungeon resets, but your soul remembers.  (Loop %ld)",
              echoes_loop_count + 1L);
}

/* The soul's death summary, shown as a life ends. */
void
echoes_death_summary(long gained, long deepest)
{
    pline("Your soul fades, %ld memory fragment%s richer for reaching depth %ld.",
          gained, plur(gained), deepest);
    pline("Loop %ld ends with %ld memory fragment%s gathered in all.",
          echoes_loop_count + 1L, echoes_fragments, plur(echoes_fragments));
}

/* Called at the start of game-over handling, BEFORE end-of-game disclosure
   inflates what is "known".  Awards fragments, shows the death summary,
   advances the loop counter, and persists the soul's genuine knowledge. */
void
echoes_on_death(void)
{
    long deepest, gained;

    if (!echoes_mode())
        return;

    /* Award memory fragments for this life's progress.  Spec formula uses a
       depth term plus contributions from regions/bosses/titles/etc.; for now
       we use the depth reached and the soul's experience this life. */
    deepest = (long) deepest_lev_reached(FALSE);
    gained = (3L * deepest) / 2L + (long) u.ulevel;
    echoes_fragments += gained;

    if (!echoes_selftest_active())
        echoes_death_summary(gained, deepest);

    echoes_loop_count++;
    echoes_save_soul();
}

/* Headless self-test hook.  When NETHACK_ECHOES_DUMP names a file, write a
   checksum of the freshly generated level (terrain + floor objects) together
   with the timeline seed, then exit -- before the interactive game loop.
   This lets the dungeon be compared across runs without a playthrough.
   No-op unless the variable is set. */
void
echoes_selftest_dump(void)
{
    const char *path = getenv("NETHACK_ECHOES_DUMP");
    unsigned long h = 2166136261UL; /* FNV-1a 32-bit offset basis */
    int x, y;
    FILE *fp;

    if (!path || !*path)
        return;

    for (y = 0; y < ROWNO; y++)
        for (x = 0; x < COLNO; x++) {
            struct obj *o = svl.level.objects[x][y];

            h ^= (unsigned long) (unsigned char) levl[x][y].typ;
            h *= 16777619UL;
            h ^= (unsigned long) (unsigned) (o ? o->otyp : 0);
            h *= 16777619UL;
        }

    fp = fopen(path, "w");
    if (fp) {
        (void) fprintf(fp, "seed %lu\nmapsum %lu\nrole %s\nthsword_max %d\n",
                       echoes_timeline_seed, h, gu.urole.name.m,
                       (int) u.weapon_skills[P_TWO_HANDED_SWORD].max_skill);
        (void) fclose(fp);
    }
    nh_terminate(EXIT_SUCCESS);
}

/* Headless knowledge round-trip self-test.  Two phases, by env var:
     NETHACK_ECHOES_KINJECT  -- learn a fixed test monster + spell, then die
                                (which persists the soul) and exit.
     NETHACK_ECHOES_KVERIFY=<file> -- a fresh life has restored the soul by now;
                                report whether the test knowledge survived, exit.
   No-op when neither variable is set. */
void
echoes_selftest_knowledge(void)
{
    const char *verify = getenv("NETHACK_ECHOES_KVERIFY");
    int mon_mndx = LOW_PM + 50; /* an arbitrary but stable test species */
    short test_spell = SPE_FORCE_BOLT;

    if (getenv("NETHACK_ECHOES_KINJECT") != 0) {
        if (mon_mndx < NUMMONS)
            svm.mvitals[mon_mndx].mvflags |= G_KNOWN;
        (void) force_learn_spell(test_spell);
        echoes_on_death();          /* advance the loop and save the soul */
        nh_terminate(EXIT_SUCCESS);
    } else if (verify != 0 && *verify != '\0') {
        FILE *fp = fopen(verify, "w");
        int i, spell_ok = 0;
        int mon_ok = (mon_mndx < NUMMONS
                      && (svm.mvitals[mon_mndx].mvflags & G_KNOWN)) ? 1 : 0;

        for (i = 0; i < MAXSPELL; i++)
            if (spellid(i) == test_spell) {
                spell_ok = 1;
                break;
            }

        if (fp) {
            (void) fprintf(fp, "mon %d\nspell %d\n", mon_ok, spell_ok);
            (void) fclose(fp);
        }
        nh_terminate(EXIT_SUCCESS);
    }
}

/* Called from really_done() after clearlocks().  On a genuine death, relaunch
   the timeline's next loop; quit, escape, ascension, and panic end the run so
   the player can always stop.  No-op outside Echoes mode. */
void
echoes_reloop(int how)
{
    if (!echoes_mode() || echoes_selftest_active())
        return;
    if (how == PANICKED || how >= QUIT) /* not on crash / quit / escape / ascend */
        return;
    echoes_reexec();
}

/* Headless auto-loop self-test.  With NETHACK_ECHOES_REEXEC_TARGET=N and
   NETHACK_ECHOES_REEXEC_RESULT=<file>, each life advances the loop and
   relaunches until N loops have run, then writes the final loop count.  This
   exercises the real re-exec + lock + soul-carry chain without gameplay. */
void
echoes_selftest_reexec(void)
{
    const char *targ = getenv("NETHACK_ECHOES_REEXEC_TARGET");
    const char *result = getenv("NETHACK_ECHOES_REEXEC_RESULT");

    if (targ == 0 || *targ == '\0' || result == 0 || *result == '\0')
        return;

    echoes_on_death(); /* advance loop + award fragments + save, like a real death */

    if (echoes_loop_count < atol(targ)) {
        clearlocks(); /* free the lock before respawning, as really_done does */
        echoes_reexec();
        nh_terminate(EXIT_SUCCESS);
    } else {
        FILE *fp = fopen(result, "w");

        if (fp) {
            (void) fprintf(fp, "loops %ld\nfragments %ld\n",
                           echoes_loop_count, echoes_fragments);
            (void) fclose(fp);
        }
        nh_terminate(EXIT_SUCCESS);
    }
}

/* The #echoes extended command: show the player the state of their soul --
   the current loop, memory fragments, and which knowledge the memory tree is
   retaining across loops. */
int
echoes_status_cmd(void)
{
    char buf[BUFSZ];

    if (!echoes_mode()) {
        pline("The soul stirs only in Echoes of the Soul mode.");
        return ECMD_OK;
    }

    pline("Soul: loop %ld, holding %ld memory fragment%s.",
          echoes_loop_count + 1L, echoes_fragments, plur(echoes_fragments));

    buf[0] = '\0';
    if (echoes_upgrades & ECHOES_MEM_ITEMS)
        Strcat(buf, "items ");
    if (echoes_upgrades & ECHOES_MEM_MONSTERS)
        Strcat(buf, "monsters ");
    if (echoes_upgrades & ECHOES_MEM_SPELLS)
        Strcat(buf, "spells ");
    pline("Memory retained across loops: %s", *buf ? buf : "nothing yet.");
    return ECMD_OK;
}

/* Headless test of the #echoes status display: when NETHACK_ECHOES_STATUS is
   set, invoke the command handler (its messages are captured via the UI
   transcript) and exit.  Lets the player-facing status be verified without
   driving the extended-command menu. */
void
echoes_selftest_status(void)
{
    if (getenv("NETHACK_ECHOES_STATUS") == 0)
        return;
    (void) echoes_status_cmd();
    nh_terminate(EXIT_SUCCESS);
}

/* Headless test of the death summary: when NETHACK_ECHOES_SUMMARY is set, show
   the summary (captured via the UI transcript) and exit. */
void
echoes_selftest_summary(void)
{
    if (getenv("NETHACK_ECHOES_SUMMARY") == 0)
        return;
    {
        long deepest = (long) deepest_lev_reached(FALSE);
        long gained = (3L * deepest) / 2L + (long) u.ulevel;

        echoes_fragments += gained;
        echoes_death_summary(gained, deepest);
    }
    nh_terminate(EXIT_SUCCESS);
}

/* ---- headless UI tester -------------------------------------------------
   Two hooks in the tty windowport let the game's interface be driven and
   observed without a console:
     NETHACK_ECHOES_KEYS=<file>  : tty_nhgetch() reads keystrokes from <file>
                                   (ESC once the script is exhausted).
     NETHACK_ECHOES_UILOG=<file> : tty_putstr() appends displayed text -- which
                                   includes pline() messages and menu lines --
                                   to <file> for inspection.
   Both are no-ops unless their variable is set. */

boolean
echoes_scripted_active(void)
{
    const char *k = getenv("NETHACK_ECHOES_KEYS");

    return (boolean) (k != 0 && *k != '\0');
}

int
echoes_scripted_key(void)
{
    static boolean opened = FALSE;
    static FILE *fp = NULL;
    int c;

    if (!opened) {
        const char *k = getenv("NETHACK_ECHOES_KEYS");

        opened = TRUE;
        if (k != 0 && *k != '\0')
            fp = fopen(k, "r");
    }
    if (fp != NULL && (c = fgetc(fp)) != EOF)
        return c;
    return '\033'; /* ESC once the script runs out: cancels prompts/--More-- */
}

void
echoes_transcript(const char *s)
{
    const char *path = getenv("NETHACK_ECHOES_UILOG");
    FILE *fp;

    if (path == 0 || *path == '\0' || s == 0)
        return;
    fp = fopen(path, "a");
    if (fp != NULL) {
        (void) fprintf(fp, "%s\n", s);
        (void) fclose(fp);
    }
}

/*echoes.c*/
