/******************************************************************************
 * Copyright (c) 2026 Sims 4 mods developers
 *****************************************************************************/

#include <algorithm>
#include <array>
#include <atomic>
#include <thread>
#include <vector>

#include <core/HostCommand.h>
#include <core/HostProtocol.h>
#include <core/PlayerPage.h>
#include <gtest/gtest.h>

namespace host = ts4_mod::youtube_tv::host;
using ts4_mod::youtube_tv::YuvMatrix;
using ts4_mod::youtube_tv::YuvRange;

namespace
{

/// Memory for a shared block, aligned as the header needs.
class BlockMemory
{
public:
	explicit BlockMemory(std::size_t bytes)
	    : _blocks((bytes + sizeof(Block) - 1) / sizeof(Block))
	{
	}

	[[nodiscard]] std::span<std::byte> Bytes()
	{
		return {reinterpret_cast<std::byte*>(_blocks.data()), _blocks.size() * sizeof(Block)};
	}

private:
	struct alignas(64) Block
	{
		std::array<std::byte, 64> bytes;
	};

	std::vector<Block> _blocks;
};

/// Fill all three planes of the back slot with one value.
void FillBack(host::FrameBuffers& frames, std::uint8_t value)
{
	const auto planes = frames.BackPlanes();
	std::ranges::fill(planes.y, value);
	std::ranges::fill(planes.u, value);
	std::ranges::fill(planes.v, value);
}

[[nodiscard]] bool IsPermutation(std::array<std::uint32_t, 3> indexes)
{
	std::ranges::sort(indexes);
	return indexes == std::array<std::uint32_t, 3> {0, 1, 2};
}

} // namespace

TEST(SharedBlock, TheParentReadsWhatTheHelperWrote)
{
	BlockMemory memory(host::SharedBlockBytes(64, 32));
	auto helper = host::SharedBlock::Make(memory.Bytes(), 64, 32, YuvMatrix::Bt709, YuvRange::Full);
	ASSERT_TRUE(helper) << helper.GetError().message;

	auto parent = host::SharedBlock::Attach(memory.Bytes());
	ASSERT_TRUE(parent) << parent.GetError().message;
	EXPECT_EQ(parent->Frames().Width(), 64U);
	EXPECT_EQ(parent->Frames().Height(), 32U);
	EXPECT_EQ(parent->Frames().Matrix(), YuvMatrix::Bt709);
	EXPECT_EQ(parent->Frames().Range(), YuvRange::Full);
	EXPECT_EQ(parent->Frames().FrameCounter(), 0U);
	EXPECT_EQ(parent->Audio().Channels(), host::k_AudioChannels);
	EXPECT_EQ(parent->Audio().SampleRate(), host::k_AudioSampleRate);
	EXPECT_EQ(parent->Audio().CapacityFrames(), host::k_AudioCapacityFrames);

	const std::array<float, 4> samples {0.5F, -0.5F, 0.25F, -0.25F};
	EXPECT_EQ(helper->Audio().Write(samples), 2U);
	std::array<float, 4> out {};
	EXPECT_EQ(parent->Audio().Read(out), 2U);
	EXPECT_EQ(out, samples);

	FillBack(helper->Frames(), 77);
	helper->Frames().Publish(1234);
	EXPECT_EQ(parent->Frames().FrameCounter(), 1U);
	ASSERT_TRUE(parent->Frames().TakeNewest());
	const auto planes = parent->Frames().FrontPlanes();
	EXPECT_EQ(planes.pitchY, 64U);
	EXPECT_EQ(planes.pitchUV, 32U);
	EXPECT_EQ(planes.y.size(), 64U * 32U);
	EXPECT_EQ(planes.u.size(), 32U * 16U);
	EXPECT_TRUE(std::ranges::all_of(planes.y, [](auto value) { return value == 77; }));
	EXPECT_TRUE(std::ranges::all_of(planes.v, [](auto value) { return value == 77; }));
	EXPECT_EQ(parent->Frames().FrontFrameNumber(), 1U);
	EXPECT_EQ(parent->Frames().FrontTimeUs(), 1234U);
}

TEST(SharedBlock, StartsWithBlackFrames)
{
	BlockMemory memory(host::SharedBlockBytes(32, 16));
	auto block = host::SharedBlock::Make(memory.Bytes(), 32, 16, YuvMatrix::Bt601, YuvRange::Limited);
	ASSERT_TRUE(block);
	const auto planes = block->Frames().FrontPlanes();
	EXPECT_TRUE(std::ranges::all_of(planes.y, [](auto value) { return value == 16; }));
	EXPECT_TRUE(std::ranges::all_of(planes.u, [](auto value) { return value == 128; }));
	EXPECT_EQ(block->Frames().FrontFrameNumber(), 0U);
}

