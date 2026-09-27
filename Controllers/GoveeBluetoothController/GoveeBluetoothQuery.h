/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once

#include "GoveeBluetoothProtocol.h"

namespace GoveeBluetooth
{
struct QueryResult
{
    Packet reply;
    unsigned int attempts;
};

// Only a missing reply permits another read. GATT errors, cancellation and
// invalid state must propagate immediately. Exchange clears stale replies and
// performs one bounded request/response on the existing connection.
template<class ReplyTimeout, class Exchange>
QueryResult QueryWithRetry(Profile profile, uint8_t command, Exchange&& exchange)
{
    const unsigned int limit = profile == Profile::H6159 || command == 1 ? 2u : 1u;
    for(unsigned int attempt = 1; ; ++attempt)
    {
        try { return {exchange(attempt), attempt}; }
        catch(const ReplyTimeout&)
        {
            if(attempt >= limit) throw;
        }
    }
}
}
