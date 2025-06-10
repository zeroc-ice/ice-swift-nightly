// Copyright (c) ZeroC, Inc.

#include "SHA1.h"

#if defined(_WIN32)
#    include "Ice/LocalExceptions.h"
// clang-format off
#    include <windows.h>
#    include <Wincrypt.h>
// clang-format on
#elif defined(__APPLE__)
#    include <CommonCrypto/CommonDigest.h>
#else
#    include <openssl/sha.h>
#    // Ignore OpenSSL 3.0 deprecation warning
#    pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif

using namespace std;

namespace IceInternal
{
    class SHA1::Hasher
    {
    public:
        Hasher();
        Hasher(const Hasher&) = delete;
#ifdef _WIN32
        ~Hasher();
#endif
        Hasher operator=(const Hasher&) = delete;

        void update(const unsigned char*, std::size_t);
        void finalize(std::vector<unsigned char>&);
        void finalize(vector<byte>&);

    private:
#if defined(_WIN32)
        HCRYPTPROV _ctx;
        HCRYPTHASH _hash;
#elif defined(__APPLE__)
        CC_SHA1_CTX _ctx;
#else
        SHA_CTX _ctx;
#endif
    };
}

#if defined(_WIN32)

namespace
{
    const int SHA_DIGEST_LENGTH = 20;
}

IceInternal::SHA1::Hasher::Hasher() : _ctx(0), _hash(0)
{
    if (!CryptAcquireContext(&_ctx, 0, MS_DEF_PROV, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT))
    {
        throw Ice::SyscallException{__FILE__, __LINE__, "CryptAcquireContext failed", GetLastError()};
    }

    if (!CryptCreateHash(_ctx, CALG_SHA1, 0, 0, &_hash))
    {
        throw Ice::SyscallException{__FILE__, __LINE__, "CryptCreateHash failed", GetLastError()};
    }
}

IceInternal::SHA1::Hasher::~Hasher()
{
    if (_hash)
    {
        CryptDestroyHash(_hash);
    }

    if (_ctx)
    {
        CryptReleaseContext(_ctx, 0);
    }
}
#elif defined(__APPLE__)
IceInternal::SHA1::Hasher::Hasher() { CC_SHA1_Init(&_ctx); }
#else
IceInternal::SHA1::Hasher::Hasher() { SHA1_Init(&_ctx); }
#endif

void
IceInternal::SHA1::Hasher::update(const unsigned char* data, size_t length)
{
#if defined(_WIN32)
    if (!CryptHashData(_hash, data, static_cast<DWORD>(length), 0))
    {
        throw Ice::SyscallException{__FILE__, __LINE__, "CryptHashData failed", GetLastError()};
    }
#elif defined(__APPLE__)
    CC_SHA1_Update(&_ctx, reinterpret_cast<const void*>(data), static_cast<CC_LONG>(length));
#else
    SHA1_Update(&_ctx, reinterpret_cast<const void*>(data), length);
#endif
}

void
IceInternal::SHA1::Hasher::finalize(vector<unsigned char>& md)
{
#if defined(_WIN32)
    md.resize(SHA_DIGEST_LENGTH);
    DWORD length = SHA_DIGEST_LENGTH;
    if (!CryptGetHashParam(_hash, HP_HASHVAL, &md[0], &length, 0))
    {
        throw Ice::SyscallException{__FILE__, __LINE__, "CryptGetHashParam failed", GetLastError()};
    }
#elif defined(__APPLE__)
    md.resize(CC_SHA1_DIGEST_LENGTH);
    CC_SHA1_Final(&md[0], &_ctx);
#else
    md.resize(SHA_DIGEST_LENGTH);
    SHA1_Final(&md[0], &_ctx);
#endif
}

void
IceInternal::SHA1::Hasher::finalize(vector<byte>& md)
{
#if defined(_WIN32)
    md.resize(SHA_DIGEST_LENGTH);
    DWORD length = SHA_DIGEST_LENGTH;
    if (!CryptGetHashParam(_hash, HP_HASHVAL, reinterpret_cast<unsigned char*>(&md[0]), &length, 0))
    {
        throw Ice::SyscallException{__FILE__, __LINE__, "CryptGetHashParam failed", GetLastError()};
    }
#elif defined(__APPLE__)
    md.resize(CC_SHA1_DIGEST_LENGTH);
    CC_SHA1_Final(reinterpret_cast<unsigned char*>(&md[0]), &_ctx);
#else
    md.resize(SHA_DIGEST_LENGTH);
    SHA1_Final(reinterpret_cast<unsigned char*>(&md[0]), &_ctx);
#endif
}

IceInternal::SHA1::SHA1() : _hasher(new Hasher()) {}

IceInternal::SHA1::~SHA1() = default;

void
IceInternal::SHA1::update(const unsigned char* data, std::size_t length)
{
    _hasher->update(data, length);
}

void
IceInternal::SHA1::finalize(std::vector<unsigned char>& md)
{
    _hasher->finalize(md);
}

void
IceInternal::SHA1::finalize(std::vector<byte>& md)
{
    _hasher->finalize(md);
}

void
IceInternal::sha1(const unsigned char* data, size_t length, vector<unsigned char>& md)
{
#if defined(_WIN32)
    SHA1 hasher;
    hasher.update(data, length);
    hasher.finalize(md);
#elif defined(__APPLE__)
    md.resize(CC_SHA1_DIGEST_LENGTH);
    CC_SHA1(&data[0], static_cast<CC_LONG>(length), &md[0]);
#else
    md.resize(SHA_DIGEST_LENGTH);
    ::SHA1(&data[0], length, &md[0]);
#endif
}

void
IceInternal::sha1(const byte* data, size_t length, vector<byte>& md)
{
#if defined(_WIN32)
    SHA1 hasher;
    hasher.update(reinterpret_cast<const unsigned char*>(data), length);
    hasher.finalize(md);
#elif defined(__APPLE__)
    md.resize(CC_SHA1_DIGEST_LENGTH);
    CC_SHA1(
        reinterpret_cast<const unsigned char*>(&data[0]),
        static_cast<CC_LONG>(length),
        reinterpret_cast<unsigned char*>(&md[0]));
#else
    md.resize(SHA_DIGEST_LENGTH);
    ::SHA1(reinterpret_cast<const unsigned char*>(&data[0]), length, reinterpret_cast<unsigned char*>(&md[0]));
#endif
}