TEST(SharedBlock, RefusesABlockThatIsNotCorrect)
{
	const auto bytes = host::SharedBlockBytes(64, 32);
	BlockMemory memory(bytes);
	EXPECT_FALSE(host::SharedBlock::Attach(memory.Bytes()));
	EXPECT_FALSE(host::SharedBlock::Make(memory.Bytes(), 8, 32, YuvMatrix::Bt601, YuvRange::Limited));
	EXPECT_FALSE(host::SharedBlock::Make(memory.Bytes(), 60, 32, YuvMatrix::Bt601, YuvRange::Limited));
	EXPECT_FALSE(host::SharedBlock::Make(memory.Bytes(), 64, 5008, YuvMatrix::Bt601, YuvRange::Limited));
	EXPECT_FALSE(host::SharedBlock::Make(memory.Bytes().first(bytes - 1), 64, 32, YuvMatrix::Bt601, YuvRange::Limited));

	ASSERT_TRUE(host::SharedBlock::Make(memory.Bytes(), 64, 32, YuvMatrix::Bt601, YuvRange::Limited));
	EXPECT_FALSE(host::SharedBlock::Attach(memory.Bytes().first(bytes - 64)));

	auto* header = reinterpret_cast<host::SharedBlockHeader*>(memory.Bytes().data());
	header->version = host::k_ProtocolVersion + 1;
	EXPECT_FALSE(host::SharedBlock::Attach(memory.Bytes()));
}

TEST(HostProtocol, RoundsTheVideoSize)
{
	EXPECT_EQ(host::RoundVideoSize(0), 16U);
	EXPECT_EQ(host::RoundVideoSize(16), 16U);
	EXPECT_EQ(host::RoundVideoSize(17), 32U);
	EXPECT_EQ(host::RoundVideoSize(720), 720U);
	EXPECT_EQ(host::RoundVideoSize(1080), 1088U);
	EXPECT_EQ(host::RoundVideoSize(5000), 4096U);
}

TEST(FrameBuffers, TheConsumerGetsOnlyNewFrames)
{
	BlockMemory memory(host::FrameBuffers::RequiredBytes(32, 16));
	auto frames = host::FrameBuffers::Make(memory.Bytes(), 32, 16, YuvMatrix::Bt601, YuvRange::Limited);
	ASSERT_TRUE(frames);
	EXPECT_FALSE(frames->TakeNewest());

	FillBack(*frames, 1);
	frames->Publish(10);
	EXPECT_TRUE(frames->TakeNewest());
	EXPECT_EQ(frames->FrontFrameNumber(), 1U);
	EXPECT_FALSE(frames->TakeNewest());
	EXPECT_EQ(frames->FrontFrameNumber(), 1U);
	EXPECT_EQ(frames->FrontPlanes().y[0], 1);
}

TEST(FrameBuffers, TheConsumerGetsTheNewestOfManyFrames)
{
	BlockMemory memory(host::FrameBuffers::RequiredBytes(32, 16));
	auto frames = host::FrameBuffers::Make(memory.Bytes(), 32, 16, YuvMatrix::Bt601, YuvRange::Limited);
	ASSERT_TRUE(frames);
	for (std::uint8_t value = 1; value <= 5; ++value)
	{
		FillBack(*frames, value);
		frames->Publish(value);
	}
	EXPECT_EQ(frames->FrameCounter(), 5U);
	ASSERT_TRUE(frames->TakeNewest());
	EXPECT_EQ(frames->FrontFrameNumber(), 5U);
	EXPECT_EQ(frames->FrontPlanes().y[0], 5);
	EXPECT_EQ(frames->FrontPlanes().v[0], 5);
}

