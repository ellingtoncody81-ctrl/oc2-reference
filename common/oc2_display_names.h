#pragma once

// Display text only. Definition IDs, icon paths and network identities are
// supplied by their existing catalogs and shared weapon tables.
namespace oc2_display_names
{
inline const char *Knife(int definition)
{
    switch(definition)
    {
    case 0:case 59:return u8"默认匕首";
    case 42:return u8"默认反恐精英匕首";
    case 500:return u8"刺刀";
    case 503:return u8"经典匕首";
    case 505:return u8"折叠刀";
    case 506:return u8"穿肠刀";
    case 507:return u8"爪子刀";
    case 508:return u8"M9 刺刀";
    case 509:return u8"猎杀者匕首";
    case 512:return u8"弯刀";
    case 514:return u8"鲍伊猎刀";
    case 515:return u8"蝴蝶刀";
    case 516:return u8"暗影双匕";
    case 517:return u8"系绳匕首";
    case 518:return u8"求生匕首";
    case 519:return u8"熊刀";
    case 520:return u8"折刀";
    case 521:return u8"流浪者匕首";
    case 522:return u8"短剑";
    case 523:return u8"锯齿爪刀";
    case 525:return u8"骷髅匕首";
    default:return u8"匕首";
    }
}
}
