// Copyright (c) ZeroC, Inc.

// slice2cpp version 3.8.0-alpha.0
// <auto-generated>Generated from Slice file 'LocatorRegistry.ice'.</auto-generated>
// clang-format off

#ifndef Ice_LocatorRegistry_h_
#define Ice_LocatorRegistry_h_

#include <Ice/PushDisableWarnings.h>
#include <Ice/Ice.h>
#include "Locator.h"

#ifndef ICE_DISABLE_VERSION
#   if ICE_INT_VERSION  != 30850
#       error Ice version mismatch: an exact match is required for beta generated code
#   endif
#endif

// NOLINTBEGIN(modernize-concat-nested-namespaces)

namespace Ice
{
    class ProcessPrx;

    class LocatorRegistryPrx;
}

namespace Ice
{
    /// A server application registers the endpoints of its indirect object adapters with the LocatorRegistry object.
    /// @headerfile Ice/Ice.h
    class ICE_API LocatorRegistryPrx : public Ice::Proxy<LocatorRegistryPrx, Ice::ObjectPrx>
    {
    public:
        /// Constructs a proxy from a Communicator and a proxy string.
        /// @param communicator The communicator of the new proxy.
        /// @param proxyString The proxy string to parse.
        LocatorRegistryPrx(const Ice::CommunicatorPtr& communicator, std::string_view proxyString) : Ice::ObjectPrx{communicator, proxyString} {} // NOLINT(modernize-use-equals-default)

        /// Copy constructor. Constructs with a copy of the contents of @p other.
        /// @param other The proxy to copy from.
        LocatorRegistryPrx(const LocatorRegistryPrx& other) noexcept : Ice::ObjectPrx{other} {} // NOLINT(modernize-use-equals-default)

        /// Move constructor. Constructs a proxy with the contents of @p other using move semantics.
        /// @param other The proxy to move from.
        LocatorRegistryPrx(LocatorRegistryPrx&& other) noexcept : Ice::ObjectPrx{std::move(other)} {} // NOLINT(modernize-use-equals-default)

        ~LocatorRegistryPrx() override;

        /// Copy assignment operator. Replaces the contents of this proxy with a copy of the contents of @p rhs.
        /// @param rhs The proxy to copy from.
        /// @return A reference to this proxy.
        LocatorRegistryPrx& operator=(const LocatorRegistryPrx& rhs) noexcept
        {
            if (this != &rhs)
            {
                Ice::ObjectPrx::operator=(rhs);
            }
            return *this;
        }

        /// Move assignment operator. Replaces the contents of this proxy with the contents of @p rhs using move semantics.
        /// @param rhs The proxy to move from.
        LocatorRegistryPrx& operator=(LocatorRegistryPrx&& rhs) noexcept
        {
            if (this != &rhs)
            {
                Ice::ObjectPrx::operator=(std::move(rhs));
            }
            return *this;
        }

        /// Registers or unregisters the endpoints of an object adapter.
        /// @param id The adapter ID.
        /// @param proxy A dummy proxy created by the object adapter. @p proxy carries the object adapter's endpoints.
        /// The locator considers an object adapter to be active after it has registered its endpoints.
        /// When @p proxy is null, the endpoints are unregistered and the locator considers the object adapter inactive.
        /// @param context The request context.
        /// @throws Ice::AdapterAlreadyActiveException Thrown when an object adapter with the same adapter ID has already
        /// registered its endpoints. Since this operation is marked idempotent, this exception may be thrown when the
        /// Ice client runtime retries an invocation with a non-null @p proxy.
        /// @throws Ice::AdapterNotFoundException Thrown when the locator only allows registered object adapters to register
        /// their endpoints and no object adapter with this adapter ID was registered with the locator.
        void setAdapterDirectProxy(std::string_view id, const std::optional<Ice::ObjectPrx>& proxy, const Ice::Context& context = Ice::noExplicitContext) const;

        /// Registers or unregisters the endpoints of an object adapter.
        /// @param id The adapter ID.
        /// @param proxy A dummy proxy created by the object adapter. @p proxy carries the object adapter's endpoints.
        /// The locator considers an object adapter to be active after it has registered its endpoints.
        /// When @p proxy is null, the endpoints are unregistered and the locator considers the object adapter inactive.
        /// @param context The request context.
        /// @return A future that becomes available when the invocation completes.
        [[nodiscard]] std::future<void> setAdapterDirectProxyAsync(std::string_view id, const std::optional<Ice::ObjectPrx>& proxy, const Ice::Context& context = Ice::noExplicitContext) const;

