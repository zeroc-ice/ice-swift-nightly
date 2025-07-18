// Copyright (c) ZeroC, Inc.

// slice2cpp version 3.8.0-alpha.0
// <auto-generated>Generated from Slice file 'Process.ice'.</auto-generated>

#ifndef ICE_API_EXPORTS
#   define ICE_API_EXPORTS
#endif
#define ICE_BUILDING_GENERATED_CODE

#include "Ice/Process.h"
#include <Ice/AsyncResponseHandler.h>
#include <Ice/DefaultSliceLoader.h>
#include <Ice/OutgoingAsync.h>
#include <algorithm>
#include <array>

#if defined(_MSC_VER)
#   pragma warning(disable : 4458) // declaration of ... hides class member
#   pragma warning(disable : 4996) // ... was declared deprecated
#elif defined(__clang__)
#   pragma clang diagnostic ignored "-Wshadow"
#   pragma clang diagnostic ignored "-Wdeprecated-declarations"
#elif defined(__GNUC__)
#   pragma GCC diagnostic ignored "-Wshadow"
#   pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif

#ifndef ICE_DISABLE_VERSION
#   if ICE_INT_VERSION  != 30850
#       error Ice version mismatch: an exact match is required for beta generated code
#   endif
#endif

namespace
{
}

Ice::ProcessPrx::~ProcessPrx() = default;

void
Ice::ProcessPrx::shutdown(const Ice::Context& context) const
{
    IceInternal::makePromiseOutgoing<void>(true, this, &ProcessPrx::_iceI_shutdown, context).get();
}

std::future<void>
Ice::ProcessPrx::shutdownAsync(const Ice::Context& context) const
{
    return IceInternal::makePromiseOutgoing<void>(false, this, &ProcessPrx::_iceI_shutdown, context);
}

std::function<void()>
Ice::ProcessPrx::shutdownAsync(std::function<void()> response, std::function<void(std::exception_ptr)> exception, std::function<void(bool)> sent, const Ice::Context& context) const
{
    return IceInternal::makeLambdaOutgoing<void>(std::move(response), std::move(exception), std::move(sent), this, &Ice::ProcessPrx::_iceI_shutdown, context);
}

void
Ice::ProcessPrx::_iceI_shutdown(const std::shared_ptr<IceInternal::OutgoingAsyncT<void>>& outAsync, const Ice::Context& context) const
{
    static constexpr std::string_view operationName = "shutdown";

    outAsync->invoke(
        operationName,
        Ice::OperationMode::Normal,
        std::nullopt,
        context,
        nullptr,
        nullptr);
}

void
Ice::ProcessPrx::writeMessage(std::string_view iceP_message, std::int32_t iceP_fd, const Ice::Context& context) const
{
    IceInternal::makePromiseOutgoing<void>(true, this, &ProcessPrx::_iceI_writeMessage, iceP_message, iceP_fd, context).get();
}

std::future<void>
Ice::ProcessPrx::writeMessageAsync(std::string_view iceP_message, std::int32_t iceP_fd, const Ice::Context& context) const
{
    return IceInternal::makePromiseOutgoing<void>(false, this, &ProcessPrx::_iceI_writeMessage, iceP_message, iceP_fd, context);
}

std::function<void()>
Ice::ProcessPrx::writeMessageAsync(std::string_view iceP_message, std::int32_t iceP_fd, std::function<void()> response, std::function<void(std::exception_ptr)> exception, std::function<void(bool)> sent, const Ice::Context& context) const
{
    return IceInternal::makeLambdaOutgoing<void>(std::move(response), std::move(exception), std::move(sent), this, &Ice::ProcessPrx::_iceI_writeMessage, iceP_message, iceP_fd, context);
}

void
Ice::ProcessPrx::_iceI_writeMessage(const std::shared_ptr<IceInternal::OutgoingAsyncT<void>>& outAsync, std::string_view iceP_message, std::int32_t iceP_fd, const Ice::Context& context) const
{
    static constexpr std::string_view operationName = "writeMessage";

    outAsync->invoke(
        operationName,
        Ice::OperationMode::Normal,
        std::nullopt,
        context,
        [&](Ice::OutputStream* ostr)
        {
            ostr->writeAll(iceP_message, iceP_fd);
        },
        nullptr);
}

const char*
Ice::ProcessPrx::ice_staticId() noexcept
{
    return "::Ice::Process";
}

void
Ice::Process::dispatch(Ice::IncomingRequest& request, std::function<void(Ice::OutgoingResponse)> sendResponse)
{
    static constexpr std::array<std::string_view, 6> allOperations{"ice_id", "ice_ids", "ice_isA", "ice_ping", "shutdown", "writeMessage"};

    const Ice::Current& current = request.current();
    auto r = std::equal_range(allOperations.begin(), allOperations.end(), current.operation); // NOLINT(modernize-use-ranges)
    if (r.first == r.second)
    {
        sendResponse(Ice::makeOutgoingResponse(std::make_exception_ptr(Ice::OperationNotExistException{__FILE__, __LINE__}), current));
        return;
    }

    switch (r.first - allOperations.begin())
    {
        case 0:
        {
            _iceD_ice_id(request, std::move(sendResponse));
            break;
        }
        case 1:
        {
            _iceD_ice_ids(request, std::move(sendResponse));
            break;
        }
        case 2:
        {
            _iceD_ice_isA(request, std::move(sendResponse));
            break;
        }
        case 3:
        {
            _iceD_ice_ping(request, std::move(sendResponse));
            break;
        }
        case 4:
        {
            _iceD_shutdown(request, std::move(sendResponse));
            break;
        }
        case 5:
        {
            _iceD_writeMessage(request, std::move(sendResponse));
            break;
        }
        default:
        {
            assert(false);
            sendResponse(Ice::makeOutgoingResponse(std::make_exception_ptr(Ice::OperationNotExistException{__FILE__, __LINE__}), current));
        }
    }
}

std::vector<std::string>
Ice::Process::ice_ids(const Ice::Current&) const
{
    static const std::vector<std::string> allTypeIds = {"::Ice::Object", "::Ice::Process"};
    return allTypeIds;
}

std::string
Ice::Process::ice_id(const Ice::Current&) const
{
    return std::string{ice_staticId()};
}

void
Ice::Process::_iceD_shutdown(
    Ice::IncomingRequest& request,
    std::function<void(Ice::OutgoingResponse)> sendResponse) // NOLINT(performance-unnecessary-value-param)
{
    checkNonIdempotent(request.current());
    request.inputStream().skipEmptyEncapsulation();
    this->shutdown(request.current());
    sendResponse(Ice::makeEmptyOutgoingResponse(request.current()));
}

void
Ice::Process::_iceD_writeMessage(
    Ice::IncomingRequest& request,
    std::function<void(Ice::OutgoingResponse)> sendResponse) // NOLINT(performance-unnecessary-value-param)
{
    checkNonIdempotent(request.current());
    auto istr = &request.inputStream();
    istr->startEncapsulation();
    std::string iceP_message;
    std::int32_t iceP_fd;
    istr->readAll(iceP_message, iceP_fd);
    istr->endEncapsulation();
    this->writeMessage(std::move(iceP_message), iceP_fd, request.current());
    sendResponse(Ice::makeEmptyOutgoingResponse(request.current()));
}

const char*
Ice::Process::ice_staticId() noexcept
{
    return "::Ice::Process";
}
