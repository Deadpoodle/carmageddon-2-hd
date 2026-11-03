@echo off
rem Carmageddon II HD logging what the minimap shows (for the minimap noise).
rem When the minimap looks wrong, press F12: the map and minimap pictures are saved
rem to the shots folder. Then quit; Carma2HD.log and the shots folder have what is needed.
set C2HD_MAPLOG=1
start "" "%~dp0CARMA2_HW.EXE"