        /// Registers or unregisters the endpoints of an object adapter.
        /// @param id The adapter ID.
        /// @param proxy A dummy proxy created by the object adapter. @p proxy carries the object adapter's endpoints.
        /// The locator considers an object adapter to be active after it has registered its endpoints.
        /// When @p proxy is null, the endpoints are unregistered and the locator considers the object adapter inactive.
        /// @param response The response callback.
        /// @param exception The exception callback.
        /// @param sent The sent callback.
        /// @param context The request context.
        /// @return A function that can be called to cancel the invocation locally.
        // NOLINTNEXTLINE(modernize-use-nodiscard)
        std::function<void()> setAdapterDirectProxyAsync(std::string_view id, const std::optional<Ice::ObjectPrx>& proxy, std::function<void()> response, std::function<void(std::exception_ptr)> exception = nullptr, std::function<void(bool)> sent = nullptr, const Ice::Context& context = Ice::noExplicitContext) const;

        /// @private
        void _iceI_setAdapterDirectProxy(const std::shared_ptr<IceInternal::OutgoingAsyncT<void>>&, std::string_view, const std::optional<Ice::ObjectPrx>&, const Ice::Context&) const;

        /// Registers or unregisters the endpoints of an object adapter. This object adapter is a member of a replica
        /// group.
        /// @param adapterId The adapter ID.
        /// @param replicaGroupId The replica group ID.
        /// @param proxy A dummy proxy created by the object adapter. @p proxy carries the object adapter's endpoints.
        /// The locator considers an object adapter to be active after it has registered its endpoints. When @p proxy is
        /// null, the endpoints are unregistered and the locator considers the object adapter inactive.
        /// @param context The request context.
        /// @throws Ice::AdapterAlreadyActiveException Thrown when an object adapter with the same adapter ID has already
        /// registered its endpoints. Since this operation is marked idempotent, this exception may be thrown when the
        /// Ice client runtime retries an invocation with a non-null @p proxy.
        /// @throws Ice::AdapterNotFoundException Thrown when the locator only allows registered object adapters to register
        /// their endpoints and no object adapter with this adapter ID was registered with the locator.
        /// @throws Ice::InvalidReplicaGroupIdException Thrown when the given replica group does not match the replica group
        /// associated with the adapter ID in the locator's database.
        void setReplicatedAdapterDirectProxy(std::string_view adapterId, std::string_view replicaGroupId, const std::optional<Ice::ObjectPrx>& proxy, const Ice::Context& context = Ice::noExplicitContext) const;

        /// Registers or unregisters the endpoints of an object adapter. This object adapter is a member of a replica
        /// group.
        /// @param adapterId The adapter ID.
        /// @param replicaGroupId The replica group ID.
        /// @param proxy A dummy proxy created by the object adapter. @p proxy carries the object adapter's endpoints.
        /// The locator considers an object adapter to be active after it has registered its endpoints. When @p proxy is
        /// null, the endpoints are unregistered and the locator considers the object adapter inactive.
        /// @param context The request context.
        /// @return A future that becomes available when the invocation completes.
        [[nodiscard]] std::future<void> setReplicatedAdapterDirectProxyAsync(std::string_view adapterId, std::string_view replicaGroupId, const std::optional<Ice::ObjectPrx>& proxy, const Ice::Context& context = Ice::noExplicitContext) const;

        /// Registers or unregisters the endpoints of an object adapter. This object adapter is a member of a replica
        /// group.
        /// @param adapterId The adapter ID.
        /// @param replicaGroupId The replica group ID.
        /// @param proxy A dummy proxy created by the object adapter. @p proxy carries the object adapter's endpoints.
        /// The locator considers an object adapter to be active after it has registered its endpoints. When @p proxy is
        /// null, the endpoints are unregistered and the locator considers the object adapter inactive.
        /// @param response The response callback.
        /// @param exception The exception callback.
        /// @param sent The sent callback.
        /// @param context The request context.
        /// @return A function that can be called to cancel the invocation locally.
        // NOLINTNEXTLINE(modernize-use-nodiscard)
        std::function<void()> setReplicatedAdapterDirectProxyAsync(std::string_view adapterId, std::string_view replicaGroupId, const std::optional<Ice::ObjectPrx>& proxy, std::function<void()> response, std::function<void(std::exception_ptr)> exception = nullptr, std::function<void(bool)> sent = nullptr, const Ice::Context& context = Ice::noExplicitContext) const;

        /// @private
        void _iceI_setReplicatedAdapterDirectProxy(const std::shared_ptr<IceInternal::OutgoingAsyncT<void>>&, std::string_view, std::string_view, const std::optional<Ice::ObjectPrx>&, const Ice::Context&) const;

