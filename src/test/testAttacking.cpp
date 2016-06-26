// Copyright (c) 2016 Settlers Freaks (sf-team at siedler25.org)
//
// This file is part of Return To The Roots.
//
// Return To The Roots is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 2 of the License, or
// (at your option) any later version.
//
// Return To The Roots is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with Return To The Roots. If not, see <http://www.gnu.org/licenses/>.

#include "defines.h" // IWYU pragma: keep
#include "test/WorldWithGCExecution.h"
#include "factories/BuildingFactory.h"
#include "pathfinding/FindPathForRoad.h"
#include "buildings/nobBaseWarehouse.h"
#include "buildings/nobMilitary.h"
#include "world/GameWorldViewer.h"
#include "gameData/SettingTypeConv.h"
#include <boost/test/unit_test.hpp>
#include "nodeObjs/noFlag.h"
#include "figures/nofPassiveSoldier.h"

BOOST_AUTO_TEST_SUITE(AttackSuite)

// Size is choosen based on current maximum attacking distances!
struct AttackFixture: public WorldWithGCExecution<3, 58, 38>
{
    typedef WorldWithGCExecution<3, 58, 38> Parent;
    using Parent::world;
    using Parent::curPlayer;

    /// Positions of the players HQ
    std::array<MapPoint, 3> hqPos;
    /// Tested positions for military buildings
    MapPoint milBld1NearPos, milBld1FarPos, milBld2Pos;
    /// Military buildings of players 1 and 2 (for 1, one is close and one far away from player 1)
    const nobMilitary *milBld1Near, *milBld1Far, *milBld2;
    GameWorldViewer gwv;

    AttackFixture(): gwv(curPlayer, world)
    {
        // Make sure attacking is not limited by visibility
        RTTR_FOREACH_PT(MapPoint, world.GetWidth(), world.GetHeight())
        {
            world.SetVisibility(pt, 2, VIS_VISIBLE, this->em.GetCurrentGF());
        }

        for(unsigned i = 0; i < 3; i++)
        {
            curPlayer = i;
            hqPos[i] = world.GetPlayer(i).GetHQPos();
            nobBaseWarehouse* hq = world.GetSpecObj<nobBaseWarehouse>(hqPos[i]);
            Inventory goods;
            goods.Add(JOB_GENERAL, 3);
            hq->AddGoods(goods, true);
            this->ChangeMilitary(MILITARY_SETTINGS_SCALE);
        }
        // Assert player positions: 0: Top-Left, 1: Top-Right, 2: Bottom-Left
        BOOST_REQUIRE_LT(hqPos[0].x, hqPos[1].x);
        BOOST_REQUIRE_LT(hqPos[0].y, hqPos[2].y);
        BOOST_REQUIRE_LT(hqPos[1].y, hqPos[2].y);

        // Build some military buildings

        milBld1NearPos = hqPos[1] - MapPoint(8, 0);
        BOOST_REQUIRE_EQUAL(world.GetBQ(milBld1NearPos, 1), BQ_CASTLE);
        milBld1Near = dynamic_cast<nobMilitary*>(BuildingFactory::CreateBuilding(&world, BLD_WATCHTOWER, milBld1NearPos, 1, NAT_ROMANS));
        BOOST_REQUIRE(milBld1Near);

        milBld1FarPos = hqPos[1] + MapPoint(5, 4);
        BOOST_REQUIRE_EQUAL(world.GetBQ(milBld1FarPos, 1), BQ_CASTLE);
        milBld1Far = dynamic_cast<nobMilitary*>(BuildingFactory::CreateBuilding(&world, BLD_WATCHTOWER, milBld1FarPos, 1, NAT_ROMANS));
        BOOST_REQUIRE(milBld1Far);

        milBld2Pos = hqPos[2] - MapPoint(0, 6);
        BOOST_REQUIRE_EQUAL(world.GetBQ(milBld2Pos, 2), BQ_CASTLE);
        milBld2 = dynamic_cast<nobMilitary*>(BuildingFactory::CreateBuilding(&world, BLD_WATCHTOWER, milBld2Pos, 2, NAT_BABYLONIANS));
        BOOST_REQUIRE(milBld2);
    }

    /// Constructs a road connecting 2 buildings and checks for success
    void BuildRoadForBlds(const MapPoint bldPosFrom, const MapPoint bldPosTo)
    {
        const MapPoint start = world.GetNeighbour(bldPosFrom, Direction::SOUTHEAST);
        const MapPoint end = world.GetNeighbour(bldPosTo, Direction::SOUTHEAST);
        std::vector<unsigned char> road = FindPathForRoad(world, start, end, world, false);
        BOOST_REQUIRE(!road.empty());
        this->BuildRoad(start, false, road);
        BOOST_REQUIRE_EQUAL(world.GetPointRoad(start, road.front()), RoadSegment::RT_NORMAL + 1);
    }

    void SetCurPlayer(unsigned playerIdx)
    {
        curPlayer = playerIdx;
        gwv.ChangePlayer(playerIdx);
    }

