// ============================================================================
// Custom HUD Mod — GTA SA Mobile (AML), versi 32-bit (armeabi-v7a)
// ============================================================================
// DESAIN: setiap elemen HUD (bar HP, armor, breath, vehicle health, sprint,
// icon senjata, uang) BERDIRI SENDIRI -- masing-masing punya posX/posY/sizeX/
// sizeY/color sendiri di config, TIDAK saling menempel ke rect manapun.
//
// Caranya: kita TIDAK memanggil CWidgetPlayerInfo::Draw yang asli sama sekali
// (kalau mode custom aktif) -- kita gambar ulang semua elemen dari nol pakai
// CSprite2d::DrawBarChart / DrawWeaponIcon / DrawAmmo / CFont::PrintString,
// masing-masing dengan koordinat independen dari config.
//
// Konsekuensi: jam & bintang wanted TIDAK digambar lagi saat mode custom aktif
// (karena itu bagian dari CWidgetPlayerInfo::Draw yang kita skip). Set
// UseCustomLayout=false di config untuk kembali ke HUD 100% default.
//
// BAGIAN YANG PERLU KAMU VERIFIKASI/UJI DI DEVICE (ditandai [VERIFY]):
//   - Skala maksimum CPlayerData::m_fBreath dan m_fTimeCanRun (saya kasih
//     config MaxValue supaya bisa disetel tanpa compile ulang)
//   - Urutan parameter DrawWeaponIcon (param pertama tidak jelas fungsinya
//     dari decompile, saya isi 0)
// ============================================================================

#include <mod/amlmod.h>
#include <mod/config.h>
#include <mod/logger.h>
#include <aml-psdk/game_sa/plugin.h>
#include <aml-psdk/game_sa/engine/Font.h>
#include <aml-psdk/game_sa/engine/Sprite2d.h>
#include <aml-psdk/game_sa/other/PlayerInfo.h>
#include <aml-psdk/game_sa/other/PlayerData.h>
#include <aml-psdk/game_sa/entity/Ped.h>
#include <aml-psdk/game_sa/entity/PlayerPed.h>
#include <aml-psdk/game_sa/entity/Vehicle.h>
#include <aml-psdk/game_sa/engine/World.h>
#include <string>
#include <cstdio>
#include <cmath>

using namespace plugin;

MYMODCFG(net.psdk.customhud.guid, Custom HUD, 1.0, YourName)

// ----------------------------------------------------------------------------
// Struct konfigurasi 1 bar (posisi + ukuran + warna, semua independen)
// ----------------------------------------------------------------------------
struct BarConfig
{
    bool  enabled  = true;
    float posX = 20.0f, posY = 20.0f;
    float sizeX = 200.0f, sizeY = 16.0f;
    int   colorR = 255, colorG = 0, colorB = 0, colorA = 255;
    bool  showText = true;
    float maxValue = 100.0f; // dipakai untuk hitung persentase
};

BarConfig g_HealthBar;
BarConfig g_ArmorBar;
BarConfig g_BreathBar;
BarConfig g_VehicleHealthBar;
BarConfig g_SprintBar;

struct WeaponIconConfig
{
    bool  enabled = true;
    float posX = 20.0f, posY = 250.0f;
    float sizeX = 60.0f, sizeY = 60.0f;
} g_WeaponIcon;

struct MoneyConfig
{
    bool  enabled = true;
    float posX = 20.0f, posY = 320.0f;
    float scale = 0.5f;
    int   colorR = 0, colorG = 255, colorB = 0, colorA = 255;
    int   displayMode = 1;           // 0=off, 1=".000" ribuan, 2=".00" sen
    std::string separator = ".";
    std::string centSeparator = ".";
} g_Money;

bool g_bEnabled = true;
bool g_bUseCustomLayout = true;

// ----------------------------------------------------------------------------
// Helper: load 1 BarConfig dari section config
// ----------------------------------------------------------------------------
static void LoadBarConfig(BarConfig& bar, const char* section)
{
    bar.enabled  = cfg->Bind((std::string(section) + "_Enabled").c_str(), bar.enabled, section)->GetBool();
    bar.posX     = cfg->Bind((std::string(section) + "_PosX").c_str(), bar.posX, section)->GetFloat();
    bar.posY     = cfg->Bind((std::string(section) + "_PosY").c_str(), bar.posY, section)->GetFloat();
    bar.sizeX    = cfg->Bind((std::string(section) + "_SizeX").c_str(), bar.sizeX, section)->GetFloat();
    bar.sizeY    = cfg->Bind((std::string(section) + "_SizeY").c_str(), bar.sizeY, section)->GetFloat();
    bar.colorR   = cfg->Bind((std::string(section) + "_ColorR").c_str(), bar.colorR, section)->GetInt();
    bar.colorG   = cfg->Bind((std::string(section) + "_ColorG").c_str(), bar.colorG, section)->GetInt();
    bar.colorB   = cfg->Bind((std::string(section) + "_ColorB").c_str(), bar.colorB, section)->GetInt();
    bar.colorA   = cfg->Bind((std::string(section) + "_ColorA").c_str(), bar.colorA, section)->GetInt();
    bar.showText = cfg->Bind((std::string(section) + "_ShowText").c_str(), bar.showText, section)->GetBool();
    bar.maxValue = cfg->Bind((std::string(section) + "_MaxValue").c_str(), bar.maxValue, section)->GetFloat();
}

