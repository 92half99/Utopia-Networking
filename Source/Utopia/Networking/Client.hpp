#pragma once

#include "Utopia/Core/Buffer.hpp"

#include <steam/steamnetworkingsockets.h>
#include <steam/isteamnetworkingutils.h>
#ifndef STEAMNETWORKINGSOCKETS_OPENSOURCE
#include <steam/steam_api.h>
#endif

#include <string>
#include <map>
#include <thread>
#include <functional>
#include <atomic>
#include <mutex>

// Forward-declare this struct so we don't need the full header here.
struct SteamNetConnectionStatusChangedCallback_t;

namespace Utopia {

    class Client
    {
    public:
        enum class ConnectionStatus
        {
            Disconnected = 0,
            Connected,
            Connecting,
            FailedToConnect
        };

    public:
        using DataReceivedCallback = std::function<void(const Buffer)>;
        using ServerConnectedCallback = std::function<void()>;
        using ServerDisconnectedCallback = std::function<void()>;

    public:
        Client() = default;

        ~Client() noexcept;

        // Prohibit copying
        Client(const Client&) = delete;
        Client& operator=(const Client&) = delete;

        // Prohibit moving unless you really need it
        Client(Client&&) = delete;
        Client& operator=(Client&&) = delete;

        void ConnectToServer(const std::string& serverAddress);
        void Disconnect();

        ////////////////////////////////////////////////////////////////////////////////////////////////////////////
        // Set callbacks for server events
        // These callbacks will be called from the network thread
        ////////////////////////////////////////////////////////////////////////////////////////////////////////////
        void SetDataReceivedCallback(const DataReceivedCallback& function);
        void SetServerConnectedCallback(const ServerConnectedCallback& function);
        void SetServerDisconnectedCallback(const ServerDisconnectedCallback& function);

        ////////////////////////////////////////////////////////////////////////////////////////////////////////////
        // Send Data
        ////////////////////////////////////////////////////////////////////////////////////////////////////////////
        void SendBuffer(Buffer buffer, bool reliable = true);
        void SendString(const std::string& string, bool reliable = true);

        template<typename T>
        void SendData(const T& data, bool reliable = true)
        {
            SendBuffer(Buffer(&data, sizeof(T)), reliable);
        }

        ////////////////////////////////////////////////////////////////////////////////////////////////////////////
        // Connection Status & Debugging
        ////////////////////////////////////////////////////////////////////////////////////////////////////////////
        bool IsRunning() const { return m_Running.load(); }
        ConnectionStatus GetConnectionStatus() const { return m_ConnectionStatus.load(); }
        const std::string& GetConnectionDebugMessage() const { return m_ConnectionDebugMessage; }

    private:
        void NetworkThreadFunc();
        void Shutdown();

        static void ConnectionStatusChangedCallback(SteamNetConnectionStatusChangedCallback_t* info);
        void OnConnectionStatusChanged(SteamNetConnectionStatusChangedCallback_t* info);

        void PollIncomingMessages();
        void PollConnectionStateChanges();

        void OnFatalError(const std::string& message);

    private:
        std::thread m_NetworkThread;

        // Callbacks
        DataReceivedCallback       m_DataReceivedCallback;
        ServerConnectedCallback    m_ServerConnectedCallback;
        ServerDisconnectedCallback m_ServerDisconnectedCallback;

        std::atomic<ConnectionStatus> m_ConnectionStatus{ ConnectionStatus::Disconnected };
        std::string m_ConnectionDebugMessage;

        std::string m_ServerAddress;
        std::atomic_bool m_Running{ false };

        ISteamNetworkingSockets* m_Interface = nullptr;
        HSteamNetConnection m_Connection = k_HSteamNetConnection_Invalid;

        // For the "one instance" approach
        static Client* s_Instance;
        mutable std::mutex m_Mutex;
    };

} // namespace Utopia
