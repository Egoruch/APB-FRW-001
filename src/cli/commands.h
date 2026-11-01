#pragma once
#include "cli/CliEngine.h"

class App;

/* One register function per category file, so adding a command normally means
 * editing a single category file (requirement 36). */
void cmdRegisterSystem(CliEngine &cli, App &app);
void cmdRegisterPower(CliEngine &cli, App &app);
void cmdRegisterEload(CliEngine &cli, App &app);
void cmdRegisterRte(CliEngine &cli, App &app);
void cmdRegisterTerminal(CliEngine &cli, App &app);
void cmdRegisterMonitor(CliEngine &cli, App &app);
void cmdRegisterBattery(CliEngine &cli, App &app);
void cmdRegisterEnergy(CliEngine &cli, App &app);
void cmdRegisterSensors(CliEngine &cli, App &app);
void cmdRegisterDisplay(CliEngine &cli, App &app);
void cmdRegisterTimeout(CliEngine &cli, App &app);
void cmdRegisterLight(CliEngine &cli, App &app);
void cmdRegisterRgb(CliEngine &cli, App &app);
void cmdRegisterBuzzer(CliEngine &cli, App &app);
void cmdRegisterCharge(CliEngine &cli, App &app);
void cmdRegisterMelody(CliEngine &cli, App &app);
void cmdRegisterRtc(CliEngine &cli, App &app);
void cmdRegisterFram(CliEngine &cli, App &app);
void cmdRegisterI2C(CliEngine &cli, App &app);
void cmdRegisterWakeup(CliEngine &cli, App &app);
void cmdRegisterDebug(CliEngine &cli, App &app);
void cmdRegisterTest(CliEngine &cli, App &app);
void cmdRegisterIP5356(CliEngine &cli, App &app);
void cmdRegisterCable(CliEngine &cli, App &app);
