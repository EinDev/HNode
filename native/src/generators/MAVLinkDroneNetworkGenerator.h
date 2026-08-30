#pragma once
// Port of Assets/Plugin/Generators/MAVLinkDrone/GeneratorMAVLinkDroneNetwork.cs
// (`class MAVLinkDroneNetwork : IDMXGenerator`) - simulates a network of MAVLink
// "drones" (Skybrush-server-style drone show participants) over UDP: sends
// HEARTBEAT/GPS_RAW_INT/GLOBAL_POSITION_INT/SYS_STATUS, answers capability/parameter/
// mission/FTP requests, and packs each drone's current 3D position (plus LED color or
// pyro state) into a DMX channel block.
//
// Phase 1 of this port (this file): MAVLink transport, heartbeat/GPS/param/command/FTP
// handling (FTP accepts and buffers show files but doesn't parse them yet), and DMX
// packing with GridLayout/CircularLayout initial placement. Phase 2/3 (show-file
// trajectory/light-program/pyro playback) land in Drone.h's showFile_ once ShowFile
// exists - see native/README.md for current status.
//
// Threading: the C# reference runs 3 concurrent UniTask loops (SendData,
// SendHeartbeat, ReceiveData). This port collapses that to a single background thread
// that both drains incoming packets and paces outgoing heartbeat/data batches each
// iteration - simpler, and there's no correctness reason (no true concurrency
// requirement between those 3 loops) to keep them separate. mutex_ guards every
// Drone's mutable state, since it's written from this network thread (inbound
// PARAM_SET/COMMAND_INT/FTP writes) and read from GenerateDMX() on the main thread.
#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "IGenerator.h"
#include "mavlink/Drone.h"
#include "mavlink/DroneLayoutProvider.h"

class MAVLinkDroneNetworkGenerator : public IGenerator {
public:
    MAVLinkDroneNetworkGenerator();
    ~MAVLinkDroneNetworkGenerator() override;

    MAVLinkDroneNetworkGenerator(const MAVLinkDroneNetworkGenerator&) = delete;
    MAVLinkDroneNetworkGenerator& operator=(const MAVLinkDroneNetworkGenerator&) = delete;

    const char* Name() const override { return "MAVLinkDroneNetwork"; }

    // Drone positions/state change continuously from network activity on a background
    // thread, independent of ArtNet - needs the render-on-change loop to keep ticking
    // GenerateDMX() rather than freezing whenever ArtNet itself goes idle.
    bool IsAnimated() const override { return true; }

    int droneCount = 254;
    int networkPort = 14550;
    int channelStart = 0;
    bool pyroFeature = false;
    std::unique_ptr<IDroneLayoutProvider> layoutProvider;

    void Construct() override;
    void Deconstruct() override;

    void GenerateDMX(std::vector<uint8_t>& dmxData) override;

    bool DrawUi() override;
    void ReadYaml(const YAML::Node& node) override;
    void WriteYaml(YAML::Emitter& out) const override;

private:
    void OpenSocket();
    void CloseSocket();
    void NetworkLoop();
    void DrainReceive();
    void SendHeartbeatBatch(size_t count);
    void SendDataBatch(size_t count);
    void HandleMessage(const mavlink_message_t& msg);

    std::uintptr_t socketHandle_ = 0; // SOCKET, type-erased (matches other exporters/generators' pattern)

    std::mutex mutex_;
    std::map<uint8_t, Drone> drones_;

    std::thread networkThread_;
    std::atomic<bool> stopRequested_{false};

    // Round-robin cursors for the batched heartbeat/data sends - matches the C#
    // reference's `perUpdate = 10` batching, just paced from one loop instead of two.
    std::map<uint8_t, Drone>::iterator heartbeatCursor_;
    std::map<uint8_t, Drone>::iterator dataCursor_;
};
