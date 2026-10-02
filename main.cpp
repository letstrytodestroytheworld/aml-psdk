// ============================================================================
// Custom HUD Mod — GTA SA Mobile (AML), versi 32-bit (armeabi-v7a)
// ============================================================================
// RESTRUKTURISASI: pindah dari hook manual CWidgetPlayerInfo::Draw ke
// Events::drawHudEvent (API resmi aml-psdk) untuk semua elemen KECUALI
// icon senjata (masih butuh pointer "this" widget asli -- lihat catatan
// di bagian g_CachedWidgetThis).
//
// Posisi & ukuran sekarang RESOLUTION-INDEPENDENT: semua angka posisi/ukuran
// di config dianggap dalam "kanvas referensi" 640x448 (standar lama GTA SA),
// lalu di-scale otomatis ke resolusi layar device sebenarnya lewat RsGlobal.
// (Scale font TIDAK ikut di-scale manual -- CFont::SetScale sudah otomatis
// menyesuaikan resolusi secara internal.)
// ============================================================================

#include <mod/amlmod.h>
#include <mod/config.h>
#include <mod/logger.h>
#include <aml-psdk/game_sa/plugin.h>
#include <aml-psdk/game_sa/Events.h>
#include <aml-psdk/game_sa/engine/RsGlobal.h>
#include <aml-psdk/game_sa/engine/Font.h>
#include <aml-psdk/game_sa/engine/Sprite2d.h>
#include <aml-psdk/game_sa/other/PlayerInfo.h>
#include <aml-psdk/game_sa/other/PlayerData.h>
#include <aml-psdk/game_sa/other/Stats.h>
#include <aml-psdk/game_sa/entity/Ped.h>
#include <aml-psdk/game_sa/entity/PlayerPed.h>
#include <aml-psdk/game_sa/entity/Vehicle.h>
#include <aml-psdk/game_sa/ai/PedIntelligence.h>
#include <aml-psdk/game_sa/engine/World.h>
#include <aml-psdk/gta_base/Rect.h>
#include <string>
#include <cstdio>
#include <cmath>

MYMODCFG(net.psdk.customhud.guid, Custom HUD, 1.0, YourName)

// ----------------------------------------------------------------------------
// Resolution-independent scaling (kanvas referensi 640x448, pola sama persis
// seperti contoh resmi StaminaBar aml-psdk)
// ----------------------------------------------------------------------------
#define SCALEX(__v) ( (__v) * RsGlobal.maximumWidth  / 640.0f )
#define SCALEY(__v) ( (__v) * RsGlobal.maximumHeight / 448.0f )

// ----------------------------------------------------------------------------
// Konstanta tetap (bukan setting -- nilai resmi dari engine)
// ----------------------------------------------------------------------------
static const float HEALTH_MAX  = 100.0f;
static const float ARMOUR_MAX  = 100.0f;
static const float VEHICLE_HEALTH_MAX = 1000.0f;
static const float BREATH_MAX  = 39.97000244f;

// ----------------------------------------------------------------------------
// Style teks (independen per elemen)
// ----------------------------------------------------------------------------
struct TextStyle
{
    bool  enabled  = true;
    float posX = 0.0f, posY = 0.0f; // dalam kanvas referensi 640x448
    float scale = 0.4f;
    int   colorR = 255, colorG = 255, colorB = 255, colorA = 255;
    int   fontStyle = FONT_GOTHIC;
    bool  outline = false;
    int   outlineR = 0, outlineG = 0, outlineB = 0, outlineA = 255;
};

