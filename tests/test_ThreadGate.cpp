/******************************************************************************
 * Copyright (c) 2026 Sims 4 mods developers
 *****************************************************************************/

#include <cstdint>

#include <atomic>
#include <string>
#include <thread>
#include <vector>

#include <core/LineQueue.h>
#include <core/ThreadGate.h>
#include <gtest/gtest.h>

namespace ts4_mod::youtube_tv
{

TEST(ThreadGate, KeepsTheThreadOfTheFirstCall)
{
	ThreadGate gate;
	EXPECT_EQ(gate.Thread(), 0u);
	EXPECT_EQ(gate.Enter(42), ThreadGate::Entry::First);
	EXPECT_EQ(gate.Thread(), 42u);
	EXPECT_EQ(gate.Enter(42), ThreadGate::Entry::Accepted);
	EXPECT_EQ(gate.Enter(7), ThreadGate::Entry::Refused);
	EXPECT_EQ(gate.Thread(), 42u);
}

TEST(ThreadGate, RefusesTheIdZero)
{
	ThreadGate gate;
	EXPECT_EQ(gate.Enter(0), ThreadGate::Entry::Refused);
	EXPECT_EQ(gate.Thread(), 0u);
}

TEST(ThreadGate, GivesOneFirstEntryWhenThreadsCallAtTheSameTime)
{
	ThreadGate gate;
	std::atomic<int> first = 0;
	std::vector<std::jthread> threads;
	for (std::uint32_t id = 1; id <= 8; ++id)
	{
		threads.emplace_back([&gate, &first, id] {
			if (gate.Enter(id) == ThreadGate::Entry::First)
			{
				++first;
			}
		});
	}
	threads.clear();
	EXPECT_EQ(first.load(), 1);
	EXPECT_NE(gate.Thread(), 0u);
}

TEST(LineQueue, GivesTheLinesInOrder)
{
	LineQueue queue;
	EXPECT_TRUE(queue.Push("a"));
	EXPECT_TRUE(queue.Push("b"));
	EXPECT_EQ(queue.Pop(), "a");
	EXPECT_EQ(queue.Pop(), "b");
}

TEST(LineQueue, GivesTheWaitingLinesAfterCloseThenNothing)
{
	LineQueue queue;
	EXPECT_TRUE(queue.Push("quit"));
	queue.Close();
	EXPECT_TRUE(queue.Closed());
	EXPECT_FALSE(queue.Push("late"));
	EXPECT_EQ(queue.Pop(), "quit");
	EXPECT_EQ(queue.Pop(), std::nullopt);
}

TEST(LineQueue, CloseWakesTheWriter)
{
	LineQueue queue;
	std::optional<std::string> got = "not set";
	std::jthread writer([&] { got = queue.Pop(); });
	queue.Close();
	writer.join();
	EXPECT_EQ(got, std::nullopt);
}

TEST(LineQueue, APushDoesNotWaitForTheWriter)
{
	LineQueue queue;
	for (int i = 0; i < 10000; ++i)
	{
		ASSERT_TRUE(queue.Push(std::to_string(i)));
	}
	std::vector<std::string> lines;
	std::jthread writer([&] {
		while (auto line = queue.Pop())
		{
			lines.push_back(*line);
		}
	});
	queue.Close();
	writer.join();
	ASSERT_EQ(lines.size(), 10000u);
	EXPECT_EQ(lines.front(), "0");
	EXPECT_EQ(lines.back(), "9999");
}

} // namespace ts4_mod::youtube_tv