    void AddSoldiers(MapPoint bldPos, unsigned numWeak, unsigned numStrong)
    {
        nobMilitary* bld = world.GetSpecObj<nobMilitary>(bldPos);
        BOOST_REQUIRE(bld);
        for(unsigned i = 0; i < numWeak; i++)
        {
            nofPassiveSoldier* soldier = new nofPassiveSoldier(bldPos, bld->GetPlayer(), NULL, bld, 0);
            bld->AddPassiveSoldier(soldier);
        }

        for(unsigned i = 0; i < numStrong; i++)
        {
            nofPassiveSoldier* soldier = new nofPassiveSoldier(bldPos, bld->GetPlayer(), NULL, bld, 4);
            bld->AddPassiveSoldier(soldier);
        }
    }
};

BOOST_FIXTURE_TEST_CASE(NumSoldiersForAttack, AttackFixture)
{
    // Connect buildings
    curPlayer = 1;
    BuildRoadForBlds(hqPos[1], milBld1Near->GetPos());
    BuildRoadForBlds(hqPos[1], milBld1Far->GetPos());
    curPlayer = 2;
    BuildRoadForBlds(hqPos[2], milBld2->GetPos());
    // Let soldiers get into blds. (6 soldiers, 6 fields distance, 20GFs per field, 30GFs for leaving HQ)
    unsigned numGFs = 6 * (6 * 20 + 30);
    // And 8 fields distance for far bld
    numGFs += 6 * (8 * 20 + 30);
    for(unsigned gf = 0; gf < numGFs; gf++)
        this->em.ExecuteNextGF();
    BOOST_REQUIRE_EQUAL(milBld1Near->GetTroopsCount(), 6u);
    BOOST_REQUIRE_EQUAL(milBld1Far->GetTroopsCount(), 6u);
    BOOST_REQUIRE_EQUAL(milBld2->GetTroopsCount(), 6u);

    // Player 0 has no military blds -> Can't attack
    SetCurPlayer(0);
    BOOST_REQUIRE_EQUAL(gwv.GetNumSoldiersForAttack(hqPos[1]), 0u);
    BOOST_REQUIRE_EQUAL(gwv.GetNumSoldiersForAttack(hqPos[2]), 0u);
    BOOST_REQUIRE_EQUAL(gwv.GetNumSoldiersForAttack(milBld1Near->GetPos()), 0u);
    BOOST_REQUIRE_EQUAL(gwv.GetNumSoldiersForAttack(milBld1Far->GetPos()), 0u);
    BOOST_REQUIRE_EQUAL(gwv.GetNumSoldiersForAttack(milBld2->GetPos()), 0u);
    SetCurPlayer(1);
    // No self attack
    BOOST_REQUIRE_EQUAL(gwv.GetNumSoldiersForAttack(hqPos[1]), 0u);
    BOOST_REQUIRE_EQUAL(gwv.GetNumSoldiersForAttack(milBld1Near->GetPos()), 0u);
    BOOST_REQUIRE_EQUAL(gwv.GetNumSoldiersForAttack(milBld1Far->GetPos()), 0u);
    // Attack both others
    BOOST_REQUIRE_EQUAL(gwv.GetNumSoldiersForAttack(hqPos[0]), 5u);
    BOOST_REQUIRE_EQUAL(gwv.GetNumSoldiersForAttack(hqPos[2]), 5u);
    // This is in the extended range of the far bld -> 2 more (with current range)
    BOOST_REQUIRE_EQUAL(gwv.GetNumSoldiersForAttack(milBld2->GetPos()), 7u);
    SetCurPlayer(2);
    // No self attack
    BOOST_REQUIRE_EQUAL(gwv.GetNumSoldiersForAttack(hqPos[2]), 0u);
    BOOST_REQUIRE_EQUAL(gwv.GetNumSoldiersForAttack(milBld2->GetPos()), 0u);
    // Attack both others
    BOOST_REQUIRE_EQUAL(gwv.GetNumSoldiersForAttack(hqPos[0]), 5u);
    BOOST_REQUIRE_EQUAL(gwv.GetNumSoldiersForAttack(hqPos[1]), 5u);
    BOOST_REQUIRE_EQUAL(gwv.GetNumSoldiersForAttack(milBld1Near->GetPos()), 5u);
    // Counterpart: 2 possible for far bld
    BOOST_REQUIRE_EQUAL(gwv.GetNumSoldiersForAttack(milBld1Far->GetPos()), 2u);

    // Functions related to stationed soldiers
    BOOST_REQUIRE(milBld1Near->HasMaxRankSoldier());
    BOOST_REQUIRE(!milBld1Far->HasMaxRankSoldier());
    BOOST_REQUIRE(milBld2->HasMaxRankSoldier());
    BOOST_REQUIRE_GT(milBld1Near->GetSoldiersStrength(), milBld1Far->GetSoldiersStrength());
}

