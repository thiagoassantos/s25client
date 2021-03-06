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
#include "AIInterface.h"
#include "ai/AIJHHelper.h"

#include "buildings/noBuilding.h"
#include "buildings/nobHQ.h"
#include "buildings/nobHarborBuilding.h"
#include "buildings/nobMilitary.h"
#include "buildings/nobShipYard.h"
#include "pathfinding/FreePathFinder.h"
#include "pathfinding/RoadPathFinder.h"
#include "nodeObjs/noFlag.h"
#include "nodeObjs/noTree.h"
#include "gameTypes/BuildingCount.h"
#include "gameData/TerrainData.h"
#include <limits>
class noRoadNode;

// from Pathfinding.cpp TODO: in nice
bool IsPointOK_RoadPath(const GameWorldBase& gwb, const MapPoint pt, const Direction dir, const void* param);
bool IsPointOK_RoadPathEvenStep(const GameWorldBase& gwb, const MapPoint pt, const Direction dir, const void* param);

AIJH::Resource AIInterface::GetSubsurfaceResource(const MapPoint pt) const
{
    unsigned char subres = gwb.GetNode(pt).resources;

    if(subres > 0x40 + 0 * 8 && subres < 0x48 + 0 * 8)
        return AIJH::COAL;
    else if(subres > 0x40 + 1 * 8 && subres < 0x48 + 1 * 8)
        return AIJH::IRONORE;
    else if(subres > 0x40 + 2 * 8 && subres < 0x48 + 2 * 8)
        return AIJH::GOLD;
    else if(subres > 0x40 + 3 * 8 && subres < 0x48 + 3 * 8)
        return AIJH::GRANITE;
    else if(subres > 0x80 && subres < 0x90)
        return AIJH::FISH;
    else
        return AIJH::NOTHING;
}

AIJH::Resource AIInterface::GetSurfaceResource(const MapPoint pt) const
{
    NodalObjectType no = gwb.GetNO(pt)->GetType();
    TerrainType t1 = gwb.GetNode(pt).t1;
    // valid terrain?
    if(TerrainData::IsUseable(t1))
    {
        if(no == NOP_TREE)
        {
            // exclude pineapple because it's not a real tree
            if(gwb.GetSpecObj<noTree>(pt)->ProducesWood())
                return AIJH::WOOD;
            else
                return AIJH::BLOCKED;
        } else if(no == NOP_GRANITE)
            return AIJH::STONES;
        else if(no == NOP_NOTHING || no == NOP_ENVIRONMENT)
            return AIJH::NOTHING;
        else
            return AIJH::BLOCKED;
    } else
        return AIJH::BLOCKED;
}

int AIInterface::GetResourceRating(const MapPoint pt, AIJH::Resource res) const
{
    // surface resource?
    if(res == AIJH::PLANTSPACE || res == AIJH::BORDERLAND || res == AIJH::WOOD || res == AIJH::STONES)
    {
        AIJH::Resource surfaceRes = GetSurfaceResource(pt);
        TerrainType t1 = gwb.GetNode(pt).t1, t2 = gwb.GetNode(pt).t2;
        if(surfaceRes == res || (res == AIJH::PLANTSPACE && surfaceRes == AIJH::NOTHING && TerrainData::IsVital(t1))
           || (res == AIJH::BORDERLAND && (IsBorder(pt) || !IsOwnTerritory(pt))
               && (TerrainData::IsUseable(t1) || TerrainData::IsUseable(t2))))
        {
            return AIJH::RES_RADIUS[res];
        }
        // another building using our "resource"? reduce rating!
        if(res == AIJH::WOOD && IsBuildingOnNode(pt, BLD_WOODCUTTER))
            return -40;
        if(res == AIJH::PLANTSPACE && IsBuildingOnNode(pt, BLD_FORESTER))
            return -40;
    }
    // so it's a subsurface resource or something we dont calculate (multiple,blocked,nothing)
    else
    {
        if(GetSubsurfaceResource(pt) == res)
            return AIJH::RES_RADIUS[res];
    }
    return 0;
}

