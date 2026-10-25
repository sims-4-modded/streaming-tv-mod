/******************************************************************************
 * Copyright (c) 2026 Sims 4 mods developers
 *****************************************************************************/

#pragma once

#include <cstddef>
#include <cstdint>

#include <array>
#include <chrono>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <common/Result.h>
#include <core/HostCommand.h>
#include <core/SpeedPolicy.h>
#include <core/YoutubeUrl.h>
#include <core/Yuv.h>
#include <nlohmann/json.hpp>

/// The videos of the TVs: which TV plays which video of the helper, and the commands that the helper must get. The
/// manager has no thread and no Windows type. The mod calls it on the main thread, and sends the commands that it
/// returns.
namespace ts4_mod::youtube_tv
{

/// The key of a resource of the game: the key of the clip that a video channel plays.
struct ScreenKey
{
	std::uint64_t instance = 0;
	std::uint32_t type = 0;
	std::uint32_t group = 0;

	[[nodiscard]] bool operator==(const ScreenKey&) const = default;
};

/// The position (x, y, z) and the orientation (a quaternion x, y, z, w) of an object, in the axes of the game.
struct ObjectTransform
{
	std::array<float, 3> position {};
	std::array<float, 4> orientation {0.0F, 0.0F, 0.0F, 1.0F};

	[[nodiscard]] bool operator==(const ObjectTransform&) const = default;
};

/// The object id that means "the first TV that shows a video and has no video of the mod". The inspector uses it for
/// tests.
constexpr std::uint64_t k_AnyObject = 0;

enum class SessionState
{
	Opening, ///< The mod sent `open`; the helper did not send `opened` yet.
	Loading, ///< The shared memory is there; the player is not ready.
	Playing,
	Paused,
	Buffering,
	Ended,
	Failed,
};

[[nodiscard]] const char* SessionStateName(SessionState state);

/// One video on one TV.
struct Session
{
	std::uint64_t objectId = k_AnyObject;
	std::int32_t helperId = 0; ///< The `id` of the video in the commands and the events of the helper.
	YoutubeVideo video;
	SessionState state = SessionState::Opening;
	bool ready = false;  ///< The helper sent `ready`: the player takes commands.
	bool mapped = false; ///< The mod has the shared memory of the video.
	/// The video channel of the game that shows this video, or 0. Only the session of `k_AnyObject` keeps it here; the
	/// other sessions use the channel of their object.
	std::uintptr_t anyChannel = 0;
	std::optional<double> sentRate; ///< The last rate that the mod sent to the helper.
	std::optional<bool> sentPlay;   ///< True after `play`, false after `pause`.
	std::string error;
	std::uint64_t framesWritten = 0;
};

/// What the mod must do after a change: send these commands to the helper, and release the shared memory and the
/// audio of these videos.
struct SessionActions
{
	std::vector<host::HostCommand> commands;
	std::vector<std::int32_t> released;

	void Add(SessionActions&& other);
};

/// The settings of the videos.
struct SessionSettings
{
	std::size_t maxSessions = 2;
	std::uint32_t width = 640;  ///< The size of the frames of the helper.
	std::uint32_t height = 368; ///< VP6 clips and the helper use multiples of 16.
	std::uint32_t frameRate = 30;
	YuvMatrix matrix = YuvMatrix::Bt601;
	YuvRange range = YuvRange::Limited;
	SpeedRules speed;
};

class SessionManager
{
public:
	explicit SessionManager(SessionSettings settings);

	/// Use new settings. The size, the frame rate and the matrix apply to the next videos. The result has the
	/// commands of the new speed rules.
	[[nodiscard]] SessionActions SetSettings(SessionSettings settings);

	/// Start a video on the TV `objectId`, or replace its video. `url` is a video id or a URL of a YouTube video. The
	/// result is an error for a URL that is not correct, or if the limit of videos is full.
	[[nodiscard]] ts4::Result<SessionActions> Play(std::uint64_t objectId, std::string_view url,
	                                               std::optional<ObjectTransform> transform = std::nullopt);
	/// Stop the video of the TV. The result is empty if the TV has no video.
	[[nodiscard]] std::optional<SessionActions> Stop(std::uint64_t objectId);
	/// Stop each video, for example when the zone unloads.
	[[nodiscard]] SessionActions StopAll();

	/// Give the speed of the clock of the game to each video.
	[[nodiscard]] SessionActions SetSpeed(ClockSpeedMode mode, bool superSpeed);
	[[nodiscard]] ClockSpeedMode Speed() const { return _mode; }
	[[nodiscard]] bool SuperSpeed() const { return _superSpeed; }
	/// The playback of the current speed.
	[[nodiscard]] Playback CurrentPlayback() const;

