#include "compat_profile.h"

#include <stddef.h>
#include <string.h>

enum CompatTaskStopExitRequirement
{
    COMPAT_TASK_STOP_EXIT_ALWAYS,
    COMPAT_TASK_STOP_EXIT_WHEN_FRONTEND_QUIT_REQUESTED,
    COMPAT_TASK_STOP_EXIT_AFTER_SUSPICIOUS_FILE_OPEN_FAILURE,
};

struct CompatTaskStopExitRule
{
    const char* appSha256;
    uint32_t returnAddress;
    CompatTaskStopExitRequirement requirement;
    const char* label;
};

struct CompatRuntimeExceptionExitRule
{
    const char* appSha256;
    uint32_t pc;
    uint32_t returnAddress;
    uint32_t v0;
    const char* label;
};

struct CompatProfile
{
    const char* appSha256;
    const char* name;
    double defaultHostDelayScale;
    bool useBinResourceView;
    bool hasForcedBackend;
    ExecutionBackend forcedBackend;
};

static const CompatProfile kCompatProfiles[] =
{
    // Add new entries only after checking whether a generic HLE fix is safer.
    // The SHA256 is the immutable app identity; the name is only a log label.
    // Entries tied to local samples are sorted by the sample .app file name.

    // AliBaba.app: save-state restore is stable on the interpreter backend.
    { "1B5A929A93DDA5C312E01205F95F363EFA0F69F1EAD2F703714D4366F8495912", "AliBaba", 1.0, false, true, EXECUTION_BACKEND_INTERPRETER },

    // DaZhuanKuai.app: compact .bin resources need a host-side view before the loader can consume them.
    { "C5ADC7DED226705FCB3A1AA80AC41D9AB96B6B6916D99A59A7068FEA722B9F93", "Block Breaker", 1.0, true, false, EXECUTION_BACKEND_PPSSPP_IRJIT },

    // DingooLianliankan.app: save-state restore is stable on the interpreter backend.
    { "59DD65FE27D82293B828570C4F3D34874EA265E518F0DC150B58D21489C0A722", "DingooLianliankan", 1.0, false, true, EXECUTION_BACKEND_INTERPRETER },

    // LubiLubi.app: save-state restore is stable on the interpreter backend.
    { "2804FF20F07F82BDCA59EB1BCD6ACE9615862788559F865E11BF0F67547BE6F1", "LubiLubi", 1.0, false, true, EXECUTION_BACKEND_INTERPRETER },

    { "6FA335AD49FE2FE68E6ECE552D72C2DEC352E715B7255FDCE9AED88248FB2C23", "TiandiDao", 1.0, false, false, EXECUTION_BACKEND_PPSSPP_IRJIT },

    { "71C10376DEDEEB30607D9C332F883FF549962094311A967618C9C323A2C18331", "ZhanShenXingTian", 1.0, false, false, EXECUTION_BACKEND_PPSSPP_IRJIT },
};