BOOST_FIXTURE_TEST_CASE(StartAttack, AttackFixture)
{
    world.GetPlayer(0).team = TM_TEAM1;
    world.GetPlayer(1).team = TM_TEAM2;
    world.GetPlayer(2).team = TM_TEAM1;
    for(unsigned i = 0; i < 3; i++)
        world.GetPlayer(i).MakeStartPacts();
    // Add soldiers to far and player 2 bld (3 strong, 3 weak)
    AddSoldiers(milBld1FarPos, 3, 3);
    AddSoldiers(milBld2Pos, 3, 3);

    const nobMilitary& attackSrc = *milBld2;
    const MapPoint usualBldPos = hqPos[1] + MapPoint(2, 0);
    BOOST_REQUIRE_GT(world.GetBQ(usualBldPos, 1), BQ_FLAG);
    const noBuilding* usualBld = BuildingFactory::CreateBuilding(&world, BLD_WOODCUTTER, usualBldPos, 1, NAT_ROMANS);
    BOOST_REQUIRE(usualBld);

    SetCurPlayer(attackSrc.GetPlayer());

    // Try to attack non-military bld -> Fail
    BOOST_REQUIRE_EQUAL(gwv.GetNumSoldiersForAttack(usualBldPos), 0u);
    this->Attack(usualBldPos, 1, true);
    BOOST_REQUIRE_EQUAL(attackSrc.GetTroopsCount(), 6u);

    // Try to attack ally -> Fail
    BOOST_REQUIRE_EQUAL(gwv.GetNumSoldiersForAttack(hqPos[0]), 0u);
    this->Attack(hqPos[0], 1, true);
    BOOST_REQUIRE_EQUAL(attackSrc.GetTroopsCount(), 6u);

    // Try to attack newly build bld -> Fail
    BOOST_REQUIRE_EQUAL(world.CalcWithAllyVisiblity(milBld1NearPos, curPlayer), VIS_VISIBLE);
    BOOST_REQUIRE(milBld1Near->IsNewBuilt());
    BOOST_REQUIRE_EQUAL(gwv.GetNumSoldiersForAttack(milBld1NearPos), 0u);
    this->Attack(milBld1NearPos, 1, true);
    BOOST_REQUIRE_EQUAL(attackSrc.GetTroopsCount(), 6u);

    // Add soldier
    AddSoldiers(milBld1NearPos, 1, 0);
    BOOST_REQUIRE(!milBld1Near->IsNewBuilt());
    // Try to attack invisible bld -> Fail
    MapNode& node = world.GetNodeWriteable(milBld1NearPos);
    node.fow[0].visibility = VIS_FOW;
    node.fow[2].visibility = VIS_FOW;
    BOOST_REQUIRE_EQUAL(world.CalcWithAllyVisiblity(milBld1NearPos, curPlayer), VIS_FOW);
    BOOST_REQUIRE_EQUAL(gwv.GetNumSoldiersForAttack(milBld1NearPos), 5u);
    this->Attack(milBld1NearPos, 1, true);
    BOOST_REQUIRE_EQUAL(attackSrc.GetTroopsCount(), 6u);

    // Attack it
    node.fow[0].visibility = VIS_VISIBLE;
    std::vector<nofPassiveSoldier*> soldiers(attackSrc.GetTroops().begin(), attackSrc.GetTroops().end());
    BOOST_REQUIRE_EQUAL(soldiers.size(), 6u);
    for(int i = 0; i < 3; i++)
        BOOST_REQUIRE_EQUAL(soldiers[i]->GetRank(), 0u);
    for(int i = 3; i < 6; i++)
        BOOST_REQUIRE_EQUAL(soldiers[i]->GetRank(), 4u);
    this->Attack(milBld1NearPos, 1, true);
    // 1 strong soldier has left
    soldiers.assign(attackSrc.GetTroops().begin(), attackSrc.GetTroops().end());
    BOOST_REQUIRE_EQUAL(soldiers.size(), 5u);
    for(int i = 0; i < 3; i++)
        BOOST_REQUIRE_EQUAL(soldiers[i]->GetRank(), 0u);
    for(int i = 3; i < 5; i++)
        BOOST_REQUIRE_EQUAL(soldiers[i]->GetRank(), 4u);
    // Attack with 1 weak soldier
    this->Attack(milBld1NearPos, 1, false);
    // 1 weak soldier has left
    soldiers.assign(attackSrc.GetTroops().begin(), attackSrc.GetTroops().end());
    BOOST_REQUIRE_EQUAL(soldiers.size(), 4u);
    for(int i = 0; i < 2; i++)
        BOOST_REQUIRE_EQUAL(soldiers[i]->GetRank(), 0u);
    for(int i = 2; i < 4; i++)
        BOOST_REQUIRE_EQUAL(soldiers[i]->GetRank(), 4u);
    // -> 2 strong, 2 weak remaining, attack with 3 weak ones -> 1 strong remaining
    this->Attack(milBld1NearPos, 3, false);
    soldiers.assign(attackSrc.GetTroops().begin(), attackSrc.GetTroops().end());
    BOOST_REQUIRE_EQUAL(soldiers.size(), 1u);
    BOOST_REQUIRE_EQUAL(soldiers[0]->GetRank(), 4u);
}

BOOST_AUTO_TEST_SUITE_END()
