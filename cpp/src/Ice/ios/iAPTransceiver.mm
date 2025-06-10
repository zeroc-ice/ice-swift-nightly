// Copyright (c) ZeroC, Inc.

#include "Ice/Config.h"

#if TARGET_OS_IPHONE != 0

#    include "../ProtocolInstance.h"
#    include "Ice/Buffer.h"
#    include "Ice/LocalExceptions.h"
#    include "iAPEndpointI.h"
#    include "iAPTransceiver.h"

#    import <Foundation/NSError.h>
#    import <Foundation/NSRunLoop.h>
#    import <Foundation/NSString.h>

using namespace std;
using namespace Ice;
using namespace IceInternal;

@interface iAPTransceiverCallback : NSObject <NSStreamDelegate>
{
@private

    SelectorReadyCallback* callback;
}
- (id)init:(SelectorReadyCallback*)cb;
@end

@implementation iAPTransceiverCallback
- (id)init:(SelectorReadyCallback*)cb
{
    self = [super init];
    if (!self)
    {
        return nil;
    }
    callback = cb;
    return self;
}

- (void)stream:(NSStream*)stream handleEvent:(NSStreamEvent)eventCode
{
    switch (eventCode)
    {
        case NSStreamEventHasBytesAvailable:
            callback->readyCallback(SocketOperationRead);
            break;
        case NSStreamEventHasSpaceAvailable:
            callback->readyCallback(SocketOperationWrite);
            break;
        case NSStreamEventOpenCompleted:
            if ([[stream class] isSubclassOfClass:[NSInputStream class]])
            {
                callback->readyCallback(static_cast<SocketOperation>(SocketOperationConnect | SocketOperationRead));
            }
            else
            {
                callback->readyCallback(static_cast<SocketOperation>(SocketOperationConnect | SocketOperationWrite));
            }
            break;
        default:
            if ([[stream class] isSubclassOfClass:[NSInputStream class]])
            {
                callback->readyCallback(SocketOperationRead, -1); // Error
            }
            else
            {
                callback->readyCallback(SocketOperationWrite, -1); // Error
            }
    }
}
@end

void
IceObjC::iAPTransceiver::initStreams(SelectorReadyCallback* callback)
{
    _callback = [[iAPTransceiverCallback alloc] init:callback];
    [_writeStream setDelegate:_callback];
    [_readStream setDelegate:_callback];
}

SocketOperation
IceObjC::iAPTransceiver::registerWithRunLoop(SocketOperation op)
{
    lock_guard lock(_mutex);
    SocketOperation readyOp = SocketOperationNone;
    if (op & SocketOperationConnect)
    {
        if ([_writeStream streamStatus] != NSStreamStatusNotOpen || [_readStream streamStatus] != NSStreamStatusNotOpen)
        {
            return SocketOperationConnect;
        }

        _opening = true;

        [_writeStream scheduleInRunLoop:[NSRunLoop currentRunLoop] forMode:NSDefaultRunLoopMode];
        [_readStream scheduleInRunLoop:[NSRunLoop currentRunLoop] forMode:NSDefaultRunLoopMode];

        _writeStreamRegistered = true; // Note: this must be set after the schedule call
        _readStreamRegistered = true;  // Note: this must be set after the schedule call

        [_writeStream open];
        [_readStream open];
    }
    else
    {
        if (op & SocketOperationWrite)
        {
            if ([_writeStream hasSpaceAvailable])
            {
                readyOp = static_cast<SocketOperation>(readyOp | SocketOperationWrite);
            }
            else if (!_writeStreamRegistered)
            {
                [_writeStream scheduleInRunLoop:[NSRunLoop currentRunLoop] forMode:NSDefaultRunLoopMode];
                _writeStreamRegistered = true; // Note: this must be set after the schedule call
                if ([_writeStream hasSpaceAvailable])
                {
                    readyOp = static_cast<SocketOperation>(readyOp | SocketOperationWrite);
                }
            }
        }

        if (op & SocketOperationRead)
        {
            if ([_readStream hasBytesAvailable])
            {
                readyOp = static_cast<SocketOperation>(readyOp | SocketOperationRead);
            }
            else if (!_readStreamRegistered)
            {
                [_readStream scheduleInRunLoop:[NSRunLoop currentRunLoop] forMode:NSDefaultRunLoopMode];
                _readStreamRegistered = true; // Note: this must be set after the schedule call
                if ([_readStream hasBytesAvailable])
                {
                    readyOp = static_cast<SocketOperation>(readyOp | SocketOperationRead);
                }
            }
        }
    }
    return readyOp;
}

