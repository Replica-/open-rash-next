/* The player's weapons in a Big Game (Hurt Hardware on The Street, Schmooze deals and bounties) - shared by the game (src\race\arsenal.c: the state,
 * saved with the player file; the race rules) and the shim (src\platform\ddraw\shop.inc: the shop screen; weapons.inc: the picture; weather.inc:
 * the Esc pause row; gpuscene.inc: the bounty on the course map).
 *
 * Weapon classes are the game's own: 0 = fists, 1 = club, 2 = chain (tbl_aiThrottleByClass: hit strength 256 / 320 / 384; RidersAlongside: reach
 * 5 / 7 / 8 x 0x1000).  A weapon here is a class with its own strength and reach (percent of the class's, tenths of 0x1000) and a picture for the
 * club's swing sprites (weapons.inc g_wp: 0 = the game's club, 1 = the hammer, 2 = the tire iron). */
#ifndef ARSENAL_API_H
#define ARSENAL_API_H

#define RRA_N       5                       /* weapons in the shop, 0 = bare knuckles (always owned) */
#define RRA_MAXLV   3                       /* upgrade levels per weapon (damage, reach) */
#define RRA_DMGSTEP 10                      /* + percent damage per damage level */
#define RRA_REACHSTEP 5                     /* + tenths of reach per reach level */

typedef struct RrWeaponDef {
    const char *name;
    int price;                              /* dollars (0 = not for sale) */
    int cls;                                /* the game's weapon class */
    int pic;                                /* weapons.inc picture over the club (0 = the game's own) */
    int dmg;                                /* percent of the class's hit strength */
    int reach;                              /* tenths of 0x1000 (the class's: 50 / 70 / 80) */
    const char *blurb;
} RrWeaponDef;

static const RrWeaponDef g_rraWeapons[RRA_N] = {
    { "Bare Knuckles",   0, 0, 0, 100, 50, "What your mother gave you.  Free, and you can't lose them." },
    { "Billy Club",    150, 1, 0, 100, 70, "Hardwood, leather strap.  A classic for a reason." },
    { "Claw Hammer",   600, 1, 1, 115, 70, "Drives nails, and points home.  Hits harder than a club." },
    { "Tire Iron",    1200, 1, 2, 125, 80, "Cast steel and a long handle: more reach, more hurt." },
    { "Bike Chain",   2000, 2, 0, 110, 90, "Whips out further than anything in the shop.  Mind your knuckles." },
};

/* the price of the next upgrade from level lv (0..2) of weapon w */
static int RraUpgradeCost(int w, int lv)
{
    int base = g_rraWeapons[w].price / 4;
    if (base < 100) base = 100;
    return (base * (lv + 1) + 5) / 10 * 10;
}

/* saved after the player file's 0xA0 bytes (savegame.c); all zero = a new rider: bare knuckles, nothing owned, no bounty */
typedef struct RrArsenalSave {
    unsigned char owned;                    /* bit w = weapon w is the player's (bit 0 implied) */
    unsigned char equipped;                 /* the weapon carried into races */
    unsigned char dmgLv[RRA_N], reachLv[RRA_N];
    unsigned char bountyChar;               /* 0 = no bounty; 1..8 = the character to take out in the next race */
    unsigned char bountyBy;                 /* the character who pays (1..8) */
    unsigned char medikits;                 /* first-aid kits: one press of the medikit key in a race = full health again */
    unsigned char nitroPacks;               /* nitro packs: RRA_NITRO_SHOTS charges each on top of a nitro bike's own (used after them) */
    int bountyReward, bountyPenalty;        /* dollars */
    /* version 2 (the first 24 bytes above are version 1: savegame.c loads either) */
    unsigned char backLv[RRA_N];            /* backhand boost level per weapon (0..RRA_MAXLV): a backhand hit gives the player speed */
    unsigned char bootDmg, bootPush;        /* boots: steel toes (kick damage), spring heels (kick push), 0..RRA_MAXLV */
    unsigned char vest;                     /* 1 = the armoured vest is worn */
    unsigned char adrenaline;               /* adrenaline shots (the adrenaline key in a race) */
    unsigned char scanners;                 /* police scanners: one is used up by each race */
    unsigned char insurance;                /* 0 = none, 1..3 = the plan (g_rraPlans) */
    unsigned char charms;                   /* bit c = charm c owned (and working) */
    unsigned char pad2[4];
} RrArsenalSave;