	/// Keep the transform of an object.
	void SetTransform(std::uint64_t objectId, const ObjectTransform& transform);
	/// Keep the key of the clip that the script mod put on the TV. The mod uses it if it does not know the channel.
	void SetScreenKey(std::uint64_t objectId, const ScreenKey& key);
	/// Keep the video channel of an object.
	void BindChannel(std::uint64_t objectId, std::uintptr_t channel);
	/// Keep the clip keys of the newest list of clips that the game gave to an object at the time `now`.
	void NotePlaylist(std::uint64_t objectId, std::vector<ScreenKey> keys, std::chrono::milliseconds now);

	/// The rule that made a new link of a channel.
	enum class LinkRule
	{
		Clip, ///< The newest list of the object has the clip of the channel.
		Time, ///< The channel started a new clip just after the object got its newest list.
	};
	struct ChannelLink
	{
		std::uint64_t objectId = 0;
		LinkRule rule = LinkRule::Clip;
	};
	/// The longest time from a list of clips to the first frame of its clip, for the rule `LinkRule::Time`.
	static constexpr std::chrono::milliseconds k_LinkWindow {1500};

	/// Link a channel to an object by the clip that the channel plays at the time `now`. "Not shown" below means that
	/// no channel showed a clip of the newest list of the object yet. The rules, in this order:
	/// 1. If the object of the channel has this clip in its newest list (or has an empty list), the link stays.
	/// 2. Else the channel goes to an object whose newest list has this clip and is not shown. An object with no
	///    channel comes first. Of these, the object with the newest list comes first.
	/// 3. Else, if the channel plays a different clip than at the last call, the channel goes to the object whose
	///    newest list is not shown and came in the last `k_LinkWindow`. The newest list comes first. A TV of the
	///    game needs this rule: the clip of its list is not the clip that its channel plays.
	/// 4. Else the link stays.
	/// The result has the object id if the call made a new link.
	[[nodiscard]] std::optional<ChannelLink> LinkChannelByClip(std::uintptr_t channel, const ScreenKey& clipKey,
	                                                           std::chrono::milliseconds now);

	/// Find the session that shows its video on this channel. `clipKey` is the key of the clip that the channel plays
	/// now, if the mod could read it. The order:
	/// 1. The session of the object that has this channel.
	/// 2. The session of the object whose screen key is `clipKey`. The manager then keeps the channel for the object.
	/// 3. The session of `k_AnyObject`. It takes the first channel that it gets here, and then keeps only that channel.
	[[nodiscard]] Session* MatchChannel(std::uintptr_t channel, const std::optional<ScreenKey>& clipKey);

	/// Use one event of the helper. The result has the commands that the event makes necessary.
	[[nodiscard]] SessionActions OnEvent(const host::HostEvent& event);
	/// The helper stopped: each video stops.
	[[nodiscard]] SessionActions OnHelperStopped(std::string_view reason);

	[[nodiscard]] Session* Find(std::uint64_t objectId);
	[[nodiscard]] Session* FindHelper(std::int32_t helperId);
	[[nodiscard]] const std::vector<Session>& Sessions() const { return _sessions; }
	[[nodiscard]] std::optional<ObjectTransform> TransformOf(std::uint64_t objectId) const;

	/// The state of the videos as JSON, for the inspector and the status of the mod.
	[[nodiscard]] nlohmann::json Status() const;

	/// The limit of objects that the manager keeps without a session.
	static constexpr std::size_t k_MaxKnownObjects = 256;

private:
	struct KnownObject
	{
		std::uintptr_t channel = 0;
		std::optional<ScreenKey> screenKey;
		std::optional<ObjectTransform> transform;
		/// The clip keys of the newest list of clips, and the number of that list. The numbers increase.
		std::vector<ScreenKey> playlist;
		std::uint64_t playlistNumber = 0;
		std::chrono::milliseconds playlistTime {};
		/// The number of the list that a channel of the object played last.
		std::uint64_t shownNumber = 0;
	};

	[[nodiscard]] KnownObject& Known(std::uint64_t objectId);
	[[nodiscard]] SessionActions Release(std::size_t index);
	[[nodiscard]] SessionActions Apply(Session& session);

	SessionSettings _settings;
	std::vector<Session> _sessions;
	std::map<std::uint64_t, KnownObject> _objects;
	std::int32_t _nextHelperId = 1;
	std::uint64_t _nextPlaylistNumber = 1;
	/// The clip of each channel at the last link call.
	std::map<std::uintptr_t, ScreenKey> _channelClips;
	/// The limit of channels in `_channelClips`.
	static constexpr std::size_t k_MaxChannelClips = 256;
	ClockSpeedMode _mode = ClockSpeedMode::Normal;
	bool _superSpeed = false;
};

} // namespace ts4_mod::youtube_tv
