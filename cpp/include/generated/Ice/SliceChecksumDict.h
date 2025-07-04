// Copyright (c) ZeroC, Inc.

// slice2cpp version 3.8.0-alpha.0
// <auto-generated>Generated from Slice file 'SliceChecksumDict.ice'.</auto-generated>
// clang-format off

#ifndef Ice_SliceChecksumDict_h_
#define Ice_SliceChecksumDict_h_

#include <Ice/PushDisableWarnings.h>
#include <Ice/Ice.h>

#ifndef ICE_DISABLE_VERSION
#   if ICE_INT_VERSION  != 30850
#       error Ice version mismatch: an exact match is required for beta generated code
#   endif
#endif

// NOLINTBEGIN(modernize-concat-nested-namespaces)

namespace Ice
{
    /// Mapping from type IDs to Slice checksums. This dictionary allows verification at runtime that a client and a
    /// server use essentially the same Slice definitions.
    using SliceChecksumDict [[deprecated("As of Ice 3.8, the Slice compilers no longer generate Slice checksums.")]] = std::map<std::string, std::string>;
}

// NOLINTEND(modernize-concat-nested-namespaces)

#include <Ice/PopDisableWarnings.h>
#endif
