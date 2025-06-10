// Copyright (c) ZeroC, Inc.

#include "Ice/Config.h"

#if TARGET_OS_IPHONE != 0

#    include "../ProtocolInstance.h"
#    include "iAPConnector.h"
#    include "iAPEndpointI.h"
#    include "iAPTransceiver.h"

using namespace std;
using namespace Ice;
using namespace IceInternal;

TransceiverPtr
IceObjC::iAPConnector::connect()
{
    EASession* session = [[EASession alloc] initWithAccessory:_accessory forProtocol:_protocol];
    if (!session)
    {
        throw Ice::ConnectFailedException(__FILE__, __LINE__, 0);
    }
    auto transceiver = make_shared<iAPTransceiver>(_instance, session);
#    if defined(__clang__) && !__has_feature(objc_arc)
    [session release];
#    endif
    return transceiver;
}

int16_t
IceObjC::iAPConnector::type() const
{
    return _instance->type();
}

string
IceObjC::iAPConnector::toString() const
{
    ostringstream os;
    os << [_accessory.name UTF8String];
    os << " model `" << [_accessory.modelNumber UTF8String] << "'";
    os << " made by `" << [_accessory.manufacturer UTF8String] << "'";
    os << " protocol `" << [_protocol UTF8String] << "'";
    return os.str();
}

bool
IceObjC::iAPConnector::operator==(const IceInternal::Connector& r) const
{
    const iAPConnector* p = dynamic_cast<const iAPConnector*>(&r);
    if (!p)
    {
        return false;
    }

    if (_timeout != p->_timeout)
    {
        return false;
    }

    if (_connectionId != p->_connectionId)
    {
        return false;
    }

    if (![_accessory isEqual:p->_accessory])
    {
        return false;
    }

    if (![_protocol isEqual:p->_protocol])
    {
        return false;
    }

    return true;
}

bool
IceObjC::iAPConnector::operator<(const IceInternal::Connector& r) const
{
    const iAPConnector* p = dynamic_cast<const iAPConnector*>(&r);
    if (!p)
    {
        return type() < r.type();
    }

    if (_timeout < p->_timeout)
    {
        return true;
    }
    else if (p->_timeout < _timeout)
    {
        return false;
    }

    if (_connectionId < p->_connectionId)
    {
        return true;
    }
    else if (p->_connectionId < _connectionId)
    {
        return false;
    }

    if ([_accessory hash] < [p->_accessory hash])
    {
        return true;
    }
    else if ([p->_accessory hash] < [_accessory hash])
    {
        return false;
    }

    NSInteger order = [_protocol compare:p->_protocol];
    if (order == NSOrderedAscending)
    {
        return true;
    }
    else if (order == NSOrderedDescending)
    {
        return false;
    }

    return false;
}

IceObjC::iAPConnector::iAPConnector(
    const ProtocolInstancePtr& instance,
    int32_t timeout,
    const string& connectionId,
    NSString* protocol,
    EAAccessory* accessory)
    : _instance(instance),
      _timeout(timeout),
      _connectionId(connectionId),
#    if defined(__clang__) && !__has_feature(objc_arc)
      _protocol([protocol retain]),
      _accessory([accessory retain])
#    else
      _protocol(protocol),
      _accessory(accessory)
#    endif
{
}

IceObjC::iAPConnector::~iAPConnector()
{
#    if defined(__clang__) && !__has_feature(objc_arc)
    [_protocol release];
    [_accessory release];
#    endif
}

#endif
