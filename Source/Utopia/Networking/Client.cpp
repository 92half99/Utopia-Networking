#include "Client.hpp"

#include "Utopia/Core/Buffer.hpp"
#include "Utopia/Core/Log.hpp"

#include <chrono>
#include <cassert>
#include <format>

namespace Utopia {

    // Can only have one server instance per-process
    Client* Client::s_Instance = nullptr;

    Client::~Client() noexcept
    {
        // Ensure we aren't running. If we are, shut down gracefully.
        if (m_Running.load())
        {
            Shutdown();
        }

        // Join the thread if it's still joinable.
        if (m_NetworkThread.joinable())
        {
            m_NetworkThread.join();
        }
    }

    void Client::ConnectToServer(const std::string& serverAddress)
    {
        // If we're already running, bail out.
        if (m_Running.load())
            return;

        // If an old thread is still around, join it before starting a new one
        if (m_NetworkThread.joinable())
        {
            m_NetworkThread.join();
        }

        m_ServerAddress = serverAddress;
        m_NetworkThread = std::thread([this]()
            {
                NetworkThreadFunc();
            });
    }

    void Client::Disconnect()
    {
        // Signal the worker thread to stop
        m_Running.store(false);

        // Join once we are done
        if (m_NetworkThread.joinable())
        {
            m_NetworkThread.join();
        }
    }

