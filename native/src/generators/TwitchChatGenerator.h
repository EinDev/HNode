#pragma once
// Port of Assets/Plugin/Generators/GeneratorTwitchChat.cs (`class TwitchChat : Text`).
// Joins a Twitch chat channel anonymously (read-only, no OAuth token - matches the C#
// reference forcing useAnonymousLogin=true unconditionally) over a raw IRC connection
// and writes the last N chat lines into a DMX text field, one line per buffered
// chatter, oldest first, formatted "login: message\n" - exactly what the C# reference
// rebuilds into its `text` field on every incoming PRIVMSG.
//
// The C# original used the `Lexone.UnityTwitchChat` UPM package (DryWetMidi-style
// external dependency) for the IRC protocol; there's no equivalent here, so the IRC
// connection (irc.chat.twitch.tv:6667, plain TCP, no TLS - anonymous login never needs
// one) is hand-rolled directly against Winsock, following the same background-thread-
// with-a-stop-flag shape as FrameSnapshotExporter's UDP listener.
#include <atomic>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include "IGenerator.h"
#include "TextGenerator.h"

class TwitchChatGenerator : public IGenerator {
public:
    TwitchChatGenerator();
    ~TwitchChatGenerator() override;

    TwitchChatGenerator(const TwitchChatGenerator&) = delete;
    TwitchChatGenerator& operator=(const TwitchChatGenerator&) = delete;

    const char* Name() const override { return "TwitchChat"; }

    std::string channelName;
    int chatMessages = 15; // ring buffer capacity - matches C#'s CircularBuffer<Chatter>

    // Mirror the inner TextGenerator's non-text fields - see SrtGenerator.h for why
    // (the C# reference still exposes these via its base.ConstructUserInterface();
    // only `text` itself is disabled, since GenerateDMX overwrites it every call).
    int channelStart = 0;
    bool unicode = false;
    bool limitLength = false;
    int maxCharacters = 32;

    bool IsAnimated() const override { return true; }

    void Construct() override;
    void Deconstruct() override;

    void GenerateDMX(std::vector<uint8_t>& dmxData) override;

    bool DrawUi() override;
    void ReadYaml(const YAML::Node& node) override;
    void WriteYaml(YAML::Emitter& out) const override;

    bool IsConnected() const { return connected_.load(); }

private:
    // Runs on connectionThread_: connects, does the anonymous-login handshake, joins
    // the channel, and reads lines until disconnected or Deconstruct() closes the
    // socket out from under it. Returns so ConnectionLoop() can retry with backoff.
    void RunConnection();
    void ConnectionLoop();
    void HandleLine(const std::string& line);

    std::thread connectionThread_;
    std::atomic<bool> stopRequested_{false};
    std::atomic<bool> connected_{false};
    std::uintptr_t socketHandle_ = 0; // SOCKET, type-erased to keep <winsock2.h> out of this header
    std::mutex socketMutex_;          // guards socketHandle_ (Deconstruct() closes it from the main thread)

    std::mutex messagesMutex_;
    std::deque<std::pair<std::string, std::string>> messages_; // (login, message), oldest first

    TextGenerator inner_;
};