// ----------------------------------------------------------------------------
// Deklarasi manual fungsi yang belum ada di aml-psdk (hasil decompile IDA,
// nama simbol dari libGTASA.so 32-bit -- belum di-strip, jadi bisa di-resolve
// by-name, tahan perubahan versi selama nama simbol tidak berubah).
// ----------------------------------------------------------------------------
static inline auto CWidgetPlayerInfo_Draw_Sym =
    GetMainLibrarySymbol<void(*)(void*)>("_ZN17CWidgetPlayerInfo4DrawEv");

// DrawWeaponIcon(param1_tidak_jelas, pPed, x1, y1, x2, y2, alpha) -- [VERIFY]
static inline auto DrawWeaponIcon_Sym =
    GetMainLibrarySymbol<void(*)(int, CPed*, float, float, float, float, float)>(
        "_ZN17CWidgetPlayerInfo14DrawWeaponIconEP4CPedffffh"); // nama mungkin perlu
                                                                 // disesuaikan, cek
                                                                 // Functions window IDA

// ----------------------------------------------------------------------------
// Money separator (logic sama seperti moneySeparator.cpp)
// ----------------------------------------------------------------------------
static std::string AddSeparators(std::string aValue)
{
    if (g_Money.displayMode == 0 || aValue.empty()) return aValue;

    bool isNegative = false;
    while (!aValue.empty() && aValue[0] == '-') { isNegative = true; aValue.erase(0, 1); }
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
    if (isNegative) result = "-" + result;
    return result;
}

// ----------------------------------------------------------------------------
// Gambar 1 bar generik + teks angka opsional
// ----------------------------------------------------------------------------
static void DrawCustomBar(const BarConfig& bar, float currentValue)
{
    if (!bar.enabled) return;

    float pct = currentValue / bar.maxValue;
    if (pct < 0.0f) pct = 0.0f;
    if (pct > 1.0f) pct = 1.0f;

    CRGBA color((u8)bar.colorR, (u8)bar.colorG, (u8)bar.colorB, (u8)bar.colorA);
    CRGBA bgColor(0, 0, 0, 150);

    CSprite2d::DrawBarChart(
        bar.posX, bar.posY,
        (u16)bar.sizeX, (u8)bar.sizeY,
        pct * 100.0f, 0, 0, 1, color, bgColor);

    if (bar.showText)
    {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d", (int)currentValue);
        CFont::SetScale(0.4f, 0.4f);
        CFont::SetColor(CRGBA(255, 255, 255, 255));
        CFont::SetBackground(false, false);
        CFont::PrintString(bar.posX + bar.sizeX + 5.0f, bar.posY, buf);
    }
}

// ----------------------------------------------------------------------------
// Hook utama
// ----------------------------------------------------------------------------
DECL_HOOKv(CWidgetPlayerInfo__Draw, void* thisWidget)
{
    if (!g_bEnabled || !g_bUseCustomLayout)
    {
        // Mode default: pakai HUD asli 100% (jam, wanted, semuanya normal)
        CWidgetPlayerInfo__Draw(thisWidget);
        return;
    }

    // Mode custom: SKIP fungsi asli sepenuhnya, gambar semua dari nol.
    // (Efeknya: jam & bintang wanted tidak tampil di mode ini.)

    CPed* pPed = FindPlayerPed(-1);
    if (pPed)
    {
        DrawCustomBar(g_HealthBar, pPed->m_fHealth);
        DrawCustomBar(g_ArmorBar, pPed->m_fArmour);
        DrawCustomBar(g_BreathBar, pPed->m_PlayerData.m_fBreath);      // [VERIFY skala max]
        DrawCustomBar(g_SprintBar, pPed->m_PlayerData.m_fTimeCanRun);  // [VERIFY skala max]

        if (g_WeaponIcon.enabled)
        {
            DrawWeaponIcon_Sym(0, pPed,
                g_WeaponIcon.posX, g_WeaponIcon.posY,
                g_WeaponIcon.posX + g_WeaponIcon.sizeX,
                g_WeaponIcon.posY + g_WeaponIcon.sizeY,
                255.0f);
        }
    }

    CVehicle* pVeh = FindPlayerVehicle(-1, false);
    if (pVeh)
    {
        DrawCustomBar(g_VehicleHealthBar, pVeh->m_fHealth);
    }

    if (g_Money.enabled)
    {
        CPlayerInfo& pi = CWorld::Players[(u8)CWorld::PlayerInFocus];
        char buf[32];
        snprintf(buf, sizeof(buf), "$%d", pi.m_nDisplayMoney);
        std::string text = AddSeparators(std::string(buf));

        CFont::SetScale(g_Money.scale, g_Money.scale);
        CFont::SetColor(CRGBA((u8)g_Money.colorR, (u8)g_Money.colorG, (u8)g_Money.colorB, (u8)g_Money.colorA));
        CFont::SetBackground(false, false);
        CFont::PrintString(g_Money.posX, g_Money.posY, text.c_str());
    }
}

