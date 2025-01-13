#pragma once

#include <string>
#include <string_view>
#include <optional>

namespace Utopia::Utils {

    // Validates whether the given string is a valid IP address
    [[nodiscard]] bool IsValidIPAddress(std::string_view ipAddress) noexcept;

    // Resolves a domain name to its corresponding IP address
    // Returns std::nullopt if resolution fails
    [[nodiscard]] std::optional<std::string> ResolveDomainName(std::string_view name) noexcept;

} // namespace Utopia::Utils
