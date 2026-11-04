#pragma once
// The main menu's GROUP row: arrows beside it go back to the race groups
// already finished ([Game] EarlierGroups), and the game loads the latest save
// at start-up by default ([Game] AutoLoad, the Graphics screen's Auto Load).

namespace groups {

// Before the game runs.
void Install();

// The Graphics screen was left: its Auto Load row goes to Carma2HD.ini.
void SaveAutoLoad();

}  // namespace groups