        /// Registers a proxy to the ::Ice::ProcessPrx object of a server application.
        /// @param id The server ID.
        /// @param proxy A proxy to the Process object of the server. This proxy is never null.
        /// @param context The request context.
        /// @throws Ice::ServerNotFoundException Thrown when the locator does not know a server application with this server
        /// ID.
        void setServerProcessProxy(std::string_view id, const std::optional<ProcessPrx>& proxy, const Ice::Context& context = Ice::noExplicitContext) const;

        /// Registers a proxy to the ::Ice::ProcessPrx object of a server application.
        /// @param id The server ID.
        /// @param proxy A proxy to the Process object of the server. This proxy is never null.
        /// @param context The request context.
        /// @return A future that becomes available when the invocation completes.
        [[nodiscard]] std::future<void> setServerProcessProxyAsync(std::string_view id, const std::optional<ProcessPrx>& proxy, const Ice::Context& context = Ice::noExplicitContext) const;

        /// Registers a proxy to the ::Ice::ProcessPrx object of a server application.
        /// @param id The server ID.
        /// @param proxy A proxy to the Process object of the server. This proxy is never null.
        /// @param response The response callback.
        /// @param exception The exception callback.
        /// @param sent The sent callback.
        /// @param context The request context.
        /// @return A function that can be called to cancel the invocation locally.
        // NOLINTNEXTLINE(modernize-use-nodiscard)
        std::function<void()> setServerProcessProxyAsync(std::string_view id, const std::optional<ProcessPrx>& proxy, std::function<void()> response, std::function<void(std::exception_ptr)> exception = nullptr, std::function<void(bool)> sent = nullptr, const Ice::Context& context = Ice::noExplicitContext) const;

        /// @private
        void _iceI_setServerProcessProxy(const std::shared_ptr<IceInternal::OutgoingAsyncT<void>>&, std::string_view, const std::optional<ProcessPrx>&, const Ice::Context&) const;

        /// Gets the type ID of the associated Slice interface.
        /// @return The string `"::Ice::LocatorRegistry"`.
        static const char* ice_staticId() noexcept;

        /// @private
        static LocatorRegistryPrx _fromReference(IceInternal::ReferencePtr ref) { return LocatorRegistryPrx{std::move(ref)}; }

    protected:
        /// @private
        LocatorRegistryPrx() = default;

        /// @private
        explicit LocatorRegistryPrx(IceInternal::ReferencePtr&& ref) : Ice::ObjectPrx{std::move(ref)}
        {
        }
    };
}

namespace Ice
{
    /// The exception that is thrown when a server application tries to register endpoints for an object adapter that is
    /// already active.
    /// @headerfile Ice/Ice.h
    class ICE_API AdapterAlreadyActiveException : public Ice::UserException
    {
    public:
        /// Gets the type ID of the associated Slice exception.
        /// @return The string `"::Ice::AdapterAlreadyActiveException"`.
        static const char* ice_staticId() noexcept;

        [[nodiscard]] const char* ice_id() const noexcept override;

        void ice_throw() const override;

    protected:
        /// @private
        void _writeImpl(Ice::OutputStream*) const override;

        /// @private
        void _readImpl(Ice::InputStream*) override;
    };

    /// The exception that is thrown when the provided replica group is invalid.
    /// @headerfile Ice/Ice.h
    class ICE_API InvalidReplicaGroupIdException : public Ice::UserException
    {
    public:
        /// Gets the type ID of the associated Slice exception.
        /// @return The string `"::Ice::InvalidReplicaGroupIdException"`.
        static const char* ice_staticId() noexcept;

        [[nodiscard]] const char* ice_id() const noexcept override;

        void ice_throw() const override;

    protected:
        /// @private
        void _writeImpl(Ice::OutputStream*) const override;

        /// @private
        void _readImpl(Ice::InputStream*) override;
    };

    /// The exception that is thrown when a server was not found.
    /// @headerfile Ice/Ice.h
    class ICE_API ServerNotFoundException : public Ice::UserException
    {
    public:
        /// Gets the type ID of the associated Slice exception.
        /// @return The string `"::Ice::ServerNotFoundException"`.
        static const char* ice_staticId() noexcept;

        [[nodiscard]] const char* ice_id() const noexcept override;

        void ice_throw() const override;

    protected:
        /// @private
        void _writeImpl(Ice::OutputStream*) const override;

        /// @private
        void _readImpl(Ice::InputStream*) override;
    };
}

namespace Ice
{
    /// A server application registers the endpoints of its indirect object adapters with the LocatorRegistry object.
    /// @headerfile Ice/Ice.h
    class ICE_API LocatorRegistry : public virtual Ice::Object
    {
    public:
        /// The associated proxy type.
        using ProxyType = LocatorRegistryPrx;

