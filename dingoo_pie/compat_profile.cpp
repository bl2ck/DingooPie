#include "compat_profile.h"

#include <stddef.h>
#include <string.h>

struct CompatExitPromotion
{
    const char* appSha256;
    uint32_t returnAddress;
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

    // DaZhuanKuai.app: compact .bin resources need a host-side view before the loader can consume them.
    { "C5ADC7DED226705FCB3A1AA80AC41D9AB96B6B6916D99A59A7068FEA722B9F93", "Block Breaker", 1.0, true, false, EXECUTION_BACKEND_PPSSPP_IRJIT },
};

static const CompatExitPromotion kExitPromotions[] =
{
    // Some apps finish their quit-confirm path through OSTaskDel in a subtask.
    // These rules promote only the observed app hash + return address pair.
    // Entries tied to local samples are sorted by the sample .app file name.

    // AliBaba.app: quit-confirm path stops a subtask through OSTaskDel at this return address.
    { "1B5A929A93DDA5C312E01205F95F363EFA0F69F1EAD2F703714D4366F8495912", 0x80a1ee9cu },

    // DingooLianliankan.app: quit-confirm path stops a subtask through OSTaskDel at this return address.
    { "59DD65FE27D82293B828570C4F3D34874EA265E518F0DC150B58D21489C0A722", 0x80a06d10u },

    // Doudizhu.app: quit-confirm path stops a subtask through OSTaskDel at this return address.
    { "A591E374807627B8E8A952F5421349AFDEF9FC99F4DC0B982418A1C0323C6A89", 0x80a0a710u },

    // JixianPiaoyi.app: quit-confirm path stops a subtask through OSTaskDel at this return address.
    { "E4E23B19515716445EEE4A79BF6F081B77F5C0911D43456205902475653373F9", 0x80a0067cu },

    // LubiLubi.app: quit-confirm path stops a subtask through OSTaskDel at this return address.
    { "2804FF20F07F82BDCA59EB1BCD6ACE9615862788559F865E11BF0F67547BE6F1", 0x80a058f0u },

    // Paopaolong.app: quit-confirm path stops a subtask through OSTaskDel at this return address.
    { "387DE314AC5A96A00FF4E85AAACCE14265305270ACD1C1DF6004F59976D0D57B", 0x80a08758u },

    // QiYeZhengShiBan.app: quit-confirm path stops a subtask through OSTaskDel at this return address.
    { "AF681C338A9932C98A3B450D4391C43D13747F1DFD937232AE38BEDB44359BF0", 0x80a4796cu },

    // Tangguowu.app: quit-confirm path stops a subtask through OSTaskDel at this return address.
    { "A374186A06EDF34B1BEA824679AEA087393D2BC441BE963C22A057D7B82A9978", 0x80a16c90u },

    // TiandiDao.app: quit-confirm path stops a subtask through OSTaskDel at this return address.
    { "6FA335AD49FE2FE68E6ECE552D72C2DEC352E715B7255FDCE9AED88248FB2C23", 0x80a6e578u },

    // ZhanshenXingtian.app: quit-confirm path stops a subtask through OSTaskDel at this return address.
    { "71C10376DEDEEB30607D9C332F883FF549962094311A967618C9C323A2C18331", 0x80a3d1c8u },

    // ZhaoyunZhuan.app: quit-confirm path stops a subtask through OSTaskDel at this return address.
    { "3A59BD1C0DABFF74C8CCED69F50E3E95BC74CE0EA613AD6BE9D77F48D9967ECE", 0x80a09aa0u },
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

bool compatShouldPromoteTaskStopToGuestExit(const char* appSha256, uint32_t returnAddress)
{
    for (size_t i = 0; i < sizeof(kExitPromotions) / sizeof(kExitPromotions[0]); ++i)
    {
        if (returnAddress == kExitPromotions[i].returnAddress &&
            shaEquals(appSha256, kExitPromotions[i].appSha256))
        {
            return true;
        }
    }
    return false;
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
