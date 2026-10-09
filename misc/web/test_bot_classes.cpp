// Run the shipped class manager in the actual GameMonkey interpreter.
#include "gmMachine.h"
#include "gmTableObject.h"
#include "gmCall.h"
#include <cassert>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

static void run(gmMachine &machine, const std::string &source, gmVariable *module = nullptr)
{
    int errors = machine.ExecuteString(source.c_str(), nullptr, true, "bot-class-audit", module);
    bool first = true;
    while (const char *entry = machine.GetLog().GetEntry(first)) {
        std::cerr << entry << '\n';
        ++errors;
    }
    assert(errors == 0);
}

static int value(gmMachine &machine, const char *name)
{
    gmVariable result = machine.GetGlobals()->Get(&machine, name);
    assert(result.IsInt());
    return result.GetInt();
}

static std::string script(const std::string &path)
{
    std::ifstream input(path);
    assert(input.good());
    return std::string((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
}

static void event(gmMachine &machine, const char *goalName, int eventId, bool queued = false)
{
    const gmVariable goal = machine.GetGlobals()->Get(&machine, goalName);
    const gmVariable events = goal.GetTableObjectSafe()->Get(&machine, "Events");
    const gmVariable callback = events.GetTableObjectSafe()->Get(gmVariable(eventId));
    gmCall call;
    assert(call.BeginFunction(&machine, callback.GetFunctionObjectSafe(), goal, queued));
    call.AddParamInt(1);
    call.End();
}

int main(int, char **)
{
    gmMachine machine;
    for (const char *map : {"oasis", "goldrush", "battery", "fueldump", "radar", "railgun"}) {
        const std::string path = std::string("/nav/") + map + ".gm";
        int errors = 0;
        assert(machine.CompileStringToFunction(script(path).c_str(), &errors, path.c_str()));
        assert(errors == 0);
    }
    run(machine, R"GM(
        global CLASS = { SOLDIER = 1, MEDIC = 2, ENGINEER = 3, FIELDOPS = 4, COVERTOPS = 5 };
        global Util = { PlayerClassTable = {1, 2, 3, 4, 5} };
        global Server = { MaxPlayers = 16, NumPlayers = 3, ClassCount = {}, MinClassCount = {} };
        Server.ClassCount[1] = {}; Server.ClassCount[2] = {};
        Server.MinClassCount[1] = {0, 0, 0, 2, 1, 1};
        global Entities = {};
        Entities[0] = { team = 2, playerClass = 2 };
        Entities[9] = { team = 1, playerClass = 3 };
        Entities[15] = { team = 1, playerClass = 3 };
        Entities[16] = { team = 1, playerClass = 3 };
        global EntityIsValid = function(id) { return Entities[id] != null; };
        global GetEntTeam = function(id) { return Entities[id].team; };
        global GetEntClass = function(id) { return Entities[id].playerClass; };
        global RandInt = function(lo, hi) { return lo; };
    )GM");
    gmVariable module(machine.AllocTableObject());
    machine.GetGlobals()->Set(&machine, "ClassManager", module);
    run(machine, script("/et_classmanager.gm"), &module);
    run(machine, R"GM(
        ClassManager.UpdateClasses(1); ClassManager.UpdateClasses(2);
        global Engineers = Server.ClassCount[1][3];
        global AlliedMedics = Server.ClassCount[2][2];
        global EmptySoldiers = Server.ClassCount[1][1];
        global NextClass = ClassManager.EvalClassByTeam(1);
    )GM");
    assert(value(machine, "Engineers") == 2);
    assert(value(machine, "AlliedMedics") == 1);
    assert(value(machine, "EmptySoldiers") == 0);
    assert(value(machine, "NextClass") != 3); // Existing engineers must be counted.
    run(machine, R"GM(
        Entities[9] = null; Entities[15].playerClass = 4; Server.NumPlayers = 2;
        ClassManager.UpdateClasses(1);
        global EngineersAfterDeparture = Server.ClassCount[1][3];
        global FieldOpsAfterDeparture = Server.ClassCount[1][4];
    )GM");
    assert(value(machine, "EngineersAfterDeparture") == 0);
    assert(value(machine, "FieldOpsAfterDeparture") == 1);
    run(machine, R"GM(
        global ConfigGet = function(section, key, fallback) { return fallback; };
        global EVENT = { WEAPON_CHANGE = 1 };
        global DifficultyGoal = { Commands = {}, Events = {} };
    )GM");
    gmVariable difficulty = machine.GetGlobals()->Get(&machine, "DifficultyGoal");
    run(machine, script("/goal_difficulty.gm"), &difficulty);
    run(machine, R"GM(
        global DifficultyOrder = DifficultyGoal.difficulties[2].ReactionTime > DifficultyGoal.difficulties[4].ReactionTime
            && DifficultyGoal.difficulties[4].ReactionTime > DifficultyGoal.difficulties[6].ReactionTime
            && DifficultyGoal.difficulties[2].AimTolerance > DifficultyGoal.difficulties[4].AimTolerance
            && DifficultyGoal.difficulties[4].AimTolerance > DifficultyGoal.difficulties[6].AimTolerance;
    )GM");
    assert(value(machine, "DifficultyOrder") == 1);
    run(machine, R"GM(
        global TEAM = { AXIS = 1, ALLIES = 2 };
        global WEAPON = { MOBILE_MG42 = 1, MORTAR = 2, GARAND = 3, K43 = 4, PROTO = 5 };
        global EVENT = { CHANGETEAM = 1, CHANGECLASS = 2, DISCONNECTED = 3 };
        global Commands = {};
        global Map = { Roles = {} };
        global GetGameState = function() { return "Playing"; };
        Util.TeamName = function(team) { return "Axis"; };
        global WeaponGoal = { Events = {}, Bot = {} };
        global RoleGoal = { Events = {}, Bot = {} };
        global WeaponCalls = 0;
        global RoleCalls = 0;
    )GM");
    gmVariable weapons = machine.GetGlobals()->Get(&machine, "WeaponGoal");
    gmVariable roles = machine.GetGlobals()->Get(&machine, "RoleGoal");
    run(machine, script("/goal_selectweapons.gm"), &weapons);
    run(machine, script("/goal_rolemanager.gm"), &roles);
    run(machine, R"GM(
        WeaponGoal.SelectWeapon = function() { global WeaponCalls = WeaponCalls + 1; };
        RoleGoal.FindFreeRoleSlot = function() { global RoleCalls = RoleCalls + 1; };
        RoleGoal.justStarted = true;
    )GM");
    event(machine, "WeaponGoal", 1);
    event(machine, "WeaponGoal", 2);
    event(machine, "RoleGoal", 1);
    run(machine, "global EventCheck = 1;"); // Check the event calls' interpreter logs too.
    assert(value(machine, "WeaponCalls") == 1);
    assert(value(machine, "RoleCalls") == 1);
    // Events already queued retain their callback even after goal teardown.
    event(machine, "WeaponGoal", 1, true);
    event(machine, "WeaponGoal", 2, true);
    event(machine, "RoleGoal", 1, true);
    run(machine, R"GM(
        WeaponGoal.Bot = null; WeaponGoal.SelectWeapon = null;
        RoleGoal.Bot = null; RoleGoal.FindFreeRoleSlot = null;
    )GM");
    machine.Execute(1);
    run(machine, "global EventCheck = 2;");
    assert(value(machine, "WeaponCalls") == 1);
    assert(value(machine, "RoleCalls") == 1);
    run(machine, script("/server_manager.gm"));
    run(machine, R"GM(
        global Round = function(n) { return n; };
        global Clamp = function(n, lo, hi) { if(n < lo) { return lo; } if(n > hi) { return hi; } return n; };
        global ToInt = function(n, fallback) { return n; };
        global ConfigSet = function(section, key, value) {};
        global print = function(a, b) {};
        global AddCalls = 0; global KickCalls = 0;
        global AddBot = function() { global AddCalls = AddCalls + 1; };
        global KickABotOnHeavyTeam = function() { global KickCalls = KickCalls + 1; };
        Server.MaxPlayers = 16; Server.MinBots = 0; Server.MaxBots = 0; Server.NumBots = 1;
        Commands.maxbots.Func({-1});
        MinAndMaxBots(1);
        global ManualMax = Server.MaxBots;
        global ManualMin = Server.MinBots;
        Commands.minbots.Func({2});
        MinAndMaxBots(1);
        global UnboundedMin = Server.MinBots;
        Commands.maxbots.Func({1});
        global BoundedMax = Server.MaxBots;
        Commands.minbots.Func({9});
        global BoundedMin = Server.MinBots;
    )GM");
    assert(value(machine, "ManualMax") == -1);
    assert(value(machine, "ManualMin") == 0);
    assert(value(machine, "UnboundedMin") == 2);
    assert(value(machine, "AddCalls") == 1);
    assert(value(machine, "KickCalls") == 0);
    assert(value(machine, "BoundedMax") == 2);
    assert(value(machine, "BoundedMin") == 2);
    run(machine, R"GM(
        global ToInt = function(n) { return n; };
        Server.BotTeam = 1; Server.HumanTeam = 1;
        Commands.humanteam.Func({2});
        global HumanTeamCommand = Server.HumanTeam;
        global BotTeamAfterHumanCommand = Server.BotTeam;
        Server.BotTeam = 2; Server.HumanTeam = 1; Server.BotsPerHuman = 3;
        Server.Team[1] = { NumHumans = 1, NumBots = 0 };
        Server.Team[2] = { NumHumans = 0, NumBots = 2 };
        Server.MaxBots = -1;
        global RatioAddCalls = 0; global RatioKickCalls = 0; global AddedTeam = 0;
        global AddBot = function(team) { global RatioAddCalls = RatioAddCalls + 1; global AddedTeam = team; };
        global KickBotFromTeam = function(team) { global RatioKickCalls = RatioKickCalls + 1; };
        AdjustForBotTeam(3);
    )GM");
    machine.Execute(1);
    run(machine, "global RatioCheck = 1;");
    std::cout << "Human team command: " << value(machine, "HumanTeamCommand")
              << ", bot team: " << value(machine, "BotTeamAfterHumanCommand")
              << ", unlimited ratio adds: " << value(machine, "RatioAddCalls")
              << ", kicks: " << value(machine, "RatioKickCalls") << '\n';
    assert(value(machine, "HumanTeamCommand") == 2);
    assert(value(machine, "BotTeamAfterHumanCommand") == 1);
    assert(value(machine, "RatioAddCalls") == 1);
    assert(value(machine, "AddedTeam") == 2);
    assert(value(machine, "RatioKickCalls") == 0);
    run(machine, R"GM(
        global RatioMoveCalls = 0;
        global MoveBotToAnotherTeam = function(from, to) { global RatioMoveCalls = RatioMoveCalls + 1; };
        Server.Team[1].NumBots = 1;
        AdjustForBotTeam(4);
    )GM");
    machine.Execute(1);
    run(machine, "global RatioCheck = 2;");
    assert(value(machine, "RatioMoveCalls") == 1);
    assert(value(machine, "RatioKickCalls") == 0);
    run(machine, R"GM(
        Server.Team[1].NumBots = 0; Server.Team[2].NumBots = 4;
        AdjustForBotTeam(5);
    )GM");
    machine.Execute(1);
    run(machine, "global RatioCheck = 3;");
    assert(value(machine, "RatioKickCalls") == 1);
    run(machine, R"GM(
        Server.Team[2].NumBots = 2; Server.MaxBots = 3;
        AdjustForBotTeam(3);
    )GM");
    machine.Execute(1);
    run(machine, "global RatioCheck = 4;");
    assert(value(machine, "RatioAddCalls") == 2); // No addition beyond the positive cap.
    run(machine, R"GM(
        Server.MaxBots = -1; Server.NumPlayers = 16;
        AdjustForBotTeam(3); // Spectators occupy slots even when not counted for balancing.
    )GM");
    machine.Execute(1);
    run(machine, "global RatioCheck = 5;");
    assert(value(machine, "RatioAddCalls") == 2); // An unbounded ratio still respects capacity.
    run(machine, R"GM(
        Server.MinBots = 4; Server.MaxBots = 4; Server.NumBots = 2;
        global CapacityAddCalls = 0;
        global AddBot = function() { global CapacityAddCalls = CapacityAddCalls + 1; };
        MinAndMaxBots(3);
        global FullServerAddCalls = CapacityAddCalls;
        Server.MinBots = 0;
        MinAndMaxBots(3);
        global FullServerMaxAddCalls = CapacityAddCalls;
        Server.NumPlayers = 15;
        MinAndMaxBots(3);
        global AvailableSlotAddCalls = CapacityAddCalls;
    )GM");
    assert(value(machine, "FullServerAddCalls") == 0);
    assert(value(machine, "FullServerMaxAddCalls") == 0);
    assert(value(machine, "AvailableSlotAddCalls") == 1);
    run(machine, R"GM(
        Server.MaxBots = 2; Server.Team[2].NumBots = 3;
        AdjustForBotTeam(4);
    )GM");
    machine.Execute(1);
    run(machine, "global RatioCheck = 6;");
    assert(value(machine, "RatioKickCalls") == 2);
    std::cout << "Bot checks passed (six map scripts, sparse/highest slots, humans, team isolation, departures, class selection, difficulty ordering, queued goal teardown, manual bot limits, human-team command and capped/unbounded bot ratios).\n";
}