static const CompatTaskStopExitRule kTaskStopExitRules[] =
{
    // Some apps finish their quit-confirm path by deleting a guest subtask
    // instead of calling a global exit API. Match both app hash and return
    // address because the same SDK call is also used for normal short-lived
    // subtasks. Use a guarded requirement when the same call site is known to
    // occur during non-exit menu actions.
    // Entries tied to local samples are sorted by the sample .app file name.

    // AliBaba.app: quit-confirm path stops a subtask through OSTaskDel at this return address.
    { "1B5A929A93DDA5C312E01205F95F363EFA0F69F1EAD2F703714D4366F8495912", 0x80a1ee9cu, COMPAT_TASK_STOP_EXIT_ALWAYS, "AliBaba task-stop exit" },

    // DingooLianliankan.app: quit-confirm path stops a subtask through OSTaskDel at this return address.
    { "59DD65FE27D82293B828570C4F3D34874EA265E518F0DC150B58D21489C0A722", 0x80a06d10u, COMPAT_TASK_STOP_EXIT_ALWAYS, "DingooLianliankan task-stop exit" },

    // Doudizhu.app: quit-confirm path stops a subtask through OSTaskDel at this return address.
    { "A591E374807627B8E8A952F5421349AFDEF9FC99F4DC0B982418A1C0323C6A89", 0x80a0a710u, COMPAT_TASK_STOP_EXIT_ALWAYS, "Doudizhu task-stop exit" },

    // JixianPiaoyi.app: the same OSTaskDel call site is used by the sound
    // toggle. The confirmed in-game exit path first tries to open a corrupt
    // control-character filename, shows the resource-load failure page, then
    // reaches this task stop after the user presses a key.
    { "E4E23B19515716445EEE4A79BF6F081B77F5C0911D43456205902475653373F9", 0x80a0067cu, COMPAT_TASK_STOP_EXIT_AFTER_SUSPICIOUS_FILE_OPEN_FAILURE, "JixianPiaoyi resource-failure exit" },

    // LubiLubi.app: quit-confirm path stops a subtask through OSTaskDel at this return address.
    { "2804FF20F07F82BDCA59EB1BCD6ACE9615862788559F865E11BF0F67547BE6F1", 0x80a058f0u, COMPAT_TASK_STOP_EXIT_ALWAYS, "LubiLubi task-stop exit" },

    // Paopaolong.app: quit-confirm path stops a subtask through OSTaskDel at this return address.
    { "387DE314AC5A96A00FF4E85AAACCE14265305270ACD1C1DF6004F59976D0D57B", 0x80a08758u, COMPAT_TASK_STOP_EXIT_ALWAYS, "Paopaolong task-stop exit" },

    // QiYeZhengShiBan.app: this call site also runs when toggling music, so it
    // must not promote by itself. The confirmed in-game quit path is handled by
    // the runtime-exception rule below.
    { "AF681C338A9932C98A3B450D4391C43D13747F1DFD937232AE38BEDB44359BF0", 0x80a4796cu, COMPAT_TASK_STOP_EXIT_WHEN_FRONTEND_QUIT_REQUESTED, "QiYe guarded task-stop exit" },

    // Tangguowu.app: quit-confirm path stops a subtask through OSTaskDel at this return address.
    { "A374186A06EDF34B1BEA824679AEA087393D2BC441BE963C22A057D7B82A9978", 0x80a16c90u, COMPAT_TASK_STOP_EXIT_ALWAYS, "Tangguowu task-stop exit" },

    // TiandiDao.app: quit-confirm path stops a subtask through OSTaskDel at this return address.
    { "6FA335AD49FE2FE68E6ECE552D72C2DEC352E715B7255FDCE9AED88248FB2C23", 0x80a6e578u, COMPAT_TASK_STOP_EXIT_ALWAYS, "TiandiDao task-stop exit" },

    // ZhanshenXingtian.app: quit-confirm path stops a subtask through OSTaskDel at this return address.
    { "71C10376DEDEEB30607D9C332F883FF549962094311A967618C9C323A2C18331", 0x80a3d1c8u, COMPAT_TASK_STOP_EXIT_ALWAYS, "ZhanshenXingtian task-stop exit" },

    // ZhaoyunZhuan.app: quit-confirm path stops a subtask through OSTaskDel at this return address.
    { "3A59BD1C0DABFF74C8CCED69F50E3E95BC74CE0EA613AD6BE9D77F48D9967ECE", 0x80a09aa0u, COMPAT_TASK_STOP_EXIT_ALWAYS, "ZhaoyunZhuan task-stop exit" },
};

static const CompatRuntimeExceptionExitRule kRuntimeExceptionExitRules[] =
{
    // Runtime-exception rules are for games whose own quit code reaches a
    // stable invalid callback after all in-game confirmation has already
    // completed. Keep all register fields exact; do not replace this with a
    // broad "any Guest exception exits normally" policy.

    // QiYeZhengShiBan.app: confirmed quit-game path ends by jumping through this invalid callback.
    { "AF681C338A9932C98A3B450D4391C43D13747F1DFD937232AE38BEDB44359BF0", 0x80a2d7a8u, 0x80a8b260u, 0x42074207u, "QiYe exit exception" },
};

