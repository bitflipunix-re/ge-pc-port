/* D318: port-side recovery for the Facility Ourumov/Trevelyan execution
 * softlock. The original game can enter a pinned ACT_ATTACK state where the
 * scripted stop check never becomes true. This watchdog recognizes only that
 * exact signature and re-seeds the attack after a long confirmation window. */
#include <stdlib.h>

#include "PR/os.h"
#include "bondtypes.h"
#include "chr.h"
#include "chrai.h"
#include "chraction.h"
#include "lv.h"

#ifdef PORT

extern s32 chraiGetAIListID(AIRecord *AIList, bool *isGlobalAIList);
extern void sub_GAME_7F025560(ChrRecord *self, s32 attack_type, s32 arg2);

#define D318_LIST_AI22       0x0417
#define D318_OFF_LOOP_MIN    321
#define D318_OFF_LOOP_MAX    333
#define D318_OURUMOV_CHR     78
#define D318_TREVELYAN_CHR   67
#define D318_DEADLOCK_TICKS  600
#define D318_REARM_TICKS     600

static ChrRecord *d318FindChr(s16 chrnum)
{
    s32 i;

    for (i = 0; i < g_NumChrSlots; i++)
    {
        if (g_ChrSlots[i].chrnum == chrnum)
        {
            return &g_ChrSlots[i];
        }
    }

    for (i = 0; i < g_ActiveChrsCount; i++)
    {
        if (g_ActiveChrs[i].chrnum == chrnum)
        {
            return &g_ActiveChrs[i];
        }
    }

    return NULL;
}

void d318WatchdogTick(void)
{
    static int enabled = -1;
    static s32 held = 0;
    static s32 absent = 0;
    static bool fired = FALSE;
    ChrRecord *ourumov;
    ChrRecord *trevelyan;
    bool global = FALSE;
    bool signature;

    if (enabled < 0)
    {
        const char *e = getenv("GE_D318W");
        enabled = (e && e[0] == '0') ? 0 : 1;
    }

    if (!enabled)
    {
        return;
    }

    ourumov = d318FindChr(D318_OURUMOV_CHR);
    trevelyan = d318FindChr(D318_TREVELYAN_CHR);

    signature = (ourumov && ourumov->ailist
        && chraiGetAIListID(ourumov->ailist, &global) == D318_LIST_AI22
        && ourumov->aioffset >= D318_OFF_LOOP_MIN
        && ourumov->aioffset < D318_OFF_LOOP_MAX
        && ourumov->actiontype == ACT_ATTACK
        && (s32)ourumov->act_attack.entityid == D318_TREVELYAN_CHR
        && trevelyan && trevelyan->prop && !chrIsDead(trevelyan));

    if (signature)
    {
        absent = 0;

        if (!fired && ++held >= D318_DEADLOCK_TICKS)
        {
            osSyncPrintf("D318W: recovering pinned Facility execution attack at t=%d\n",
                         (int)g_GlobalTimer);
            sub_GAME_7F025560(ourumov,
                              (s32)ourumov->act_attack.attacktype,
                              (s32)ourumov->act_attack.entityid);
            fired = TRUE;
        }
    }
    else
    {
        held = 0;

        if (fired && ++absent >= D318_REARM_TICKS)
        {
            fired = FALSE;
            absent = 0;
        }
    }
}

#endif