SocketOperation
IceObjC::iAPTransceiver::unregisterFromRunLoop(SocketOperation op, bool error)
{
    lock_guard lock(_mutex);
    _error |= error;

    if (_opening)
    {
        // Wait for the stream to be ready for write
        if (op == SocketOperationWrite)
        {
            _writeStreamRegistered = false;
        }

        //
        // We don't wait for the stream to be ready for read (even if
        // it's a client connection) because there's no guarantees that
        // the server might actually send data right away. If we use
        // the WebSocket transport, the server actually waits for the
        // client to write the HTTP upgrade request.
        //
        // if(op & SocketOperationRead && (_fd != INVALID_SOCKET || !(op & SocketOperationConnect)))
        if (op == (SocketOperationRead | SocketOperationConnect))
        {
            _readStreamRegistered = false;
        }

        if (error || (!_readStreamRegistered && !_writeStreamRegistered))
        {
            [_writeStream removeFromRunLoop:[NSRunLoop currentRunLoop] forMode:NSDefaultRunLoopMode];
            [_readStream removeFromRunLoop:[NSRunLoop currentRunLoop] forMode:NSDefaultRunLoopMode];
            _opening = false;
            return SocketOperationConnect;
        }
        else
        {
            return SocketOperationNone;
        }
    }
    else
    {
        if (op & SocketOperationWrite && _writeStreamRegistered)
        {
            [_writeStream removeFromRunLoop:[NSRunLoop currentRunLoop] forMode:NSDefaultRunLoopMode];
            _writeStreamRegistered = false;
        }

        if (op & SocketOperationRead && _readStreamRegistered)
        {
            [_readStream removeFromRunLoop:[NSRunLoop currentRunLoop] forMode:NSDefaultRunLoopMode];
            _readStreamRegistered = false;
        }
    }
    return op;
}

void
IceObjC::iAPTransceiver::closeStreams()
{
    [_writeStream setDelegate:nil];
    [_readStream setDelegate:nil];

#    if defined(__clang__) && !__has_feature(objc_arc)
    [_callback release];
#    endif
    _callback = 0;

    [_writeStream close];
    [_readStream close];
}

IceInternal::NativeInfoPtr
IceObjC::iAPTransceiver::getNativeInfo()
{
    return shared_from_this();
}

SocketOperation
IceObjC::iAPTransceiver::initialize(Buffer& /*readBuffer*/, Buffer& /*writeBuffer*/)
{
    lock_guard lock(_mutex);
    if (_state == StateNeedConnect)
    {
        _state = StateConnectPending;
        return SocketOperationConnect;
    }

    if (_state <= StateConnectPending)
    {
        if (_error)
        {
            checkErrorStatus(_writeStream, __FILE__, __LINE__);
            checkErrorStatus(_readStream, __FILE__, __LINE__);
        }
        _state = StateConnected;
    }
    assert(_state == StateConnected);
    return SocketOperationNone;
}

SocketOperation
IceObjC::iAPTransceiver::closing(bool initiator, exception_ptr)
{
    // If we are initiating the connection closure, wait for the peer
    // to close the TCP/IP connection. Otherwise, close immediately.
    return initiator ? SocketOperationRead : SocketOperationNone;
}

void
IceObjC::iAPTransceiver::close()
{
}

SocketOperation
IceObjC::iAPTransceiver::write(Buffer& buf)
{
    // Don't hold the lock while calling on the NSStream API to avoid deadlocks in case the NSStream API calls
    // the stream notification callbacks with an internal lock held.
    {
        lock_guard lock(_mutex);
        if (_error)
        {
            checkErrorStatus(_writeStream, __FILE__, __LINE__);
        }
        else if (_writeStreamRegistered)
        {
            return SocketOperationWrite;
        }
    }

    size_t packetSize = static_cast<size_t>(buf.b.end() - buf.i);
    while (buf.i != buf.b.end())
    {
        if (![_writeStream hasSpaceAvailable])
        {
            return SocketOperationWrite;
        }
        assert([_writeStream streamStatus] >= NSStreamStatusOpen);

        NSInteger ret = [_writeStream write:reinterpret_cast<const UInt8*>(&*buf.i) maxLength:packetSize];
        if (ret == SOCKET_ERROR)
        {
            checkErrorStatus(_writeStream, __FILE__, __LINE__);
            if (noBuffers() && packetSize > 1024)
            {
                packetSize /= 2;
            }
            continue;
        }

        buf.i += ret;

        if (packetSize > static_cast<size_t>(buf.b.end() - buf.i))
        {
            packetSize = static_cast<size_t>(buf.b.end() - buf.i);
        }
    }

    return SocketOperationNone;
}

