// Copyright (c) ZeroC, Inc.

// slice2cpp version 3.8.0-alpha.0
// <auto-generated>Generated from Slice file 'DBTypes.ice'.</auto-generated>
// clang-format off

#ifndef DBTypes_h_
#define DBTypes_h_

#include <Ice/PushDisableWarnings.h>
#include <Ice/Ice.h>
#include <IceGrid/Admin.h>

#ifndef ICE_DISABLE_VERSION
#   if ICE_INT_VERSION  != 30850
#       error Ice version mismatch: an exact match is required for beta generated code
#   endif
#endif

// NOLINTBEGIN(modernize-concat-nested-namespaces)

namespace IceGrid
{
    using StringLongDict = std::map<std::string, std::int64_t>;

    struct AllData;
}

namespace IceGrid
{
    struct AllData
    {
        ::IceGrid::ApplicationInfoSeq applications;

        ::IceGrid::AdapterInfoSeq adapters;

        ::IceGrid::ObjectInfoSeq objects;

        ::IceGrid::ObjectInfoSeq internalObjects;

        ::IceGrid::StringLongDict serials;

        /// Creates a tuple with all the fields of this struct.
        /// @return A tuple with all the fields of this struct.
        [[nodiscard]] std::tuple<const ::IceGrid::ApplicationInfoSeq&, const ::IceGrid::AdapterInfoSeq&, const ::IceGrid::ObjectInfoSeq&, const ::IceGrid::ObjectInfoSeq&, const ::IceGrid::StringLongDict&> ice_tuple() const
        {
            return std::tie(applications, adapters, objects, internalObjects, serials);
        }

        /// Outputs the name and value of each field of this instance to the stream.
        /// @param os The output stream.
        void ice_printFields(std::ostream& os) const;
    };

    /// Outputs the description of an AllData to a stream, including all its fields.
    /// @param os The output stream.
    /// @param value The instance to output.
    /// @return The output stream.
    std::ostream& operator<<(std::ostream& os, const AllData& value);

    /// @cond INTERNAL
    using Ice::Tuple::operator<;
    using Ice::Tuple::operator<=;
    using Ice::Tuple::operator>;
    using Ice::Tuple::operator>=;
    using Ice::Tuple::operator==;
    using Ice::Tuple::operator!=;
    /// @endcond
}

namespace Ice
{
    /// @cond INTERNAL
    template<>
    struct StreamableTraits<::IceGrid::AllData>
    {
        static constexpr StreamHelperCategory helper = StreamHelperCategoryStruct;
        static constexpr int minWireSize = 5;
        static constexpr bool fixedLength = false;
    };

    template<>
    struct StreamReader<::IceGrid::AllData>
    {
        /// Unmarshals a ::IceGrid::AllData from the input stream.
        static void read(InputStream* istr, ::IceGrid::AllData& v)
        {
            istr->readAll(v.applications, v.adapters, v.objects, v.internalObjects, v.serials);
        }
    };
    /// @endcond
}

// NOLINTEND(modernize-concat-nested-namespaces)

#include <Ice/PopDisableWarnings.h>
#endif