        /// Dispatches an incoming request to one of the member functions of this generated class, based on the operation name carried by the request.
        /// @param request The incoming request.
        /// @param sendResponse The callback to send the response.
        void dispatch(IncomingRequest& request, std::function<void(OutgoingResponse)> sendResponse) override;

        [[nodiscard]] std::vector<std::string> ice_ids(const Ice::Current& current) const override;

        [[nodiscard]] std::string ice_id(const Ice::Current& current) const override;

        /// Registers or unregisters the endpoints of an object adapter.
        /// @param id The adapter ID.
        /// @param proxy A dummy proxy created by the object adapter. @p proxy carries the object adapter's endpoints.
        /// The locator considers an object adapter to be active after it has registered its endpoints.
        /// When @p proxy is null, the endpoints are unregistered and the locator considers the object adapter inactive.
        /// @param response The response callback.
        /// @param exception The exception callback.
        /// @param current The Current object of the incoming request.
        /// @throws Ice::AdapterAlreadyActiveException Thrown when an object adapter with the same adapter ID has already
        /// registered its endpoints. Since this operation is marked idempotent, this exception may be thrown when the
        /// Ice client runtime retries an invocation with a non-null @p proxy.
        /// @throws Ice::AdapterNotFoundException Thrown when the locator only allows registered object adapters to register
        /// their endpoints and no object adapter with this adapter ID was registered with the locator.
        virtual void setAdapterDirectProxyAsync(std::string id, std::optional<Ice::ObjectPrx> proxy, std::function<void()> response, std::function<void(std::exception_ptr)> exception, const Ice::Current& current) = 0;

        /// @private
        void _iceD_setAdapterDirectProxy(Ice::IncomingRequest&, std::function<void(Ice::OutgoingResponse)>);

        /// Registers or unregisters the endpoints of an object adapter. This object adapter is a member of a replica
        /// group.
        /// @param adapterId The adapter ID.
        /// @param replicaGroupId The replica group ID.
        /// @param proxy A dummy proxy created by the object adapter. @p proxy carries the object adapter's endpoints.
        /// The locator considers an object adapter to be active after it has registered its endpoints. When @p proxy is
        /// null, the endpoints are unregistered and the locator considers the object adapter inactive.
        /// @param response The response callback.
        /// @param exception The exception callback.
        /// @param current The Current object of the incoming request.
        /// @throws Ice::AdapterAlreadyActiveException Thrown when an object adapter with the same adapter ID has already
        /// registered its endpoints. Since this operation is marked idempotent, this exception may be thrown when the
        /// Ice client runtime retries an invocation with a non-null @p proxy.
        /// @throws Ice::AdapterNotFoundException Thrown when the locator only allows registered object adapters to register
        /// their endpoints and no object adapter with this adapter ID was registered with the locator.
        /// @throws Ice::InvalidReplicaGroupIdException Thrown when the given replica group does not match the replica group
        /// associated with the adapter ID in the locator's database.
        virtual void setReplicatedAdapterDirectProxyAsync(std::string adapterId, std::string replicaGroupId, std::optional<Ice::ObjectPrx> proxy, std::function<void()> response, std::function<void(std::exception_ptr)> exception, const Ice::Current& current) = 0;

        /// @private
        void _iceD_setReplicatedAdapterDirectProxy(Ice::IncomingRequest&, std::function<void(Ice::OutgoingResponse)>);

        /// Registers a proxy to the ::Ice::ProcessPrx object of a server application.
        /// @param id The server ID.
        /// @param proxy A proxy to the Process object of the server. This proxy is never null.
        /// @param response The response callback.
        /// @param exception The exception callback.
        /// @param current The Current object of the incoming request.
        /// @throws Ice::ServerNotFoundException Thrown when the locator does not know a server application with this server
        /// ID.
        virtual void setServerProcessProxyAsync(std::string id, std::optional<ProcessPrx> proxy, std::function<void()> response, std::function<void(std::exception_ptr)> exception, const Ice::Current& current) = 0;

        /// @private
        void _iceD_setServerProcessProxy(Ice::IncomingRequest&, std::function<void(Ice::OutgoingResponse)>);

        /// Gets the type ID of the associated Slice interface.
        /// @return The string `"::Ice::LocatorRegistry"`.
        static const char* ice_staticId() noexcept;
    };

    /// A shared pointer to a LocatorRegistry.
    using LocatorRegistryPtr = std::shared_ptr<LocatorRegistry>;
}

// NOLINTEND(modernize-concat-nested-namespaces)

#include <Ice/PopDisableWarnings.h>
#endif
