#include "Utopia/Networking/NetworkingUtils.hpp"

#include <WinSock2.h>
#include <ws2tcpip.h>
#include <optional>
#include <string>
#include <cassert>
#include "Utopia/Core/Log.hpp"

namespace Utopia::Utils {

    // A small RAII helper to ensure WSAStartup/WSACleanup are always balanced
    struct WinsockInit
    {
        WinsockInit()
        {
            const int result = ::WSAStartup(MAKEWORD(2, 2), &m_WsaData);
            if (result != 0)
            {
                UT_ERROR_TAG("NETWORK", "WSAStartup failed with error: {}", ::WSAGetLastError());
            }
        }

        ~WinsockInit()
        {
            ::WSACleanup();
        }

        // Non-copyable, non-movable
        WinsockInit(const WinsockInit&) = delete;
        WinsockInit& operator=(const WinsockInit&) = delete;
        WinsockInit(WinsockInit&&) = delete;
        WinsockInit& operator=(WinsockInit&&) = delete;

    private:
        WSADATA m_WsaData{};
    };

    [[nodiscard]] std::optional<std::string> ResolveDomainName(std::string_view name) noexcept
    {
        try
        {
            WinsockInit wsa;
        }
        catch (...)
        {
            UT_ERROR_TAG("NETWORK", "WSAStartup threw an exception");
            return std::nullopt;
        }

        addrinfo hints{};
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = IPPROTO_TCP;

        addrinfo* addressResult = nullptr;
        const int retval = ::getaddrinfo(name.data(), nullptr, &hints, &addressResult);
        if (retval != 0)
        {
            UT_ERROR_TAG("NETWORK", "getaddrinfo failed with error: {}", retval);
            return std::nullopt;
        }

        auto cleanupAddrInfo = [&]() noexcept {
            if (addressResult) ::freeaddrinfo(addressResult);
            };

        std::string ipAddressStr;

        for (addrinfo* ptr = addressResult; ptr != nullptr; ptr = ptr->ai_next)
        {
            switch (ptr->ai_family)
            {
            case AF_INET:
            {
                const auto* sockaddr_ipv4 = reinterpret_cast<sockaddr_in*>(ptr->ai_addr);

                char ipAddress[INET_ADDRSTRLEN] = {};
                if (!::inet_ntop(AF_INET, &sockaddr_ipv4->sin_addr, ipAddress, INET_ADDRSTRLEN))
                {
                    UT_ERROR_TAG("NETWORK", "inet_ntop(AF_INET) failed. LastError: {}", ::WSAGetLastError());
                    continue;
                }

                ipAddressStr = ipAddress;
                cleanupAddrInfo();
                return ipAddressStr;
            }
            case AF_INET6:
            {
                const DWORD ipBufferLength = 46;
                wchar_t ipStringBuffer[ipBufferLength] = {};
                DWORD actualLen = ipBufferLength;

                LPSOCKADDR sockaddr_ip = reinterpret_cast<LPSOCKADDR>(ptr->ai_addr);
                const INT wsaRet = ::WSAAddressToStringW(
                    sockaddr_ip,
                    static_cast<DWORD>(ptr->ai_addrlen),
                    nullptr,
                    ipStringBuffer,
                    &actualLen
                );

                if (wsaRet != 0)
                {
                    UT_ERROR_TAG("NETWORK", "WSAAddressToStringW failed. LastError: {}", ::WSAGetLastError());
                    continue;
                }

                std::wstring wideStr(ipStringBuffer);
                ipAddressStr.assign(wideStr.begin(), wideStr.end());

                cleanupAddrInfo();
                return ipAddressStr;
            }
            default:
                // We only handle IPv4 and IPv6. (AF_UNSPEC means "unspecified", so skip.)
                break;
            }
        }

        cleanupAddrInfo();
        return std::nullopt;
    }

} // namespace Utopia::Utils