TEST(FrameBuffers, TheProducerNeverWritesTheSlotOfTheConsumer)
{
	BlockMemory memory(host::FrameBuffers::RequiredBytes(32, 16));
	auto frames = host::FrameBuffers::Make(memory.Bytes(), 32, 16, YuvMatrix::Bt601, YuvRange::Limited);
	ASSERT_TRUE(frames);
	EXPECT_EQ(frames->SlotIndexes(), (std::array<std::uint32_t, 3> {0, 1, 2}));

	FillBack(*frames, 9);
	frames->Publish(1);
	ASSERT_TRUE(frames->TakeNewest());
	const auto consumer = frames->SlotIndexes()[2];
	for (std::uint8_t value = 10; value < 30; ++value)
	{
		const auto indexes = frames->SlotIndexes();
		ASSERT_TRUE(IsPermutation(indexes));
		ASSERT_NE(indexes[0], consumer);
		FillBack(*frames, value);
		frames->Publish(value);
	}
	// The slot of the consumer still has its frame.
	EXPECT_EQ(frames->FrontPlanes().y[0], 9);
	EXPECT_EQ(frames->FrontFrameNumber(), 1U);
	EXPECT_TRUE(IsPermutation(frames->SlotIndexes()));
}

TEST(FrameBuffers, AttachKeepsTheSlotOfTheConsumer)
{
	BlockMemory memory(host::FrameBuffers::RequiredBytes(32, 16));
	auto producer = host::FrameBuffers::Make(memory.Bytes(), 32, 16, YuvMatrix::Bt601, YuvRange::Limited);
	ASSERT_TRUE(producer);
	FillBack(*producer, 3);
	producer->Publish(1);
	auto first = host::FrameBuffers::Attach(memory.Bytes());
	ASSERT_TRUE(first);
	ASSERT_TRUE(first->TakeNewest());

	// A new consumer (for example after a reload of the DLL mod) reads the same slot.
	auto second = host::FrameBuffers::Attach(memory.Bytes());
	ASSERT_TRUE(second);
	EXPECT_EQ(second->FrontFrameNumber(), 1U);
	EXPECT_EQ(second->FrontPlanes().y[0], 3);
}

TEST(FrameBuffers, NoFrameTearsWithTwoThreads)
{
	constexpr int k_Frames = 3000;
	BlockMemory memory(host::FrameBuffers::RequiredBytes(64, 32));
	auto made = host::FrameBuffers::Make(memory.Bytes(), 64, 32, YuvMatrix::Bt601, YuvRange::Limited);
	ASSERT_TRUE(made);
	auto producer = *made;
	auto consumer = *host::FrameBuffers::Attach(memory.Bytes());

	std::atomic<bool> done {false};
	std::thread thread([&] {
		for (int frame = 1; frame <= k_Frames; ++frame)
		{
			FillBack(producer, static_cast<std::uint8_t>(frame % 251));
			producer.Publish(static_cast<std::uint64_t>(frame));
		}
		done = true;
	});

	bool complete = true;
	std::uint64_t last = 0;
	bool inOrder = true;
	for (;;)
	{
		const bool finished = done.load();
		if (!consumer.TakeNewest())
		{
			if (finished)
			{
				break;
			}
			std::this_thread::yield();
			continue;
		}
		const auto planes = consumer.FrontPlanes();
		const auto value = planes.y[0];
		complete = complete && std::ranges::all_of(planes.y, [&](auto v) { return v == value; }) &&
		           std::ranges::all_of(planes.u, [&](auto v) { return v == value; }) &&
		           std::ranges::all_of(planes.v, [&](auto v) { return v == value; });
		const auto number = consumer.FrontFrameNumber();
		inOrder = inOrder && number > last && value == number % 251;
		last = number;
	}
	thread.join();
	EXPECT_TRUE(complete);
	EXPECT_TRUE(inOrder);
	EXPECT_EQ(consumer.FrameCounter(), static_cast<std::uint64_t>(k_Frames));
	EXPECT_EQ(consumer.FrontFrameNumber(), static_cast<std::uint64_t>(k_Frames));
}

TEST(HostProtocol, NamesThePlayerStatesAndErrors)
{
	EXPECT_STREQ(host::PlayerStateName(1), "playing");
	EXPECT_STREQ(host::PlayerStateName(2), "paused");
	EXPECT_STREQ(host::PlayerStateName(-1), "unstarted");
	EXPECT_STREQ(host::PlayerStateName(4), "unknown");
	EXPECT_STREQ(host::PlayerErrorText(150), host::PlayerErrorText(101));
	EXPECT_STRNE(host::PlayerErrorText(100), host::PlayerErrorText(150));
}

