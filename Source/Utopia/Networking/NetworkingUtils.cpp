#include "NetworkingUtils.hpp"

#include <steam/isteamnetworkingutils.h>
#include <string>
#include <string_view>
#include <optional>

namespace Utopia::Utils {

    [[nodiscard]] bool IsValidIPAddress(std::string_view ipAddress) noexcept
    {
        // Use std::string for compatibility with SteamNetworkingIPAddr
        const std::string ipAddressStr(ipAddress);

        SteamNetworkingIPAddr address;
        return address.ParseString(ipAddressStr.c_str());
    }

    [[nodiscard]] std::optional<std::string> ResolveDomainName(std::string_view name) noexcept
    {
        // TODO (Implement): Add actual domain resolution logic
        //                   Ensure error handling and cross-platform compatibility
        //                   For now, return std::nullopt to indicate unimplemented functionality
        return std::nullopt;
    }

} // namespace Utopia::Utils
