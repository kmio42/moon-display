#ifndef I18N_IDS_H
#define I18N_IDS_H

#include <stdint.h>

/*
 * IDs aller übersetzten Texte.
 *
 * Neuer Text: ID hier ergänzen und in lang/en.h eintragen. Die anderen Sprachen sind optional,
 * fehlende Texte erscheinen englisch.
 *
 * Namenskonvention:
 *  - …Fmt      printf-Format; alle Sprachen müssen dieselben Platzhalter in derselben Reihenfolge haben
 *  - …Pattern  strftime-Format; die Platzhalter sind frei wählbar
 *  - Cmd…      Beschreibung eines Befehls (Hilfe und Telegram-Menü)
 *  - Lbl…      Bezeichnung im Festbreiten-Block von "moon", höchstens 9 Zeichen
 */
#define TEXT_IDS(X) \
    X(NoValidTime) \
    X(UnknownCommandFmt) \
    X(InvalidInputFmt) \
    X(ChangeFmt) \
    X(On) \
    X(Off) \
    /* Hilfe */ \
    X(HelpTitle) \
    X(HelpSettings) \
    X(HelpLegend) \
    X(CmdHelp) \
    X(CmdMoon) \
    X(CmdSun) \
    X(CmdReport) \
    X(CmdImage) \
    X(CmdClock) \
    X(CmdAvatar) \
    X(CmdLocation) \
    X(CmdOptions) \
    X(CmdMode) \
    X(CmdDaily) \
    X(CmdTimezone) \
    X(CmdLanguage) \
    X(CmdShow) \
    X(CmdTime) \
    X(CmdWifi) \
    /* Einstellungen */ \
    X(LocationFmt) \
    X(LocationRange) \
    X(OptionsFmt) \
    X(OptDarkenUnlit) \
    X(OptBluishTint) \
    X(OptNasaTexture) \
    X(OptLibration) \
    X(OptIgnoreParallactic) \
    X(OptIgnorePositionAngle) \
    X(ModeFmt) \
    X(ModeMoon) \
    X(ModeClock) \
    X(DailyFmt) \
    X(DailyTargetsFmt) \
    X(TargetChat) \
    X(TargetGroup) \
    X(TargetNone) \
    X(TimezoneFmt) \
    X(LocalTimeFmt) \
    X(TimezoneExample) \
    X(LanguageFmt) \
    X(LanguagesFmt) \
    /* Nur seriell */ \
    X(ShowLive) \
    X(ShowAtFmt) \
    X(SystemTimeFmt) \
    X(WifiConnectedFmt) \
    X(WifiDisconnected) \
    X(WifiOff) \
    /* Datum */ \
    X(DatePattern) \
    X(DateTimePattern) \
    /* Mond und Sonne */ \
    X(MoonTitle) \
    X(LblEcliptic) \
    X(LblRaDec) \
    X(LblAzAlt) \
    X(LblDiameter) \
    X(LblElongation) \
    X(LblPhase) \
    X(LblLibration) \
    X(LblRise) \
    X(LblTransit) \
    X(LblSet) \
    X(LblPerigee) \
    X(LblApogee) \
    X(LblNode) \
    X(East) \
    X(West) \
    X(Waxing) \
    X(Waning) \
    X(NodeAscending) \
    X(NodeDescending) \
    X(SunTitle) \
    X(SunRaDec) \
    X(SunAzAlt) \
    X(PhaseNew) \
    X(PhaseWaxingCrescent) \
    X(PhaseFirstQuarter) \
    X(PhaseWaxingGibbous) \
    X(PhaseFull) \
    X(PhaseWaningGibbous) \
    X(PhaseLastQuarter) \
    X(PhaseWaningCrescent) \
    /* Telegram */ \
    X(ImageCaptionFmt) \
    X(ImageFailed) \
    X(ClockNoMemory) \
    X(ProfilePhotoSet) \
    X(ProfilePhotoFailed) \
    X(GroupPhotoSet) \
    X(GroupPhotoFailed) \
    X(ReportPushoverFailed)

enum class Txt : uint16_t {
#define TXT_ENUM(id) id,
    TEXT_IDS(TXT_ENUM)
#undef TXT_ENUM
    COUNT
};

#endif // I18N_IDS_H