static void ApplyTextStyle(const TextStyle& t)
{
    CFont::SetFontStyle((u8)t.fontStyle);
    CFont::SetProportional(true);
    CFont::SetBackground(false, false);
    CFont::SetJustify(false);
    CFont::SetOrientation(ALIGN_LEFT);
    CFont::SetRightJustifyWrap(0.0f);
    CFont::SetScale(t.scale, t.scale);
    CFont::SetColor(CRGBA((u8)t.colorR, (u8)t.colorG, (u8)t.colorB, (u8)t.colorA));

    if (t.outline)
    {
        CFont::SetDropColor(CRGBA((u8)t.outlineR, (u8)t.outlineG, (u8)t.outlineB, (u8)t.outlineA));
        CFont::SetEdge(1);
    }
    else
    {
        CFont::SetEdge(0);
    }
}

static void PrintStyledText(const TextStyle& t, const char* text)
{
    if (!t.enabled) return;
    ApplyTextStyle(t);
    CFont::PrintString(SCALEX(t.posX), SCALEY(t.posY), text);
}

// ----------------------------------------------------------------------------
// Konfigurasi 1 bar
// ----------------------------------------------------------------------------
struct BarConfig
{
    bool  enabled  = true;
    float posX = 20.0f, posY = 20.0f;     // kanvas referensi 640x448
    float sizeX = 200.0f, sizeY = 16.0f;  // kanvas referensi 640x448
    int   colorR = 255, colorG = 0, colorB = 0, colorA = 255;
    TextStyle indicator;
};

BarConfig g_HealthBar, g_ArmorBar, g_BreathBar, g_VehicleHealthBar, g_SprintBar;
int g_VehBarDisplayMode = 1; // setting KHUSUS VehicleHealthBar: 1=0-100, 2=0-1000

struct WeaponIconConfig
{
    bool  enabled = true;
    float posX = 20.0f, posY = 250.0f;
    float sizeX = 60.0f, sizeY = 60.0f; // kanvas referensi 640x448
} g_WeaponIcon;

struct MoneyConfig
{
    bool  enabled = true;
    int   displayMode = 1;
    std::string separator = ".";
    std::string centSeparator = ".";
    TextStyle style;
} g_Money;

bool g_bEnabled = true;

// ----------------------------------------------------------------------------
// Cache pointer "this" widget, DITANGKAP lewat hook CWidgetPlayerInfo::Draw,
// TAPI TIDAK memanggil fungsi aslinya (supaya HUD default tidak dobel
// tampil bareng elemen custom kita). Dipakai HANYA untuk DrawWeaponIcon,
// karena fungsi itu tetap butuh pointer widget asli yang valid.
// ----------------------------------------------------------------------------
static void* g_CachedWidgetThis = nullptr;

static inline auto CWidgetPlayerInfo_Draw_Sym =
    GetMainLibrarySymbol<void(*)(void*)>("_ZN17CWidgetPlayerInfo4DrawEv");

static inline auto DrawWeaponIcon_Sym =
    GetMainLibrarySymbol<void(*)(void*, CPed*, CRect, float)>(
        "_ZN17CWidgetPlayerInfo14DrawWeaponIconEP4CPed5CRectf");

DECL_HOOKv(CWidgetPlayerInfo__Draw, void* thisWidget)
{
    g_CachedWidgetThis = thisWidget;
    // Sengaja TIDAK memanggil CWidgetPlayerInfo__Draw(thisWidget) di sini --
    // semua render custom kita sekarang dilakukan lewat Events::drawHudEvent.
}

// ----------------------------------------------------------------------------
// Money separator
// ----------------------------------------------------------------------------
static std::string AddSeparators(std::string aValue)
{
    if (g_Money.displayMode == 0 || aValue.empty()) return aValue;
    while (aValue.length() > 1 && aValue[0] == '0') aValue.erase(0, 1);

    std::string result;
    if (g_Money.displayMode == 1)
    {
        int len = (int)aValue.length();
        int size = 3;
        while (len > size)
        {
            aValue.insert(len - size, g_Money.separator);
            size += 3 + (int)g_Money.separator.length();
            len += (int)g_Money.separator.length();
        }
        result = aValue;
    }
    else if (g_Money.displayMode == 2)
    {
        while (aValue.length() < 3) aValue.insert(0, "0");
        std::string cents = aValue.substr(aValue.length() - 2);
        std::string dollars = aValue.substr(0, aValue.length() - 2);
        int len = (int)dollars.length();
        int size = 3;
        while (len > size)
        {
            dollars.insert(len - size, g_Money.separator);
            size += 3 + (int)g_Money.separator.length();
            len += (int)g_Money.separator.length();
        }
        result = dollars + g_Money.centSeparator + cents;
    }
    return result;
}

