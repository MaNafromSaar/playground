# Heritage Game Duel Mechanics

## Core Mode: Classic Duel (2 Players)

- Each player builds a roster of 5 starting creatures.
- Allowed starting classes: Ranger, Warrior, Tank.
- Match format: best-of-3 rounds (first to 2 rounds wins).
- Each round is a 3v3 lineup battle.
- Network server prompts every required input (roster, upgrades, lineups).
- Party is displayed between interactive phases.
- Network transport now emits structured JSON event lines by default.

## Match Length Presets

- `1`: one-round pub duel
- `3`: best of 3
- `7`: best of 7
- `max`: ends when one player reaches 3 flawless wins, or when both players reach the level cap and points decide the winner

Point scoring for long sessions:

- each duel win: +1 point
- each round win: +1 point
- each flawless round: +2 extra points

In `max` mode, if both players hit the level cap before anyone reaches 3 flawless wins, points are compared to decide the winner.

## Round Flow

1. First picker for round 1 is random.
2. In rounds 2 and 3, first picker is the winner of the previous round.
3. First picker chooses 3 unique creatures from their 5.
4. If there was a previous round winner, the previous round loser gets advantage:
	- sees 2 random class types from the winner's chosen 3
	- gets a small tactical combat edge this round
5. Second picker chooses their own 3 unique creatures.
6. Three duels are resolved slot-vs-slot.
7. Round winner is the player with more duel wins.

If a round is drawn, first-pick priority remains with the previous first picker.

## Initiative And Comeback Balance

- Winner side attacks first in each duel of that round.
- Loser side gets information and a slight tactical edge in combat.
- This intentionally favors comeback potential without removing reward for winning.
- In higher tiers, this gets deeper because allrounders can absorb partial counter-picks.

## Anti One-Shot Combat Rules

Combat is HP-based per duel and uses bounded damage to avoid instant deletions:

- Evasion has a cap range (roughly 5% to 35% with level influence).
- Hit damage has a floor to prevent stalemates.
- Hit damage has a cap to prevent one-shot kills.
- Overtime minimum damage ramps up so fights still end by knockout.

This keeps first-attack meaningful but not oppressive.

## Current Competitive Value Ranges

The current code is competitive for early iterations with these practical ranges:

- Attack: about 5 to 20+
- Defense: about 3 to 20+
- Life: about 18 to 45+
- Evasion: 0.08 to about 0.30 (8% to 30%)
- Type matchup multiplier: 0.85x to 1.15x
- Duel variance: random factor around 0.94x to 1.06x
- Damage cap per strike: constrained to prevent one-shot outcomes

These ranges allow counters to matter, but not hard-lock every duel.

## XP And Leveling

Player progression is XP-based with level cap 9.

- Start resources: 13 upgrade points per player before round 1.
- Level-up reward: +5 upgrade points per level gained.
- Upgrade points can be spent between rounds and before lineups.

- Participation XP: +40 (for being in the 3v3 lineup)
- Duel win XP: +30
- Round win XP: +25
- Flawless bonus XP: +35 (if round score is 3-0)

### Level Thresholds

- Level 1: 0 XP
- Level 2: 80 XP
- Level 3: 165 XP
- Level 4: 255 XP
- Level 5: 350 XP
- Level 6: 450 XP
- Level 7: 555 XP
- Level 8: 665 XP
- Level 9: 780 XP

Design intent:

- Regular participation and normal wins usually reach mid levels.
- Level 9 is reachable with strong play, especially with flawless bonus rounds.
- This rewards tactical performance and comeback planning, not passive accumulation.

## Upgrade Inputs

Server upgrade prompt format:

- `<creature_index> <stat> <points>`
- Example: `3 atk 2`
- Stats: `atk`, `def`, `life`, `eva`
- `done` keeps remaining points for later rounds.

## JSON Transport

The network duel server is now suitable as a UI backend.

- Default behavior: emits structured event lines.
- Event line prefix: `@event `
- Format: one JSON object per line after the prefix.
- Optional fallback: `--text-only` disables structured event output.

### Typical Event Types

- `match_started`
- `player_connected`
- `party_state`
- `prompt`
- `roster_pick_locked`
- `upgrade_phase_started`
- `upgrade_applied`
- `upgrade_phase_finished`
- `lineup_locked`
- `lineup_reveal`
- `round_started`
- `duel_started`
- `duel_resolved`
- `xp_awarded`
- `level_up`
- `round_resolved`
- `match_resolved`

## Visual Client Direction

- A Python client should consume the JSON events as the authoritative state feed.
- The human-readable combat narration should still be displayed in the visual version.
- Best approach: show both layers together:
	- structured card/board updates from JSON events
	- readable combat log so the player sees what actually happened

### Current Prototype

- File: `visual_duel_client.py`
- Stack: Python standard library with Tkinter
- Purpose: simple playable visual shell around the JSON transport

Run flow:

1. Start the server:
	- `./network_duel_server 5050`
2. Launch one visual client per player:
	- `python3 visual_duel_client.py`
	- or `make visual-client`
3. In each client window:
	- enter host, port, and player name
	- click Connect
	- use the bottom input bar for roster picks, upgrades, and lineups

Prototype UI includes:

- your party panel
- opponent party panel
- current round / duel / reveal status
- readable combat log
- guided input field for all prompts

## Top-Range Highlighting

- Each shown stat is compared against that creature's own max achievable value at level 9.
- If current value is in the top 10% of achievable range, server marks it with `[TOP10]`.
- This helps players instantly spot highly optimized builds.

## Unified Balance Scale (Recommended)

To keep future features readable and comparable, use a 0 to 100 normalized combat model internally for design, while still storing raw stats:

- Power score: normalized attack contribution
- Guard score: normalized defense contribution
- Vitality score: normalized life contribution
- Agility score: normalized evasion and speed contribution

Then convert effects to probabilities where possible.

### Suggested Probability Caps

- Evasion chance: 5% to 35%
- Crit chance: 0% to 30%
- Block chance: 0% to 35%
- Status apply chance: 10% to 70%
- Cleanse/resist chance: 5% to 60%

Use soft caps so stacking gives diminishing returns near the top.

## Example Scoring Pattern

A stable, explainable duel formula can be:

- BaseCombat = 0.35 * Power + 0.30 * Guard + 0.25 * Vitality + 0.10 * Agility
- MatchupCombat = BaseCombat * TypeMultiplier
- FinalCombat = MatchupCombat * RandomVariance

Where:

- TypeMultiplier is usually 85% to 115%
- RandomVariance is usually 94% to 106%

This keeps matchup identity meaningful while preserving comeback potential.

## Strategy Notes

- Balanced 5-unit rosters reduce risk against reveal-based counter-play.
- Triple-single-type lineups can spike one round but become predictable.
- If the opponent reveals two aggressive types, consider one hard counter and two allrounders.
- In higher tiers, allrounders are valuable because they reduce punishment from imperfect reads.
- First pick is strongest when your roster has flexible dueling slots, not only hard specialists.
- If you won the previous round, expect counter-adaptation and avoid repeating the exact same 3 picks.
- If you lost the previous round, use the reveal to force one favorable duel and stabilize the other two.

## Next Extensions

- Add hidden order mind-games (second picker can reorder after seeing reveals).
- Add a ban phase (each player bans one class before lineup selection).
- Add stamina/cooldown between rounds so repeated usage has tradeoffs.
- Add trait probabilities (crit, block, resist) using the same capped percentage model.