int AIInterface::CalcResourceValue(const MapPoint pt, AIJH::Resource res, char direction, int lastval) const
{
    int returnVal;
    if(direction == -1) // calculate complete value from scratch (3n^2+3n+1)
    {
        returnVal = 0;
        std::vector<MapPoint> pts = GetPointsInRadius(pt, AIJH::RES_RADIUS[res]);
        for(std::vector<MapPoint>::const_iterator it = pts.begin(); it != pts.end(); ++it)
            returnVal += GetResourceRating(*it, res);
        // add the center point value
        returnVal += GetResourceRating(pt, res);
    } else // calculate different nodes only (4n+2 ?anyways much faster)
    {
        returnVal = lastval;
        // add new points
        // first: go radius steps towards direction-1
        MapPoint tmpPt(pt);
        for(unsigned i = 0; i < AIJH::RES_RADIUS[res]; i++)
            tmpPt = gwb.GetNeighbour(tmpPt, (direction + 5) % 6);
        // then clockwise around at radius distance to get all new points
        for(int i = direction + 1; i < (direction + 3); ++i)
        {
            int resRadius = AIJH::RES_RADIUS[res];
            // add 1 extra step on the second side we check to complete the side
            if(i == direction + 2)
                ++resRadius;
            for(MapCoord r2 = 0; r2 < resRadius; ++r2)
            {
                returnVal += GetResourceRating(tmpPt, res);
                tmpPt = gwb.GetNeighbour(tmpPt, i % 6);
            }
        }
        // now substract old points not in range of new point
        // go to old center point:
        tmpPt = pt;
        tmpPt = gwb.GetNeighbour(tmpPt, (direction + 3) % 6);
        // next: go to the first old point we have to substract
        for(unsigned i = 0; i < AIJH::RES_RADIUS[res]; i++)
            tmpPt = gwb.GetNeighbour(tmpPt, (direction + 2) % 6);
        // now clockwise around at radius distance to remove all old points
        for(int i = direction + 4; i < (direction + 6); ++i)
        {
            int resRadius = AIJH::RES_RADIUS[res];
            if(i == direction + 5)
                ++resRadius;
            for(MapCoord r2 = 0; r2 < resRadius; ++r2)
            {
                returnVal -= GetResourceRating(tmpPt, res);
                tmpPt = gwb.GetNeighbour(tmpPt, i % 6);
            }
        }
    }
    // if(returnval<0&&lastval>=0&&res==AIJH::BORDERLAND)
    // LOG.write(("AIInterface::CalcResourceValue - warning: negative returnvalue direction %i oldval %i\n", direction, lastval);
    return returnVal;
}

bool AIInterface::IsRoadPoint(const MapPoint pt) const
{
    for(unsigned char dir = 0; dir < Direction::COUNT; ++dir)
    {
        if(gwb.GetPointRoad(pt, Direction::fromInt(dir)))
            return true;
    }
    return false;
}

bool AIInterface::FindFreePathForNewRoad(MapPoint start, MapPoint target, std::vector<Direction>* route /*= NULL*/,
                                         unsigned* length /*= NULL*/) const
{
    bool boat = false;
    return gwb.GetFreePathFinder().FindPathAlternatingConditions(start, target, false, 100, route, length, NULL, IsPointOK_RoadPath,
                                                                 IsPointOK_RoadPathEvenStep, NULL, (void*)&boat);
}

bool AIInterface::CalcBQSumDifference(const MapPoint pt1, const MapPoint pt2)
{
    return gwb.GetBQ(pt2, playerID_) < gwb.GetBQ(pt1, playerID_);
}

BuildingQuality AIInterface::GetBuildingQuality(const MapPoint pt) const
{
    return gwb.GetBQ(pt, playerID_);
}

BuildingQuality AIInterface::GetBuildingQualityAnyOwner(const MapPoint pt) const
{
    return gwb.GetNode(pt).bq;
}

bool AIInterface::FindPathOnRoads(const noRoadNode& start, const noRoadNode& target, unsigned* length) const
{
    if(length)
        return gwb.GetRoadPathFinder().FindPath(start, target, false, std::numeric_limits<unsigned>::max(), NULL, length);
    else
        return gwb.GetRoadPathFinder().PathExists(start, target, false);
}

const nobHQ* AIInterface::GetHeadquarter() const
{
    return gwb.GetSpecObj<nobHQ>(player_.GetHQPos());
}

BuildingCount AIInterface::GetBuildingCount() const
{
    return player_.GetBuildingCount();
}

bool AIInterface::IsExplorationDirectionPossible(const MapPoint pt, const nobHarborBuilding* originHarbor, ShipDirection direction) const
{
    return IsExplorationDirectionPossible(pt, originHarbor->GetHarborPosID(), direction);
}

bool AIInterface::IsExplorationDirectionPossible(const MapPoint pt, unsigned originHarborID, ShipDirection direction) const
{
    return gwb.GetNextFreeHarborPoint(pt, originHarborID, direction, playerID_) > 0;
}

void AIInterface::SetCoinsAllowed(const nobMilitary* building, const bool enabled)
{
    SetCoinsAllowed(building->GetPos(), enabled);
}
void AIInterface::StartExpedition(const nobHarborBuilding* harbor)
{
    StartExpedition(harbor->GetPos());
}
void AIInterface::ToggleShipYardMode(const nobShipYard* yard)
{
    ToggleShipYardMode(yard->GetPos());
}
void AIInterface::DestroyBuilding(const noBuilding* building)
{
    DestroyBuilding(building->GetPos());
}
void AIInterface::DestroyFlag(const noFlag* flag)
{
    DestroyFlag(flag->GetPos());
}
void AIInterface::CallGeologist(const noFlag* flag)
{
    CallGeologist(flag->GetPos());
}