static bool shaEquals(const char* a, const char* b)
{
    return a && b && strcmp(a, b) == 0;
}

static const CompatProfile* findCompatProfile(const char* appSha256)
{
    for (size_t i = 0; i < sizeof(kCompatProfiles) / sizeof(kCompatProfiles[0]); ++i)
    {
        if (shaEquals(appSha256, kCompatProfiles[i].appSha256))
        {
            return &kCompatProfiles[i];
        }
    }
    return NULL;
}

static CompatGuestExitDecision makeGuestExitDecision(bool matched, bool shouldExit, const char* label)
{
    CompatGuestExitDecision decision;
    decision.matched = matched;
    decision.shouldExit = shouldExit;
    decision.label = label;
    return decision;
}

static bool taskStopRuleAllowsExit(const CompatTaskStopExitRule* rule, const CompatTaskStopExitContext* context)
{
    if (!rule || !context)
    {
        return false;
    }

    switch (rule->requirement)
    {
    case COMPAT_TASK_STOP_EXIT_ALWAYS:
        return true;
    case COMPAT_TASK_STOP_EXIT_WHEN_FRONTEND_QUIT_REQUESTED:
        return context->frontendQuitRequested;
    case COMPAT_TASK_STOP_EXIT_AFTER_SUSPICIOUS_FILE_OPEN_FAILURE:
        return context->sawSuspiciousFileOpenFailure;
    default:
        return false;
    }
}

const char* compatProfileName(const char* appSha256)
{
    const CompatProfile* profile = findCompatProfile(appSha256);
    return profile ? profile->name : "default";
}

double compatDefaultHostDelayScale(const char* appSha256)
{
    const CompatProfile* profile = findCompatProfile(appSha256);
    return profile ? profile->defaultHostDelayScale : 1.0;
}

CompatGuestExitDecision compatTaskStopGuestExitDecision(const char* appSha256, const CompatTaskStopExitContext* context)
{
    if (!context)
    {
        return makeGuestExitDecision(false, false, NULL);
    }

    for (size_t i = 0; i < sizeof(kTaskStopExitRules) / sizeof(kTaskStopExitRules[0]); ++i)
    {
        if (context->returnAddress == kTaskStopExitRules[i].returnAddress &&
            shaEquals(appSha256, kTaskStopExitRules[i].appSha256))
        {
            return makeGuestExitDecision(
                true,
                taskStopRuleAllowsExit(&kTaskStopExitRules[i], context),
                kTaskStopExitRules[i].label);
        }
    }
    return makeGuestExitDecision(false, false, NULL);
}

CompatGuestExitDecision compatRuntimeExceptionGuestExitDecision(const char* appSha256, const CompatRuntimeExceptionExitContext* context)
{
    if (!context)
    {
        return makeGuestExitDecision(false, false, NULL);
    }

    for (size_t i = 0; i < sizeof(kRuntimeExceptionExitRules) / sizeof(kRuntimeExceptionExitRules[0]); ++i)
    {
        if (context->pc == kRuntimeExceptionExitRules[i].pc &&
            context->returnAddress == kRuntimeExceptionExitRules[i].returnAddress &&
            context->v0 == kRuntimeExceptionExitRules[i].v0 &&
            shaEquals(appSha256, kRuntimeExceptionExitRules[i].appSha256))
        {
            return makeGuestExitDecision(true, true, kRuntimeExceptionExitRules[i].label);
        }
    }
    return makeGuestExitDecision(false, false, NULL);
}

bool compatShouldUseBinResourceView(const char* appSha256)
{
    const CompatProfile* profile = findCompatProfile(appSha256);
    return profile ? profile->useBinResourceView : false;
}

bool compatForcedBackend(const char* appSha256, ExecutionBackend* backend)
{
    const CompatProfile* profile = findCompatProfile(appSha256);
    if (!profile || !profile->hasForcedBackend)
    {
        return false;
    }

    if (backend)
    {
        *backend = profile->forcedBackend;
    }
    return true;
}