    void Client::SetDataReceivedCallback(const DataReceivedCallback& function)
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_DataReceivedCallback = function;
    }

    void Client::SetServerConnectedCallback(const ServerConnectedCallback& function)
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_ServerConnectedCallback = function;
    }

    void Client::SetServerDisconnectedCallback(const ServerDisconnectedCallback& function)
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_ServerDisconnectedCallback = function;
    }

    void Client::NetworkThreadFunc()
    {
        // Set the static instance pointer for callbacks
        s_Instance = this;

        // Reset connection status
        m_ConnectionStatus.store(ConnectionStatus::Connecting);

        SteamDatagramErrMsg errMsg{};
        if (!GameNetworkingSockets_Init(nullptr, errMsg))
        {
            m_ConnectionDebugMessage = "Could not initialize GameNetworkingSockets";
            m_ConnectionStatus.store(ConnectionStatus::FailedToConnect);
            return;
        }

        m_Interface = SteamNetworkingSockets();
        assert(m_Interface && "SteamNetworkingSockets() returned nullptr!");

        // Start connecting
        SteamNetworkingIPAddr address;
        if (!address.ParseString(m_ServerAddress.c_str()))
        {
            OnFatalError(fmt::format("Invalid IP address - could not parse {}", m_ServerAddress));
            m_ConnectionDebugMessage = "Invalid IP address";
            m_ConnectionStatus.store(ConnectionStatus::FailedToConnect);
            return;
        }

        SteamNetworkingConfigValue_t options;
        options.SetPtr(
            k_ESteamNetworkingConfig_Callback_ConnectionStatusChanged,
            (void*)ConnectionStatusChangedCallback
        );

        m_Connection = m_Interface->ConnectByIPAddress(address, 1, &options);
        if (m_Connection == k_HSteamNetConnection_Invalid)
        {
            m_ConnectionDebugMessage = "Failed to create connection";
            m_ConnectionStatus.store(ConnectionStatus::FailedToConnect);
            return;
        }

        m_Running.store(true);

        while (m_Running.load())
        {
            PollIncomingMessages();
            PollConnectionStateChanges();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        // Close the connection gracefully
        bool closeResult = m_Interface->CloseConnection(m_Connection, 0, nullptr, false);
        if (!closeResult)
        {
            UT_WARN_TAG("CLIENT", "CloseConnection returned false, indicating an error");
        }

        m_ConnectionStatus.store(ConnectionStatus::Disconnected);

        // Shut down the networking
        GameNetworkingSockets_Kill();
    }

    void Client::Shutdown()
    {
        // Graceful shutdown
        m_Running.store(false);
    }

    void Client::SendBuffer(Buffer buffer, bool reliable)
    {
        EResult result = k_EResultInvalidState;
        if (m_Interface && m_Connection != k_HSteamNetConnection_Invalid)
        {
            result = m_Interface->SendMessageToConnection(
                m_Connection,
                buffer.Data,
                static_cast<uint32_t>(buffer.Size),
                reliable ? k_nSteamNetworkingSend_Reliable : k_nSteamNetworkingSend_Unreliable,
                nullptr // pOutMessageNumber is optional, passing nullptr
            );
        }
        else
        {
            UT_WARN_TAG("CLIENT", "SendMessageToConnection called on an invalid connection.");
            return;
        }

        if (result != k_EResultOK)
        {
            UT_WARN_TAG("CLIENT", "SendMessageToConnection failed with EResult code: {}", static_cast<int>(result));
        }
    }

    void Client::SendString(const std::string& string, bool reliable)
    {
        SendBuffer(Buffer(string.data(), string.size()), reliable);
    }

    void Client::PollIncomingMessages()
    {
        while (m_Running.load())
        {
            ISteamNetworkingMessage* incomingMessage = nullptr;
            int messageCount = 0;

            if (m_Interface && m_Connection != k_HSteamNetConnection_Invalid)
            {
                messageCount = m_Interface->ReceiveMessagesOnConnection(
                    m_Connection,
                    &incomingMessage,
                    1
                );
            }

            if (messageCount == 0)
                break;

            if (messageCount < 0)
            {
                UT_ERROR_TAG("CLIENT", "ReceiveMessagesOnConnection returned a critical error: {}", messageCount);
                m_Running.store(false);
                return;
            }

            {
                std::lock_guard<std::mutex> lock(m_Mutex);
                if (m_DataReceivedCallback)
                {
                    m_DataReceivedCallback(Buffer(incomingMessage->m_pData, incomingMessage->m_cbSize));
                }
            }

            // Release when done
            incomingMessage->Release();
        }
    }

    void Client::PollConnectionStateChanges()
    {
        if (m_Interface)
        {
            m_Interface->RunCallbacks();
        }
    }

    void Client::ConnectionStatusChangedCallback(SteamNetConnectionStatusChangedCallback_t* info)
    {
        if (s_Instance)
        {
            s_Instance->OnConnectionStatusChanged(info);
        }
    }

    void Client::OnConnectionStatusChanged(SteamNetConnectionStatusChangedCallback_t* info)
    {
        switch (info->m_info.m_eState)
        {
        case k_ESteamNetworkingConnectionState_None:
            // Ignore
            break;

        case k_ESteamNetworkingConnectionState_ClosedByPeer:
        case k_ESteamNetworkingConnectionState_ProblemDetectedLocally:
        {
            m_Running.store(false);
            m_ConnectionStatus.store(ConnectionStatus::FailedToConnect);
            m_ConnectionDebugMessage = info->m_info.m_szEndDebug;

            if (info->m_eOldState == k_ESteamNetworkingConnectionState_Connecting)
            {
                // Could not connect
                UT_ERROR_TAG("CLIENT", "Could not connect to remote host. {}", info->m_info.m_szEndDebug);
            }
            else if (info->m_info.m_eState == k_ESteamNetworkingConnectionState_ProblemDetectedLocally)
            {
                UT_WARN_TAG("CLIENT", "Lost connection with remote host. {}", info->m_info.m_szEndDebug);
            }
            else
            {
                UT_INFO_TAG("CLIENT", "Disconnected from host. {}", info->m_info.m_szEndDebug);
            }

            if (m_Interface)
            {
                bool closeResult = m_Interface->CloseConnection(info->m_hConn, 0, nullptr, false);
                if (closeResult != k_EResultOK)
                {
                    UT_WARN_TAG("CLIENT", "CloseConnection returned an error code: {}", closeResult);
                }
            }
            m_Connection = k_HSteamNetConnection_Invalid;
            m_ConnectionStatus.store(ConnectionStatus::Disconnected);
            break;
        }

        case k_ESteamNetworkingConnectionState_Connecting:
            // We can ignore this
            break;

        case k_ESteamNetworkingConnectionState_Connected:
        {
            m_ConnectionStatus.store(ConnectionStatus::Connected);
            std::lock_guard<std::mutex> lock(m_Mutex);
            if (m_ServerConnectedCallback)
            {
                m_ServerConnectedCallback();
            }
            break;
        }

        default:
            break;
        }
    }

    void Client::OnFatalError(const std::string& message)
    {
        UT_ERROR_TAG("CLIENT", "Fatal Error: {}", message);
        m_Running.store(false);
    }

} // namespace Utopia