// ----------------------------------------------------------------------------
ON_MOD_LOAD()
{
    logger->SetTag("CustomHUD");

    g_bEnabled         = cfg->Bind("Enabled", g_bEnabled, "General")->GetBool();
    g_bUseCustomLayout = cfg->Bind("UseCustomLayout", g_bUseCustomLayout, "General")->GetBool();

    g_HealthBar.colorR = 255; g_HealthBar.colorG = 0; g_HealthBar.colorB = 0;
    g_HealthBar.maxValue = 100.0f;
    LoadBarConfig(g_HealthBar, "HealthBar");

    g_ArmorBar.colorR = 0; g_ArmorBar.colorG = 150; g_ArmorBar.colorB = 255;
    g_ArmorBar.maxValue = 100.0f;
    g_ArmorBar.posY = 45.0f;
    LoadBarConfig(g_ArmorBar, "ArmorBar");

    g_BreathBar.colorR = 0; g_BreathBar.colorG = 200; g_BreathBar.colorB = 255;
    g_BreathBar.maxValue = 100.0f; // [VERIFY]
    g_BreathBar.posY = 70.0f;
    g_BreathBar.enabled = false; // biasanya cuma relevan pas nyelam, default off
    LoadBarConfig(g_BreathBar, "BreathBar");

    g_VehicleHealthBar.colorR = 255; g_VehicleHealthBar.colorG = 165; g_VehicleHealthBar.colorB = 0;
    g_VehicleHealthBar.maxValue = 1000.0f; // CVehicle::m_fHealth full = 1000
    g_VehicleHealthBar.posY = 100.0f;
    LoadBarConfig(g_VehicleHealthBar, "VehicleHealthBar");

    g_SprintBar.colorR = 0; g_SprintBar.colorG = 255; g_SprintBar.colorB = 100;
    g_SprintBar.maxValue = 1000.0f; // [VERIFY] skala m_fTimeCanRun
    g_SprintBar.posY = 125.0f;
    LoadBarConfig(g_SprintBar, "SprintBar");

    g_WeaponIcon.enabled = cfg->Bind("WeaponIcon_Enabled", g_WeaponIcon.enabled, "WeaponIcon")->GetBool();
    g_WeaponIcon.posX    = cfg->Bind("WeaponIcon_PosX", g_WeaponIcon.posX, "WeaponIcon")->GetFloat();
    g_WeaponIcon.posY    = cfg->Bind("WeaponIcon_PosY", g_WeaponIcon.posY, "WeaponIcon")->GetFloat();
    g_WeaponIcon.sizeX   = cfg->Bind("WeaponIcon_SizeX", g_WeaponIcon.sizeX, "WeaponIcon")->GetFloat();
    g_WeaponIcon.sizeY   = cfg->Bind("WeaponIcon_SizeY", g_WeaponIcon.sizeY, "WeaponIcon")->GetFloat();

    g_Money.enabled     = cfg->Bind("Money_Enabled", g_Money.enabled, "Money")->GetBool();
    g_Money.posX        = cfg->Bind("Money_PosX", g_Money.posX, "Money")->GetFloat();
    g_Money.posY        = cfg->Bind("Money_PosY", g_Money.posY, "Money")->GetFloat();
    g_Money.scale        = cfg->Bind("Money_Scale", g_Money.scale, "Money")->GetFloat();
    g_Money.colorR      = cfg->Bind("Money_ColorR", g_Money.colorR, "Money")->GetInt();
    g_Money.colorG      = cfg->Bind("Money_ColorG", g_Money.colorG, "Money")->GetInt();
    g_Money.colorB      = cfg->Bind("Money_ColorB", g_Money.colorB, "Money")->GetInt();
    g_Money.displayMode = cfg->Bind("Money_DisplayMode", g_Money.displayMode, "Money")->GetInt();

    uintptr_t pGame = aml->GetLib("libGTASA.so");
    if (!pGame)
    {
        logger->Error("libGTASA.so not found");
        return;
    }

    HOOK(CWidgetPlayerInfo__Draw, CWidgetPlayerInfo_Draw_Sym);

    logger->Info("Custom HUD loaded (fully independent layout mode).");
}
