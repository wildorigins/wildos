#pragma once

// The names in ~/.config/wildoscoolerrc. The Cooler window writes them and
// the program that draws the faces (wildos-cooler-faces) reads them, so they
// are spelled once, here.

namespace Cooler::Settings
{
inline constexpr char file[] = "wildoscoolerrc";

inline constexpr char screenGroup[] = "Screen";
// A face id from WildOS.Cooler.Faces.
inline constexpr char face[] = "Face";
inline constexpr char brightness[] = "Brightness";
inline constexpr char rotation[] = "Rotation";
inline constexpr char showMark[] = "ShowMark";
inline constexpr char themeColours[] = "ThemeColours";
// Whose temperature the faces lead with: "cpu", "gpu" or "both" (taking
// turns). The faces read it as the reading "show".
inline constexpr char temperature[] = "Temperature";
inline constexpr char dimWhenIdle[] = "DimWhenIdle";
inline constexpr char warnLiquidAbove[] = "WarnLiquidAbove";
inline constexpr char warnCpuLimitSeconds[] = "WarnCpuLimitSeconds";
// false leaves the cooler on its own liquid screen. The window has no
// control for it yet; it is here for whoever wants the faces off.
inline constexpr char showFaces[] = "ShowFaces";

inline constexpr char coolingGroup[] = "Cooling";
inline constexpr char mode[] = "Mode";
inline constexpr char fanCurve[] = "FanCurve";
inline constexpr char pumpCurve[] = "PumpCurve";
inline constexpr char stopFansWhenCool[] = "StopFansWhenCool";
inline constexpr char pumpMinimum[] = "PumpMinimum";
inline constexpr char fullSpeedInGames[] = "FullSpeedInGames";

// What holds until the person chooses otherwise.
inline constexpr char defaultFace[] = "liquid";
inline constexpr int defaultBrightness = 70;
inline constexpr bool defaultShowMark = true;
inline constexpr bool defaultThemeColours = true;
inline constexpr char defaultTemperature[] = "cpu";
inline constexpr bool defaultDimWhenIdle = true;
inline constexpr int defaultWarnLiquidAbove = 50;
inline constexpr int defaultWarnCpuLimitSeconds = 30;
inline constexpr bool defaultFullSpeedInGames = true;
}