// ----------------------------------------------------------------------------
// Helper load config (dipakai untuk semua bar & TextStyle, hindari duplikasi)
// ----------------------------------------------------------------------------
static void LoadTextStyle(TextStyle& t, const std::string& prefix)
{
    t.enabled   = cfg->Bind((prefix + "_Enabled").c_str(), t.enabled, prefix.c_str())->GetBool();
    t.posX      = cfg->Bind((prefix + "_PosX").c_str(), t.posX, prefix.c_str())->GetFloat();
    t.posY      = cfg->Bind((prefix + "_PosY").c_str(), t.posY, prefix.c_str())->GetFloat();
    t.scale     = cfg->Bind((prefix + "_Scale").c_str(), t.scale, prefix.c_str())->GetFloat();
    t.colorR    = cfg->Bind((prefix + "_ColorR").c_str(), t.colorR, prefix.c_str())->GetInt();
    t.colorG    = cfg->Bind((prefix + "_ColorG").c_str(), t.colorG, prefix.c_str())->GetInt();
    t.colorB    = cfg->Bind((prefix + "_ColorB").c_str(), t.colorB, prefix.c_str())->GetInt();
    t.colorA    = cfg->Bind((prefix + "_ColorA").c_str(), t.colorA, prefix.c_str())->GetInt();
    t.fontStyle = cfg->Bind((prefix + "_FontStyle").c_str(), t.fontStyle, prefix.c_str())->GetInt();
    t.outline   = cfg->Bind((prefix + "_Outline").c_str(), t.outline, prefix.c_str())->GetBool();
    t.outlineR  = cfg->Bind((prefix + "_OutlineR").c_str(), t.outlineR, prefix.c_str())->GetInt();
    t.outlineG  = cfg->Bind((prefix + "_OutlineG").c_str(), t.outlineG, prefix.c_str())->GetInt();
    t.outlineB  = cfg->Bind((prefix + "_OutlineB").c_str(), t.outlineB, prefix.c_str())->GetInt();
    t.outlineA  = cfg->Bind((prefix + "_OutlineA").c_str(), t.outlineA, prefix.c_str())->GetInt();
}

static void LoadBarConfig(BarConfig& bar, const std::string& section)
{
    bar.enabled = cfg->Bind((section + "_Enabled").c_str(), bar.enabled, section.c_str())->GetBool();
    bar.posX    = cfg->Bind((section + "_PosX").c_str(), bar.posX, section.c_str())->GetFloat();
    bar.posY    = cfg->Bind((section + "_PosY").c_str(), bar.posY, section.c_str())->GetFloat();
    bar.sizeX   = cfg->Bind((section + "_SizeX").c_str(), bar.sizeX, section.c_str())->GetFloat();
    bar.sizeY   = cfg->Bind((section + "_SizeY").c_str(), bar.sizeY, section.c_str())->GetFloat();
    bar.colorR  = cfg->Bind((section + "_ColorR").c_str(), bar.colorR, section.c_str())->GetInt();
    bar.colorG  = cfg->Bind((section + "_ColorG").c_str(), bar.colorG, section.c_str())->GetInt();
    bar.colorB  = cfg->Bind((section + "_ColorB").c_str(), bar.colorB, section.c_str())->GetInt();
    bar.colorA  = cfg->Bind((section + "_ColorA").c_str(), bar.colorA, section.c_str())->GetInt();
    LoadTextStyle(bar.indicator, section + "_Indicator");
}

