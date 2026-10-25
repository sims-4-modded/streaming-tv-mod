/******************************************************************************
 * Copyright (c) 2026 Sims 4 mods developers
 *****************************************************************************/

#include <array>
#include <vector>

#include <core/HostCommand.h>
#include <core/HostProtocol.h>
#include <core/PlayerPage.h>
#include <gtest/gtest.h>

namespace host = ts4_mod::youtube_tv::host;

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

} // namespace

TEST(SharedBlock, TheParentReadsWhatTheHelperWrote)
{
	BlockMemory memory(host::SharedBlockBytes());
	auto helper = host::SharedBlock::Make(memory.Bytes(), 1280, 720);
	ASSERT_TRUE(helper) << helper.GetError().message;

	auto parent = host::SharedBlock::Attach(memory.Bytes());
	ASSERT_TRUE(parent) << parent.GetError().message;
	EXPECT_EQ(parent->Frame().width, 1280U);
	EXPECT_EQ(parent->Frame().height, 720U);
	EXPECT_EQ(parent->Frame().format, host::k_FrameFormatBgra8);
	EXPECT_EQ(parent->FrameCounter(), 0U);
	EXPECT_EQ(parent->Audio().Channels(), host::k_AudioChannels);
	EXPECT_EQ(parent->Audio().SampleRate(), host::k_AudioSampleRate);
	EXPECT_EQ(parent->Audio().CapacityFrames(), host::k_AudioCapacityFrames);

	helper->PublishFrame(1000);
	helper->PublishFrame(2000);
	EXPECT_EQ(parent->FrameCounter(), 2U);
	EXPECT_EQ(parent->FrameTimeUs(), 2000U);

	const std::array<float, 4> samples {0.5F, -0.5F, 0.25F, -0.25F};
	EXPECT_EQ(helper->Audio().Write(samples), 2U);
	std::array<float, 4> out {};
	EXPECT_EQ(parent->Audio().Read(out), 2U);
	EXPECT_EQ(out, samples);
}

TEST(SharedBlock, RefusesABlockThatIsNotCorrect)
{
	BlockMemory memory(host::SharedBlockBytes());
	EXPECT_FALSE(host::SharedBlock::Attach(memory.Bytes()));
	EXPECT_FALSE(host::SharedBlock::Make(memory.Bytes(), 8, 720));
	EXPECT_FALSE(host::SharedBlock::Make(memory.Bytes(), 1280, 5000));
	EXPECT_FALSE(host::SharedBlock::Make(memory.Bytes().first(host::SharedBlockBytes() - 1), 1280, 720));

	ASSERT_TRUE(host::SharedBlock::Make(memory.Bytes(), 640, 360));
	EXPECT_FALSE(host::SharedBlock::Attach(memory.Bytes().first(host::SharedBlockBytes() - 4)));

	auto* header = reinterpret_cast<host::SharedBlockHeader*>(memory.Bytes().data());
	header->version = host::k_ProtocolVersion + 1;
	EXPECT_FALSE(host::SharedBlock::Attach(memory.Bytes()));
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
	EXPECT_EQ(command->paint, host::PaintMode::Gpu);
	EXPECT_EQ(command->frameRate, 30U);
}

TEST(HostCommand, ReadsOpenWithAVideoId)
{
	const auto command = host::ParseHostCommand(
	    R"({"cmd":"open","id":1,"videoId":"M7lc1UVf-VE","width":640,"height":360,"paint":"cpu","fps":60,"start":7})");
	ASSERT_TRUE(command) << command.GetError().message;
	EXPECT_EQ(command->paint, host::PaintMode::Cpu);
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
	    host::ParseHostCommand(R"({"cmd":"open","id":1,"videoId":"M7lc1UVf-VE","width":640,"height":360,"paint":"magic"})"));
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
	                        .height = 450,
	                        .frameRate = 25,
	                        .paint = host::PaintMode::Cpu};
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