TEST(HostCommand, ReadsOpenWithAUrl)
{
	const auto command =
	    host::ParseHostCommand(R"({"cmd":"open","id":3,"url":"https://youtu.be/M7lc1UVf-VE?t=5","width":1280,"height":720})");
	ASSERT_TRUE(command) << command.GetError().message;
	EXPECT_EQ(command->type, host::CommandType::Open);
	EXPECT_EQ(command->id, 3);
	EXPECT_EQ(command->videoId, "M7lc1UVf-VE");
	EXPECT_EQ(command->startSeconds, 5U);
	EXPECT_EQ(command->width, 1280U);
	EXPECT_EQ(command->height, 720U);
	EXPECT_EQ(command->matrix, YuvMatrix::Bt601);
	EXPECT_EQ(command->range, YuvRange::Limited);
	EXPECT_EQ(command->frameRate, 30U);
}

TEST(HostCommand, ReadsOpenWithAVideoIdAndRoundsTheSize)
{
	const auto command = host::ParseHostCommand(
	    R"({"cmd":"open","id":1,"videoId":"M7lc1UVf-VE","width":630,"height":360,"matrix":"bt709","range":"full","fps":60,"start":7})");
	ASSERT_TRUE(command) << command.GetError().message;
	EXPECT_EQ(command->width, 640U);
	EXPECT_EQ(command->height, 368U);
	EXPECT_EQ(command->matrix, YuvMatrix::Bt709);
	EXPECT_EQ(command->range, YuvRange::Full);
	EXPECT_EQ(command->frameRate, 60U);
	EXPECT_EQ(command->startSeconds, 7U);
}

TEST(HostCommand, RefusesAnOpenThatIsNotCorrect)
{
	EXPECT_FALSE(host::ParseHostCommand(R"({"cmd":"open","id":1,"width":640,"height":360})"));
	EXPECT_FALSE(host::ParseHostCommand(R"({"cmd":"open","id":1,"videoId":"bad","width":640,"height":360})"));
	EXPECT_FALSE(host::ParseHostCommand(R"({"cmd":"open","id":1,"url":"https://vimeo.com/1","width":640,"height":360})"));
	EXPECT_FALSE(host::ParseHostCommand(R"({"cmd":"open","id":1,"videoId":"M7lc1UVf-VE","width":640})"));
	EXPECT_FALSE(host::ParseHostCommand(R"({"cmd":"open","id":1,"videoId":"M7lc1UVf-VE","width":9000,"height":360})"));
	EXPECT_FALSE(
	    host::ParseHostCommand(R"({"cmd":"open","id":1,"videoId":"M7lc1UVf-VE","width":640,"height":360,"matrix":"bt2020"})"));
	EXPECT_FALSE(host::ParseHostCommand(R"({"cmd":"open","id":1,"videoId":"M7lc1UVf-VE","width":640,"height":360,"range":1})"));
	EXPECT_FALSE(host::ParseHostCommand(R"({"cmd":"open","id":1,"videoId":"M7lc1UVf-VE","width":640,"height":360,"fps":240})"));
	EXPECT_FALSE(host::ParseHostCommand(R"({"cmd":"open","id":0,"videoId":"M7lc1UVf-VE","width":640,"height":360})"));
}

TEST(HostCommand, ReadsTheOtherCommands)
{
	EXPECT_EQ(host::ParseHostCommand(R"({"cmd":"play","id":2})")->type, host::CommandType::Play);
	EXPECT_EQ(host::ParseHostCommand(R"({"cmd":"pause","id":2})")->type, host::CommandType::Pause);
	EXPECT_EQ(host::ParseHostCommand(R"({"cmd":"close","id":2})")->type, host::CommandType::Close);
	EXPECT_EQ(host::ParseHostCommand(R"({"cmd":"quit"})")->type, host::CommandType::Quit);
	EXPECT_DOUBLE_EQ(host::ParseHostCommand(R"({"cmd":"setRate","id":2,"rate":0.25})")->rate, 0.25);
	EXPECT_DOUBLE_EQ(host::ParseHostCommand(R"({"cmd":"setRate","id":2,"rate":2})")->rate, 2.0);
	EXPECT_DOUBLE_EQ(host::ParseHostCommand(R"({"cmd":"seek","id":2,"seconds":12.5})")->seconds, 12.5);
}

TEST(HostCommand, RefusesCommandsThatAreNotCorrect)
{
	EXPECT_FALSE(host::ParseHostCommand("not json"));
	EXPECT_FALSE(host::ParseHostCommand("[1,2]"));
	EXPECT_FALSE(host::ParseHostCommand(R"({"id":1})"));
	EXPECT_FALSE(host::ParseHostCommand(R"({"cmd":"jump","id":1})"));
	EXPECT_FALSE(host::ParseHostCommand(R"({"cmd":"play"})"));
	EXPECT_FALSE(host::ParseHostCommand(R"({"cmd":"play","id":"1"})"));
	EXPECT_FALSE(host::ParseHostCommand(R"({"cmd":"setRate","id":1,"rate":3})"));
	EXPECT_FALSE(host::ParseHostCommand(R"({"cmd":"setRate","id":1,"rate":0.1})"));
	EXPECT_FALSE(host::ParseHostCommand(R"({"cmd":"setRate","id":1})"));
	EXPECT_FALSE(host::ParseHostCommand(R"({"cmd":"seek","id":1,"seconds":-1})"));
}

TEST(HostCommand, WritesALineThatItCanRead)
{
	host::HostCommand open {.type = host::CommandType::Open,
	                        .id = 4,
	                        .videoId = "M7lc1UVf-VE",
	                        .startSeconds = 3,
	                        .width = 800,
	                        .height = 448,
	                        .frameRate = 25,
	                        .matrix = YuvMatrix::Bt709,
	                        .range = YuvRange::Full};
	EXPECT_EQ(*host::ParseHostCommand(host::CommandLine(open)), open);

	host::HostCommand rate {.type = host::CommandType::SetRate, .id = 4, .rate = 1.5};
	EXPECT_EQ(*host::ParseHostCommand(host::CommandLine(rate)), rate);

	host::HostCommand quit {.type = host::CommandType::Quit};
	EXPECT_EQ(*host::ParseHostCommand(host::CommandLine(quit)), quit);
}

TEST(HostEvent, WritesAndReadsAnEvent)
{
	const auto line = host::EventLine("rate", 7, {{"rate", 2.0}});
	EXPECT_EQ(line.find('\n'), std::string::npos);
	const auto event = host::ParseHostEvent(line);
	ASSERT_TRUE(event) << event.GetError().message;
	EXPECT_EQ(event->type, "rate");
	EXPECT_EQ(event->id, 7);
	EXPECT_DOUBLE_EQ(event->data["rate"].get<double>(), 2.0);

	EXPECT_EQ(host::ParseHostEvent(host::EventLine("hello", 0))->id, 0);
	EXPECT_FALSE(host::ParseHostEvent(R"({"id":1})"));
	EXPECT_FALSE(host::ParseHostEvent("{"));
}

TEST(PlayerPage, FillsTheTemplate)
{
	const auto nonce = host::MakeNonce(0x0123456789abcdefULL, 42);
	EXPECT_EQ(nonce.size(), host::k_NonceLength);
	const auto page = host::MakePlayerPage("n={{NONCE}};v='{{VIDEO_ID}}';s={{START}};again={{NONCE}}",
	                                       {.nonce = nonce, .videoId = "M7lc1UVf-VE", .startSeconds = 12});
	ASSERT_TRUE(page) << page.GetError().message;
	EXPECT_EQ(*page, "n=" + nonce + ";v='M7lc1UVf-VE';s=12;again=" + nonce);
}

TEST(PlayerPage, RefusesValuesThatAreNotCorrect)
{
	const auto nonce = host::MakeNonce(1, 2);
	const std::string_view page = "{{NONCE}} {{VIDEO_ID}} {{START}}";
	EXPECT_FALSE(host::MakePlayerPage(page, {.nonce = nonce, .videoId = "x');alert(1);//"}));
	EXPECT_FALSE(host::MakePlayerPage(page, {.nonce = "short", .videoId = "M7lc1UVf-VE"}));
	EXPECT_FALSE(host::MakePlayerPage("{{NONCE}} {{START}}", {.nonce = nonce, .videoId = "M7lc1UVf-VE"}));
}

TEST(PlayerPage, AcceptsOnlyMessagesWithTheNonce)
{
	const auto nonce = host::MakeNonce(5, 6);
	EXPECT_EQ(host::PageMessage(nonce + R"({"event":"ready"})", nonce), R"({"event":"ready"})");
	EXPECT_FALSE(host::PageMessage(R"({"event":"ready"})", nonce));
	EXPECT_FALSE(host::PageMessage(host::MakeNonce(5, 7) + R"({"event":"ready"})", nonce));
	EXPECT_FALSE(host::PageMessage(nonce + "text", nonce));
	EXPECT_FALSE(host::PageMessage(nonce + "{}", ""));
}
