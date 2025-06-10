// Copyright (c) ZeroC, Inc.

#include "ClientBlobject.h"
#include "../Ice/CheckIdentity.h"
#include "FilterManager.h"
#include "FilterT.h"
#include "RoutingTable.h"

using namespace std;
using namespace Ice;
using namespace Glacier2;

Glacier2::ClientBlobject::ClientBlobject(
    shared_ptr<Instance> instance,
    shared_ptr<FilterManager> filters,
    const Ice::Context& sslContext,
    shared_ptr<RoutingTable> routingTable)
    :

      Glacier2::Blobject(std::move(instance), nullptr, sslContext),
      _routingTable(std::move(routingTable)),
      _filters(std::move(filters)),
      _rejectTraceLevel(_instance->properties()->getIcePropertyAsInt("Glacier2.Client.Trace.Reject"))
{
}

void
Glacier2::ClientBlobject::ice_invokeAsync(
    pair<const byte*, const byte*> inParams,
    function<void(bool, pair<const byte*, const byte*>)> response,
    function<void(exception_ptr)> error,
    const Current& current)
{
    bool matched = false;
    bool hasFilters = false;
    string rejectedFilters;

    Ice::checkIdentity(current.id, __FILE__, __LINE__);

    if (!_filters->categories()->empty())
    {
        hasFilters = true;
        if (_filters->categories()->match(current.id.category))
        {
            matched = true;
        }
        else if (_rejectTraceLevel >= 1)
        {
            if (rejectedFilters.size() != 0)
            {
                rejectedFilters += ", ";
            }
            rejectedFilters += "category filter";
        }
    }

    if (!_filters->identities()->empty())
    {
        hasFilters = true;
        if (_filters->identities()->match(current.id))
        {
            matched = true;
        }
        else if (_rejectTraceLevel >= 1)
        {
            if (rejectedFilters.size() != 0)
            {
                rejectedFilters += ", ";
            }
            rejectedFilters += "identity filter";
        }
    }

    auto proxy = _routingTable->get(current.id);
    if (!proxy)
    {
        // We use a special operation name to indicate to the client that the proxy for the Ice object has not been
        // found in our routing table. This can happen if the proxy was evicted from the routing table.
        throw ObjectNotExistException{__FILE__, __LINE__, current.id, current.facet, "ice_add_proxy"};
    }

    string adapterId = proxy->ice_getAdapterId();

    if (!adapterId.empty() && !_filters->adapterIds()->empty())
    {
        hasFilters = true;
        if (_filters->adapterIds()->match(adapterId))
        {
            matched = true;
        }
        else if (_rejectTraceLevel >= 1)
        {
            if (rejectedFilters.size() != 0)
            {
                rejectedFilters += ", ";
            }
            rejectedFilters += "adapter id filter";
        }
    }

    if (hasFilters && !matched)
    {
        if (_rejectTraceLevel >= 1)
        {
            Trace out(_instance->logger(), "Glacier2");
            out << "rejecting request: " << rejectedFilters << "\n";
            out << "identity: " << _instance->communicator()->identityToString(current.id);
        }

        throw ObjectNotExistException{__FILE__, __LINE__};
    }

    invoke(proxy.value(), inParams, std::move(response), std::move(error), current);
}

shared_ptr<StringSet>
ClientBlobject::categories()
{
    return _filters->categories();
}

shared_ptr<StringSet>
ClientBlobject::adapterIds()
{
    return _filters->adapterIds();
}

shared_ptr<IdentitySet>
ClientBlobject::identities()
{
    return _filters->identities();
}
