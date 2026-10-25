/******************************************************************************
 * Copyright (c) 2026 Sims 4 mods developers
 *****************************************************************************/

#include <core/YoutubeUrl.h>
#include <gtest/gtest.h>

using ts4_mod::youtube_tv::IsVideoId;
using ts4_mod::youtube_tv::ParseStartTime;
using ts4_mod::youtube_tv::ParseYoutubeVideo;

namespace
{

constexpr const char* k_Id = "M7lc1UVf-VE";

void ExpectId(const char* text, const char* id = k_Id, std::uint32_t start = 0)
{
	const auto video = ParseYoutubeVideo(text);
	ASSERT_TRUE(video) << text << ": " << video.GetError().message;
	EXPECT_EQ(video->id, id) << text;
	EXPECT_EQ(video->startSeconds, start) << text;
}

void ExpectRefused(const char* text)
{
	EXPECT_FALSE(ParseYoutubeVideo(text)) << text;
}

} // namespace

TEST(YoutubeUrl, AcceptsACorrectVideoId)
{
	EXPECT_TRUE(IsVideoId("M7lc1UVf-VE"));
	EXPECT_TRUE(IsVideoId("dQw4w9WgXcQ"));
	EXPECT_TRUE(IsVideoId("a_b-C1234_9"));
	EXPECT_FALSE(IsVideoId("M7lc1UVf-V"));
	EXPECT_FALSE(IsVideoId("M7lc1UVf-VEE"));
	EXPECT_FALSE(IsVideoId("M7lc1UVf VE"));
	EXPECT_FALSE(IsVideoId("M7lc1UVf\"VE"));
	ExpectId("M7lc1UVf-VE");
	ExpectId("  M7lc1UVf-VE \n");
}

TEST(YoutubeUrl, ReadsTheWatchPage)
{
	ExpectId("https://www.youtube.com/watch?v=M7lc1UVf-VE");
	ExpectId("http://youtube.com/watch?v=M7lc1UVf-VE");
	ExpectId("www.youtube.com/watch?v=M7lc1UVf-VE");
	ExpectId("HTTPS://WWW.YOUTUBE.COM/watch?v=M7lc1UVf-VE");
	ExpectId("https://m.youtube.com/watch?v=M7lc1UVf-VE");
	ExpectId("https://music.youtube.com/watch?v=M7lc1UVf-VE");
}

TEST(YoutubeUrl, ReadsTheWatchPageWithOtherParameters)
{
	ExpectId("https://www.youtube.com/watch?feature=share&v=M7lc1UVf-VE&list=PL123&index=2");
	ExpectId("https://www.youtube.com/watch?v=M7lc1UVf-VE&ab_channel=YouTubeDevelopers");
	ExpectId("https://www.youtube.com/watch?v=M7lc1UVf-VE#comments");
	ExpectId("https://www.youtube.com/watch?v=M7lc1UVf-VE&t=90", k_Id, 90);
	ExpectId("https://www.youtube.com/watch?v=M7lc1UVf-VE&t=1m30s", k_Id, 90);
}

TEST(YoutubeUrl, ReadsTheShortLink)
{
	ExpectId("https://youtu.be/M7lc1UVf-VE");
	ExpectId("youtu.be/M7lc1UVf-VE/");
	ExpectId("https://youtu.be/M7lc1UVf-VE?si=AbCdEf123&t=42", k_Id, 42);
}

TEST(YoutubeUrl, ReadsShortsEmbedAndLive)
{
	ExpectId("https://www.youtube.com/shorts/M7lc1UVf-VE");
	ExpectId("https://youtube.com/shorts/M7lc1UVf-VE?feature=share");
	ExpectId("https://www.youtube.com/embed/M7lc1UVf-VE?start=10&autoplay=1", k_Id, 10);
	ExpectId("https://www.youtube-nocookie.com/embed/M7lc1UVf-VE");
	ExpectId("https://www.youtube.com/live/M7lc1UVf-VE?si=x");
}

TEST(YoutubeUrl, IgnoresAStartTimeThatIsNotCorrect)
{
	ExpectId("https://youtu.be/M7lc1UVf-VE?t=abc", k_Id, 0);
}

TEST(YoutubeUrl, RefusesOtherSites)
{
	ExpectRefused("https://vimeo.com/123456789");
	ExpectRefused("https://www.youtube.com.example.com/watch?v=M7lc1UVf-VE");
	ExpectRefused("https://notyoutube.com/watch?v=M7lc1UVf-VE");
	ExpectRefused("https://user@www.youtube.com/watch?v=M7lc1UVf-VE");
	ExpectRefused("https://www.youtube.com:8080/watch?v=M7lc1UVf-VE");
	ExpectRefused("ftp://youtu.be/M7lc1UVf-VE");
}

TEST(YoutubeUrl, RefusesPagesWithoutAVideo)
{
	ExpectRefused("");
	ExpectRefused("   ");
	ExpectRefused("https://www.youtube.com/");
	ExpectRefused("https://www.youtube.com/playlist?list=PL590L5WQmH8fJ54F369BLDSqIwcs-TCfs");
	ExpectRefused("https://www.youtube.com/@YouTubeDevelopers");
	ExpectRefused("https://www.youtube.com/channel/UC_x5XG1OV2P6uZZ5FSM9Ttw");
	ExpectRefused("https://www.youtube.com/watch?v=short");
	ExpectRefused("https://www.youtube.com/watch?vv=M7lc1UVf-VE");
	ExpectRefused("https://youtu.be/");
	ExpectRefused("https://youtu.be/M7lc1UVf-VE/extra");
	ExpectRefused("https://www.youtube.com/shorts/");
}

TEST(YoutubeUrl, ReadsTheStartTime)
{
	EXPECT_EQ(*ParseStartTime("0"), 0U);
	EXPECT_EQ(*ParseStartTime("90"), 90U);
	EXPECT_EQ(*ParseStartTime("90s"), 90U);
	EXPECT_EQ(*ParseStartTime("2m"), 120U);
	EXPECT_EQ(*ParseStartTime("1h2m3s"), 3723U);
	EXPECT_FALSE(ParseStartTime(""));
	EXPECT_FALSE(ParseStartTime("m"));
	EXPECT_FALSE(ParseStartTime("1x"));
	EXPECT_FALSE(ParseStartTime("-5"));
	EXPECT_FALSE(ParseStartTime("99999999999"));
}
