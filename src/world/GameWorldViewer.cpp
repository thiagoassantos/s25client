// Copyright (c) 2005 - 2015 Settlers Freaks (sf-team at siedler25.org)
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
#include "world/GameWorldViewer.h"
#include "GameClient.h"
#include "GamePlayer.h"
#include "buildings/nobMilitary.h"
#include "drivers/VideoDriverWrapper.h"
#include "notifications/NodeNote.h"
#include "notifications/PlayerNodeNote.h"
#include "notifications/RoadNote.h"
#include "world/BQCalculator.h"
#include "world/GameWorldBase.h"
#include "nodeObjs/noShip.h"
#include "gameTypes/MapCoordinates.h"
#include <boost/lambda/bind.hpp>
#include <boost/lambda/if.hpp>
#include <boost/lambda/lambda.hpp>

GameWorldViewer::GameWorldViewer(unsigned playerId, GameWorldBase& gwb) : playerId_(playerId), gwb(gwb)
{
    InitVisualData();
}

void GameWorldViewer::InitVisualData()
{
    visualNodes.resize(gwb.GetWidth() * gwb.GetHeight());
    for(MapPoint pt(0, 0); pt.y < gwb.GetHeight(); ++pt.y)
    {
        for(pt.x = 0; pt.x < gwb.GetWidth(); ++pt.x)
        {
            VisualMapNode& vNode = visualNodes[gwb.GetIdx(pt)];
            const MapNode& node = gwb.GetNode(pt);
            vNode.bq = node.bq;
            // Roads are only overlays. At first we don't have any -> 0=use real road
            std::fill(vNode.roads.begin(), vNode.roads.end(), 0);
        }
    }
    namespace bl = boost::lambda;
    using bl::_1;
    evRoadConstruction = gwb.GetNotifications().subscribe<RoadNote>(bl::bind(&GameWorldViewer::RoadConstructionEnded, this, _1));
    evBQChanged = gwb.GetNotifications().subscribe<NodeNote>(
      bl::if_(bl::bind(&NodeNote::type, _1) == NodeNote::BQ)[bl::bind(&GameWorldViewer::RecalcBQ, this, bl::bind(&NodeNote::pt, _1))]);
}

void GameWorldViewer::InitTerrainRenderer()
{
    tr.GenerateOpenGL(*this);
    namespace bl = boost::lambda;
    using bl::_1;
    // Notify renderer about altitude changes
    evAltitudeChanged = gwb.GetNotifications().subscribe<NodeNote>(
      bl::if_(bl::bind(&NodeNote::type, _1)
              == NodeNote::Altitude)[bl::bind(&TerrainRenderer::AltitudeChanged, &tr, bl::bind(&NodeNote::pt, _1), boost::cref(*this))]);
    // And visibility changes
    evVisibilityChanged =
      gwb.GetNotifications().subscribe<PlayerNodeNote>(bl::if_(bl::bind(&PlayerNodeNote::type, _1) == PlayerNodeNote::Visibility)[bl::bind(
        &GameWorldViewer::VisibilityChanged, this, bl::bind(&PlayerNodeNote::pt, _1), bl::bind(&PlayerNodeNote::player, _1))]);
}

const GamePlayer& GameWorldViewer::GetPlayer() const
{
    return GetWorld().GetPlayer(playerId_);
}

unsigned GameWorldViewer::GetPlayerCount() const
{
    return GetWorld().GetPlayerCount();
}

unsigned GameWorldViewer::GetNumSoldiersForAttack(const MapPoint pt) const
{
    const nobBaseMilitary* attacked_building = GetWorld().GetSpecObj<nobBaseMilitary>(pt);
    // Can we actually attack this bld?
    if(!attacked_building || !attacked_building->IsAttackable(playerId_))
        return 0;

    // Militärgebäude in der Nähe finden
    unsigned total_count = 0;

    sortedMilitaryBlds buildings = GetWorld().LookForMilitaryBuildings(pt, 3);
    for(sortedMilitaryBlds::iterator it = buildings.begin(); it != buildings.end(); ++it)
    {
        // Muss ein Gebäude von uns sein und darf nur ein "normales Militärgebäude" sein (kein HQ etc.)
        if((*it)->GetPlayer() == playerId_ && (*it)->GetBuildingType() >= BLD_BARRACKS && (*it)->GetBuildingType() <= BLD_FORTRESS)
            total_count += static_cast<nobMilitary*>(*it)->GetNumSoldiersForAttack(pt);
    }

    return total_count;
}