/* supplies, gear and charms */
#define RRA_MEDIKIT_PRICE 200
#define RRA_MEDIKIT_MAX   5
#define RRA_NITRO_PRICE   250
#define RRA_NITRO_MAX     9
#define RRA_NITRO_SHOTS   3                 /* nitro charges in a pack (a nitro bike has 10 of its own per race: bike models 6..9) */
#define RRA_ADREN_PRICE   300
#define RRA_ADREN_MAX     3
#define RRA_ADREN_TICKS   300               /* race ticks (30 a second) an adrenaline shot lasts: hits dealt +50 %, taken -50 % */
#define RRA_SCAN_PRICE    150
#define RRA_SCAN_MAX      5
#define RRA_VEST_PRICE    900               /* damage taken -25 % */
#define RRA_BOOT_PRICE(lv) (150 * ((lv) + 1))   /* each boots upgrade */
#define RRA_BACK_PRICE(lv) (120 * ((lv) + 1))   /* each backhand boost level */
#define RRA_BACK_SPEED    350               /* speed (rider +0xec) a backhand hit adds per backhand level */

/* insurance: a premium after every race; a bust fine or a repair bill costs you only the excess */
typedef struct RrPlan { const char *name; int premium, excess; } RrPlan;
static const RrPlan g_rraPlans[4] = { { "No insurance", 0, 0 }, { "Basic", 60, 400 }, { "Standard", 110, 200 }, { "Gold", 180, 50 } };

/* charms: bought once, work from then on */
enum { RRC_NITRO, RRC_BOOST, RRC_LEECH, RRC_TORCH, RRC_N };
typedef struct RrCharm { const char *name; int price; const char *blurb; } RrCharm;
static const RrCharm g_rraCharms[RRC_N] = {
    { "Spark Plug Charm", 900, "Every hit you land tops up your nitro meter.  Worthless on a bike without nitro." },
    { "Rabbit's Foot", 700, "A backhand that lands kicks you forward: a burst of speed on top of any backhand upgrade." },
    { "Vampire Fang", 1100, "Each hit you land patches you up a little.  Bleed them, heal yourself." },
    { "Miner's Lamp", 600, "See further in the dark, the fog and the rain: a brighter headlight and the murk pushed back." },
};

typedef struct RrArsenal {
    RrArsenalSave s;
    int *cash;                              /* the player's cash (rec_playerRecord.g_playerCash) */
    int bigGame;                            /* 1 = a Big Game is in play: the arsenal decides the player's weapon */
    int unsaved;                            /* the shop set it after a purchase: the game marks its unsaved changes */
    int bikeNitro;                          /* the player's bike has nitro (the packs work only there) */
    char medKey;                            /* the medikit key's letter (decided at each race start: a key no control uses) */
    char adrKey;                            /* the adrenaline key's letter */
} RrArsenal;

/* the player's strength (percent of the class) and reach (tenths of 0x1000) with weapon w */
static int RraDamage(const RrArsenalSave *s, int w) { return g_rraWeapons[w].dmg + (w ? RRA_DMGSTEP : 25) * s->dmgLv[w]; }   /* (bare knuckles: +25 % a level - brass knuckles) */
static int RraReach(const RrArsenalSave *s, int w) { return g_rraWeapons[w].reach + RRA_REACHSTEP * s->reachLv[w]; }
static int RraOwns(const RrArsenalSave *s, int w) { return w == 0 || (s->owned >> w & 1); }
/* bare knuckles with damage upgrades are brass knuckles */
static const char *RraName(const RrArsenalSave *s, int w) { static const char *const brass[4] = { "Bare Knuckles", "Brass Knuckles", "Studded Brass", "Spiked Brass" }; return w == 0 ? brass[s->dmgLv[0] < 4 ? s->dmgLv[0] : 3] : g_rraWeapons[w].name; }

#endif
