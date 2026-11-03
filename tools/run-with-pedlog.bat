@echo off
rem Carmageddon II HD logging every pedestrian move change (for the twitching pedestrians) in Carma2HD.log
set C2HD_PEDLOG=1
set C2HD_FLOWLOG=1
start "" "%~dp0CARMA2_HW.EXE"