SocketOperation
IceObjC::iAPTransceiver::read(Buffer& buf)
{
    // Don't hold the lock while calling on the NSStream API to avoid deadlocks in case the NSStream API calls
    // the stream notification callbacks with an internal lock held.
    {
        lock_guard lock(_mutex);
        if (_error)
        {
            checkErrorStatus(_readStream, __FILE__, __LINE__);
        }
        else if (_readStreamRegistered)
        {
            return SocketOperationRead;
        }
    }

    size_t packetSize = static_cast<size_t>(buf.b.end() - buf.i);
    while (buf.i != buf.b.end())
    {
        if (![_readStream hasBytesAvailable] && [_readStream streamStatus] != NSStreamStatusError)
        {
            return SocketOperationRead;
        }
        assert([_readStream streamStatus] >= NSStreamStatusOpen);

        NSInteger ret = [_readStream read:reinterpret_cast<UInt8*>(&*buf.i) maxLength:packetSize];
        if (ret == 0)
        {
            throw ConnectionLostException{__FILE__, __LINE__, 0};
        }

        if (ret == SOCKET_ERROR)
        {
            checkErrorStatus(_readStream, __FILE__, __LINE__);
            if (noBuffers() && packetSize > 1024)
            {
                packetSize /= 2;
            }
            continue;
        }

        buf.i += ret;

        if (packetSize > static_cast<size_t>(buf.b.end() - buf.i))
        {
            packetSize = static_cast<size_t>(buf.b.end() - buf.i);
        }
    }

    return SocketOperationNone;
}

string
IceObjC::iAPTransceiver::protocol() const
{
    return _instance->protocol();
}

string
IceObjC::iAPTransceiver::toString() const
{
    return _desc;
}

string
IceObjC::iAPTransceiver::toDetailedString() const
{
    return toString();
}

Ice::ConnectionInfoPtr
IceObjC::iAPTransceiver::getInfo([[maybe_unused]] bool incoming, string adapterName, string connectionId) const
{
    assert(!incoming);

    return make_shared<Ice::IAPConnectionInfo>(
        std::move(adapterName),
        std::move(connectionId),
        [_session.accessory.name UTF8String],
        [_session.accessory.manufacturer UTF8String],
        [_session.accessory.modelNumber UTF8String],
        [_session.accessory.firmwareRevision UTF8String],
        [_session.accessory.hardwareRevision UTF8String],
        [_session.protocolString UTF8String]);
}

void
IceObjC::iAPTransceiver::checkSendSize(const Buffer& /*buf*/)
{
}

void
IceObjC::iAPTransceiver::setBufferSize(int, int)
{
}

IceObjC::iAPTransceiver::iAPTransceiver(const ProtocolInstancePtr& instance, EASession* session)
    : StreamNativeInfo(INVALID_SOCKET),
      _instance(instance),
#    if defined(__clang__) && !__has_feature(objc_arc)
      _session([session retain]),
#    else
      _session(session),
#    endif
      _readStream([session inputStream]),
      _writeStream([session outputStream]),
      _readStreamRegistered(false),
      _writeStreamRegistered(false),
      _error(false),
      _state(StateNeedConnect)
{
    ostringstream os;
    os << "name = " << [session.accessory.name UTF8String] << "\n";
    os << "protocol = " << [session.protocolString UTF8String];
    _desc = os.str();
}

IceObjC::iAPTransceiver::~iAPTransceiver()
{
#    if defined(__clang__) && !__has_feature(objc_arc)
    [_session release];
#    endif
}

void
IceObjC::iAPTransceiver::checkErrorStatus(NSStream* stream, const char* file, int line)
{
    NSStreamStatus status = [stream streamStatus];
    if (status == NSStreamStatusAtEnd || status == NSStreamStatusClosed)
    {
        throw ConnectionLostException{file, line, 0};
    }

    assert(status == NSStreamStatusError);
    NSError* err = [stream streamError];
    assert(err != nil);

    NSString* domain = [err domain];
    if ([domain compare:NSPOSIXErrorDomain] == NSOrderedSame)
    {
        errno = static_cast<int>([err code]);
        if (interrupted() || noBuffers())
        {
            return;
        }
        else if (connectionRefused())
        {
            throw ConnectionRefusedException{file, line};
        }
        else if (connectFailed())
        {
            throw ConnectFailedException{file, line, getSocketErrno()};
        }
        else
        {
            throw SocketException{file, line, "CFNetwork error", getSocketErrno()};
        }
    }

    // Otherwise throw a generic exception.
    throw SocketException{
        file,
        line,
        "CFNetwork error in domain " + string{[domain UTF8String]} + ": " + to_string([err code])};
}

#endif
