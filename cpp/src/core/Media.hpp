#pragma once

#include "Types.hpp"

namespace galleon {

// Fills the now-playing fields of `state` from playerctl. Cached for under a second.
// An empty `players` list follows whichever player playerctl picks. Names in the
// list are the only players that may appear; a Playing one wins over a Paused one.
void fillMedia(LcdState &state, const QStringList &players = {});

// Default sink volume, 0–100, from wpctl or pactl. `percent` is -1 when unknown.
void readVolume(int &percent, bool &muted);

// Stable names of running MPRIS players. firefox.instance123 is reported as firefox.
QStringList listMediaPlayers();

// Name to store for a lock. An instance is reduced to its player name, and a
// pattern that contains * or ? is kept. Empty when the text cannot be saved.
QString normalizeMediaPlayer(const QString &raw);

} // namespace galleon
