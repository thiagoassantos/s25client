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
#ifndef GAMEMANAGER_H_INCLUDED
#define GAMEMANAGER_H_INCLUDED

#pragma once

#include "libutil/src/Singleton.h"

// Die verschiedenen Cursor mit ihren Indizes in resource.idx
enum CursorType
{
    CURSOR_NONE,
    CURSOR_HAND,
    CURSOR_SCROLL = 32,
    CURSOR_MOON = 33,
    CURSOR_RM = 34,
    CURSOR_RM_PRESSED = 35
};

/// "Die" GameManager-Klasse
class GameManager : public Singleton<GameManager, SingletonPolicies::WithLongevity>
{
public:
    BOOST_STATIC_CONSTEXPR unsigned Longevity = 15;

    GameManager();

    bool Start();
    void Stop();
    bool Run();

    bool StartMenu();
    bool ShowMenu();

    /// Average FPS Zähler zurücksetzen.
    inline void ResetAverageFPS()
    {
        run_time = 0;
        frame_count = 0;
    }

    inline unsigned GetRuntime() { return run_time; }

    inline unsigned GetFrameCount() { return frame_count; }

    inline unsigned GetAverageFPS()
    {
        if(run_time == 0)
            return 0;
        return (frame_count / run_time);
    }

    inline unsigned GetFPS() { return framerate; }

    void SetCursor(CursorType cursor = CURSOR_HAND, bool once = false);

private:
    void DrawCursor();

private:
    unsigned frames;
    unsigned frame_count;
    unsigned framerate;
    unsigned frame_time;
    unsigned run_time;
    unsigned last_time;
    unsigned skipgf_last_time;
    unsigned skipgf_last_report_gf;
    CursorType cursor_;
    CursorType cursor_next;
};

#define GAMEMANAGER GameManager::inst()

#endif // GAMEMANAGER_H_INCLUDED
