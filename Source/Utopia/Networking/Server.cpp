#include "Server.hpp"

#include "Utopia/Core/Log.hpp"
#include "Utopia/Core/Buffer.hpp"

#include <chrono>
#include <cassert>
#include <format>
#include <iostream>

namespace Utopia {

    // Can only have one server instance per-process
    Server* Server::s_Instance = nullptr;

    Server::Server(int port)
        : m_Port(port)
    {
        // TODO: Potentially verify port validity here, e.g., if (port <= 0) ...
    }

    Server::~Server() noexcept
    {
        // If the network thread is still active, stop it and join
        if (m_Running.load())
        {
            Stop();
        }

        if (m_NetworkThread.joinable())
        {
            m_NetworkThread.join();
        }
    }

    void Server::Start()
    {
        if (m_Running.load())
        {
            return;
        }

        m_NetworkThread = std::thread([this]()
            {
                NetworkThreadFunc();
            });
    }

    void Server::Stop()
    {
        m_Running.store(false);
    }

    void Server::NetworkThreadFunc()
    {
        s_Instance = this;
        m_Running.store(true);

        SteamDatagramErrMsg errMsg;
        if (!GameNetworkingSockets_Init(nullptr, errMsg))
        {
            OnFatalError(fmt::format("GameNetworkingSockets_Init failed: {}", errMsg));
            return;
        }

        m_Interface = SteamNetworkingSockets();
        assert(m_Interface && "SteamNetworkingSockets() returned nullptr!");

        SteamNetworkingIPAddr serverLocalAddress;
        serverLocalAddress.Clear();
        serverLocalAddress.m_port = static_cast<uint16>(m_Port);

        SteamNetworkingConfigValue_t options;
        options.SetPtr(
            k_ESteamNetworkingConfig_Callback_ConnectionStatusChanged,
            (void*)Server::ConnectionStatusChangedCallback
        );

        m_ListenSocket = m_Interface->CreateListenSocketIP(serverLocalAddress, 1, &options);
        if (m_ListenSocket == k_HSteamListenSocket_Invalid)
        {
            OnFatalError(fmt::format("Fatal error: Failed to listen on port {}", m_Port));
            return;
        }

        m_PollGroup = m_Interface->CreatePollGroup();
        if (m_PollGroup == k_HSteamNetPollGroup_Invalid)
        {
            OnFatalError(fmt::format("Fatal error: Failed to create poll group on port {}", m_Port));
            return;
        }

        UT_INFO_TAG("SERVER", "Server listening on port {}", m_Port);
        std::cout << "Server listening on port " << m_Port << std::endl;

        while (m_Running.load())
        {
            PollIncomingMessages();
            PollConnectionStateChanges();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        // Begin shutdown process
        UT_INFO_TAG("SERVER", "Closing connections...");
        std::cout << "Closing connections..." << std::endl;
        for (const auto& [clientID, clientInfo] : m_ConnectedClients)
        {
            m_Interface->CloseConnection(clientID, 0, "Server Shutdown", true);
        }
        m_ConnectedClients.clear();

        m_Interface->CloseListenSocket(m_ListenSocket);
        m_ListenSocket = k_HSteamListenSocket_Invalid;

        m_Interface->DestroyPollGroup(m_PollGroup);
        m_PollGroup = k_HSteamNetPollGroup_Invalid;

        GameNetworkingSockets_Kill();
    }

    void Server::ConnectionStatusChangedCallback(SteamNetConnectionStatusChangedCallback_t* info)
    {
        if (s_Instance)
        {
            s_Instance->OnConnectionStatusChanged(info);
        }
    }

    void Server::OnConnectionStatusChanged(SteamNetConnectionStatusChangedCallback_t* status)
    {
        // Handle connection state
        switch (status->m_info.m_eState)
        {
        case k_ESteamNetworkingConnectionState_None:
            // NOTE: We will get callbacks here when we destroy connections. You can ignore these.
            break;

        case k_ESteamNetworkingConnectionState_ClosedByPeer:
        case k_ESteamNetworkingConnectionState_ProblemDetectedLocally:
        {
            if (status->m_eOldState == k_ESteamNetworkingConnectionState_Connected)
            {
                auto itClient = m_ConnectedClients.find(status->m_hConn);
                if (itClient != m_ConnectedClients.end())
                {
                    if (m_ClientDisconnectedCallback)
                    {
                        m_ClientDisconnectedCallback(itClient->second);
                    }
                    m_ConnectedClients.erase(itClient);
                }
            }

            m_Interface->CloseConnection(status->m_hConn, 0, nullptr, false);
            break;
        }

        case k_ESteamNetworkingConnectionState_Connecting:
        {
            // Try to accept incoming connection
            if (m_Interface->AcceptConnection(status->m_hConn) != k_EResultOK)
            {
                m_Interface->CloseConnection(status->m_hConn, 0, nullptr, false);
                UT_WARN_TAG("SERVER", "Couldn't accept incoming connection (already closed?)");
                std::cout << "Couldn't accept connection (it was already closed?)" << std::endl;
                break;
            }

            // Assign the poll group
            if (!m_Interface->SetConnectionPollGroup(status->m_hConn, m_PollGroup))
            {
                m_Interface->CloseConnection(status->m_hConn, 0, nullptr, false);
                UT_WARN_TAG("SERVER", "Failed to set poll group for new connection");
                std::cout << "Failed to set poll group" << std::endl;
                break;
            }

            // Retrieve connection info
            SteamNetConnectionInfo_t connectionInfo;
            m_Interface->GetConnectionInfo(status->m_hConn, &connectionInfo);

            // Register connected client
            ClientInfo& client = m_ConnectedClients[status->m_hConn];
            client.ID = status->m_hConn;
            client.ConnectionDesc = connectionInfo.m_szConnectionDescription;

            // User callback
            if (m_ClientConnectedCallback)
            {
                m_ClientConnectedCallback(client);
            }

            break;
        }

        case k_ESteamNetworkingConnectionState_Connected:
            break;

        default:
            break;
        }
    }

    void Server::PollConnectionStateChanges()
    {
        if (m_Interface)
        {
            m_Interface->RunCallbacks();
        }
    }

    void Server::PollIncomingMessages()
    {
        // Process all messages
        while (m_Running.load())
        {
            ISteamNetworkingMessage* incomingMessage = nullptr;
            int messageCount = m_Interface->ReceiveMessagesOnPollGroup(m_PollGroup, &incomingMessage, 1);
            if (messageCount == 0)
                break;

            if (messageCount < 0)
            {
                UT_ERROR_TAG("SERVER", "ReceiveMessagesOnPollGroup returned a critical error: {}", messageCount);
                m_Running.store(false);
                return;
            }

            // We only asked for 1 message
            assert(messageCount == 1 && incomingMessage);

            auto itClient = m_ConnectedClients.find(incomingMessage->m_conn);
            if (itClient == m_ConnectedClients.end())
            {
                UT_WARN_TAG("SERVER", "Received data from unregistered client");
                std::cout << "ERROR: Received data from unregistered client\n";
                incomingMessage->Release();
                continue;
            }

            if (incomingMessage->m_cbSize > 0 && m_DataReceivedCallback)
            {
                m_DataReceivedCallback(
                    itClient->second,
                    Buffer(incomingMessage->m_pData, incomingMessage->m_cbSize)
                );
            }

            incomingMessage->Release();
        }
    }

    void Server::SetClientNick(HSteamNetConnection hConn, const char* nick)
    {
        if (m_Interface)
        {
            m_Interface->SetConnectionName(hConn, nick);
        }
    }

    //////////////////////////////////////////////////////////////////////////////////////////////////
    // User Callbacks
    //////////////////////////////////////////////////////////////////////////////////////////////////
    void Server::SetDataReceivedCallback(const DataReceivedCallback& function)
    {
        m_DataReceivedCallback = function;
    }

    void Server::SetClientConnectedCallback(const ClientConnectedCallback& function)
    {
        m_ClientConnectedCallback = function;
    }

    void Server::SetClientDisconnectedCallback(const ClientDisconnectedCallback& function)
    {
        m_ClientDisconnectedCallback = function;
    }

    //////////////////////////////////////////////////////////////////////////////////////////////////
    // Sending Data
    //////////////////////////////////////////////////////////////////////////////////////////////////
    void Server::SendBufferToClient(ClientID clientID, Buffer buffer, bool reliable)
    {
        if (!m_Interface)
        {
            UT_WARN_TAG("SERVER", "Cannot send data; m_Interface is null");
            return;
        }

        EResult result = m_Interface->SendMessageToConnection(
            static_cast<HSteamNetConnection>(clientID),
            buffer.Data,
            static_cast<uint32_t>(buffer.Size),
            reliable ? k_nSteamNetworkingSend_Reliable : k_nSteamNetworkingSend_Unreliable,
            nullptr
        );

        if (result != k_EResultOK)
        {
            UT_WARN_TAG("SERVER",
                "SendMessageToConnection failed for ClientID {} with EResult code: {}",
                static_cast<uint32_t>(clientID),
                static_cast<int>(result)
            );
        }
    }

    void Server::SendBufferToAllClients(Buffer buffer, ClientID excludeClientID, bool reliable)
    {
        for (const auto& [clientID, clientInfo] : m_ConnectedClients)
        {
            if (clientID == excludeClientID)
                continue;
            SendBufferToClient(clientID, buffer, reliable);
        }
    }

    void Server::SendStringToClient(ClientID clientID, const std::string& string, bool reliable)
    {
        SendBufferToClient(
            clientID,
            Buffer(string.data(), string.size()),
            reliable
        );
    }

    void Server::SendStringToAllClients(const std::string& string, ClientID excludeClientID, bool reliable)
    {
        SendBufferToAllClients(
            Buffer(string.data(), string.size()),
            excludeClientID,
            reliable
        );
    }

    //////////////////////////////////////////////////////////////////////////////////////////////////
    // Utility
    //////////////////////////////////////////////////////////////////////////////////////////////////
    void Server::KickClient(ClientID clientID)
    {
        if (!m_Interface)
        {
            UT_WARN_TAG("SERVER", "Cannot kick client; m_Interface is null");
            return;
        }

        bool success = m_Interface->CloseConnection(
            static_cast<HSteamNetConnection>(clientID),
            0,
            "Kicked by host",
            false
        );
        if (!success)
        {
            UT_WARN_TAG("SERVER", "CloseConnection returned false when kicking ClientID {}", static_cast<uint32_t>(clientID));
        }
    }

    void Server::OnFatalError(const std::string& message)
    {
        UT_ERROR_TAG("SERVER", "{}", message);
        m_Running.store(false);
    }

} // namespace Utopia
