#include "cli/commands.h"
#include "app/App.h"

void registerCommands(CliEngine &cli, App &app) {
    cmdRegisterSystem(cli, app);
    cmdRegisterPower(cli, app);
    cmdRegisterEload(cli, app);
    cmdRegisterRte(cli, app);
    cmdRegisterTerminal(cli, app);
    cmdRegisterMonitor(cli, app);
    cmdRegisterBattery(cli, app);
    cmdRegisterEnergy(cli, app);
    cmdRegisterSensors(cli, app);
    cmdRegisterDisplay(cli, app);
    cmdRegisterTimeout(cli, app);
    cmdRegisterLight(cli, app);
    cmdRegisterRgb(cli, app);
    cmdRegisterBuzzer(cli, app);
    cmdRegisterCharge(cli, app);
    cmdRegisterMelody(cli, app);
    cmdRegisterRtc(cli, app);
    cmdRegisterFram(cli, app);
    cmdRegisterI2C(cli, app);
    cmdRegisterWakeup(cli, app);
    cmdRegisterDebug(cli, app);
    cmdRegisterTest(cli, app);
    cmdRegisterIP5356(cli, app);
    cmdRegisterCable(cli, app);
}