BuildingQuality GameWorldViewer::GetBQ(const MapPoint& pt) const
{
    return GetWorld().AdjustBQ(pt, playerId_, visualNodes[GetWorld().GetIdx(pt)].bq);
}

Visibility GameWorldViewer::GetVisibility(const MapPoint pt) const
{
    /// Replaymodus und FoW aus? Dann alles sichtbar
    if(GAMECLIENT.IsReplayModeOn() && GAMECLIENT.IsReplayFOWDisabled())
        return VIS_VISIBLE;

    // Spieler schon tot? Dann auch alles sichtbar?
    if(GetPlayer().IsDefeated())
        return VIS_VISIBLE;

    return GetWorld().CalcVisiblityWithAllies(pt, playerId_);
}

bool GameWorldViewer::IsOwner(const MapPoint& pt) const
{
    return GetWorld().GetNode(pt).owner == playerId_ + 1;
}

bool GameWorldViewer::IsPlayerTerritory(const MapPoint& pt) const
{
    return GetWorld().IsPlayerTerritory(pt);
}

const MapNode& GameWorldViewer::GetNode(const MapPoint& pt) const
{
    return GetWorld().GetNode(pt);
}

MapPoint GameWorldViewer::GetNeighbour(const MapPoint pt, const Direction dir) const
{
    return GetWorld().GetNeighbour(pt, dir);
}

void GameWorldViewer::RecalcAllColors()
{
    tr.UpdateAllColors(*this);
}

/// liefert sichtbare Straße, im FoW entsprechend die FoW-Straße
unsigned char GameWorldViewer::GetVisibleRoad(const MapPoint pt, unsigned char roadDir, const Visibility visibility) const
{
    if(visibility == VIS_VISIBLE)
        return GetVisibleRoad(pt, roadDir);
    else if(visibility == VIS_FOW)
        return GetYoungestFOWNode(pt).roads[roadDir];
    else
        return 0; // No road
}

unsigned char GameWorldViewer::GetVisibleRoad(const MapPoint pt, unsigned char roadDir) const
{
    unsigned char visualResult = visualNodes[GetWorld().GetIdx(pt)].roads[roadDir];
    if(visualResult)
        return visualResult;
    else
        return GetWorld().GetRoad(pt, roadDir);
}

unsigned char GameWorldViewer::GetVisiblePointRoad(const MapPoint pt, Direction dir) const
{
    if(dir.toUInt() >= 3)
        return GetVisibleRoad(pt, dir.toUInt() - 3);
    else
        return GetVisibleRoad(GetNeighbour(pt, dir), dir.toUInt());
}

void GameWorldViewer::SetVisiblePointRoad(const MapPoint& pt, Direction dir, unsigned char type)
{
    MapPoint nodePt;
    if(dir.toUInt() >= 3)
    {
        nodePt = pt;
        dir = Direction::fromInt(dir.toUInt() - 3);
    } else
        nodePt = GetNeighbour(pt, dir);
    visualNodes[GetWorld().GetIdx(nodePt)].roads[dir.toUInt()] = type;
}

bool GameWorldViewer::IsOnRoad(const MapPoint& pt) const
{
    for(unsigned roadDir = 0; roadDir < 3; ++roadDir)
        if(GetVisibleRoad(pt, roadDir))
            return true;
    for(unsigned roadDir = 0; roadDir < 3; ++roadDir)
        if(GetVisibleRoad(GetNeighbour(pt, Direction::fromInt(roadDir)), roadDir))
            return true;
    return false;
}

/// Return a ship at this position owned by the given player. Prefers ships that need instructions.
noShip* GameWorldViewer::GetShip(const MapPoint pt) const
{
    noShip* resultShip = NULL;

    for(unsigned i = 0; i < 7; ++i)
    {
        MapPoint curPt;

        if(i == 6)
            curPt = pt;
        else
            curPt = GetWorld().GetNeighbour(pt, i);

        const std::list<noBase*>& figures = GetWorld().GetFigures(curPt);
        for(std::list<noBase*>::const_iterator it = figures.begin(); it != figures.end(); ++it)
        {
            if((*it)->GetGOT() != GOT_SHIP)
                continue;
            noShip* curShip = static_cast<noShip*>(*it);

            if(curShip->GetPlayerId() == playerId_ && (curShip->GetPos() == pt || curShip->GetDestinationForCurrentMove() == pt))
            {
                if(curShip->IsWaitingForExpeditionInstructions())
                    return curShip;
                resultShip = curShip;
            }
        }
    }

    return resultShip;
}

