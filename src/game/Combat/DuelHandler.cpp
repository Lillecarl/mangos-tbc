/*
 * This file is part of the CMaNGOS Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "Common.h"
#include "WorldPacket.h"
#include "Server/WorldSession.h"
#include "Log.h"
#include "Entities/Player.h"
#include "World/World.h"

void WorldSession::HandleDuelAcceptedOpcode(WorldPacket& recvPacket)
{
    ObjectGuid guid;
    recvPacket >> guid;

    // Check for own duel info first
    Player* self = GetPlayer();
    if (!self || !self->duel)
        return;

    // Check if we are not accepting our own duel request
    Player* initiator = self->duel->initiator;
    if (!initiator || self == initiator)
        return;

    // Check for opponent
    Player* opponent = self->duel->opponent;
    if (!opponent || self == opponent)
        return;

    // Check if duel is starting
    if (self->duel->startTimer != 0 || opponent->duel->startTimer != 0)
        return;

    // Check if duel is in progress
    if (self->duel->startTime != 0 || opponent->duel->startTime != 0)
        return;

    DEBUG_FILTER_LOG(LOG_FILTER_COMBAT, "WORLD: received CMSG_DUEL_ACCEPTED");
    DEBUG_FILTER_LOG(LOG_FILTER_COMBAT, "Player 1 is: %u (%s)", self->GetGUIDLow(), self->GetName());
    DEBUG_FILTER_LOG(LOG_FILTER_COMBAT, "Player 2 is: %u (%s)", opponent->GetGUIDLow(), opponent->GetName());

    time_t now = time(nullptr);
    self->duel->startTimer = now;
    opponent->duel->startTimer = now;

    // Duel Reset code in scope below
    {
        // Vector with duel initiator and opponent
        std::vector<Unit*> UnitsToReset = { self, opponent };

        // Also fill pets up if they're alive. Can't use range because we're modifying the container
        for (size_t i = 0; i < UnitsToReset.size(); ++i)
            if (Pet* pet = UnitsToReset[i]->GetPet())
                if (pet->IsAlive())
                    UnitsToReset.push_back(pet);

        for (auto& i : UnitsToReset)
        {
            // Only allow duel resetting if we're on a continent:w
            if (i->GetMap() && i->GetMap()->IsContinent())
                continue;

            // Reset health
            if (sWorld.getConfig(CONFIG_BOOL_DUELRESET_HEALTH))
                i->SetHealth(i->GetMaxHealth());
            // Reset power for classes that has it (should be everything but rage)
            if (sWorld.getConfig(CONFIG_BOOL_DUELRESET_POWER))
            {
                i->SetPower(POWER_MANA, i->GetMaxPower(POWER_MANA));
                i->SetPower(POWER_ENERGY, i->GetMaxPower(POWER_ENERGY));
            }

            // Remove all cooldowns for pets
            if (!i->IsPlayer() &&
               (sWorld.getConfig(CONFIG_BOOL_DUELRESET_ALLCOOLDOWNS) ||
                sWorld.getConfig(CONFIG_BOOL_DUELRESET_ARENACOOLDOWNS)))
                i->RemoveAllCooldowns();

            // Remove arena cooldowns if player isn't in a dungeon
            if (Player* player = i->ToPlayer())
            {
                if (sWorld.getConfig(CONFIG_BOOL_DUELRESET_ARENACOOLDOWNS))
                    player->RemoveArenaSpellCooldowns();
                if (sWorld.getConfig(CONFIG_BOOL_DUELRESET_ALLCOOLDOWNS))
                    player->RemoveAllCooldowns();
            }
        }
    }

    self->SendDuelCountdown(3000);
    opponent->SendDuelCountdown(3000);
}

void WorldSession::HandleDuelCancelledOpcode(WorldPacket& recvPacket)
{
    ObjectGuid guid;
    recvPacket >> guid;

    // Check for own duel info first
    Player* self = GetPlayer();
    if (!self || !self->duel)
        return;

    // Check for opponent
    Player* opponent = self->duel->opponent;
    if (!opponent)
        return;

    DEBUG_LOG("WORLD: Received opcode CMSG_DUEL_CANCELLED");

    // If duel is in progress, then player surrendered in a duel using /forfeit
    if (self->duel->startTime != 0)
    {
        self->CombatStopWithPets(true);
        opponent->CombatStopWithPets(true);
        self->CastSpell(self, 7267, TRIGGERED_OLD_TRIGGERED);    // beg
        self->DuelComplete(DUEL_WON);
        return;
    }
    // Player either discarded the duel using the "discard button" or used "/forfeit" before countdown reached 0
    self->DuelComplete(DUEL_INTERRUPTED);
}