// ----------------------------------------------------------------------------
// Gambar 1 bar + indicator
// ----------------------------------------------------------------------------
static void DrawCustomBar(const BarConfig& bar, float currentValue, float maxValue,
                           bool showRawInsteadOfPercent = false)
{
    if (!bar.enabled) return;

    float pct = currentValue / maxValue;
    if (pct < 0.0f) pct = 0.0f;
    if (pct > 1.0f) pct = 1.0f;

    CRGBA color((u8)bar.colorR, (u8)bar.colorG, (u8)bar.colorB, (u8)bar.colorA);
    CRGBA bgColor(0, 0, 0, 150);

    CSprite2d::DrawBarChart(
        SCALEX(bar.posX), SCALEY(bar.posY),
        (u16)SCALEX(bar.sizeX), (u8)SCALEY(bar.sizeY),
        pct * 100.0f, 0, 0, 1, color, bgColor);

    if (bar.indicator.enabled)
    {
        char buf[16];
        if (showRawInsteadOfPercent)
            snprintf(buf, sizeof(buf), "%d", (int)currentValue);
        else
            snprintf(buf, sizeof(buf), "%d", (int)(pct * 100.0f));
        PrintStyledText(bar.indicator, buf);
    }
}

// ----------------------------------------------------------------------------
ON_MOD_LOAD()
{
    logger->SetTag("CustomHUD");

    g_bEnabled = cfg->Bind("Enabled", g_bEnabled, "General")->GetBool();

    g_HealthBar.colorR = 255; g_HealthBar.colorG = 0; g_HealthBar.colorB = 0;
    g_HealthBar.indicator.posX = 225; g_HealthBar.indicator.posY = 20;
    LoadBarConfig(g_HealthBar, "HealthBar");

    g_ArmorBar.colorR = 0; g_ArmorBar.colorG = 150; g_ArmorBar.colorB = 255;
    g_ArmorBar.posY = 45; g_ArmorBar.indicator.posX = 225; g_ArmorBar.indicator.posY = 45;
    LoadBarConfig(g_ArmorBar, "ArmorBar");

    g_BreathBar.colorR = 0; g_BreathBar.colorG = 200; g_BreathBar.colorB = 255;
    g_BreathBar.posY = 70; g_BreathBar.indicator.posX = 225; g_BreathBar.indicator.posY = 70;
    LoadBarConfig(g_BreathBar, "BreathBar");

    g_VehicleHealthBar.colorR = 255; g_VehicleHealthBar.colorG = 165; g_VehicleHealthBar.colorB = 0;
    g_VehicleHealthBar.posY = 100; g_VehicleHealthBar.indicator.posX = 225; g_VehicleHealthBar.indicator.posY = 100;
    LoadBarConfig(g_VehicleHealthBar, "VehicleHealthBar");
    g_VehBarDisplayMode = cfg->Bind("VehicleHealthBar_DisplayMode", g_VehBarDisplayMode, "VehicleHealthBar")->GetInt();

    g_SprintBar.colorR = 0; g_SprintBar.colorG = 255; g_SprintBar.colorB = 100;
    g_SprintBar.posY = 125; g_SprintBar.indicator.posX = 225; g_SprintBar.indicator.posY = 125;
    LoadBarConfig(g_SprintBar, "SprintBar");

    g_WeaponIcon.enabled = cfg->Bind("WeaponIcon_Enabled", g_WeaponIcon.enabled, "WeaponIcon")->GetBool();
    g_WeaponIcon.posX    = cfg->Bind("WeaponIcon_PosX", g_WeaponIcon.posX, "WeaponIcon")->GetFloat();
    g_WeaponIcon.posY    = cfg->Bind("WeaponIcon_PosY", g_WeaponIcon.posY, "WeaponIcon")->GetFloat();
    g_WeaponIcon.sizeX   = cfg->Bind("WeaponIcon_SizeX", g_WeaponIcon.sizeX, "WeaponIcon")->GetFloat();
    g_WeaponIcon.sizeY   = cfg->Bind("WeaponIcon_SizeY", g_WeaponIcon.sizeY, "WeaponIcon")->GetFloat();

    g_Money.enabled     = cfg->Bind("Money_Enabled", g_Money.enabled, "Money")->GetBool();
    g_Money.displayMode = cfg->Bind("Money_DisplayMode", g_Money.displayMode, "Money")->GetInt();
    g_Money.style.posX = 20; g_Money.style.posY = 320;
    g_Money.style.colorR = 0; g_Money.style.colorG = 255; g_Money.style.colorB = 0;
    LoadTextStyle(g_Money.style, "Money");

    uintptr_t pGame = aml->GetLib("libGTASA.so");
    if (!pGame)
    {
        logger->Error("libGTASA.so not found");
        return;
    }

    HOOK(CWidgetPlayerInfo__Draw, CWidgetPlayerInfo_Draw_Sym);

    if (!DrawWeaponIcon_Sym)
        logger->Error("DrawWeaponIcon symbol NOT FOUND -- WeaponIcon dinonaktifkan otomatis.");

    // ------------------------------------------------------------------
    // Event resmi aml-psdk, dipanggil tiap frame saat HUD digambar
    // ------------------------------------------------------------------
    Events::drawHudEvent += []()
    {
        if (!g_bEnabled) return;

        CPlayerPed* pPed = FindPlayerPed(-1);
        if (pPed)
        {
            DrawCustomBar(g_HealthBar, pPed->m_fHealth, HEALTH_MAX);
            DrawCustomBar(g_ArmorBar, pPed->m_fArmour, ARMOUR_MAX);

            if (pPed->m_pIntelligence && pPed->m_pIntelligence->GetTaskSwim() && pPed->m_pPlayerData)
                DrawCustomBar(g_BreathBar, pPed->m_pPlayerData->m_fBreath, BREATH_MAX);

            if (pPed->m_pPlayerData)
            {
                float stamina = pPed->m_pPlayerData->m_fTimeCanRun;
                float maxStamina = CStats::GetFatAndMuscleModifier(STAT_MOD_TIME_CAN_RUN);
                DrawCustomBar(g_SprintBar, stamina, maxStamina);
            }

            if (g_WeaponIcon.enabled && DrawWeaponIcon_Sym && g_CachedWidgetThis)
            {
                CRect rect(SCALEX(g_WeaponIcon.posX), SCALEY(g_WeaponIcon.posY),
                           SCALEX(g_WeaponIcon.posX + g_WeaponIcon.sizeX),
                           SCALEY(g_WeaponIcon.posY + g_WeaponIcon.sizeY));
                DrawWeaponIcon_Sym(g_CachedWidgetThis, pPed, rect, 255.0f);
            }
        }

        CVehicle* pVeh = FindPlayerVehicle(-1, false);
        if (pVeh)
            DrawCustomBar(g_VehicleHealthBar, pVeh->m_fHealth, VEHICLE_HEALTH_MAX, g_VehBarDisplayMode == 2);

        if (g_Money.enabled)
        {
            CPlayerInfo& pi = CWorld::Players[(u8)CWorld::PlayerInFocus];
            bool isNegative = pi.m_nDisplayMoney < 0;
            char buf[32];
            snprintf(buf, sizeof(buf), "%d", isNegative ? -pi.m_nDisplayMoney : pi.m_nDisplayMoney);
            std::string text = AddSeparators(std::string(buf));
            text = (isNegative ? "-$" : "$") + text;
            PrintStyledText(g_Money.style, text.c_str());
        }
    };

    logger->Info("Custom HUD loaded (Events::drawHudEvent architecture).");
}