/// Gibt die verfügbar Anzahl der Angreifer für einen Seeangriff zurück
unsigned GameWorldViewer::GetNumSoldiersForSeaAttack(const MapPoint pt) const
{
    return unsigned(GetWorld().GetSoldiersForSeaAttack(playerId_, pt).size());
}

void GameWorldViewer::ChangePlayer(unsigned player, bool updateVisualData /* = true*/)
{
    if(player == playerId_)
        return;
    playerId_ = player;
    if(updateVisualData)
    {
        RecalcAllColors();
        InitVisualData();
    }
}

void GameWorldViewer::VisibilityChanged(const MapPoint& pt, unsigned player)
{
    // If visibility changed for us, or our team mate if shared view is on -> Update renderer
    if(player == playerId_ || (GetWorld().GetGGS().teamView && GetWorld().GetPlayer(playerId_).IsAlly(player)))
        tr.VisibilityChanged(pt, *this);
}

void GameWorldViewer::RoadConstructionEnded(const RoadNote& note)
{
    if(note.player != playerId_ || (note.type != RoadNote::Constructed && note.type != RoadNote::ConstructionFailed))
        return;
    // Road construction command ended -> Remove visual overlay
    RemoveVisualRoad(note.pos, note.route);
}

void GameWorldViewer::RecalcBQ(const MapPoint& pt)
{
    BQCalculator calcBQ(GetWorld());
    visualNodes[GetWorld().GetIdx(pt)].bq = calcBQ(pt, boost::lambda::bind(&GameWorldViewer::IsOnRoad, this, boost::lambda::_1));
}

void GameWorldViewer::RecalcBQForRoad(const MapPoint& pt)
{
    RecalcBQ(pt);

    for(unsigned i = 3; i < 6; ++i)
        RecalcBQ(GetNeighbour(pt, Direction::fromInt(i)));
}

void GameWorldViewer::RemoveVisualRoad(const MapPoint& start, const std::vector<Direction>& route)
{
    MapPoint curPt = start;
    for(unsigned i = 0; i < route.size(); ++i)
    {
        SetVisiblePointRoad(curPt, route[i], 0);
        RecalcBQForRoad(curPt);
        curPt = GetWorld().GetNeighbour(curPt, route[i]);
    }
    RecalcBQForRoad(curPt);
}

bool GameWorldViewer::IsRoadAvailable(bool isWaterRoad, const MapPoint& pt) const
{
    return !IsOnRoad(pt) && GetWorld().IsRoadAvailable(isWaterRoad, pt);
}

/// Get the "youngest" FOWObject of all players who share the view with the local player
const FOWObject* GameWorldViewer::GetYoungestFOWObject(const MapPoint pos) const
{
    return GetYoungestFOWNode(pos).object;
}

/// Gets the youngest fow node of all visible objects of all players who are connected
/// with the local player via team view
const FoWNode& GameWorldViewer::GetYoungestFOWNode(const MapPoint pos) const
{
    const MapNode& node = GetWorld().GetNode(pos);
    const FoWNode* bestNode = &node.fow[playerId_];
    unsigned youngest_time = bestNode->last_update_time;

    // Shared team view enabled?
    if(GetWorld().GetGGS().teamView)
    {
        const GamePlayer& player = GetWorld().GetPlayer(playerId_);
        // Then check if team members have a better (="younger", see our economy) fow object
        for(unsigned i = 0; i < GetWorld().GetPlayerCount(); ++i)
        {
            if(!player.IsAlly(i))
                continue;
            // Has the player FOW at this point at all?
            const FoWNode* curNode = &node.fow[i];
            if(curNode->visibility == VIS_FOW)
            {
                // Younger than the youngest or no object at all?
                if(curNode->last_update_time > youngest_time)
                {
                    // Then take it
                    youngest_time = curNode->last_update_time;
                    // And remember its owner
                    bestNode = curNode;
                }
            }
        }
    }

    return *bestNode;
}
